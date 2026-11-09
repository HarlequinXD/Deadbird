/*
 * Builds Deadbird's level at start-up: the shinigami energy-extraction plant, dock to reactor.
 *
 *   rooms on a 2 m texture grid joined by straight halls; walls are plain quads with every detail
 *   painted into the textures; props (machines, consoles, pods, the great tank, the soul reactor,
 *   cages, racks, stairs, catwalks) are boxes, cylinders and quads.
 *   Lighting is baked into per-vertex colours from ~150 coloured point lights with shadow rays
 *   against every solid block, plus contact occlusion on floors and toward the floor on walls.
 *
 * Output: the LabData that game/lab_data.h exposes (chunked mesh, collision boxes, halos, fans,
 * areas with a potentially-visible set, camera views, the route waypoints).
 */
#include <stdlib.h>
#include <string.h>
#include "gen_lab.h"
#include "map.h"
#include "gmath.h"
#include "lab_data.h"

/* face flags (must match game/level.c) */
enum { F_QUAD = 1, F_ADD = 2, F_HALF = 4, F_DOUBLE = 8, F_PULSE = 16, F_FLOOR = 32, F_EMIT = 64, F_ANIMUV = 128 };

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 mul(V3 a, float k) { return v3(a.x * k, a.y * k, a.z * k); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float len(V3 a) { return gsqrt(dot(a, a)); }
static V3 neg(V3 a) { return v3(-a.x, -a.y, -a.z); }
static int iround(float x) { return (int)gfloor(x + 0.5f); }
static int iceil(float x) { int i = (int)x; return (float)i < x ? i + 1 : i; }

/* ------------------------------------------------------------------ build state */
typedef struct { V3 p[4]; int n, tile, flags, anim, emit, area; unsigned char uv[4][2]; int er, eg, eb; float rgb[4][3]; V3 nrm; float base; } GF;   /* base: floor y under a wall face */
typedef struct { float x0, y0, z0, x1, y1, z1; } Occ;
typedef struct { float x0, z0, x1, z1, h; } Col;
typedef struct { V3 p, c; float r; } Light;
typedef struct {
    const char *name; float x0, z0, x1, z1, H; int hall, axis_x; unsigned links; unsigned char open[4];
    int ndoor[4]; float door[4][4][2];
} Area;
enum { SS, SN, SW, SE };            /* wall sides */

static GF *faces; static int nfaces, capfaces;
static Occ occ[1024]; static int nocc;
typedef struct { float x0, z0, x1, z1, ytop, ybot; } WSeg;          /* a wall's solid span (sector maps) */
static WSeg *wsegs; static int nwsegs, capwsegs;
static Col cols[1024]; static int ncols;
static Light lights[256]; static int nlights;
static LabSprite sprites[16]; static int nsprites;
static LabFan fans[4]; static int nfans;
static LabRing rings[2]; static int nrings;
static LabPod pods[8]; static int npods;
static Col ceils[32]; static int nceils;
static Area areas[32]; static int nareas;
static int CUR;

static GF *newface(void)
{
    if (nfaces == capfaces) { capfaces = capfaces ? capfaces * 2 : 4096; faces = (GF *)realloc(faces, sizeof(GF) * (size_t)capfaces); }
    GF *f = &faces[nfaces++];
    memset(f, 0, sizeof(*f));
    f->area = CUR;
    return f;
}
static void occl(float x0, float y0, float z0, float x1, float y1, float z1) { Occ o = { x0, y0, z0, x1, y1, z1 }; occ[nocc++] = o; }
static void coll(float x0, float z0, float x1, float z1, float h) { Col c = { x0, z0, x1, z1, h }; cols[ncols++] = c; }
static void light(V3 p, float r, float g, float b, float R) { lights[nlights].p = p; lights[nlights].c = v3(r, g, b); lights[nlights++].r = R; }
static void sprite(int x, int y, int z, int s, int r, int g, int b, int anim, int area)
{
    LabSprite *p = &sprites[nsprites++];
    p->x = (s16)x; p->y = (s16)y; p->z = (s16)z; p->size = (s16)s; p->r = (u8)r; p->g = (u8)g; p->b = (u8)b; p->anim = (u8)anim; p->area = (u8)area;
}

static void tile_uv(int tile, int u0, int v0, int u1, int v1, unsigned char uv[4][2])
{
    int t = tile & 15, tx = (t % 4) * 64, ty = (t / 4) * 64;
    uv[0][0] = (u8)(tx + u0); uv[0][1] = (u8)(ty + v0); uv[1][0] = (u8)(tx + u1); uv[1][1] = (u8)(ty + v0);
    uv[2][0] = (u8)(tx + u0); uv[2][1] = (u8)(ty + v1); uv[3][0] = (u8)(tx + u1); uv[3][1] = (u8)(ty + v1);
}

#define NOND v3(0, 0, 0)
static int has(V3 a) { return a.x != 0 || a.y != 0 || a.z != 0; }

static void quad_uvr(V3 o, V3 U, V3 V, int tex, int flags, int anim, const int *emit, V3 nd, int u0, int v0, int u1, int v1)
{
    if (has(nd) && dot(cross(V, U), nd) < 0) { o = add(o, U); U = neg(U); }
    if (LT_ANIMATED(tex)) flags |= F_ANIMUV;
    int nu = iceil(len(U) / 2100.f), nv = iceil(len(V) / 2100.f);
    if (nu < 1) nu = 1;
    if (nv < 1) nv = 1;
    for (int j = 0; j < nv; j++) for (int i = 0; i < nu; i++) {
        float a0 = (float)i / nu, a1 = (float)(i + 1) / nu, b0 = (float)j / nv, b1 = (float)(j + 1) / nv;
        GF *f = newface();
        tile_uv(tex, iround(u0 + (u1 - u0) * a0), iround(v0 + (v1 - v0) * b0), iround(u0 + (u1 - u0) * a1), iround(v0 + (v1 - v0) * b1), f->uv);
        V3 q = add(o, add(mul(U, a0), mul(V, b0))), du = mul(U, 1.f / nu), dv = mul(V, 1.f / nv);
        f->p[0] = q; f->p[1] = add(q, du); f->p[2] = add(q, dv); f->p[3] = add(add(q, du), dv);
        f->n = 4; f->tile = tex; f->flags = flags | F_QUAD; f->anim = anim;
        if (emit) { f->emit = 1; f->er = emit[0]; f->eg = emit[1]; f->eb = emit[2]; }
    }
}
static void quad(V3 o, V3 U, V3 V, int tex, int flags, int anim, const int *emit, V3 nd) { quad_uvr(o, U, V, tex, flags, anim, emit, nd, 0, 0, 63, 63); }
#define Q(o, U, V, tex, nd) quad(o, U, V, tex, 0, 0, 0, nd)

static void tri(V3 a, V3 b, V3 c, int tex, int flags, V3 nd)
{
    if (has(nd) && dot(cross(sub(c, a), sub(b, a)), nd) < 0) { V3 t = b; b = c; c = t; }
    GF *f = newface();
    unsigned char uv[4][2];
    tile_uv(tex, 0, 0, 63, 63, uv);
    f->p[0] = a; f->p[1] = b; f->p[2] = c; f->p[3] = c; f->n = 3;
    f->uv[0][0] = (u8)(uv[0][0] + 32); f->uv[0][1] = uv[0][1];
    f->uv[1][0] = uv[2][0]; f->uv[1][1] = uv[2][1]; f->uv[2][0] = uv[3][0]; f->uv[2][1] = uv[3][1];
    f->uv[3][0] = uv[3][0]; f->uv[3][1] = uv[3][1];
    f->tile = tex; f->flags = flags & ~F_QUAD;
}

static void grid(V3 o, V3 U, V3 V, float cell, int tex, int flags, V3 nd)
{
    int nu = iround(len(U) / cell), nv = iround(len(V) / cell);
    nu = nu < 1 ? 1 : nu > 16 ? 16 : nu; nv = nv < 1 ? 1 : nv > 16 ? 16 : nv;
    for (int j = 0; j < nv; j++) for (int i = 0; i < nu; i++)
        quad(add(o, add(mul(U, (float)i / nu), mul(V, (float)j / nv))), mul(U, 1.f / nu), mul(V, 1.f / nv), tex, flags, 0, 0, nd);
}

typedef struct { int top, side; float cell; int front1; V3 nd_front; int nooccl, nocoll, flags; const int *emit_front; } BoxOpt;  /* front1 = front tile + 1 */
static void box_(float x0, float y0, float z0, float x1, float y1, float z1, BoxOpt o)
{
    if (o.top >= 0) grid(v3(x0, y0, z0), v3(x1 - x0, 0, 0), v3(0, 0, z1 - z0), o.cell, o.top, o.flags, v3(0, -1, 0));
    V3 so[4] = { v3(x0, y0, z1), v3(x0, y0, z0), v3(x0, y0, z0), v3(x1, y0, z0) };
    V3 su[4] = { v3(x1 - x0, 0, 0), v3(x1 - x0, 0, 0), v3(0, 0, z1 - z0), v3(0, 0, z1 - z0) };
    V3 sn[4] = { v3(0, 0, 1), v3(0, 0, -1), v3(-1, 0, 0), v3(1, 0, 0) };
    for (int k = 0; k < 4; k++) {
        V3 H = v3(0, y1 - y0, 0);
        if (o.front1 && has(o.nd_front) && dot(sn[k], o.nd_front) > 0.9f)
            quad(so[k], su[k], H, o.front1 - 1, o.flags | (o.emit_front ? F_EMIT : 0), 0, o.emit_front, sn[k]);
        else grid(so[k], su[k], H, o.cell, o.side, o.flags, sn[k]);
    }
    if (!o.nooccl) occl(x0, y0, z0, x1, y1, z1);
    if (!o.nocoll) coll(gmin(x0, x1), gmin(z0, z1), gmax(x0, x1), gmax(z0, z1), -y0);
}
#define BOX(x0, y0, z0, x1, y1, z1, top_, side_, cell_, ...) box_(x0, y0, z0, x1, y1, z1, (BoxOpt){ .top = top_, .side = side_, .cell = cell_, __VA_ARGS__ })

static void cylinder(V3 c, float r, float y0, float y1, int n, int tex, int flags, int anim, const int *emit, char axis, int cap, int rep)
{
    int rr = iceil(gabs(y1 - y0) / 2000.f);
    if (rr > rep) rep = rr;
    for (int i = 0; i < n; i++) {
        float a0 = 2 * GPI * i / n, a1 = 2 * GPI * (i + 1) / n, mid = (a0 + a1) / 2;
#define RING(a, h) (axis == 'y' ? add(c, v3(gcos(a) * r, h, gsin(a) * r)) : axis == 'x' ? add(c, v3(h, gcos(a) * r, gsin(a) * r)) : add(c, v3(gcos(a) * r, gsin(a) * r, h)))
        V3 p0 = RING(a0, y0), p1 = RING(a1, y0), nrm = sub(RING(mid, 0), c);
        nrm = mul(nrm, 1 / len(nrm));
        V3 U = sub(p1, p0), Vv = sub(RING(a0, y1), p0);
        if (rep > 1) for (int k = 0; k < rep; k++) quad(add(p0, mul(Vv, (float)k / rep)), U, mul(Vv, 1.f / rep), tex, flags, anim, emit, nrm);
        else quad(p0, U, Vv, tex, flags, anim, emit, nrm);
    }
    if (cap >= 0 && axis == 'y') {
        V3 ctr = add(c, v3(0, y1, 0));
        for (int i = 0; i < n; i++) {
            float a0 = 2 * GPI * i / n, a1 = 2 * GPI * (i + 1) / n;
            tri(ctr, add(c, v3(gcos(a0) * r, y1, gsin(a0) * r)), add(c, v3(gcos(a1) * r, y1, gsin(a1) * r)), cap, flags, v3(0, y1 < y0 ? -1.f : 1.f, 0));
        }
    }
}
#define CYL(c, r, y0, y1, n, tex) cylinder(c, r, y0, y1, n, tex, 0, 0, 0, 'y', -1, 1)

static void cyl_collide(float x, float z, float r, float h)
{
    float k = r * 0.72f;
    coll(x - r, z - k, x + r, z + k, h); coll(x - k, z - r, x + k, z + r, h);
    occl(x - k, -h, z - k, x + k, 0, z + k);
}

/* ------------------------------------------------------------------ rooms and halls */
#define DOOR_H 4000
#define WALL_T 240
#define TEX_M  2000.f
#define SUB    1000.f

static int area(const char *name, float x0, float z0, float x1, float z1, float H, int hall)
{
    Area *a = &areas[nareas];
    memset(a, 0, sizeof(*a));
    a->name = name; a->x0 = x0; a->z0 = z0; a->x1 = x1; a->z1 = z1; a->H = H; a->hall = hall;
    CUR = nareas;
    Col c = { x0, z0, x1, z1, H }; ceils[nceils++] = c;
    return nareas++;
}

typedef struct { const char *room; int side; float pos; int tile; } Placed;
static const Placed PLACED[] = {
    { "Security Checkpoint", SS, -2000, LT_WALL_GRAPH }, { "Extraction Hall", SN, 1000, LT_WALL_VITALS },
    { "Research Lab", SN, -23000, LT_WALL_VITALS }, { "Research Lab", SN, -19000, LT_WALL_GRAPH },
    { "Reactor Core", SS, 25000, LT_WALL_GRAPH },
};
typedef struct { const char *room; int nb, nm; int b[4], m[6]; } Style;
static const Style STYLES[] = {
    { "Loading Dock", 4, 6, { LT_BASE_PLAIN, LT_BASE_PIPES, LT_BASE_PLAIN, LT_BASE_HAZARD },
      { LT_DOCK_PLATE, LT_DOCK_NUMBER, LT_DOCK_PLATE, LT_MID_POSTER_DANGER, LT_DOCK_PLATE, LT_MID_ARROW } },
    { "Security Checkpoint", 2, 4, { LT_BASE_PLAIN, LT_BASE_VENT }, { LT_CHECKPOINT_PANEL, LT_MID_PANEL, LT_MID_AUTHORIZED, LT_MID_PANEL } },
    { "Processing Hall", 3, 4, { LT_BASE_PIPES, LT_BASE_PLAIN, LT_BASE_VENT }, { LT_PROCESSING_PANEL, LT_MID_VPIPES, LT_MID_PANEL, LT_MID_POSTER_POWER } },
    { "Extraction Hall", 4, 5, { LT_BASE_PLAIN, LT_BASE_PIPES, LT_BASE_VENT, LT_BASE_PLAIN },
      { LT_EXTRACTION_PANEL, LT_MID_PANEL, LT_MID_POSTER_POWER, LT_MID_VPIPES, LT_MID_POSTER_DANGER } },
    { "Containment Block", 1, 4, { LT_CELL_WALL }, { LT_CELL_WALL, LT_CELL_WARNING, LT_CELL_WALL, LT_MID_POSTER_DANGER } },
    { "Research Lab", 1, 3, { LT_LAB_PANEL }, { LT_LAB_PANEL, LT_LAB_POSTER, LT_LAB_PANEL } },
    { "Server Vault", 2, 3, { LT_BASE_PLAIN, LT_BASE_VENT }, { LT_VAULT_PANEL, LT_VAULT_PANEL, LT_MID_AUTHORIZED } },
    { "Pipe Gallery", 1, 4, { LT_GALLERY_PIPES }, { LT_GALLERY_PIPES, LT_MID_VPIPES, LT_GALLERY_PIPES, LT_MID_POSTER_DANGER } },
    { "Turbine Hall", 3, 4, { LT_BASE_PIPES, LT_BASE_HAZARD, LT_BASE_PLAIN }, { LT_TURBINE_PANEL, LT_MID_VPIPES, LT_TURBINE_PANEL, LT_MID_POSTER_DANGER } },
    { "Reactor Core", 2, 4, { LT_REACTOR_PLATE, LT_BASE_HAZARD }, { LT_REACTOR_PLATE, LT_REACTOR_WARNING, LT_REACTOR_PLATE, LT_MID_POSTER_DANGER } },
};
static const Style DEFAULT_STYLE = { "", 1, 1, { LT_BASE_PLAIN }, { LT_MID_PANEL } };

static void tex_cell(V3 o, V3 R, V3 D, float a, float b, float h0, float h1, float tu0, float hv0, int tile, int flags, const int *emit, float sub_)
{
    int nu = iceil((b - a) / sub_ - 1e-6f), nv = iceil((h1 - h0) / sub_ - 1e-6f);
    if (nu < 1) nu = 1;
    if (nv < 1) nv = 1;
    if (LT_ANIMATED(tile)) flags |= F_ANIMUV;
#define UU(p) iround(gclamp(((p) - tu0) / TEX_M * 63, 0, 63))
#define VV(p) iround(gclamp(((p) - hv0) / TEX_M * 63, 0, 63))
    for (int j = 0; j < nv; j++) {
        float v0 = h0 + (h1 - h0) * j / nv, v1 = h0 + (h1 - h0) * (j + 1) / nv;
        for (int i = 0; i < nu; i++) {
            float u0 = a + (b - a) * i / nu, u1 = a + (b - a) * (i + 1) / nu;
            V3 p0 = add(o, add(mul(R, u0), mul(D, v0)));
            GF *f = newface();
            tile_uv(tile, 0, 0, 0, 0, f->uv);
            int tx = f->uv[0][0], ty = f->uv[0][1];
            f->uv[0][0] = (u8)(tx + UU(u0)); f->uv[0][1] = (u8)(ty + VV(v0));
            f->uv[1][0] = (u8)(tx + UU(u1)); f->uv[1][1] = (u8)(ty + VV(v0));
            f->uv[2][0] = (u8)(tx + UU(u0)); f->uv[2][1] = (u8)(ty + VV(v1));
            f->uv[3][0] = (u8)(tx + UU(u1)); f->uv[3][1] = (u8)(ty + VV(v1));
            V3 dr = mul(R, u1 - u0), dd = mul(D, v1 - v0);
            f->p[0] = p0; f->p[1] = add(p0, dr); f->p[2] = add(p0, dd); f->p[3] = add(add(p0, dr), dd);
            f->n = 4; f->tile = tile; f->flags = flags | F_QUAD;
            if (emit) { f->emit = 1; f->er = emit[0]; f->eg = emit[1]; f->eb = emit[2]; }
        }
    }
}

typedef struct { float a, b, t; } Column;
static int columns(float L, int anchor /* 0 start 1 end 2 centre */, Column *out)
{
    int n = 0;
    if (anchor == 1) {
        float e = L;
        Column tmp[64];
        while (e > 1) { tmp[n].a = gmax(0, e - TEX_M); tmp[n].b = e; tmp[n].t = e - TEX_M; n++; e -= TEX_M; }
        for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
        return n;
    }
    float m = L - TEX_M * gfloor(L / TEX_M);
    float off = anchor == 0 ? 0 : -(m > 0 ? (TEX_M - m) / 2 : 0), s = off;
    while (s < L - 1) { out[n].a = gmax(0, s); out[n].b = gmin(L, s + TEX_M); out[n].t = s; n++; s += TEX_M; }
    return n;
}

typedef int (*TexF)(int i, int j, int nu, int nv);
static const Area *texf_area;
static void floor_rect(float x0, float z0, float x1, float z1, float y, TexF texf, int up, float sub_, int flags, int emit_tile)
{
    static const int EM[3] = { 150, 160, 170 };
    V3 R = v3(1, 0, 0), D = up ? v3(0, 0, -1) : v3(0, 0, 1), o = v3(x0, y, up ? z1 : z0);
    Column cu[64], cv[64];
    int nu = columns(x1 - x0, 2, cu), nv = columns(z1 - z0, 2, cv);
    for (int j = 0; j < nv; j++) for (int i = 0; i < nu; i++) {
        int tile = texf(i, j, nu, nv), em = tile == emit_tile;
        tex_cell(o, R, D, cu[i].a, cu[i].b, cv[j].a, cv[j].b, cu[i].t, cv[j].t, tile, em ? flags | F_EMIT : flags, em ? EM : 0, sub_);
    }
}

static int mid_col(int i, int j, int nu, int nv) { return texf_area->axis_x ? j == nv / 2 : i == nu / 2; }
static int hall_floor(int i, int j, int nu, int nv) { return mid_col(i, j, nu, nv) && !texf_area->axis_x ? LT_FLOOR_STRIP : LT_FLOOR_PLATE; }
static int hall_ceil(int i, int j, int nu, int nv) { return mid_col(i, j, nu, nv) ? ((i + j) % 3 == 0 ? LT_CEILING_LIGHT : LT_CEILING_CABLES) : LT_CEILING_PANEL; }
static int room_floor(int i, int j, int nu, int nv) { return (i == 0 || i == nu - 1 || j == 0 || j == nv - 1) && (i + j) % 3 == 0 ? LT_FLOOR_GRATE : LT_FLOOR_PLATE; }
static int room_ceil(int i, int j, int nu, int nv) { (void)nu; (void)nv; return i % 3 == 1 && j % 3 == 1 ? LT_CEILING_LIGHT : LT_CEILING_PANEL; }

static void wall_section(V3 o, V3 d, float p, float q, float H, V3 n, const Style *st, int hall, int door_p, int door_q, int lintel, int seed,
                         const Placed *place, int nplace)
{
    V3 right = cross(neg(n), v3(0, -1, 0));
    int flip = dot(right, d) < 0;
    V3 base = add(o, mul(d, flip ? q : p));
    float L = q - p;
    int door_left = flip ? door_q : door_p, door_right = flip ? door_p : door_q;
    Column c[64];
    int nc = columns(L, door_right && !door_left ? 1 : 0, c);
    V3 top_o = add(base, v3(0, -H, 0)), D = v3(0, 1, 0);
    for (int ci = 0; ci < nc; ci++) {
        float rows[6][3]; int rt[6], rn = 0;     /* h0, h1, anchor(0 bottom 1 top) */
#define ROW(h0_, h1_, t_, an_) (rows[rn][0] = (h0_), rows[rn][1] = (h1_), rows[rn][2] = (an_), rt[rn++] = (t_))
        if (lintel) { }
        else if (hall) { ROW(0, 2000, LT_HALL_WALL, 0); ROW(2000, gmin(H, 4000), LT_HALL_WALL_TOP, 0); }
        else {
            int k = ci + seed, bt = st->b[k % st->nb], mt = st->m[k % st->nm];
            V3 ctr = add(base, mul(right, (c[ci].a + c[ci].b) / 2));
            float along = gabs(right.x) > 0.5f ? ctr.x : ctr.z;
            for (int pi = 0; pi < nplace; pi++) {
                float s = (place[pi].pos - along) * ((right.x + right.z) > 0 ? 1 : -1);
                if (-TEX_M / 2 <= s && s < TEX_M / 2 && c[ci].b - c[ci].a > TEX_M * 0.9f) mt = place[pi].tile;
            }
            if (door_left && ci == 0) bt = mt = LT_DOOR_JAMB_R;
            if (door_right && ci == nc - 1) bt = mt = LT_DOOR_JAMB_L;
            ROW(0, 2000, bt, 0); ROW(2000, 4000, mt, 0);
        }
        if (!hall) {
            if (H <= 6000) ROW(4000, H, lintel ? LT_DOOR_LINTEL : LT_TOP_BEAM, lintel ? 0 : 1);
            else {
                ROW(4000, 6000, lintel ? LT_DOOR_LINTEL : LT_TOP_CIRCUIT, 0);
                if (H - 6000 > TEX_M) { ROW(6000, H - TEX_M, LT_TOP_CIRCUIT, 0); ROW(H - TEX_M, H, LT_TOP_BEAM, 1); }
                else ROW(6000, H, LT_TOP_BEAM, 1);
            }
        }
        for (int r = 0; r < rn; r++) {
            float h0 = rows[r][0], h1 = rows[r][1];
            if (h1 - h0 <= 1) continue;
            float v0 = H - h1, v1 = H - h0, hv0 = rows[r][2] == 0 ? H - h0 - TEX_M : H - h1;
            tex_cell(top_o, right, D, c[ci].a, c[ci].b, v0, v1, c[ci].t, hv0, rt[r], 0, 0, SUB);
        }
    }
}

static void door_frame(V3 o, V3 U, V3 n)
{
    float L = len(U);
    V3 d = mul(U, 1 / L), back = mul(neg(n), WALL_T);
    Q(add(o, v3(0, -DOOR_H, 0)), back, v3(0, DOOR_H, 0), LT_BULKHEAD, d);
    Q(add(add(o, U), v3(0, -DOOR_H, 0)), back, v3(0, DOOR_H, 0), LT_BULKHEAD, neg(d));
    Q(add(o, v3(0, -DOOR_H, 0)), U, back, LT_HAZARD, v3(0, 1, 0));
}

static void build_shell(int ai, TexF ftex, TexF ctex)
{
    Area *a = &areas[ai];
    float x0 = a->x0, z0 = a->z0, x1 = a->x1, z1 = a->z1, H = a->H;
    CUR = ai; texf_area = a;
    floor_rect(x0, z0, x1, z1, 0, ftex ? ftex : a->hall ? hall_floor : room_floor, 1, SUB, F_FLOOR, -1);
    floor_rect(x0, z0, x1, z1, -H, ctex ? ctex : a->hall ? hall_ceil : room_ceil, 0, TEX_M, 0, LT_CEILING_LIGHT);
    V3 so[4] = { v3(x0, 0, z0), v3(x0, 0, z1), v3(x0, 0, z0), v3(x1, 0, z0) };
    V3 sd[4] = { v3(1, 0, 0), v3(1, 0, 0), v3(0, 0, 1), v3(0, 0, 1) };
    float sl[4] = { x1 - x0, x1 - x0, z1 - z0, z1 - z0 };
    V3 sn[4] = { v3(0, 0, 1), v3(0, 0, -1), v3(1, 0, 0), v3(-1, 0, 0) };
    const Style *st = &DEFAULT_STYLE;
    for (unsigned i = 0; i < sizeof(STYLES) / sizeof(STYLES[0]); i++) if (!strcmp(STYLES[i].room, a->name)) st = &STYLES[i];
    for (int s = 0; s < 4; s++) {
        if (a->open[s]) continue;
        Placed place[8]; int np = 0;
        for (unsigned i = 0; i < sizeof(PLACED) / sizeof(PLACED[0]); i++)
            if (PLACED[i].side == s && !strcmp(PLACED[i].room, a->name)) place[np++] = PLACED[i];
        /* sorted doorway holes */
        float h[4][2]; int nh = a->ndoor[s];
        memcpy(h, a->door[s], sizeof(h));
        for (int i = 0; i < nh; i++) for (int j = i + 1; j < nh; j++) if (h[j][0] < h[i][0]) { float t0 = h[i][0], t1 = h[i][1]; h[i][0] = h[j][0]; h[i][1] = h[j][1]; h[j][0] = t0; h[j][1] = t1; }
        float cuts[10]; int nc = 0;
        cuts[nc++] = 0;
        for (int i = 0; i < nh; i++) { cuts[nc++] = h[i][0]; cuts[nc++] = h[i][1]; }
        cuts[nc++] = sl[s];
        V3 o = so[s], d = sd[s], n = sn[s];
        for (int k = 0; k < nc; k += 2) {
            float p = cuts[k], q = cuts[k + 1];
            if (q - p > 1) {
                wall_section(o, d, p, q, H, n, st, a->hall, k > 0, k + 2 < nc, 0, k * 3 + 1, place, np);
                V3 w0 = sub(add(o, mul(d, p)), mul(n, WALL_T)), w1 = add(o, mul(d, q));
                coll(gmin(w0.x, w1.x), gmin(w0.z, w1.z), gmax(w0.x, w1.x), gmax(w0.z, w1.z), 9000);
                occl(gmin(w0.x, w1.x), -H - 10, gmin(w0.z, w1.z), gmax(w0.x, w1.x), 10, gmax(w0.z, w1.z));
            }
        }
        for (int i = 0; i < nh; i++) {
            V3 w0 = sub(add(o, mul(d, h[i][0])), mul(n, WALL_T)), w1 = add(o, mul(d, h[i][1]));
            if (H > DOOR_H) {
                wall_section(o, d, h[i][0], h[i][1], H, n, st, a->hall, 0, 0, 1, 0, 0, 0);
                occl(gmin(w0.x, w1.x), -H - 10, gmin(w0.z, w1.z), gmax(w0.x, w1.x), -DOOR_H, gmax(w0.z, w1.z));
            }
            door_frame(add(o, mul(d, h[i][0])), mul(d, h[i][1] - h[i][0]), n);
        }
    }
}

static void add_door(int ai, int side, float c0, float c1) { Area *a = &areas[ai]; a->door[side][a->ndoor[side]][0] = c0; a->door[side][a->ndoor[side]++][1] = c1; }

static int hall_between(const char *name, int ra, int rb, float centre)
{
    const float width = 3000;
    Area *A = &areas[ra], *B = &areas[rb];
    int hi_;
    if (A->x1 <= B->x0 || B->x1 <= A->x0) {
        int lo = A->x1 <= B->x0 ? ra : rb, hi = lo == ra ? rb : ra;
        Area L = areas[lo], Hh = areas[hi];
        float z0 = centre - width / 2, z1 = centre + width / 2;
        hi_ = area(name, L.x1, z0, Hh.x0, z1, DOOR_H, 1);
        areas[hi_].open[SW] = areas[hi_].open[SE] = 1; areas[hi_].axis_x = 1;
        add_door(lo, SE, z0 - L.z0, z1 - L.z0); add_door(hi, SW, z0 - Hh.z0, z1 - Hh.z0);
    } else {
        int lo = A->z1 <= B->z0 ? ra : rb, hi = lo == ra ? rb : ra;
        Area L = areas[lo], Hh = areas[hi];
        float x0 = centre - width / 2, x1 = centre + width / 2;
        hi_ = area(name, x0, L.z1, x1, Hh.z0, DOOR_H, 1);
        areas[hi_].open[SS] = areas[hi_].open[SN] = 1; areas[hi_].axis_x = 0;
        add_door(lo, SN, x0 - L.x0, x1 - L.x0); add_door(hi, SS, x0 - Hh.x0, x1 - Hh.x0);
    }
    areas[hi_].links |= (1u << ra) | (1u << rb);
    areas[ra].links |= 1u << hi_; areas[rb].links |= 1u << hi_;
    return hi_;
}

static void dress_hall(int hi)
{
    Area *a = &areas[hi];
    CUR = hi;
    float L = a->axis_x ? a->x1 - a->x0 : a->z1 - a->z0;
    int n = (int)(L / 3000); if (n < 1) n = 1;
    for (int k = 0; k < n; k++)
        light(a->axis_x ? v3(a->x0 + (k + 0.5f) * L / n, -3600, (a->z0 + a->z1) / 2) : v3((a->x0 + a->x1) / 2, -3600, a->z0 + (k + 0.5f) * L / n), 0.75f, 0.9f, 0.95f, 4800);
}

static void ceiling_lights(int ai, float r, float g, float b, float R, float step, float h)
{
    Area *a = &areas[ai];
    float H = h ? h : a->H;
    int nx = iround((a->x1 - a->x0) / step), nz = iround((a->z1 - a->z0) / step);
    if (nx < 1) nx = 1;
    if (nz < 1) nz = 1;
    for (int j = 0; j < nz; j++) for (int i = 0; i < nx; i++)
        light(v3(a->x0 + (i + 0.5f) * (a->x1 - a->x0) / nx, -H + 300, a->z0 + (j + 0.5f) * (a->z1 - a->z0) / nz), r, g, b, R);
}

static void stairs(float x0, float z0, float x1, float z1, float top, int axis_x, int up_positive)
{
    int n = iround(top / 130);
    for (int i = 0; i < n; i++) {
        if (axis_x) {
            float L = (x1 - x0) / n, a = up_positive ? x0 + i * L : x1 - (i + 1) * L;
            BOX(a, -(i + 1) * 130.f, z0, a + L, 0, z1, LT_STAIR_TREAD, LT_DARK_METAL, 1600, .nooccl = !(i > n / 2));
        } else {
            float L = (z1 - z0) / n, a = up_positive ? z0 + i * L : z1 - (i + 1) * L;
            BOX(x0, -(i + 1) * 130.f, a, x1, 0, a + L, LT_STAIR_TREAD, LT_DARK_METAL, 1600, .nooccl = !(i > n / 2));
        }
    }
}
static void rail_x(float x0, float x1, float z, float base)
{
    float a = gmin(x0, x1), b = gmax(x0, x1);
    BOX(a, -base - 900, z - 30, b, -base, z + 30, LT_BEAM, LT_BEAM, 1200, .nooccl = 1, .nocoll = 1);
    coll(a, z - 40, b, z + 40, base + 900);
}
static void rail_z(float z0, float z1, float x, float base)
{
    float a = gmin(z0, z1), b = gmax(z0, z1);
    BOX(x - 30, -base - 900, a, x + 30, -base, b, LT_BEAM, LT_BEAM, 1200, .nooccl = 1, .nocoll = 1);
    coll(x - 40, a, x + 40, b, base + 900);
}
static void machine(float x0, float z0, float x1, float z1, float h, V3 face)
{
    BOX(x0, -h, z0, x1, 0, z1, LT_DARK_METAL, LT_MACHINE_BASE, 900, .front1 = 1 + LT_MACHINE_PANEL, .nd_front = face);
}

/* ------------------------------------------------------------------ props */
static void captive(float x, float y, float z, float s, float facing)
{
    float c = gcos(facing), sn = gsin(facing);
#define P(px, py, pz) v3(x + ((px) * c + (pz) * sn) * s, y + (py) * s, z + (-(px) * sn + (pz) * c) * s)
    static const float B[6][7] = {
        { -160, -1100, -90, 160, -500, 90, 0 }, { -130, -1450, -130, 130, -1150, 130, 1 }, { -110, -500, -70, -30, 0, 70, 0 },
        { 30, -500, -70, 110, 0, 70, 0 }, { -520, -1080, -40, -160, -980, 40, 0 }, { 160, -1080, -40, 520, -980, 40, 0 },
    };
    static const int F[6][3] = { { 0, 1, 2 }, { 4, 5, 6 }, { 0, 4, 2 }, { 1, 5, 3 }, { 0, 1, 4 }, { 2, 3, 6 } };
    static const float FN[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } };
    for (int b = 0; b < 6; b++) {
        float ax = B[b][0], ay = B[b][1], az = B[b][2], bx = B[b][3], by = B[b][4], bz = B[b][5];
        V3 k[8] = { P(ax, ay, az), P(bx, ay, az), P(ax, by, az), P(bx, by, az), P(ax, ay, bz), P(bx, ay, bz), P(ax, by, bz), P(bx, by, bz) };
        for (int f = 0; f < 6; f++) {
            V3 nn = v3(FN[f][0] * c + FN[f][2] * sn, FN[f][1], -FN[f][0] * sn + FN[f][2] * c);
            int t = (B[b][6] > 0 && FN[f][2] > 0.5f) ? LT_SHINI_SKULL : LT_SHINI_BODY;
            Q(k[F[f][0]], sub(k[F[f][1]], k[F[f][0]]), sub(k[F[f][2]], k[F[f][0]]), t, nn);
        }
    }
    quad(P(-160, -1200, -100), sub(P(-900, -1500, -300), P(-160, -1200, -100)), sub(P(-160, -600, -100), P(-160, -1200, -100)), LT_SHINI_BODY, F_DOUBLE, 0, 0, NOND);
    quad(P(160, -1200, -100), sub(P(900, -1500, -300), P(160, -1200, -100)), sub(P(160, -600, -100), P(160, -1200, -100)), LT_SHINI_BODY, F_DOUBLE, 0, 0, NOND);
#undef P
}

static void console(float x, float z, float facing, int screen, float y)
{
    static const int E150[3] = { 150, 150, 150 };
    float c = gcos(facing), s = gsin(facing);
    V3 fwd = v3(s, 0, c), right = v3(c, 0, -s), o = v3(x, y, z);
    float w = 420, d = 260;
    V3 k[4] = { sub(sub(o, mul(right, w)), mul(fwd, d)), sub(add(o, mul(right, w)), mul(fwd, d)), add(add(o, mul(right, w)), mul(fwd, d)), add(sub(o, mul(right, w)), mul(fwd, d)) };
    for (int i = 0; i < 4; i++) {
        V3 a = k[i], b = k[(i + 1) % 4], n = cross(v3(0, 1, 0), sub(b, a));
        n = mul(n, 1 / len(n));
        if (dot(n, sub(mul(add(a, b), 0.5f), o)) < 0) n = neg(n);
        Q(add(a, v3(0, -800, 0)), sub(b, a), v3(0, 800, 0), LT_MACHINE_PANEL, n);
    }
    Q(add(k[0], v3(0, -800, 0)), sub(k[1], k[0]), sub(k[3], k[0]), LT_KEYBOARD, v3(0, -1, 0));
    V3 scr = add(sub(add(o, mul(fwd, d - 40)), mul(right, 380)), v3(0, -1500, 0));
    quad(scr, mul(right, 760), v3(0, 560, 0), screen, F_EMIT, 1, E150, neg(fwd));
    Q(add(scr, mul(fwd, 60)), mul(right, 760), v3(0, 560, 0), LT_DARK_METAL, fwd);
    Q(scr, mul(fwd, 60), v3(0, 560, 0), LT_DARK_METAL, neg(right));
    Q(add(scr, mul(right, 760)), mul(fwd, 60), v3(0, 560, 0), LT_DARK_METAL, right);
    Q(scr, mul(right, 760), mul(fwd, 60), LT_DARK_METAL, v3(0, -1, 0));
    float mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f;
    for (int i = 0; i < 4; i++) { mnx = gmin(mnx, k[i].x); mxx = gmax(mxx, k[i].x); mnz = gmin(mnz, k[i].z); mxz = gmax(mxz, k[i].z); }
    coll(mnx, mnz, mxx, mxz, 800 - y);
    occl(mnx, y - 800, mnz, mxx, y, mxz);
    V3 lp = add(sub(add(scr, mul(right, 380)), mul(fwd, 300)), v3(0, 280, 0));
    if (screen == LT_SCR_GRAPH) light(lp, 0.35f, 0.5f, 0.45f, 1300); else light(lp, 0.6f, 0.2f, 0.15f, 1300);
}

static void extraction_pod(float x, float z, float facing, int anim)
{
    float r = 420;
    cylinder(v3(x, 0, z), r + 120, 0, -380, 8, LT_MACHINE_BASE, 0, 0, 0, 'y', LT_HAZARD, 1);
    cylinder(v3(x, -380, z), r, 0, -2300, 8, LT_GLASS, F_ADD, anim, 0, 'y', -1, 2);
    cylinder(v3(x, -2680, z), r + 120, 0, -600, 8, LT_DARK_METAL, 0, 0, 0, 'y', LT_DARK_METAL, 1);
    for (int k = 0; k < 3; k++) { float a = k * 2.1f + 0.3f; CYL(v3(x + gcos(a) * 260, -3280, z + gsin(a) * 260), 70, 0, -1200, 5, LT_PIPE); }
    captive(x, -500, z, 0.95f, facing);
    cyl_collide(x, z, r + 120, 3300);
    light(v3(x, -1500, z), 0.25f, 0.85f, 1.0f, 3200);
    sprite((int)x, -2650, (int)z, 380, 60, 200, 230, anim, CUR);
    pods[npods].x = (s16)x; pods[npods].z = (s16)z; pods[npods++].r = (s16)r;
    console(x + gcos(facing) * 900, z - gsin(facing) * 900, facing + GPI, LT_SCR_VITALS, 0);
}

static void rack(float x, float z, V3 nd)
{
    static const int E120[3] = { 120, 120, 120 };
    if (gabs(nd.x) > 0.5f) BOX(x - 250, -2200, z - 350, x + 250, 0, z + 350, LT_DARK_METAL, LT_DARK_METAL, 800, .front1 = 1 + LT_SERVER_RACK, .nd_front = nd, .emit_front = E120);
    else BOX(x - 350, -2200, z - 250, x + 350, 0, z + 250, LT_DARK_METAL, LT_DARK_METAL, 800, .front1 = 1 + LT_SERVER_RACK, .nd_front = nd, .emit_front = E120);
}
static void crate(float x, float z, float s, float h) { BOX(x - s / 2, -h, z - s / 2, x + s / 2, 0, z + s / 2, LT_CRATE, LT_CRATE, s); }
static void canister(float x, float z) { cylinder(v3(x, 0, z), 200, 0, -700, 8, LT_CANISTER, 0, 0, 0, 'y', LT_DARK_METAL, 1); cyl_collide(x, z, 200, 700); }
static void pillar(float x, float z, float H)
{
    BOX(x - 300, -H, z - 300, x + 300, 0, z + 300, -1, LT_BEAM, 700);
    BOX(x - 340, -500, z - 340, x + 340, 0, z + 340, LT_HAZARD, LT_HAZARD, 700, .nocoll = 1, .nooccl = 1);
}

/* ------------------------------------------------------------------ the plant's floors */
static int f_dock(int i, int j, int nu, int nv)
{
    if (j == 1 && 1 <= i && i <= nu - 2) return LT_FLOOR_HAZARD;
    if (i == nu - 1 && (j == 2 || j == 3)) return LT_FLOOR_ARROW;
    return j == nv - 1 ? LT_FLOOR_EDGE : LT_FLOOR_DOCK;
}
static int f_proc(int i, int j, int nu, int nv) { return j == nv / 2 ? LT_FLOOR_GRATE : room_floor(i, j, nu, nv); }
static int f_cont(int i, int j, int nu, int nv) { (void)nu; (void)nv; return (i * 7 + j * 3) % 11 == 0 ? LT_FLOOR_DRAIN : LT_FLOOR_CELL; }
static int f_lab(int i, int j, int nu, int nv) { (void)i; (void)j; (void)nu; (void)nv; return LT_FLOOR_LAB; }
static int f_vault(int i, int j, int nu, int nv) { (void)nu; (void)nv; return (i == 0 || i == 2 || i == 3) && j != 2 ? LT_FLOOR_GRATE : (j == 2 && i == 1 ? LT_FLOOR_ARROW : LT_FLOOR_PLATE); }
static int f_gall(int i, int j, int nu, int nv) { (void)j; (void)nu; (void)nv; return i == 2 ? LT_FLOOR_GRATE : i == 1 ? LT_FLOOR_STRIP : LT_FLOOR_PLATE; }
static int f_turb(int i, int j, int nu, int nv) { (void)nu; return j == nv - 1 ? LT_FLOOR_HAZARD : (i % 3 == 1 ? LT_FLOOR_GRATE : LT_FLOOR_PLATE); }
static int f_core(int i, int j, int nu, int nv)
{
    if (1 <= i && i <= nu - 2 && 1 <= j && j <= nv - 2 && gabs(i - nu / 2.f + 0.5f) < 3 && gabs(j - nv / 2.f + 0.5f) < 3) return LT_FLOOR_REACTOR;
    return (i == 0 || i == nu - 1 || j == 0 || j == nv - 1) ? LT_FLOOR_HAZARD : LT_FLOOR_PLATE;
}

/* ------------------------------------------------------------------ lighting bake */
static int *wnear, nwnear;
static int shadowed(V3 P, V3 L, const int *near, int nn)
{
    V3 D = sub(L, P);
    float inv[3] = { 1 / (gabs(D.x) < 1e-6f ? 1e-6f : D.x), 1 / (gabs(D.y) < 1e-6f ? 1e-6f : D.y), 1 / (gabs(D.z) < 1e-6f ? 1e-6f : D.z) };
    float p[3] = { P.x, P.y, P.z };
    for (int k = 0; k < nn; k++) {
        const Occ *o = &occ[near[k]];
        float mn[3] = { gmin(o->x0, o->x1), gmin(o->y0, o->y1), gmin(o->z0, o->z1) }, mx[3] = { gmax(o->x0, o->x1), gmax(o->y0, o->y1), gmax(o->z0, o->z1) };
        float tmin = -1e30f, tmax = 1e30f;
        for (int a = 0; a < 3; a++) {
            float t0 = (mn[a] - p[a]) * inv[a], t1 = (mx[a] - p[a]) * inv[a];
            tmin = gmax(tmin, gmin(t0, t1)); tmax = gmin(tmax, gmax(t0, t1));
        }
        if (tmax >= tmin && tmax > 0 && tmin < 0.98f) return 1;
    }
    for (int k = 0; k < nwnear; k++) {
        const WSeg *w = &wsegs[wnear[k]];
        float ex = w->x1 - w->x0, ez = w->z1 - w->z0, den = D.x * ez - D.z * ex;
        if (gabs(den) < 1e-6f) continue;
        float t = ((w->x0 - P.x) * ez - (w->z0 - P.z) * ex) / den, s_ = ((w->x0 - P.x) * D.z - (w->z0 - P.z) * D.x) / den;
        if (t <= 0.002f || t >= 0.98f || s_ < 0 || s_ > 1) continue;
        float y = P.y + D.y * t;
        if (y >= w->ytop && y <= w->ybot) return 1;
    }
    return 0;
}

static float floor_ao(V3 p)
{
    float best = 1e9f;
    for (int i = 0; i < ncols; i++) {
        const Col *c = &cols[i];
        if (!(c->h > -p.y + 20)) continue;
        float dx = gmax(gmax(c->x0 - p.x, p.x - c->x1), 0), dz = gmax(gmax(c->z0 - p.z, p.z - c->z1), 0);
        best = gmin(best, gsqrt(dx * dx + dz * dz));
    }
    for (int i = 0; i < nwsegs; i++) {
        const WSeg *w = &wsegs[i];
        if (!(w->ytop < p.y - 20 && w->ybot >= p.y - 40)) continue;
        float ex = w->x1 - w->x0, ez = w->z1 - w->z0, L2 = ex * ex + ez * ez;
        float t = L2 > 0 ? gclamp(((p.x - w->x0) * ex + (p.z - w->z0) * ez) / L2, 0, 1) : 0;
        float dx = w->x0 + ex * t - p.x, dz = w->z0 + ez * t - p.z;
        best = gmin(best, gsqrt(dx * dx + dz * dz));
    }
    return 0.45f + 0.55f * gclamp(best / 500, 0, 1);
}

static void bake(void)
{
    static const float AMB[3] = { 0.22f, 0.23f, 0.29f };
    static int near[1024];
    float *aoa = (float *)malloc(sizeof(float) * (size_t)nfaces * 4);
    wnear = (int *)malloc(sizeof(int) * (size_t)(nwsegs + 1));
    for (int fi = 0; fi < nfaces; fi++) {
        GF *f = &faces[fi];
        V3 n = cross(sub(f->p[2], f->p[0]), sub(f->p[1], f->p[0]));
        float ln = len(n);
        f->nrm = ln > 0 ? mul(n, 1 / ln) : v3(0, -1, 0);
        for (int c = 0; c < f->n; c++) {
            float ao = 1;
            V3 p = f->p[c];
            if (f->nrm.y < -0.8f) ao = floor_ao(p);
            if (gabs(f->nrm.y) < 0.3f) ao *= 0.55f + 0.45f * gclamp((f->base - p.y) / 600, 0, 1);
            for (int k = 0; k < 3; k++) f->rgb[c][k] = AMB[k] * ao;
            aoa[fi * 4 + c] = ao;
        }
    }
    for (int li = 0; li < nlights; li++) {
        const Light *L = &lights[li];
        int nn = 0;
        for (int k = 0; k < nocc; k++) {
            const Occ *o = &occ[k];
            if (gmin(o->x0, o->x1) <= L->p.x + L->r && gmax(o->x0, o->x1) >= L->p.x - L->r &&
                gmin(o->y0, o->y1) <= L->p.y + L->r && gmax(o->y0, o->y1) >= L->p.y - L->r &&
                gmin(o->z0, o->z1) <= L->p.z + L->r && gmax(o->z0, o->z1) >= L->p.z - L->r) near[nn++] = k;
        }
        nwnear = 0;
        for (int k = 0; k < nwsegs; k++) {
            const WSeg *w = &wsegs[k];
            if (gmin(w->x0, w->x1) <= L->p.x + L->r && gmax(w->x0, w->x1) >= L->p.x - L->r &&
                gmin(w->z0, w->z1) <= L->p.z + L->r && gmax(w->z0, w->z1) >= L->p.z - L->r &&
                w->ytop <= L->p.y + L->r && w->ybot >= L->p.y - L->r) wnear[nwnear++] = k;
        }
        for (int fi = 0; fi < nfaces; fi++) {
            GF *f = &faces[fi];
            for (int c = 0; c < f->n; c++) {
                V3 Pn = add(f->p[c], mul(f->nrm, 12)), D = sub(L->p, Pn);
                float d = len(D);
                if (d >= L->r) continue;
                float ndl = dot(f->nrm, mul(D, 1 / d));
                if (ndl <= 0) continue;
                float q = d / L->r, att = gpow(gclamp(1 - q * q, 0, 1), 1.5f);
                float k = ndl * att * (shadowed(Pn, L->p, near, nn) ? 0.12f : 1.0f) * (0.65f + 0.35f * aoa[fi * 4 + c]);
                f->rgb[c][0] += k * L->c.x; f->rgb[c][1] += k * L->c.y; f->rgb[c][2] += k * L->c.z;
            }
        }
    }
    free(aoa);
    free(wnear); wnear = 0;
    for (int fi = 0; fi < nfaces; fi++) {
        GF *f = &faces[fi];
        for (int c = 0; c < f->n; c++)
            for (int k = 0; k < 3; k++) {
                float v;
                if (f->emit) v = (float)(k == 0 ? f->er : k == 1 ? f->eg : f->eb);
                else if (f->flags & F_ADD) v = gclamp(f->rgb[c][k] * 60 + 40, 0, 140);
                else v = gclamp(f->rgb[c][k] * 128 * 1.15f, 0, 255);
                f->rgb[c][k] = (float)iround(v);
            }
    }
}

/* ------------------------------------------------------------------ export */
LabData lab;

typedef struct { int area, cx, cz, fi; } Key;
static int cmp_key(const void *a, const void *b)
{
    const Key *p = (const Key *)a, *q = (const Key *)b;
    if (p->area != q->area) return p->area - q->area;
    if (p->cx != q->cx) return p->cx - q->cx;
    if (p->cz != q->cz) return p->cz - q->cz;
    return p->fi - q->fi;
}

static void export_mesh(void)
{
    Key *keys = (Key *)malloc(sizeof(Key) * (size_t)nfaces);
    for (int i = 0; i < nfaces; i++) {
        V3 c = v3(0, 0, 0);
        for (int k = 0; k < faces[i].n; k++) c = add(c, faces[i].p[k]);
        c = mul(c, 1.f / faces[i].n);
        keys[i].area = faces[i].area; keys[i].cx = (int)gfloor(c.x / 3072); keys[i].cz = (int)gfloor(c.z / 3072); keys[i].fi = i;
    }
    qsort(keys, (size_t)nfaces, sizeof(Key), cmp_key);
    lab.verts = (LabVert *)malloc(sizeof(LabVert) * (size_t)nfaces * 4);
    lab.faces = (LabFace *)malloc(sizeof(LabFace) * (size_t)nfaces);
    lab.chunks = (LabChunk *)malloc(sizeof(LabChunk) * (size_t)nfaces);
    lab.nverts = lab.nfaces = lab.nchunks = 0;
    for (int s = 0; s < nfaces;) {
        int e = s;
        while (e < nfaces && keys[e].area == keys[s].area && keys[e].cx == keys[s].cx && keys[e].cz == keys[s].cz) e++;
        for (int ps = s; ps < e; ps += 300) {
            int pe = ps + 300 < e ? ps + 300 : e, v0 = lab.nverts, f0 = lab.nfaces;
            for (int k = ps; k < pe; k++) {
                const GF *f = &faces[keys[k].fi];
                LabFace *o = &lab.faces[lab.nfaces++];
                for (int c = 0; c < 4; c++) {
                    int cc = c < f->n ? c : f->n - 1;
                    int x = iround(f->p[cc].x), y = iround(f->p[cc].y), z = iround(f->p[cc].z), vi;
                    for (vi = v0; vi < lab.nverts; vi++) if (lab.verts[vi].x == x && lab.verts[vi].y == y && lab.verts[vi].z == z) break;
                    if (vi == lab.nverts) { lab.verts[vi].x = (s16)x; lab.verts[vi].y = (s16)y; lab.verts[vi].z = (s16)z; lab.nverts++; }
                    o->v[c] = (u16)(vi - v0);
                    o->uv[c * 2] = f->uv[cc][0]; o->uv[c * 2 + 1] = f->uv[cc][1];
                    for (int q = 0; q < 3; q++) o->rgb[c * 3 + q] = (u8)f->rgb[cc][q];
                }
                o->page = (u8)(f->tile / 16); o->flags = (u8)f->flags; o->anim = (u8)f->anim;
            }
            float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
            for (int vi = v0; vi < lab.nverts; vi++) {
                float p[3] = { lab.verts[vi].x, lab.verts[vi].y, lab.verts[vi].z };
                for (int q = 0; q < 3; q++) { mn[q] = gmin(mn[q], p[q]); mx[q] = gmax(mx[q], p[q]); }
            }
            LabChunk *ch = &lab.chunks[lab.nchunks++];
            ch->cx = (s16)((mn[0] + mx[0]) / 2); ch->cy = (s16)((mn[1] + mx[1]) / 2); ch->cz = (s16)((mn[2] + mx[2]) / 2);
            ch->r = (u16)((int)(len(v3(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2])) / 2) + 1);
            ch->v0 = (u16)v0; ch->nv = (u16)(lab.nverts - v0); ch->f0 = (u16)f0; ch->nf = (u16)(lab.nfaces - f0); ch->area = (u8)keys[s].area;
        }
        s = e;
    }
    free(keys);
}

/* ------------------------------------------------------------------ the plant's contents
   Most of the props are a compact placement script (s16 words: an opcode and its arguments) run
   by place_props(); the few parametric pieces (the great tank, the cells, the reactor) are code. */
static V3 nd_of(int c) { static const signed char D[7][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }; return v3(D[c][0], D[c][1], D[c][2]); }
static float facing_of(int c) { return c == 0 ? 0 : c == 1 ? GPI / 2 : c == 2 ? GPI : -GPI / 2; }

static const short PROPS[] = {
    OP_CUR, 0,
    OP_BOX, -25800, -2600, -22800, -20000, 0, -20200, LT_DARK_METAL, LT_CRATE, 1300, 0,
    OP_BOX, -25800, -5200, -22800, -21500, -2600, -20200, LT_DARK_METAL, LT_CRATE, 1300, 2,
    OP_BOX, -17400, -2600, -22800, -13200, 0, -20200, LT_DARK_METAL, LT_CRATE, 1300, 0,
    OP_BOX, -26000, -520, -29000, -23200, 0, -24000, LT_FLOOR_GRATE, LT_PLATE_SIDE, 900, 0,
    OP_STAIRS, -23200, -27500, -22100, -25500, 520, 1, 0,
    OP_RAILZ, -29000, -27500, -23200, 520,
    OP_RAILZ, -25500, -24000, -23200, 520,
    OP_CRATE, -14500, -29400, 700, 700,
    OP_CRATE, -13700, -29400, 700, 700,
    OP_CRATE, -14200, -28700, 700, 700,
    OP_CRATE, -12400, -29600, 700, 700,
    OP_CAN, -25200, -23400,
    OP_CAN, -24600, -23400,
    OP_CEIL, 0, 80, 82, 88, 8500, 4600,
    OP_LIGHT, -12300, -3300, -25000, 130, 15, 10, 2600,
    OP_SPRITE, -12350, -3300, -25000, 260, 255, 40, 30, 5,
    OP_CUR, 1,
    OP_BOX, -4200, -3400, -24000, -3400, 0, -22800, LT_DARK_METAL, LT_HAZARD, 900, 0,
    OP_BOX, -600, -3400, -24000, 200, 0, -22800, LT_DARK_METAL, LT_HAZARD, 900, 0,
    OP_BOX, -3800, -3800, -23800, -200, -3400, -23000, LT_BEAM, LT_BEAM, 1200, 2,
    OP_QUAD, -3400, -3420, -23800, 2800, 0, 0, 0, 0, 800, LT_FLOOR_LIGHT, F_EMIT | F_PULSE, 5, ND_PY, 180, 60, 50,
    OP_LIGHT, -2000, -3200, -23400, 120, 20, 15, 3200,
    OP_CONSOLE, -5000, -27200, 2, LT_SCR_GRAPH, 0,
    OP_CONSOLE, 1000, -27200, 2, LT_SCR_VITALS, 0,
    OP_CONSOLE, 1200, -22900, 0, LT_SCR_GRAPH, 0,
    OP_CEIL, 1, 75, 80, 85, 7000, 4000,
    OP_CUR, 2,
    OP_MACHINE, -8800, -16000, -6800, -14400, 2600, ND_PZ,
    OP_MACHINE, -8800, -9600, -6800, -8000, 2600, ND_NZ,
    OP_LIGHT, -7800, -1200, -14000, 110, 50, 20, 2400,
    OP_LIGHT, -7800, -1200, -10000, 110, 50, 20, 2400,
    OP_MACHINE, -6200, -16000, -4200, -14400, 2600, ND_PZ,
    OP_MACHINE, -6200, -9600, -4200, -8000, 2600, ND_NZ,
    OP_LIGHT, -5200, -1200, -14000, 110, 50, 20, 2400,
    OP_LIGHT, -5200, -1200, -10000, 110, 50, 20, 2400,
    OP_MACHINE, 200, -16000, 2200, -14400, 2600, ND_PZ,
    OP_MACHINE, 200, -9600, 2200, -8000, 2600, ND_NZ,
    OP_LIGHT, 1200, -1200, -14000, 110, 50, 20, 2400,
    OP_LIGHT, 1200, -1200, -10000, 110, 50, 20, 2400,
    OP_MACHINE, 2800, -16000, 4800, -14400, 2600, ND_PZ,
    OP_MACHINE, 2800, -9600, 4800, -8000, 2600, ND_NZ,
    OP_LIGHT, 3800, -1200, -14000, 110, 50, 20, 2400,
    OP_LIGHT, 3800, -1200, -10000, 110, 50, 20, 2400,
    OP_CONSOLE, 4500, -12000, 1, LT_SCR_GRAPH, 0,
    OP_CEIL, 2, 75, 78, 85, 7500, 4000,
    OP_CUR, 3,
    OP_SPECIAL, 0, -2000, 3000, 0,
    OP_POD, -8300, -1500, 1, 3,
    OP_POD, -8300, 7500, 1, 4,
    OP_POD, 4300, -1500, 3, 3,
    OP_POD, 4300, 7500, 3, 4,
    OP_BOX, -6000, -1040, 8000, 2000, 0, 10000, LT_FLOOR_GRATE, LT_PLATE_SIDE, 900, 0,
    OP_STAIRS, -8200, 8400, -6000, 10000, 1040, 1, 1,
    OP_RAILX, -6000, 2000, 8000, 1040,
    OP_RAILZ, 8000, 10000, 2000, 1040,
    OP_CONSOLE, -2000, 9500, 0, LT_SCR_VITALS, -1040,
    OP_PILLAR, -4500, -3400, 6500,
    OP_PILLAR, 500, -3400, 6500,
    OP_CEIL, 3, 80, 84, 92, 8500, 4700,
    OP_CUR, 4,
    OP_SPECIAL, 1, 0, 0, 0,
    OP_CAN, -27600, 2600,
    OP_CAN, -27600, 3300,
    OP_CRATE, -27500, 4300, 700, 1200,
    OP_LIGHT, -25000, -4400, 3000, 70, 60, 65, 7500,
    OP_LIGHT, -19000, -4400, 3000, 70, 60, 65, 7500,
    OP_CUR, 5,
    OP_LIGHT, -21500, -2600, 21300, 40, 55, 50, 5000,
    OP_CONSOLE, -25000, 20600, 0, LT_SCR_GRAPH, 0,
    OP_CONSOLE, -22800, 20600, 0, LT_SCR_GRAPH, 0,
    OP_CONSOLE, -20600, 20600, 0, LT_SCR_VITALS, 0,
    OP_CONSOLE, -18400, 20600, 0, LT_SCR_VITALS, 0,
    OP_BOX, -19500, -800, 15600, -18100, 0, 16400, LT_DARK_METAL, LT_MACHINE_PANEL, 800, 0,
    OP_BOX, -19300, -900, 15850, -18500, -800, 16150, LT_SHINI_BODY, LT_SHINI_BODY, 900, 3,
    OP_BOXF, -18500, -980, 15860, -18240, -800, 16140, LT_SHINI_BODY, LT_SHINI_BODY, 900, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_QUAD, -19100, -905, 16150, 400, 0, 0, -200, 0, 700, LT_SHINI_BODY, F_DOUBLE, 0, ND_NONE, -1, 0, 0,
    OP_QUAD, -19100, -905, 15850, 400, 0, 0, -200, 0, -700, LT_SHINI_BODY, F_DOUBLE, 0, ND_NONE, -1, 0, 0,
    OP_LIGHT, -18800, -3000, 16000, 100, 100, 95, 3200,
    OP_SPRITE, -18800, -3900, 16000, 450, 255, 250, 230, 0,
    OP_BOX, -27000, -800, 15000, -26400, 0, 19000, LT_DARK_METAL, LT_MACHINE_PANEL, 800, 0,
    OP_CYL, -26700, -800, 15400, 190, 0, -560, 6, LT_GLASS, F_ADD, 2, 0, 1, -1, 1,
    OP_BOXF, -26780, -1140, 15320, -26620, -980, 15480, LT_SHINI_SKULL, LT_SHINI_SKULL, 400, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_CYL, -26700, -800, 16150, 190, 0, -560, 6, LT_GLASS, F_ADD, 2, 0, 1, -1, 1,
    OP_BOXF, -26780, -1140, 16070, -26620, -980, 16230, LT_SHINI_SKULL, LT_SHINI_SKULL, 400, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_CYL, -26700, -800, 16900, 190, 0, -560, 6, LT_GLASS, F_ADD, 2, 0, 1, -1, 1,
    OP_BOXF, -26780, -1140, 16820, -26620, -980, 16980, LT_SHINI_SKULL, LT_SHINI_SKULL, 400, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_CYL, -26700, -800, 17650, 190, 0, -560, 6, LT_GLASS, F_ADD, 2, 0, 1, -1, 1,
    OP_BOXF, -26780, -1140, 17570, -26620, -980, 17730, LT_SHINI_SKULL, LT_SHINI_SKULL, 400, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_CYL, -26700, -800, 18400, 190, 0, -560, 6, LT_GLASS, F_ADD, 2, 0, 1, -1, 1,
    OP_BOXF, -26780, -1140, 18320, -26620, -980, 18480, LT_SHINI_SKULL, LT_SHINI_SKULL, 400, 3, LT_SHINI_SKULL, ND_PX, 0,
    OP_RACK, -16300, 14600, ND_NX,
    OP_RACK, -16300, 15400, ND_NX,
    OP_CEIL, 5, 80, 85, 90, 7000, 3800,
    OP_CUR, 6,
    OP_BOX, -7850, -2600, 13820, -7150, 0, 14580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -7850, -2600, 14620, -7150, 0, 15380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -7850, -2600, 15420, -7150, 0, 16180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -7850, -2600, 19820, -7150, 0, 20580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -7850, -2600, 20620, -7150, 0, 21380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -7850, -2600, 21420, -7150, 0, 22180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_LIGHT, -6250, -3600, 15000, 35, 55, 100, 4000,
    OP_LIGHT, -6250, -3600, 21000, 35, 55, 100, 4000,
    OP_BOX, -5350, -2600, 13820, -4650, 0, 14580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -5350, -2600, 14620, -4650, 0, 15380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -5350, -2600, 15420, -4650, 0, 16180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -5350, -2600, 19820, -4650, 0, 20580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -5350, -2600, 20620, -4650, 0, 21380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -5350, -2600, 21420, -4650, 0, 22180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_LIGHT, -3750, -3600, 15000, 35, 55, 100, 4000,
    OP_LIGHT, -3750, -3600, 21000, 35, 55, 100, 4000,
    OP_BOX, -2850, -2600, 13820, -2150, 0, 14580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -2850, -2600, 14620, -2150, 0, 15380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -2850, -2600, 15420, -2150, 0, 16180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -2850, -2600, 19820, -2150, 0, 20580, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -2850, -2600, 20620, -2150, 0, 21380, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_BOX, -2850, -2600, 21420, -2150, 0, 22180, LT_DARK_METAL, LT_SERVER_RACK, 800, 0,
    OP_LIGHT, -1250, -3600, 15000, 35, 55, 100, 4000,
    OP_LIGHT, -1250, -3600, 21000, 35, 55, 100, 4000,
    OP_CEIL, 6, 50, 60, 80, 6500, 4500,
    OP_CUR, 7,
    OP_BOX, 9100, -5600, 3880, 11000, -5300, 4120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_BOX, 9100, -5600, 7880, 11000, -5300, 8120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_BOX, 9100, -5600, 11880, 11000, -5300, 12120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_BOX, 9100, -5600, 15880, 11000, -5300, 16120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_BOX, 9100, -5600, 19880, 11000, -5300, 20120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_BOX, 9100, -5600, 23880, 11000, -5300, 24120, LT_BEAM, LT_BEAM, 1200, 3,
    OP_LIGHT, 8000, -5000, 3500, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 3500, 20, 90, 70, 2200,
    OP_LIGHT, 8000, -5000, 7700, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 7700, 20, 90, 70, 2200,
    OP_LIGHT, 8000, -5000, 11900, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 11900, 20, 90, 70, 2200,
    OP_LIGHT, 8000, -5000, 16100, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 16100, 20, 90, 70, 2200,
    OP_LIGHT, 8000, -5000, 20300, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 20300, 20, 90, 70, 2200,
    OP_LIGHT, 8000, -5000, 24500, 75, 80, 70, 6500,
    OP_LIGHT, 10000, -300, 24500, 20, 90, 70, 2200,
    OP_CUR, 8,
    OP_BOX, 8900, -3400, -18000, 12100, 0, -14600, LT_DARK_METAL, LT_MACHINE_BASE, 1000, 0,
    OP_FAN, 10500, -1700, -14595, 1100, 1,
    OP_CYL, 10500, -3400, -16300, 450, 0, -4100, 8, LT_PIPE, 0, 0, 0, 1, -1, 1,
    OP_LIGHT, 10500, -1700, -13600, 110, 55, 25, 3600,
    OP_BOX, 13900, -3400, -18000, 17100, 0, -14600, LT_DARK_METAL, LT_MACHINE_BASE, 1000, 0,
    OP_FAN, 15500, -1700, -14595, 1100, -1,
    OP_CYL, 15500, -3400, -16300, 450, 0, -4100, 8, LT_PIPE, 0, 0, 0, 1, -1, 1,
    OP_LIGHT, 15500, -1700, -13600, 110, 55, 25, 3600,
    OP_BOX, 18000, -1040, -18000, 22500, 0, -12000, LT_FLOOR_GRATE, LT_PLATE_SIDE, 900, 0,
    OP_STAIRS, 18000, -12000, 20000, -10400, 1040, 0, 0,
    OP_RAILX, 20000, 22500, -12000, 1040,
    OP_RAILZ, -18000, -12000, 18000, 1040,
    OP_CONSOLE, 21700, -17000, 1, LT_SCR_GRAPH, -1040,
    OP_CONSOLE, 21700, -15000, 1, LT_SCR_GRAPH, -1040,
    OP_CONSOLE, 21700, -13200, 1, LT_SCR_GRAPH, -1040,
    OP_CEIL, 8, 72, 72, 78, 9000, 5000,
    OP_CUR, 9,
    OP_SPECIAL, 2, 21500, 8500, 0,
    OP_LIGHT, 16000, -8500, 2000, 70, 70, 76, 10000,
    OP_LIGHT, 27000, -8500, 2000, 70, 70, 76, 10000,
    OP_LIGHT, 16000, -8500, 14000, 70, 70, 76, 10000,
    OP_LIGHT, 27000, -8500, 14000, 70, 70, 76, 10000,
    OP_END,

};

static void special_tank(float TX0, float TZ0)
{
    { const float TR = 1300;
      static const int E[3] = { 140, 150, 150 };
      cylinder(v3(TX0, 0, TZ0), TR + 300, 0, -600, 12, LT_MACHINE_BASE, 0, 0, 0, 'y', LT_HAZARD, 1);
      cylinder(v3(TX0, -600, TZ0), TR, 0, -3200, 12, LT_GLASS, F_ADD, 2, 0, 'y', -1, 2);
      cylinder(v3(TX0, -3800, TZ0), TR + 260, 0, -800, 12, LT_DARK_METAL, 0, 0, 0, 'y', LT_DARK_METAL, 1);
      for (int k = 0; k < 6; k++) { float a = k * GPI / 3 + 0.3f; CYL(v3(TX0 + gcos(a) * 900, -4600, TZ0 + gsin(a) * 900), 150, 0, -1900, 6, LT_PIPE); }
      for (int k = 0; k < 12; k++) {
          float a0 = 2 * GPI * k / 12, a1 = 2 * GPI * (k + 1) / 12, r0 = TR + 320, r1 = TR + 560;
          V3 p0 = v3(TX0 + gcos(a0) * r0, -3, TZ0 + gsin(a0) * r0), p1 = v3(TX0 + gcos(a1) * r0, -3, TZ0 + gsin(a1) * r0);
          V3 p2 = v3(TX0 + gcos(a0) * r1, -3, TZ0 + gsin(a0) * r1), p3 = v3(TX0 + gcos(a1) * r1, -3, TZ0 + gsin(a1) * r1);
          GF *f = newface();
          tile_uv(LT_FLOOR_LIGHT, 0, 0, 63, 63, f->uv);
          f->n = 4; f->tile = LT_FLOOR_LIGHT; f->flags = F_QUAD | F_EMIT | F_FLOOR | F_PULSE; f->anim = 2;
          f->emit = 1; f->er = E[0]; f->eg = E[1]; f->eb = E[2]; f->area = CUR;
          if (dot(cross(sub(p2, p0), sub(p1, p0)), v3(0, -1, 0)) < 0) { f->p[0] = p1; f->p[1] = p0; f->p[2] = p3; f->p[3] = p2; }
          else { f->p[0] = p0; f->p[1] = p1; f->p[2] = p2; f->p[3] = p3; }
      }
      cyl_collide(TX0, TZ0, TR + 300, 6500);
      rings[nrings].x = (s16)TX0; rings[nrings].z = (s16)TZ0; rings[nrings].r = (s16)(TR - 60); rings[nrings].yt = -3700; rings[nrings++].yb = -800;
      light(v3(TX0, -2300, TZ0), 0.3f, 1.0f, 1.3f, 10000); light(v3(TX0, -500, TZ0), 0.2f, 0.7f, 0.9f, 3600);
      sprite((int)TX0, -3850, (int)TZ0, 1100, 70, 210, 240, 2, CUR); }
}

/* a barred holding cell, 1.8 m wide and 1.6 m deep, centred at (cx, cz), open side facing `facing`
   (its back against a wall), with a captive shinigami inside and a red lamp */
static void special_cell(float cx, float cz, float facing)
{
    V3 f = v3(gsin(facing), 0, gcos(facing)), r = v3(gcos(facing), 0, -gsin(facing)), c = v3(cx, -2600, cz);
    V3 fl = add(add(c, mul(f, 800)), mul(r, -900)), bl = add(add(c, mul(f, -800)), mul(r, -900));
    quad(fl, mul(r, 1800), v3(0, 2600, 0), LT_CAGE_BARS, F_DOUBLE, 0, 0, NOND);
    quad(bl, mul(f, 1600), v3(0, 2600, 0), LT_CAGE_BARS, F_DOUBLE, 0, 0, NOND);
    quad(add(bl, mul(r, 1800)), mul(f, 1600), v3(0, 2600, 0), LT_CAGE_BARS, F_DOUBLE, 0, 0, NOND);
    grid(bl, mul(r, 1800), mul(f, 1600), 900, LT_DARK_METAL, 0, v3(0, 1, 0));
    grid(bl, mul(r, 1800), mul(f, 1600), 900, LT_BEAM, 0, v3(0, -1, 0));
    float ex = gabs(f.x) * 800 + gabs(r.x) * 900, ez = gabs(f.z) * 800 + gabs(r.z) * 900;
    coll(cx - ex, cz - ez, cx + ex, cz + ez, 2600);
    captive(cx, -60, cz, 0.85f, facing);
    light(v3(cx, -2300, cz), 1.0f, 0.15f, 0.12f, 2800);
}

static void special_cells(void)
{
    static const float CX[4] = { -26300, -24200, -19000, -16700 };
    for (int ci = 0; ci < 4; ci++) { special_cell(CX[ci], -1200, 0); special_cell(CX[ci], 7200, GPI); }
}

static void special_core(float RX, float RZ)
{
    {
      static const int E[3] = { 170, 170, 170 };
      BOX(RX - 2200, -300, RZ - 2200, RX + 2200, 0, RZ + 2200, LT_HAZARD, LT_PLATE_SIDE, 1100);
      cylinder(v3(RX, -300, RZ), 1300, 0, -5200, 12, LT_COIL, 0, 0, 0, 'y', -1, 4);
      for (int k = 0; k < 3; k++) cylinder(v3(RX, -1300.f - 1300 * k, RZ), 1370, 0, -320, 12, LT_ENERGY, F_EMIT | F_PULSE, 6 + k % 2, E, 'y', -1, 1);
      cylinder(v3(RX, -5500, RZ), 1700, 0, -700, 12, LT_DARK_METAL, 0, 0, 0, 'y', LT_DARK_METAL, 1);
      CYL(v3(RX, -6200, RZ), 600, 0, -2800, 8, LT_PIPE);
      cylinder(v3(RX + 1650, -6000, RZ), 320, 0, 5850, 8, LT_PIPE, 0, 0, 0, 'x', -1, 1);
      cylinder(v3(RX - 7500, -6000, RZ), 320, 0, 5850, 8, LT_PIPE, 0, 0, 0, 'x', -1, 1);
      cylinder(v3(RX, -6000, RZ + 1650), 320, 0, 5850, 8, LT_PIPE, 0, 0, 0, 'z', -1, 1);
      cyl_collide(RX, RZ, 2200, 300);
      coll(RX - 1400, RZ - 1400, RX + 1400, RZ + 1400, 9000);
      occl(RX - 1000, -5500, RZ - 1000, RX + 1000, 0, RZ + 1000);
      light(v3(RX, -2600, RZ), 1.5f, 0.75f, 0.3f, 14000); light(v3(RX, -7000, RZ), 0.8f, 0.5f, 0.3f, 8000);
      sprite((int)RX, -2600, (int)RZ, 2300, 255, 150, 60, 6, CUR);
      for (int k = 0; k < 6; k++) { float a = k * GPI / 3 + GPI / 6; console(RX + gsin(a) * 5200, RZ + gcos(a) * 5200, a + GPI, k % 2 ? LT_SCR_GRAPH : LT_SCR_VITALS, 0); } }
}

static void place_props_from(const short *p)
{
    for (;;) {
        int op = *p++;
#define A(i) ((float)p[i])
        switch (op) {
        case OP_END: return;
        case OP_CUR: CUR = p[0]; p += 1; break;
        case OP_BOX: case OP_BOXF: {
            BoxOpt o;
            memset(&o, 0, sizeof(o));
            o.top = p[6]; o.side = p[7]; o.cell = A(8); o.nooccl = p[9] & 1; o.nocoll = p[9] >> 1;
            if (op == OP_BOXF) {
                static int eg[3];
                o.front1 = p[10] + 1; o.nd_front = nd_of(p[11]);
                if (p[12]) { eg[0] = eg[1] = eg[2] = p[12]; o.emit_front = eg; }
            }
            box_(A(0), A(1), A(2), A(3), A(4), A(5), o);
            p += op == OP_BOXF ? 13 : 10; break; }
        case OP_STAIRS: stairs(A(0), A(1), A(2), A(3), A(4), p[5], p[6]); p += 7; break;
        case OP_RAILX: rail_x(A(0), A(1), A(2), A(3)); p += 4; break;
        case OP_RAILZ: rail_z(A(0), A(1), A(2), A(3)); p += 4; break;
        case OP_CRATE: crate(A(0), A(1), A(2), A(3)); p += 4; break;
        case OP_CAN: canister(A(0), A(1)); p += 2; break;
        case OP_CEIL: ceiling_lights(p[0], A(1) / 100.f, A(2) / 100.f, A(3) / 100.f, A(4), A(5), 0); p += 6; break;
        case OP_LIGHT: light(v3(A(0), A(1), A(2)), A(3) / 100.f, A(4) / 100.f, A(5) / 100.f, A(6)); p += 7; break;
        case OP_SPRITE: sprite(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], CUR); p += 8; break;
        case OP_CONSOLE: console(A(0), A(1), facing_of(p[2]), p[3], A(4)); p += 5; break;
        case OP_MACHINE: machine(A(0), A(1), A(2), A(3), A(4), nd_of(p[5])); p += 6; break;
        case OP_CYL: {
            static int eg[3];
            eg[0] = eg[1] = eg[2] = p[10];
            cylinder(v3(A(0), A(1), A(2)), A(3), A(4), A(5), p[6], p[7], p[8], p[9], p[10] ? eg : 0, "xyz"[p[11]], p[12], p[13]);
            p += 14; break; }
        case OP_QUAD: {
            static int e[3];
            e[0] = p[13]; e[1] = p[14]; e[2] = p[15];
            quad(v3(A(0), A(1), A(2)), v3(A(3), A(4), A(5)), v3(A(6), A(7), A(8)), p[9], p[10], p[11], p[13] >= 0 ? e : 0, nd_of(p[12]));
            p += 16; break; }
        case OP_POD: extraction_pod(A(0), A(1), facing_of(p[2]), p[3]); p += 4; break;
        case OP_PILLAR: pillar(A(0), A(1), A(2)); p += 3; break;
        case OP_RACK: rack(A(0), A(1), nd_of(p[2])); p += 3; break;
        case OP_FAN:
            fans[nfans].x = p[0]; fans[nfans].y = p[1]; fans[nfans].z = p[2]; fans[nfans].r = p[3]; fans[nfans++].speed = p[4];
            p += 5; break;
        case OP_SPECIAL:
            if (p[0] == SPECIAL_TANK) special_tank(A(1), A(2));
            else if (p[0] == SPECIAL_CELLS) special_cells();
            else if (p[0] == SPECIAL_CORE) special_core(A(1), A(2));
            else special_cell(A(1), A(2), facing_of(p[3]));
            p += 4; break;
        }
#undef A
    }
}

/* ------------------------------------------------------------------ sector maps (gen/map.h)
   A Build-style map: every sector's floor and ceiling is cut on the world's 2 m tile grid (1 m
   pieces on floors for the PS1's affine texturing), every wall into 2 m columns and rows, each
   cell with its own tile. Portals get a lower wall up to the next floor and an upper wall down
   from the next ceiling. Then the same bake and export as the plant. */
static const MapDef *MAP, *map_sel;
void gen_level_select(const MapDef *m) { map_sel = m; }
const MapDef *gen_level_map(void) { return map_sel; }

typedef struct { float h, k, ax, az, nx, nz; } Plane;          /* up-positive height = h + k * dist from hinge */
static Plane sec_plane(const MapSector *s, int ceil)
{
    Plane P;
    int w = ceil ? s->cslope : s->fslope;
    memset(&P, 0, sizeof(P));
    P.h = ceil ? s->ceil : s->floor;
    if (s->kind == SK_DOOR && ceil) P.h = (float)(s->floor + s->ka);    /* doors are built open */
    if (s->kind == SK_LIFT && !ceil) P.h = s->ka;                        /* the lift shaft's floor */
    if (w < 0) return P;
    const MapPt *a = &MAP->pts[s->p0 + w], *b = &MAP->pts[s->p0 + (w + 1) % s->np];
    float dx = (float)(b->x - a->x), dz = (float)(b->z - a->z), L = gsqrt(dx * dx + dz * dz), maxd = 0;
    P.ax = a->x; P.az = a->z; P.nx = -dz / L; P.nz = dx / L;
    for (int i = 0; i < s->np; i++) {
        const MapPt *p = &MAP->pts[s->p0 + i];
        maxd = gmax(maxd, (p->x - P.ax) * P.nx + (p->z - P.az) * P.nz);
    }
    P.k = maxd > 0 ? ((ceil ? s->cslope_h : s->fslope_h) - P.h) / maxd : 0;
    return P;
}
static float ph(const Plane *P, float x, float z) { return P->h + P->k * ((x - P->ax) * P->nx + (z - P->az) * P->nz); }
static int pmod(int a, int m) { return m <= 0 ? 0 : ((a % m) + m) % m; }
static int ffloor_div(float v, float d) { return (int)gfloor(v / d); }

static int flat_tile(unsigned off, int cx, int cz)
{
    const u8 *b = MAP->spec + off, *r = b + 2;
    int t = b[0];
    for (int i = 0; i < b[1]; i++, r += 5) if (pmod(cx, r[1]) == r[3] && pmod(cz, r[2]) == r[4]) t = r[0];
    int nc = *r++;
    for (int i = 0; i < nc; i++, r += 3) if ((s8)r[0] == cx && (s8)r[1] == cz) t = r[2];
    return t;
}

/* one face: 3 or 4 corners (4 = Z order: 0 1 / 2 3), local tile UVs 0..63, turned to face nd */
static GF *emit_face(V3 *p, float (*uv)[2], int n, int tile, int flags, V3 nd)
{
    GF *f = newface();
    int tx = (tile & 15) % 4 * 64, ty = (tile & 15) / 4 * 64, ord[4] = { 0, 1, 2, 3 };
    if (dot(cross(sub(p[2], p[0]), sub(p[1], p[0])), nd) < 0) {
        if (n == 3) { ord[1] = 2; ord[2] = 1; }
        else { ord[0] = 1; ord[1] = 0; ord[2] = 3; ord[3] = 2; }
    }
    for (int c = 0; c < 4; c++) {
        int k = ord[c < n ? c : n - 1];
        f->p[c] = p[k];
        f->uv[c][0] = (u8)(tx + iround(gclamp(uv[k][0], 0, 63)));
        f->uv[c][1] = (u8)(ty + iround(gclamp(uv[k][1], 0, 63)));
    }
    if (LT_ANIMATED(tile)) flags |= F_ANIMUV;
    f->n = n; f->tile = tile; f->flags = (flags & ~F_QUAD) | (n == 4 ? F_QUAD : 0);
    if (tile == LT_CEILING_LIGHT || tile == LT_FLOOR_LIGHT) { f->flags |= F_EMIT; f->emit = 1; f->er = 150; f->eg = 160; f->eb = 170; }
    return f;
}

/* polygon (x,z) clipped to a rectangle (Sutherland-Hodgman; a concave input may leave zero-width seams) */
static int clip_rect(float (*in)[2], int n, float x0, float z0, float x1, float z1, float (*out)[2])
{
    float buf[2][64][2];
    float (*src)[2] = in;
    int m = n;
    for (int e = 0; e < 4; e++) {
        float (*dst)[2] = buf[e & 1];
        int k = 0;
        for (int i = 0; i < m && k < 62; i++) {
            float *a = src[i], *b = src[(i + 1) % m];
            float da = e == 0 ? a[0] - x0 : e == 1 ? x1 - a[0] : e == 2 ? a[1] - z0 : z1 - a[1];
            float db = e == 0 ? b[0] - x0 : e == 1 ? x1 - b[0] : e == 2 ? b[1] - z0 : z1 - b[1];
            if (da >= 0) { dst[k][0] = a[0]; dst[k][1] = a[1]; k++; }
            if ((da >= 0) != (db >= 0)) { float t = da / (da - db); dst[k][0] = a[0] + (b[0] - a[0]) * t; dst[k][1] = a[1] + (b[1] - a[1]) * t; k++; }
        }
        src = dst; m = k;
        if (m < 3) return 0;
    }
    for (int i = 0; i < m; i++) { out[i][0] = src[i][0]; out[i][1] = src[i][1]; }
    return m;
}
static float area2d(float (*p)[2], int n)
{
    float a = 0;
    for (int i = 0; i < n; i++) a += p[i][0] * p[(i + 1) % n][1] - p[(i + 1) % n][0] * p[i][1];
    return a / 2;
}
/* ear clipping of a counter-clockwise polygon; returns the number of triangles */
static int earclip(float (*p)[2], int n, int (*tri)[3])
{
    int idx[64], m = 0, nt = 0;
    for (int i = 0; i < n; i++) {               /* drop repeats and straight-through points */
        float *a = p[(i + n - 1) % n], *b = p[i], *c = p[(i + 1) % n];
        float cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
        if (gabs(cr) > 1e-3f && (gabs(b[0] - c[0]) + gabs(b[1] - c[1]) > 0.5f)) idx[m++] = i;
    }
    int guard = 0;
    while (m > 3 && guard++ < 400) {
        int found = 0;
        for (int i = 0; i < m && !found; i++) {
            float *a = p[idx[(i + m - 1) % m]], *b = p[idx[i]], *c = p[idx[(i + 1) % m]];
            float cr = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            if (cr <= 0) continue;
            int ok = 1;
            for (int j = 0; j < m && ok; j++) {
                float *q = p[idx[j]];
                if (q == a || q == b || q == c) continue;
                float d0 = (b[0] - a[0]) * (q[1] - a[1]) - (b[1] - a[1]) * (q[0] - a[0]);
                float d1 = (c[0] - b[0]) * (q[1] - b[1]) - (c[1] - b[1]) * (q[0] - b[0]);
                float d2 = (a[0] - c[0]) * (q[1] - c[1]) - (a[1] - c[1]) * (q[0] - c[0]);
                if (d0 > 0 && d1 > 0 && d2 > 0) ok = 0;
            }
            if (!ok) continue;
            tri[nt][0] = idx[(i + m - 1) % m]; tri[nt][1] = idx[i]; tri[nt][2] = idx[(i + 1) % m]; nt++;
            for (int j = i; j < m - 1; j++) idx[j] = idx[j + 1];
            m--; found = 1;
        }
        if (!found) break;
    }
    if (m == 3) { tri[nt][0] = idx[0]; tri[nt][1] = idx[1]; tri[nt][2] = idx[2]; nt++; }
    return nt;
}

static void build_flat(const MapSector *s, int ceil)
{
    Plane P = sec_plane(s, ceil);
    float poly[64][2], piece[64][2], mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f;
    int n = s->np < 60 ? s->np : 60;
    for (int i = 0; i < n; i++) {
        poly[i][0] = MAP->pts[s->p0 + i].x; poly[i][1] = MAP->pts[s->p0 + i].z;
        mnx = gmin(mnx, poly[i][0]); mxx = gmax(mxx, poly[i][0]); mnz = gmin(mnz, poly[i][1]); mxz = gmax(mxz, poly[i][1]);
    }
    const float T = MAP_TILE, sub = ceil ? MAP_TILE : MAP_TILE / 2;
    V3 nd = v3(0, ceil ? 1.f : -1.f, 0);
    int flags = ceil ? 0 : F_FLOOR;
    for (int cz = ffloor_div(mnz, T); cz * T < mxz; cz++)
        for (int cx = ffloor_div(mnx, T); cx * T < mxx; cx++) {
            int tile = flat_tile(ceil ? s->ctex : s->ftex, cx, cz);
            for (float sz = cz * T; sz < (cz + 1) * T - 1; sz += sub)
                for (float sx = cx * T; sx < (cx + 1) * T - 1; sx += sub) {
                    int m = clip_rect(poly, n, sx, sz, sx + sub, sz + sub, piece);
                    if (m < 3) continue;
                    float a = area2d(piece, m);
                    if (a < 2000) continue;
#define FUV(x, z, o) (o[0] = ((x) - cx * T) / T * 63, o[1] = ceil ? ((z) - cz * T) / T * 63 : ((cz + 1) * T - (z)) / T * 63)
                    if (gabs(a - sub * sub) < 1) {
                        V3 q[4]; float uv[4][2];
                        float xs[4] = { sx, sx + sub, sx, sx + sub }, zs[4] = { sz, sz, sz + sub, sz + sub };
                        for (int c = 0; c < 4; c++) { q[c] = v3(xs[c], -ph(&P, xs[c], zs[c]), zs[c]); FUV(xs[c], zs[c], uv[c]); }
                        GF *f = emit_face(q, uv, 4, tile, flags, nd);
                        f->base = q[0].y;
                    } else {
                        int tri[64][3], nt = earclip(piece, m, tri);
                        for (int t = 0; t < nt; t++) {
                            V3 q[3]; float uv[3][2];
                            for (int c = 0; c < 3; c++) {
                                float *v = piece[tri[t][c]];
                                q[c] = v3(v[0], -ph(&P, v[0], v[1]), v[1]); FUV(v[0], v[1], uv[c]);
                            }
                            GF *f = emit_face(q, uv, 3, tile, flags, nd);
                            f->base = q[0].y;
                        }
                    }
#undef FUV
                }
        }
}

static void wseg(float x0, float z0, float x1, float z1, float lo, float hi)
{
    if (hi - lo < 1) return;
    if (nwsegs == capwsegs) { capwsegs = capwsegs ? capwsegs * 2 : 512; wsegs = (WSeg *)realloc(wsegs, sizeof(WSeg) * (size_t)capwsegs); }
    WSeg *w = &wsegs[nwsegs++];
    w->x0 = x0; w->z0 = z0; w->x1 = x1; w->z1 = z1; w->ytop = -hi; w->ybot = -lo;
}

/* one part of a wall: between the lines bottom (Bb at b .. Ba at a) and top, tiles from a wall spec.
   t runs from b (the viewer's left, facing the wall) to a. anchor_top: rows hang from the top (upper
   walls); otherwise they stand on the bottom, with an optional pinned top row. */
static void wall_part(unsigned spec, V3 a, V3 b, float Ba, float Bb, float Ta, float Tb, float Fa, float Fb, int anchor_top, int wflags, V3 nd)
{
    if (spec == MAP_NONE) return;
    const u8 *sp = MAP->spec + spec;
    int nrows = sp[0], top = sp[1];
    const u8 *rows[16]; int rl[16];
    const u8 *q = sp + 2;
    for (int r = 0; r < nrows && r < 16; r++) { rl[r] = q[0]; rows[r] = q + 1; q += 1 + q[0]; }
    int nov = *q++;
    const u8 *ov = q;
    const float T = MAP_TILE, SUBW = 1024;
    float L = len(sub(a, b));
    if (L < 1 || (Ta - Ba < 1 && Tb - Bb < 1)) return;
    float hb = gmin(Ba, Bb), ht = gmax(Ta, Tb);
    /* bands: [h0, h1], texture anchor height (bottom of the tile if !atop, else top), row index */
    float band[24][3]; int btile_row[24], batop[24], nb = 0;
    if (anchor_top) {
        for (int r = 0; nb < 24; r++) {
            float h1 = ht - r * T, h0 = gmax(hb, h1 - T);
            if (h1 - h0 < 1) break;
            band[nb][0] = h0; band[nb][1] = h1; band[nb][2] = h1; btile_row[nb] = r; batop[nb] = 1; nb++;
            if (h0 <= hb) break;
        }
    } else {
        float lim = ht;
        if (top != 255 && ht - hb > 1) lim = gmax(hb, ht - T);
        for (int r = 0; nb < 23; r++) {
            float h0 = hb + r * T, h1 = gmin(lim, h0 + T);
            if (h1 - h0 < 1) break;
            band[nb][0] = h0; band[nb][1] = h1; band[nb][2] = h0; btile_row[nb] = r; batop[nb] = 0; nb++;
        }
        if (top != 255 && ht - lim > 1) { band[nb][0] = lim; band[nb][1] = ht; band[nb][2] = ht; btile_row[nb] = -1; batop[nb] = 1; nb++; }
    }
    /* columns along t from b */
    float c0 = 0;
    if (wflags & WF_ALIGN_END) c0 = L - T * gfloor(L / T + 0.999f);
    else if (wflags & WF_ALIGN_MID) c0 = -(T * gfloor(L / T + 0.999f) - L) / 2;
    int col = 0;
    for (float cs = c0; cs < L - 1; cs += T, col++) {
        float t0 = gmax(0, cs), t1 = gmin(L, cs + T);
        if (t1 - t0 < 1) continue;
        for (int bi = 0; bi < nb; bi++) {
            int r = btile_row[bi], tile;
            if (r < 0) tile = top;
            else if (!nrows) tile = LT_DARK_METAL;
            else { int rr = r < nrows ? r : nrows - 1; tile = rows[rr][col % rl[rr]]; }
            for (int k = 0; k < nov; k++) if (ov[k * 3] == col && ov[k * 3 + 1] == (r < 0 ? nb - 1 : r)) tile = ov[k * 3 + 2];
            float h0 = band[bi][0], h1 = band[bi][1], anc = band[bi][2];
            int nu = iceil((t1 - t0) / SUBW - 1e-3f), nv = iceil((h1 - h0) / SUBW - 1e-3f);
            for (int j = 0; j < nv; j++)
                for (int i = 0; i < nu; i++) {
                    float u0 = t0 + (t1 - t0) * i / nu, u1 = t0 + (t1 - t0) * (i + 1) / nu;
                    float v0 = h0 + (h1 - h0) * j / nv, v1 = h0 + (h1 - h0) * (j + 1) / nv;
                    float tt[2] = { u0, u1 }, hh[4];
                    for (int e = 0; e < 2; e++) {
                        float w = tt[e] / L, B = Bb + (Ba - Bb) * w, Tt = Tb + (Ta - Tb) * w;
                        hh[e * 2] = gclamp(v1, B, Tt); hh[e * 2 + 1] = gclamp(v0, B, Tt);
                    }
                    if (hh[0] - hh[1] < 1 && hh[2] - hh[3] < 1) continue;
                    V3 p[4]; float uv[4][2];
                    for (int c = 0; c < 4; c++) {
                        int e = c & 1;                      /* 0 left (u0), 1 right (u1) */
                        float tpos = tt[e], h = c < 2 ? hh[e * 2] : hh[e * 2 + 1];
                        V3 g = add(b, mul(sub(a, b), tpos / L));
                        p[c] = v3(g.x, -h, g.z);
                        uv[c][0] = (tpos - cs) / T * 63;
                        uv[c][1] = batop[bi] ? (anc - h) / T * 63 : 63 - (h - anc) / T * 63;
                    }
                    GF *f = emit_face(p, uv, 4, tile, 0, nd);
                    f->base = -(Fb + (Fa - Fb) * ((u0 + u1) / 2 / L));
                }
        }
    }
}

static void build_walls(int si)
{
    const MapSector *s = &MAP->secs[si];
    Plane F = sec_plane(s, 0), C = sec_plane(s, 1);
    for (int i = 0; i < s->np; i++) {
        const MapPt *pa = &MAP->pts[s->p0 + i], *pb = &MAP->pts[s->p0 + (i + 1) % s->np];
        const MapWall *w = &MAP->walls[s->p0 + i];
        float dx = (float)(pb->x - pa->x), dz = (float)(pb->z - pa->z), L = gsqrt(dx * dx + dz * dz);
        V3 a = v3(pa->x, 0, pa->z), b = v3(pb->x, 0, pb->z), nd = v3(-dz / L, 0, dx / L);
        float fa = ph(&F, a.x, a.z), fb = ph(&F, b.x, b.z), ca = ph(&C, a.x, a.z), cb = ph(&C, b.x, b.z);
        int sh = !(w->flags & WF_NOSHADOW);
        if (ca - fa < 1 && cb - fb < 1) continue;                   /* a solid block: its neighbours draw it */
        if (!w->next) {
            wall_part(w->mid, a, b, fa, fb, ca, cb, fa, fb, 0, w->flags, nd);
            if (sh) wseg(a.x, a.z, b.x, b.z, gmin(fa, fb), gmax(ca, cb));
            continue;
        }
        const MapSector *o = &MAP->secs[w->next - 1];
        Plane OF = sec_plane(o, 0), OC = sec_plane(o, 1);
        float ofa = ph(&OF, a.x, a.z), ofb = ph(&OF, b.x, b.z), oca = ph(&OC, a.x, a.z), ocb = ph(&OC, b.x, b.z);
        if (oca - ofa < 1 && ocb - ofb < 1) {                       /* neighbour is solid: full wall */
            wall_part(w->mid, a, b, fa, fb, ca, cb, fa, fb, 0, w->flags, nd);
            if (sh) wseg(a.x, a.z, b.x, b.z, gmin(fa, fb), gmax(ca, cb));
            continue;
        }
        if (ofa > fa + 1 || ofb > fb + 1) {
            float ta = gmin(gmax(ofa, fa), ca), tb = gmin(gmax(ofb, fb), cb);
            wall_part(w->low != MAP_NONE ? w->low : w->mid, a, b, fa, fb, ta, tb, fa, fb, 0, w->flags & ~0, nd);
            if (sh) wseg(a.x, a.z, b.x, b.z, gmin(fa, fb), gmax(ta, tb));
        }
        if (oca < ca - 1 || ocb < cb - 1) {
            float ba = gmax(gmin(oca, ca), fa), bb = gmax(gmin(ocb, cb), fb);
            wall_part(w->up != MAP_NONE ? w->up : w->mid, a, b, ba, bb, ca, cb, fa, fb, 1, w->flags, nd);
            if (sh) wseg(a.x, a.z, b.x, b.z, gmin(ba, bb), gmax(ca, cb));
        }
        if (w->flags & (WF_GLASS | WF_BARS)) {                       /* a pane across the opening */
            float lo_a = gmax(fa, ofa), lo_b = gmax(fb, ofb), hi_a = gmin(ca, oca), hi_b = gmin(cb, ocb);
            V3 p[4] = { v3(b.x, -hi_b, b.z), v3(a.x, -hi_a, a.z), v3(b.x, -lo_b, b.z), v3(a.x, -lo_a, a.z) };
            float uv[4][2] = { { 0, 0 }, { 63, 0 }, { 0, 63 }, { 63, 63 } };
            int glass = w->flags & WF_GLASS;
            GF *f = emit_face(p, uv, 4, glass ? LT_GLASS : LT_CAGE_BARS, F_DOUBLE | (glass ? F_ADD : 0), nd);
            f->anim = glass ? 2 : 0;
        }
    }
}

static float tri_area_xz(const MapPt *a, const MapPt *b, const MapPt *c) { return ((b->x - a->x) * (float)(c->z - a->z) - (b->z - a->z) * (float)(c->x - a->x)) / 2; }

static int map_inside(const MapSector *s, float x, float z)
{
    int c = 0;
    for (int i = 0, j = s->np - 1; i < s->np; j = i++) {
        const MapPt *pi = &MAP->pts[s->p0 + i], *pj = &MAP->pts[s->p0 + j];
        if ((pi->z > z) != (pj->z > z) && x < pi->x + (z - pi->z) * (float)(pj->x - pi->x) / (float)(pj->z - pi->z)) c = !c;
    }
    return c;
}

static void gen_map_geometry(void)
{
    MAP = map_sel;
    nfaces = nocc = ncols = nlights = nsprites = nfans = nrings = npods = nceils = nareas = nwsegs = 0;
    /* rooms are the areas: bounds and links from the sectors */
    for (int r = 0; r < MAP->nrooms && r < 32; r++) {
        Area *a = &areas[nareas++];
        memset(a, 0, sizeof(*a));
        a->name = MAP->rooms[r]; a->x0 = a->z0 = 1e9f; a->x1 = a->z1 = -1e9f;
    }
    for (int si = 0; si < MAP->nsecs; si++) {
        const MapSector *s = &MAP->secs[si];
        Area *a = &areas[s->room];
        for (int i = 0; i < s->np; i++) {
            const MapPt *p = &MAP->pts[s->p0 + i];
            a->x0 = gmin(a->x0, p->x); a->x1 = gmax(a->x1, p->x); a->z0 = gmin(a->z0, p->z); a->z1 = gmax(a->z1, p->z);
            const MapWall *w = &MAP->walls[s->p0 + i];
            if (w->next) { int o = MAP->secs[w->next - 1].room; if (o != s->room) a->links |= 1u << o; }
        }
        a->H = gmax(a->H, (float)(s->ceil - s->floor));
    }
    for (int si = 0; si < MAP->nsecs; si++) {
        const MapSector *s = &MAP->secs[si];
        CUR = s->room;
        if (s->ceil > s->floor || s->kind == SK_DOOR) { build_flat(s, 0); build_flat(s, 1); }
        build_walls(si);
    }
    CUR = 0;
    {   /* props, lights and halos: the map's placement script */
        const short *save = 0; (void)save;
        place_props_from(MAP->props);
    }
    bake();
    export_mesh();

    lab.nboxes = ncols; lab.boxes = (LabBox *)malloc(sizeof(LabBox) * (size_t)(ncols + 1));
    for (int i = 0; i < ncols; i++) {
        LabBox *b = &lab.boxes[i];
        b->x0 = (s16)(int)cols[i].x0; b->z0 = (s16)(int)cols[i].z0; b->x1 = (s16)(int)cols[i].x1; b->z1 = (s16)(int)cols[i].z1;
        b->h = (s16)(int)gmin(cols[i].h, 32000);
    }
    memcpy(lab.sprites, sprites, sizeof(sprites)); lab.nsprites = nsprites;
    memcpy(lab.fans, fans, sizeof(fans)); lab.nfans = nfans;
    memcpy(lab.rings, rings, sizeof(rings)); lab.nrings = nrings;
    memcpy(lab.pods, pods, sizeof(pods)); lab.npods = npods;
    lab.nceil = 0;
    lab.nareas = nareas;
    for (int i = 0; i < nareas; i++) {
        unsigned seen = 1u << i, frontier = seen;
        for (int s = 0; s < 2; s++) {
            unsigned nxt = 0;
            for (int a = 0; a < nareas; a++) if (frontier & (1u << a)) nxt |= areas[a].links;
            for (int a = 0; a < nareas; a++) if (areas[a].links & frontier) nxt |= 1u << a;
            seen |= nxt; frontier = nxt;
        }
        LabArea *o = &lab.areas[i];
        o->x0 = (s16)areas[i].x0; o->z0 = (s16)areas[i].z0; o->x1 = (s16)areas[i].x1; o->z1 = (s16)areas[i].z1; o->pvs = seen; o->name = areas[i].name;
    }
    /* a camera view per room: from inside its biggest sector, near the corner furthest from the centre, looking across */
    lab.nviews = 0;
    for (int r = 0; r < nareas && lab.nviews < 16; r++) {
        int best = -1; float ba = 0;
        for (int si = 0; si < MAP->nsecs; si++) {
            const MapSector *s = &MAP->secs[si];
            if (s->room != r || s->ceil <= s->floor) continue;
            float A = 0;
            for (int i = 1; i + 1 < s->np; i++) A += tri_area_xz(&MAP->pts[s->p0], &MAP->pts[s->p0 + i], &MAP->pts[s->p0 + i + 1]);
            if (gabs(A) > ba) { ba = gabs(A); best = si; }
        }
        if (best < 0) continue;
        const MapSector *s = &MAP->secs[best];
        float cx = 0, cz = 0;
        for (int i = 0; i < s->np; i++) { cx += MAP->pts[s->p0 + i].x; cz += MAP->pts[s->p0 + i].z; }
        cx /= s->np; cz /= s->np;
        float vx = cx, vz = cz, fd = 0;
        for (int i = 0; i < s->np; i++) {
            const MapPt *p = &MAP->pts[s->p0 + i];
            float x = cx + (p->x - cx) * 0.8f, z = cz + (p->z - cz) * 0.8f, d = (x - cx) * (x - cx) + (z - cz) * (z - cz);
            if (d > fd && map_inside(s, x, z)) { fd = d; vx = x; vz = z; }
        }
        Plane F = sec_plane(s, 0);
        LabView *v = &lab.views[lab.nviews++];
        v->name = areas[r].name; v->x = (s32)vx; v->z = (s32)vz;
        v->y = (s32)-(ph(&F, vx, vz) + gmin((float)(s->ceil - s->floor) - 600, 3200));
        v->yaw = iround(gatan2(cx - vx, cz - vz) / (2 * GPI) * 4096) & 4095; v->pitch = 170;
    }
    /* the sectors and walls the game collides with */
    lab.nsecs = MAP->nsecs; lab.nwalls = MAP->npts;
    lab.secs = (LabSec *)malloc(sizeof(LabSec) * (size_t)MAP->nsecs);
    lab.walls = (LabWall *)malloc(sizeof(LabWall) * (size_t)MAP->npts);
    for (int si = 0; si < MAP->nsecs; si++) {
        const MapSector *s = &MAP->secs[si];
        LabSec *o = &lab.secs[si];
        memset(o, 0, sizeof(*o));
        o->x0 = o->z0 = 32767; o->x1 = o->z1 = -32767;
        for (int c = 0; c < 2; c++) {
            Plane P = sec_plane(s, c);
            LabPlane *q = c ? &o->c : &o->f;
            q->h = (s16)iround(c ? (float)s->ceil : (float)s->floor);
            if (s->kind == SK_DOOR && c) q->h = s->floor;            /* closed; the game opens it */
            if (s->kind == SK_LIFT && !c) q->h = s->floor;
            if (P.k != 0) { q->h = (s16)iround(P.h); q->ax = (s16)P.ax; q->az = (s16)P.az; q->kx = (s16)iround(P.k * P.nx * 4096); q->kz = (s16)iround(P.k * P.nz * 4096); }
        }
        o->w0 = s->p0; o->nw = s->np; o->room = s->room; o->kind = s->kind; o->lock = s->lock; o->ka = s->ka; o->kb = s->kb;
        for (int i = 0; i < s->np; i++) {
            const MapPt *pa = &MAP->pts[s->p0 + i], *pb = &MAP->pts[s->p0 + (i + 1) % s->np];
            LabWall *w = &lab.walls[s->p0 + i];
            float dx = (float)(pb->x - pa->x), dz = (float)(pb->z - pa->z), L = gsqrt(dx * dx + dz * dz);
            w->x0 = pa->x; w->z0 = pa->z; w->x1 = pb->x; w->z1 = pb->z;
            w->nx = (s16)iround(-dz / L * 4096); w->nz = (s16)iround(dx / L * 4096);
            w->next = MAP->walls[s->p0 + i].next; w->sec = (u16)si;
            if (pa->x < o->x0) o->x0 = pa->x;
            if (pa->x > o->x1) o->x1 = pa->x;
            if (pa->z < o->z0) o->z0 = pa->z;
            if (pa->z > o->z1) o->z1 = pa->z;
        }
    }
    lab.has_spawn = 0;
    for (int i = 0; i < MAP->nthings; i++)
        if (MAP->things[i].type == TH_START) { lab.spawn_x = MAP->things[i].x; lab.spawn_z = MAP->things[i].z; lab.spawn_yaw = MAP->things[i].yaw; lab.has_spawn = 1; break; }
    lab.nroute = 0;
    for (int i = 0; i < MAP->nthings && lab.nroute < 48; i++)
        if (MAP->things[i].type == TH_START || MAP->things[i].type == TH_WAY) {
            lab.route[lab.nroute][0] = MAP->things[i].x; lab.route[lab.nroute][1] = MAP->things[i].z; lab.nroute++;
        }
    free(faces); faces = 0; nfaces = capfaces = 0;
    free(wsegs); wsegs = 0; nwsegs = capwsegs = 0;
}

#define SPAWN_X -19000
#define SPAWN_Z -26500
static const int ROUTE_HALLS_N = 9;

void gen_lab_geometry(void)
{
    if (map_sel) { gen_map_geometry(); return; }
    nfaces = nocc = ncols = nlights = nsprites = nfans = nrings = npods = nceils = nareas = 0;
    int DOCK = area("Loading Dock", -26000, -30000, -12000, -20000, 6500, 0);
    int CHECK = area("Security Checkpoint", -6000, -28000, 2000, -22000, 4500, 0);
    int PROC = area("Processing Hall", -10000, -16000, 6000, -8000, 5500, 0);
    int EXTR = area("Extraction Hall", -9000, -4000, 5000, 10000, 6500, 0);
    int CONT = area("Containment Block", -28000, -2000, -15000, 8000, 4800, 0);
    int LAB = area("Research Lab", -27000, 14000, -16000, 22000, 4200, 0);
    int VAULT = area("Server Vault", -10000, 13000, 0, 23000, 4500, 0);
    int GALL = area("Pipe Gallery", 6000, 2000, 11000, 26000, 6000, 0);
    int TURB = area("Turbine Hall", 6300, -18000, 22500, -4000, 7500, 0);
    int CORE = area("Reactor Core", 14000, 0, 29000, 16000, 9000, 0);
    int H[10];
    H[0] = hall_between("Dock Hall", DOCK, CHECK, -25000);
    H[1] = hall_between("Checkpoint Hall", CHECK, PROC, -2000);
    H[2] = hall_between("Processing Hall North", PROC, EXTR, -2000);
    H[3] = hall_between("Containment Hall", CONT, EXTR, 3000);
    H[4] = hall_between("Lab Hall", CONT, LAB, -21600);
    H[5] = hall_between("Vault Hall", LAB, VAULT, 18000);
    H[6] = hall_between("Gallery Hall", VAULT, GALL, 18000);
    H[7] = hall_between("Shortcut Hall", EXTR, GALL, 4000);
    H[8] = hall_between("Turbine Hall Stair", TURB, GALL, 7900);
    H[9] = hall_between("Core Hall", TURB, CORE, 21000);

    build_shell(DOCK, f_dock, 0); build_shell(CHECK, 0, 0); build_shell(PROC, f_proc, 0); build_shell(EXTR, 0, 0);
    build_shell(CONT, f_cont, 0); build_shell(LAB, f_lab, 0); build_shell(VAULT, f_vault, 0); build_shell(GALL, f_gall, 0);
    build_shell(TURB, f_turb, 0); build_shell(CORE, f_core, 0);
    for (int k = 0; k < 10; k++) { build_shell(H[k], 0, 0); dress_hall(H[k]); }

    place_props_from(PROPS);

    bake();
    export_mesh();

    /* the rest of LabData */
    lab.nboxes = ncols; lab.boxes = (LabBox *)malloc(sizeof(LabBox) * (size_t)ncols);
    for (int i = 0; i < ncols; i++) {
        LabBox *b = &lab.boxes[i];
        b->x0 = (s16)(int)cols[i].x0; b->z0 = (s16)(int)cols[i].z0; b->x1 = (s16)(int)cols[i].x1; b->z1 = (s16)(int)cols[i].z1;
        b->h = (s16)(int)gmin(cols[i].h, 32000);
    }
    memcpy(lab.sprites, sprites, sizeof(sprites)); lab.nsprites = nsprites;
    memcpy(lab.fans, fans, sizeof(fans)); lab.nfans = nfans;
    memcpy(lab.rings, rings, sizeof(rings)); lab.nrings = nrings;
    memcpy(lab.pods, pods, sizeof(pods)); lab.npods = npods;
    lab.nceil = nceils;
    for (int i = 0; i < nceils; i++) { LabBox *b = &lab.ceil[i]; b->x0 = (s16)ceils[i].x0; b->z0 = (s16)ceils[i].z0; b->x1 = (s16)ceils[i].x1; b->z1 = (s16)ceils[i].z1; b->h = (s16)ceils[i].h; }
    lab.nareas = nareas;
    for (int i = 0; i < nareas; i++) {
        unsigned seen = 1u << i, frontier = seen;
        for (int s = 0; s < 2; s++) {
            unsigned nxt = 0;
            for (int a = 0; a < nareas; a++) if (frontier & (1u << a)) nxt |= areas[a].links;
            seen |= nxt; frontier = nxt;
        }
        LabArea *o = &lab.areas[i];
        o->x0 = (s16)areas[i].x0; o->z0 = (s16)areas[i].z0; o->x1 = (s16)areas[i].x1; o->z1 = (s16)areas[i].z1; o->pvs = seen; o->name = areas[i].name;
    }
    /* camera views: back corner of each room, looking across it */
    lab.nviews = 0;
    for (int i = 0; i < nareas; i++) {
        const Area *a = &areas[i];
        if (a->hall) continue;
        float cx = a->x0 + (a->x1 - a->x0) * 0.12f, cz = a->z0 + (a->z1 - a->z0) * 0.12f;
        LabView *v = &lab.views[lab.nviews++];
        v->name = a->name; v->x = (s32)cx; v->y = (s32)-gmin(a->H - 600, 3200); v->z = (s32)cz;
        v->yaw = iround(gatan2((a->x0 + a->x1) / 2 - cx, (a->z0 + a->z1) / 2 - cz) / (2 * GPI) * 4096) & 4095; v->pitch = 170;
    }
    /* route waypoints: spawn, through every route hall, to the reactor */
    {
        static const int RH[9] = { 0, 1, 2, 3, 4, 5, 6, 8, 9 };
        float px = SPAWN_X, pz = SPAWN_Z;
        int n = 0;
        lab.route[n][0] = SPAWN_X; lab.route[n++][1] = SPAWN_Z;
        for (int k = 0; k < ROUTE_HALLS_N; k++) {
            const Area *a = &areas[H[RH[k]]];
            float cx = (a->x0 + a->x1) / 2, cz = (a->z0 + a->z1) / 2, e[2][2], dx, dz;
            if (a->axis_x) { e[0][0] = a->x0; e[0][1] = cz; e[1][0] = a->x1; e[1][1] = cz; dx = 1; dz = 0; }
            else { e[0][0] = cx; e[0][1] = a->z0; e[1][0] = cx; e[1][1] = a->z1; dx = 0; dz = 1; }
            if ((e[1][0] - px) * (e[1][0] - px) + (e[1][1] - pz) * (e[1][1] - pz) < (e[0][0] - px) * (e[0][0] - px) + (e[0][1] - pz) * (e[0][1] - pz)) {
                float t0 = e[0][0], t1 = e[0][1]; e[0][0] = e[1][0]; e[0][1] = e[1][1]; e[1][0] = t0; e[1][1] = t1; dx = -dx; dz = -dz;
            }
            float pts[4][2] = { { e[0][0] - dx * 2000, e[0][1] - dz * 2000 }, { e[0][0], e[0][1] }, { e[1][0], e[1][1] }, { e[1][0] + dx * 2000, e[1][1] + dz * 2000 } };
            for (int q = 0; q < 4; q++) { lab.route[n][0] = (s32)pts[q][0]; lab.route[n++][1] = (s32)pts[q][1]; }
            px = pts[3][0]; pz = pts[3][1];
        }
        lab.route[n][0] = 18500; lab.route[n++][1] = 5000;
        lab.nroute = n;
    }
    free(faces); faces = 0; nfaces = capfaces = 0;
}

void gen_lab(void)
{
    if (lab.nverts) return;
    gen_lab_textures(0);
    gen_lab_geometry();
}
