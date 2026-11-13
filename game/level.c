#include "level.h"
#include "ik.h"
#include "lab_data.h"
#include "gte_hw.h"
#include "vram.h"
#include "sfx_ids.h"

/* face flags (tools/build_lab.py) */
#define F_QUAD   1
#define F_ADD    2
#define F_HALF   4
#define F_DOUBLE 8
#define F_PULSE  16
#define F_FLOOR  32
#define F_EMIT   64
#define F_ANIMUV 128          /* animated screen: U steps along a 2-frame strip */
#define SCREEN_FRAME_TICKS 12

#ifndef FOG_FAR
#define FOG_NEAR 8000
#define FOG_FAR  18000
#endif
#define NEAR_Z   140
/* guard band: polygons are clipped to x within +-2z and y within +-z, so a clipped piece is at most
   1000 px wide and 500 px tall - the PS1 GPU drops anything over 1023 x 511 (walls right beside the
   camera used to vanish) */
#define GUARD_X  2
#define GUARD_Y  1
#define MAXCV    1400

static int uploaded, polys_drawn, frame_t, flat, all_rooms, ot_bias;
void level_set_ot_bias(int b) { ot_bias = b; }      /* the editor keeps its panels in front of the level */
void level_debug_all_rooms(int on) { all_rooms = on; }
#define MAX_SECS 512
static s16 sec_dyn[MAX_SECS];                 /* sector maps - door: how far it's open; lift: floor offset */
static s16 mover_t[MAX_SECS];                 /* door: frames since she left; lift: frames she's stood on it */
static s8 mover_dir[MAX_SECS];                /* lift: moving up (1) / down (-1) */
static u8 mover_flag[MAX_SECS];               /* door: the locked warning was given */
static u8 taken[256];                         /* pickups already taken */
static int def_hint = -1, *hintp = &def_hint;
#define sec_hint (*hintp)
/* whose "last sector" the floor and room lookups start from (each body keeps its own, so the
   player and the troopers don't keep throwing each other's away: a miss scans the whole level) */
int *level_hint(int *h) { int *o = hintp == &def_hint ? 0 : hintp; hintp = h ? h : &def_hint; return o; }
int lvl_st_verts, lvl_st_faces, lvl_st_clip, lvl_st_chunks;

void level_debug_flat(int on) { flat = on; }
/*
 * A coarse grid over the level, built when it's loaded: each 4 m cell lists the sectors and
 * collision boxes within GRID_PAD of it, so a body's collision only looks at what's near it
 * instead of the whole level (on the PS1 that scan was a big part of every tick). Same order and
 * same answers as the full scan; a level too big for the tables just falls back to the scan.
 */
#define GRID_SH   12
#define GRID_PAD  1024                 /* bodies up to half this radius find everything they touch */
#define GRID_MAXC 1024
#define GRID_MAXI 8192
static s32 grid_x0, grid_z0;
static int grid_w, grid_h, grid_ok, grid_tried;
static u16 grid_start[GRID_MAXC + 1], grid_item[GRID_MAXI];   /* item: sector, or 0x8000 | box */
/* the item range of the cell holding (x, z); 1, 0 outside the grid (nothing near), -1 no grid */
static void grid_build(void);
static int grid_cell(s32 x, s32 z, int *b, int *e)
{
    int cx, cz;
    if (!grid_tried) grid_build();
    if (!grid_ok) return -1;
    cx = (int)((x - grid_x0) >> GRID_SH); cz = (int)((z - grid_z0) >> GRID_SH);
    if (cx < 0 || cz < 0 || cx >= grid_w || cz >= grid_h) { *b = *e = 0; return 0; }
    *b = grid_start[cz * grid_w + cx]; *e = grid_start[cz * grid_w + cx + 1];
    return 1;
}


void level_init(void)
{
    if (!uploaded) {
        gen_lab();
        lab_upload_textures();
        uploaded = 1;
    }
    frame_t = 0;
    grid_build();
    {
        int i;
        for (i = 0; i < LAB_NSECS && i < MAX_SECS; i++) {
            const LabSec *sc = &lab_secs[i];
            sec_dyn[i] = sc->kind == 2 ? sc->k0 - sc->ka : 0;            /* doors shut, lifts where the map puts them */
            mover_t[i] = 0; mover_dir[i] = 0; mover_flag[i] = 0;
        }
        memset(taken, 0, sizeof(taken));
        {   /* a lift she starts on sets off at once (the freight lift that brings her in) */
            s32 sx, sz; int sy, si;
            level_spawn(&sx, &sz, &sy);
            si = level_sector_at(sx, sz, -1);
            if (si >= 0 && si < MAX_SECS && lab_secs[si].kind == 2) mover_t[si] = 44;
        }
    }
}

void level_reload_textures(void) { uploaded = 0; level_init(); }
#ifndef GEN_BAKED
/* a different map (the editor's play-test; 0 = level 1): rebuilt, textures kept */
void level_load_map(const MapDef *m)
{
    gen_lab_rebuild(m);
    sec_hint = -1;
    uploaded = 0;
    level_init();
}
#endif
void level_spawn(s32 *x, s32 *z, int *yaw)
{
    gen_lab();
    *x = lab.spawn_x; *z = lab.spawn_z; *yaw = lab.spawn_yaw;
}
int level_tank(VECTOR *c)
{
    /* the first energy tank, if there is one (its hum) */
    if (LAB_NRINGS) { c->vx = lab_rings[0].x; c->vy = (lab_rings[0].yt + lab_rings[0].yb) / 2; c->vz = lab_rings[0].z; return 1; }
    c->vx = lab.spawn_x; c->vy = -2000; c->vz = lab.spawn_z; return 0;
}
int  level_polys_drawn(void) { return polys_drawn; }
int  level_data_bytes(void)
{
    return (int)(sizeof(LabVert) * (size_t)LAB_NVERTS + sizeof(LabFace) * (size_t)LAB_NFACES + sizeof(LabChunk) * (size_t)LAB_NCHUNKS
                 + sizeof(LabBox) * (size_t)LAB_NBOXES + sizeof(LabData));
}
int  level_area_count(void) { gen_lab(); return LAB_NAREAS; }   /* (tools may ask before level_init) */
const LabArea *level_area(int i) { return &lab_areas[i]; }
static const LabView *xviews; static int nxviews;
void level_extra_views(const LabView *v, int n) { xviews = v; nxviews = n; }
int  level_view_count(void) { gen_lab(); return LAB_NVIEWS + nxviews; }
int  level_route_count(void) { gen_lab(); return LAB_NROUTE; }
void level_route_point(int i, s32 *x, s32 *z) { *x = lab_route[i][0]; *z = lab_route[i][1]; }
const LabView *level_view(int i) { return i < LAB_NVIEWS ? &lab_views[i] : &xviews[i - LAB_NVIEWS]; }
int  level_area_at(s32 x, s32 z)
{
    int i;
    if (LAB_NSECS) { i = level_sector_at(x, z, sec_hint); return i < 0 ? -1 : lab_secs[i].room; }
    for (i = 0; i < LAB_NAREAS; i++) {
        const LabArea *a = &lab_areas[i];
        if (x >= a->x0 && x <= a->x1 && z >= a->z0 && z <= a->z1) return i;
    }
    return -1;
}

/* rooms worth drawing from a camera position: the PVS of every area within a doorway's
   reach of it (so standing in a doorway shows both sides) */
static u32 portal_mask(const MATRIX *view, const PROJ *pr, s32 cx, s32 cz, int start);

static u32 visible_mask(const MATRIX *view, const PROJ *pr)
{
    static u32 last = 0xffffffffu;
    VECTOR t, c;
    u32 m = 0;
    int i;
    t.vx = -view->t[0]; t.vy = -view->t[1]; t.vz = -view->t[2];
    mat_rot_tapply(view, &t, &c);
    if (LAB_NSECS) {
        static int hint = -1;
        int si = level_sector_at(c.vx, c.vz, hint);
        if (si >= 0) { hint = si; last = portal_mask(view, pr, c.vx, c.vz, si); }
        return last;
    }
    for (i = 0; i < LAB_NAREAS; i++) {
        const LabArea *a = &lab_areas[i];
        if (c.vx >= a->x0 - 500 && c.vx <= a->x1 + 500 && c.vz >= a->z0 - 500 && c.vz <= a->z1 + 500) m |= a->pvs;
    }
    if (m) last = m;
    return last;
}

/* ------------------------------------------------------------------ sectors (maps) */
#define CLEAR_H  1900                         /* headroom a walker needs under a ceiling */

static s32 plane_h(const LabPlane *p, s32 x, s32 z)
{
    if (!p->kx && !p->kz) return p->h;
    return p->h + (((x - p->ax) * p->kx + (z - p->az) * p->kz) >> 12);
}
static s32 floor_up(int si, s32 x, s32 z) { const LabSec *s = &lab_secs[si]; return plane_h(&s->f, x, z) + (s->kind == 2 ? sec_dyn[si] : 0); }
static s32 ceil_up(int si, s32 x, s32 z) { const LabSec *s = &lab_secs[si]; return plane_h(&s->c, x, z) + (s->kind == 1 ? sec_dyn[si] : 0); }
s32 level_sector_floor(int si, s32 x, s32 z) { return -floor_up(si, x, z); }
s32 level_sector_ceil(int si, s32 x, s32 z) { return -ceil_up(si, x, z); }

/* (n << 12) / d in 32 bits: a 64-bit divide is a library call costing thousands of cycles on the
   PS1. Exact while |n| < 2^19; beyond that d is scaled down instead (t then far outside 0..4096
   or accurate to 1/4096 of d) */
static s32 div12(s32 n, s32 d)
{
    if (!d) return n >= 0 ? 0x3fffffff : -0x3fffffff;
    if (n > -(1 << 19) && n < (1 << 19)) return (n << 12) / d;
    if (d > -4096 && d < 4096) return (n > 0) == (d > 0) ? 0x3fffffff : -0x3fffffff;
    return n / (d / 4096);
}

/* the point on wall w nearest (x, z), in 32 bits (positions / 4 inside: under half a centimetre).
   *t: where along the wall, 0..4096; *tp_lo: how far before its start (scaled), for early outs */
static void wall_nearest(const LabWall *w, s32 x, s32 z, s32 *qx, s32 *qz, s32 *t)
{
    s32 ex = w->x1 - w->x0, ez = w->z1 - w->z0, ax = ex >> 2, az = ez >> 2;
    s32 L2 = ax * ax + az * az, tp = ((x - w->x0) >> 2) * ax + ((z - w->z0) >> 2) * az;
    if (tp <= 0 || L2 <= 0) { *t = 0; *qx = w->x0; *qz = w->z0; return; }
    if (tp >= L2) { *t = 4096; *qx = w->x1; *qz = w->z1; return; }
    *t = L2 < (1 << 19) ? (tp << 12) / L2 : tp / (L2 >> 12);
    *qx = w->x0 + (ex * *t >> 12); *qz = w->z0 + (ez * *t >> 12);
}

static int sec_has(int si, s32 x, s32 z)
{
    const LabSec *s = &lab_secs[si];
    int i, c = 0;
    if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1) return 0;
    for (i = 0; i < s->nw; i++) {
        const LabWall *w = &lab_walls[s->w0 + i];
        if ((w->z0 > z) != (w->z1 > z)) {
            long long lhs = (long long)(x - w->x0) * (w->z1 - w->z0), rhs = (long long)(z - w->z0) * (w->x1 - w->x0);
            if (w->z1 > w->z0 ? lhs < rhs : lhs > rhs) c ^= 1;
        }
    }
    return c;
}

int level_sector_at(s32 x, s32 z, int hint)
{
    int i;
    if (!LAB_NSECS) return -1;
    if (hint >= 0 && hint < LAB_NSECS) {
        const LabSec *s = &lab_secs[hint];
        if (sec_has(hint, x, z)) return hint;
        for (i = 0; i < s->nw; i++) {
            int n = lab_walls[s->w0 + i].next - 1;
            if (n >= 0 && sec_has(n, x, z)) return n;
        }
    }
    for (i = 0; i < LAB_NSECS; i++) if (sec_has(i, x, z)) return i;
    return -1;
}

/* does wall w stop something with its feet at y (world, Y down) at (px, pz)? */
static int wall_blocks(const LabWall *w, s32 px, s32 pz, s32 y)
{
    s32 nf, nc, top;
    if (!w->next) return 1;
    nf = -floor_up(w->next - 1, px, pz); nc = -ceil_up(w->next - 1, px, pz);
    if (nf < y - STEP_HEIGHT) return 1;                  /* too tall a step */
    top = (nf < y ? nf : y) - CLEAR_H;
    return nc > top;                                     /* too low a gap */
}

/* push a circle out of the sector walls near it */
static int push_sec(int si, int own, s32 *x, s32 *z, s32 y, s32 r, s32 *nx, s32 *nz)
{
    int i, hit = 0;
    {
        const LabSec *s = &lab_secs[si];
        if (*x < s->x0 - r || *x > s->x1 + r || *z < s->z0 - r || *z > s->z1 + r) return 0;
        for (i = 0; i < s->nw; i++) {
            const LabWall *w = &lab_walls[s->w0 + i];
            s32 side, qx, qz, dx, dz, d2, d, t;
            int end;
            side = ((*x - w->x0) * w->nx + (*z - w->z0) * w->nz) >> 12;
            if (side >= r || side < (si == own ? -(r >> 1) : 1)) continue;    /* behind another sector's wall: not ours */
            wall_nearest(w, *x, *z, &qx, &qz, &t);
            end = t <= 0 || t >= 4096;
            dx = *x - qx; dz = *z - qz;
            if (dx >= r || dx <= -r || dz >= r || dz <= -r) continue;
            d2 = dx * dx + dz * dz;
            if (d2 >= r * r) continue;
            if (!wall_blocks(w, qx, qz, y)) continue;
            d = (s32)isqrt((u32)d2);
            if (d == 0 || (side <= 0 && !end)) { *nx = w->nx; *nz = w->nz; }     /* past the wall's face: back out */
            else { *nx = dx * ONE / d; *nz = dz * ONE / d; }
            *x = qx + (*nx * r >> 12); *z = qz + (*nz * r >> 12);
            hit = 1;
        }
    }
    return hit;
}


/* which rooms the camera can see: a flood from its sector through the portals on screen, each
   one narrowing the window (horizontally - like Build), stopping at shut doors and the fog */
static int seg_near(const LabWall *w, s32 x, s32 z, s32 r)
{
    s32 qx, qz, t;
    wall_nearest(w, x, z, &qx, &qz, &t);
    qx -= x; qz -= z;
    return qx > -r && qx < r && qz > -r && qz < r && qx * qx + qz * qz < r * r;
}

static u32 portal_mask(const MATRIX *view, const PROJ *pr, s32 cx, s32 cz, int start)
{
    struct { s16 sec; s32 l, r; } st[96];
    u8 seen[MAX_SECS];
    s32 rx = view->m[0][0], rz = view->m[0][2], fx = view->m[2][0], fz = view->m[2][2];
    s32 K = (SCREEN_W / 2 + 24) * 4096 / pr->h;           /* half the view's width, as lateral/depth */
    int n = 0, i;
    u32 mask = 0;
    memset(seen, 0, (size_t)(LAB_NSECS < MAX_SECS ? LAB_NSECS : MAX_SECS));
    st[n].sec = (s16)start; st[n].l = -K; st[n].r = K; n++;
    while (n) {
        int si = st[--n].sec;
        s32 wl = st[n].l, wr = st[n].r;
        const LabSec *s = &lab_secs[si];
        mask |= 1u << lab_secs[si].room;
        if (seen[si] >= 3) continue;
        seen[si]++;
        for (i = 0; i < s->nw; i++) {
            const LabWall *w = &lab_walls[s->w0 + i];
            s32 l0, l1, d0, d1, a, b;
            int o;
            if (!w->next) continue;
            o = w->next - 1;
            if (((cx - w->x0) * w->nx + (cz - w->z0) * w->nz) >> 12 < 0) continue;    /* faces away */
            if (lab_secs[o].kind == 1 && sec_dyn[o] == 0) { mask |= 1u << lab_secs[o].room; continue; }   /* shut door */
            l0 = ((w->x0 - cx) * rx + (w->z0 - cz) * rz) >> 12; d0 = ((w->x0 - cx) * fx + (w->z0 - cz) * fz) >> 12;
            l1 = ((w->x1 - cx) * rx + (w->z1 - cz) * rz) >> 12; d1 = ((w->x1 - cx) * fx + (w->z1 - cz) * fz) >> 12;
            if (seg_near(w, cx, cz, 800)) {
                a = wl; b = wr;                            /* the camera is in the doorway: keep the whole window */
            } else if (d0 < 64 && d1 < 64) {
                continue;                                  /* behind the camera */
            } else {
                if (d0 < 64) { l0 = l1 + ((l0 - l1) * div12(d1 - 64, d1 - d0) >> 12); d0 = 64; }
                if (d1 < 64) { l1 = l0 + ((l1 - l0) * div12(d0 - 64, d0 - d1) >> 12); d1 = 64; }
                if (d0 > FOG_FAR && d1 > FOG_FAR) continue;
                a = div12(l0, d0); b = div12(l1, d1);
                if (a > b) { s32 q = a; a = b; b = q; }
                if (a < wl) a = wl;
                if (b > wr) b = wr;
                if (a > b) continue;
            }
            if (n < 96) { st[n].sec = (s16)o; st[n].l = a; st[n].r = b; n++; }
        }
    }
    return mask;
}

/* ------------------------------------------------------------------ collision */
s32 level_ground(s32 x, s32 z, s32 cur_y)
{
    s32 best = 0;
    int i;
    if (flat) return 0;
    if (LAB_NSECS) {
        int si = level_sector_at(x, z, sec_hint);
        if (si < 0) return cur_y;                        /* off the map: stay put */
        sec_hint = si;
        best = -floor_up(si, x, z);
    }
    {   /* the boxes she could stand on: the grid cell's (any box containing the point is listed there) */
        int k, e, g = grid_cell(x, z, &k, &e);
        if (g < 0) { k = 0; e = LAB_NBOXES; }
        for (; k < e; k++) {
            const LabBox *b;
            if (g < 0) i = k;
            else if (!(grid_item[k] & 0x8000)) continue;
            else i = grid_item[k] & 0x7fff;
            b = &lab_boxes[i];
            if (x < b->x0 || x > b->x1 || z < b->z0 || z > b->z1) continue;
            if (-b->lo < cur_y - CLEAR_H) continue;          /* overhead */
            if (-b->h >= cur_y - STEP_HEIGHT && -b->h < best) best = -b->h;
        }
    }
    return best;
}

static int push_box(s32 *x, s32 *z, s32 r, s32 bx0, s32 bz0, s32 bx1, s32 bz1, s32 *nx, s32 *nz)
{
    s32 qx = *x < bx0 ? bx0 : (*x > bx1 ? bx1 : *x);
    s32 qz = *z < bz0 ? bz0 : (*z > bz1 ? bz1 : *z);
    s32 dx = *x - qx, dz = *z - qz, d2, d;
    if (dx >= r || dx <= -r || dz >= r || dz <= -r) return 0;   /* (also keeps the squares in 32 bits) */
    d2 = dx * dx + dz * dz;
    if (d2 >= r * r) return 0;
    if (d2 == 0) {                                    /* centre inside: push out the shortest way */
        s32 l = *x - bx0, rr = bx1 - *x, f = *z - bz0, bk = bz1 - *z, m = l;
        *nx = -ONE; *nz = 0;
        if (rr < m) { m = rr; *nx = ONE; *nz = 0; }
        if (f < m) { m = f; *nx = 0; *nz = -ONE; }
        if (bk < m) { m = bk; *nx = 0; *nz = ONE; }
        *x += (*nx * (m + r)) >> 12; *z += (*nz * (m + r)) >> 12;
        return 1;
    }
    d = (s32)isqrt((u32)d2);
    *nx = dx * ONE / d; *nz = dz * ONE / d;
    *x = qx + (*nx * r >> 12); *z = qz + (*nz * r >> 12);
    return 1;
}

static int collide_box(int i, s32 *x, s32 *z, s32 y, s32 r, s32 *nx, s32 *nz)
{
    const LabBox *b = &lab_boxes[i];
    if (-b->h >= y - STEP_HEIGHT) return 0;        /* low enough to step onto */
    if (-b->lo < y - CLEAR_H) return 0;             /* high enough to walk under */
    return push_box(x, z, r, b->x0, b->z0, b->x1, b->z1, nx, nz);
}


static int grid_obj(int k, s32 *x0, s32 *z0, s32 *x1, s32 *z1)
{
    if (k < LAB_NSECS) { const LabSec *s = &lab_secs[k]; *x0 = s->x0; *z0 = s->z0; *x1 = s->x1; *z1 = s->z1; return k; }
    k -= LAB_NSECS;
    { const LabBox *b = &lab_boxes[k]; *x0 = b->x0; *z0 = b->z0; *x1 = b->x1; *z1 = b->z1; }
    return 0x8000 | k;
}

static void grid_build(void)
{
    s32 mnx = 0x7fffffff, mnz = 0x7fffffff, mxx = -0x7fffffff, mxz = -0x7fffffff, a, b, c, d;
    int k, n = LAB_NSECS + LAB_NBOXES, cx, cz, total = 0;
    grid_ok = 0; grid_tried = 1;
    if (n <= 0 || LAB_NSECS > 0x7fff || LAB_NBOXES > 0x7fff) return;
    for (k = 0; k < n; k++) {
        grid_obj(k, &a, &b, &c, &d);
        if (a < mnx) mnx = a;
        if (b < mnz) mnz = b;
        if (c > mxx) mxx = c;
        if (d > mxz) mxz = d;
    }
    grid_x0 = mnx - GRID_PAD; grid_z0 = mnz - GRID_PAD;
    grid_w = (int)((mxx + GRID_PAD - grid_x0) >> GRID_SH) + 1;
    grid_h = (int)((mxz + GRID_PAD - grid_z0) >> GRID_SH) + 1;
    if (grid_w * grid_h > GRID_MAXC) return;
    memset(grid_start, 0, sizeof(grid_start));
    for (k = 0; k < n; k++) {                   /* count, per cell */
        grid_obj(k, &a, &b, &c, &d);
        for (cz = (int)((b - GRID_PAD - grid_z0) >> GRID_SH); cz <= (int)((d + GRID_PAD - grid_z0) >> GRID_SH); cz++)
            for (cx = (int)((a - GRID_PAD - grid_x0) >> GRID_SH); cx <= (int)((c + GRID_PAD - grid_x0) >> GRID_SH); cx++)
                grid_start[cz * grid_w + cx + 1]++, total++;
    }
    if (total > GRID_MAXI) return;
    for (k = 0; k < grid_w * grid_h; k++) grid_start[k + 1] += grid_start[k];
    {
        static u16 fill[GRID_MAXC];
        memcpy(fill, grid_start, sizeof(u16) * (size_t)(grid_w * grid_h));
        for (k = 0; k < n; k++) {               /* in index order, so each cell keeps the scan's order */
            int it = grid_obj(k, &a, &b, &c, &d);
            for (cz = (int)((b - GRID_PAD - grid_z0) >> GRID_SH); cz <= (int)((d + GRID_PAD - grid_z0) >> GRID_SH); cz++)
                for (cx = (int)((a - GRID_PAD - grid_x0) >> GRID_SH); cx <= (int)((c + GRID_PAD - grid_x0) >> GRID_SH); cx++)
                    grid_item[fill[cz * grid_w + cx]++] = (u16)it;
        }
    }
    grid_ok = 1;
}

int level_collide(s32 *x, s32 *z, s32 y, s32 r, s32 *nx, s32 *nz)
{
    int i, hit = 0, own;
    if (flat) return 0;
    if (!grid_tried) grid_build();
    own = LAB_NSECS ? level_sector_at(*x, *z, sec_hint) : -1;
    if (grid_ok && r <= GRID_PAD / 2) {
        int cx = (int)((*x - grid_x0) >> GRID_SH), cz = (int)((*z - grid_z0) >> GRID_SH);
        if (cx >= 0 && cz >= 0 && cx < grid_w && cz < grid_h) {
            int c = cz * grid_w + cx, e = grid_start[c + 1];
            for (i = grid_start[c]; i < e; i++) {
                int it = grid_item[i];
                if (it & 0x8000) hit |= collide_box(it & 0x7fff, x, z, y, r, nx, nz);
                else hit |= push_sec(it, own, x, z, y, r, nx, nz);
            }
        }
        return hit;                     /* outside the grid: nothing of the level is near */
    }
    for (i = 0; i < LAB_NSECS; i++) hit |= push_sec(i, own, x, z, y, r, nx, nz);
    for (i = 0; i < LAB_NBOXES; i++) hit |= collide_box(i, x, z, y, r, nx, nz);
    return hit;
}

/* the sectors and boxes the grid lists near a box (x0,z0)-(x1,z1), each once, in index order
   (sectors first, then 0x8000 | box): what a ray needs to test. -1: use the full scan instead. */
static u16 g_stamp_s[MAX_SECS], g_stamp_b[1024], g_stamp;
static int grid_near(s32 x0, s32 z0, s32 x1, s32 z1, u16 *out, int max)
{
    int cx0, cx1, cz0, cz1, cx, cz, n = 0, i, j;
    if (!grid_tried) grid_build();
    if (!grid_ok || LAB_NSECS > MAX_SECS || LAB_NBOXES > 1024) return -1;
    cx0 = (int)((x0 - grid_x0) >> GRID_SH); cx1 = (int)((x1 - grid_x0) >> GRID_SH);
    cz0 = (int)((z0 - grid_z0) >> GRID_SH); cz1 = (int)((z1 - grid_z0) >> GRID_SH);
    if (cx1 < 0 || cz1 < 0 || cx0 >= grid_w || cz0 >= grid_h) return 0;   /* nothing of the level is near */
    if (cx0 < 0) cx0 = 0;
    if (cz0 < 0) cz0 = 0;
    if (cx1 >= grid_w) cx1 = grid_w - 1;
    if (cz1 >= grid_h) cz1 = grid_h - 1;
    if ((cx1 - cx0 + 1) * (cz1 - cz0 + 1) > 16) return -1;              /* a long ray: the plain scan */
    if (++g_stamp == 0) { memset(g_stamp_s, 0, sizeof(g_stamp_s)); memset(g_stamp_b, 0, sizeof(g_stamp_b)); g_stamp = 1; }
    for (cz = cz0; cz <= cz1; cz++)
        for (cx = cx0; cx <= cx1; cx++) {
            int c = cz * grid_w + cx, e = grid_start[c + 1];
            for (i = grid_start[c]; i < e; i++) {
                int it = grid_item[i];
                u16 *st = (it & 0x8000) ? &g_stamp_b[it & 0x7fff] : &g_stamp_s[it];
                if (*st == g_stamp) continue;
                *st = g_stamp;
                if (n >= max) return -1;
                for (j = n; j > 0 && out[j - 1] > it; j--) out[j] = out[j - 1];   /* keep index order */
                out[j] = (u16)it; n++;
            }
        }
    return n;
}

/* the first wall a segment a->b hits from its front (t 0..4096), walls pushed out by margin m */
static s32 seg_walls(const VECTOR *a, const VECTOR *d, s32 m, int *wi)
{
    s32 best = 4096, minx, maxx, minz, maxz;
    int si, i;
    minx = d->vx < 0 ? a->vx + d->vx : a->vx; maxx = d->vx < 0 ? a->vx : a->vx + d->vx;
    minz = d->vz < 0 ? a->vz + d->vz : a->vz; maxz = d->vz < 0 ? a->vz : a->vz + d->vz;
    u16 near[96];
    int nn, k;
    *wi = -1;
    nn = m <= GRID_PAD / 2 ? grid_near(minx - m, minz - m, maxx + m, maxz + m, near, 96) : -1;
    for (k = 0; k < (nn < 0 ? LAB_NSECS : nn); k++) {
        const LabSec *s;
        si = nn < 0 ? k : near[k];
        if (si & 0x8000) break;                    /* the boxes come after the sectors */
        s = &lab_secs[si];
        if (maxx < s->x0 - m || minx > s->x1 + m || maxz < s->z0 - m || minz > s->z1 + m) continue;
        for (i = 0; i < s->nw; i++) {
            const LabWall *w = &lab_walls[s->w0 + i];
            s32 x0 = w->x0 + (w->nx * m >> 12), z0 = w->z0 + (w->nz * m >> 12), ex = w->x1 - w->x0, ez = w->z1 - w->z0;
            s32 den, tn, un, t, py, px, pz, f, c, nf, nc;
            s32 dx4 = d->vx >> 4, dz4 = d->vz >> 4, ex4 = ex >> 4, ez4 = ez >> 4, ox4 = (x0 - a->vx) >> 4, oz4 = (z0 - a->vz) >> 4;
            if (d->vx * w->nx + d->vz * w->nz >= 0) continue;                  /* not coming at its front */
            /* 32 bits, positions / 16 inside (under 2 cm): no 64-bit divides on the PS1 */
            den = dx4 * ez4 - dz4 * ex4;
            if (!den) continue;
            tn = ox4 * ez4 - oz4 * ex4;
            un = ox4 * dz4 - oz4 * dx4;
            if (den < 0) { den = -den; tn = -tn; un = -un; }
            {   /* the wall line, pushed out by m and lengthened by m at both ends, so a camera
                   ray can't slip past the outside corner of a short wall (a door jamb) */
                s32 L4 = (s32)isqrt((u32)(ex4 * ex4 + ez4 * ez4));
                s32 ext = L4 > 0 ? (den / L4) * m / 16 : 0;
                if (tn < 0 || tn >= den || un < -ext - (den >> 6) || un > den + ext + (den >> 6)) continue;
            }
            t = den < (1 << 19) ? (tn << 12) / den : tn / (den >> 12);
            if (t >= best) continue;
            px = a->vx + (d->vx * t >> 12); pz = a->vz + (d->vz * t >> 12);
            py = a->vy + (d->vy * t >> 12);
            f = -floor_up(si, px, pz) + m; c = -ceil_up(si, px, pz) - m;
            if (s->kind == 1 && !w->next) c = -(plane_h(&s->f, px, pz) + s->ka) - m;   /* a door's jambs stand full height, shut or open */
            if (w->next) {
                nf = -floor_up(w->next - 1, px, pz) - m; nc = -ceil_up(w->next - 1, px, pz) + m;
                if (nc < nf && !(py <= f && py >= nf) && !(py >= c && py <= nc)) continue;   /* through the opening */
            }
            if (py > f || py < c) continue;
            best = t; *wi = s->w0 + i;
        }
    }
    return best;
}

/* slab test of a segment against one box, t in 0..4096 (4096 = no hit) */
static s32 seg_box(const VECTOR *a, const VECTOR *d, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1)
{
    s32 lo = 0, hi = 4096;
    const s32 p[3] = { a->vx, a->vy, a->vz }, dd[3] = { d->vx, d->vy, d->vz };
    const s32 mn[3] = { x0, y0, z0 }, mx[3] = { x1, y1, z1 };
    int k;
    for (k = 0; k < 3; k++) {
        if (dd[k] == 0) {
            if (p[k] < mn[k] || p[k] > mx[k]) return 4096;
        } else {
            s32 t0 = div12(mn[k] - p[k], dd[k]);
            s32 t1 = div12(mx[k] - p[k], dd[k]);
            if (t0 > t1) { s32 s = t0; t0 = t1; t1 = s; }
            if (t0 > lo) lo = t0;
            if (t1 < hi) hi = t1;
            if (lo > hi) return 4096;
        }
    }
    return lo;
}

/* what a blade / weapon segment a->b strikes first: a collision box (wall: full-height shell piece,
   else a prop: crate, console, machine...) or the floor. Fills the contact point and the surface
   normal (4096 = 1). Returns LEVEL_HIT_NONE / _WALL / _PROP / _FLOOR. */
int level_hit_segment(const VECTOR *a, const VECTOR *b, VECTOR *at, VECTOR *nrm)
{
    VECTOR d;
    s32 best = 4096, t, minx, maxx, minz, maxz;
    int i, kind = LEVEL_HIT_NONE, bi = -1;
    d.vx = b->vx - a->vx; d.vy = b->vy - a->vy; d.vz = b->vz - a->vz;
    minx = a->vx < b->vx ? a->vx : b->vx; maxx = a->vx ^ b->vx ^ minx;
    minz = a->vz < b->vz ? a->vz : b->vz; maxz = a->vz ^ b->vz ^ minz;
    u16 near[96];
    int nn, k;
    if (flat) return LEVEL_HIT_NONE;
    nn = grid_near(minx, minz, maxx, maxz, near, 96);              /* only the boxes near it */
    for (k = 0; k < (nn < 0 ? LAB_NBOXES : nn); k++) {
        const LabBox *bx;
        if (nn < 0) i = k;
        else if (!(near[k] & 0x8000)) continue;
        else i = near[k] & 0x7fff;
        bx = &lab_boxes[i];
        if (maxx < bx->x0 || minx > bx->x1 || maxz < bx->z0 || minz > bx->z1) continue;   /* broad phase */
        t = seg_box(a, &d, bx->x0, -bx->h, bx->z0, bx->x1, -bx->lo, bx->z1);
        if (t < best) { best = t; bi = i; }
    }
    if (bi >= 0) kind = lab_boxes[bi].h >= 9000 ? LEVEL_HIT_WALL : LEVEL_HIT_PROP;
    if (LAB_NSECS) {
        int wi;
        t = seg_walls(a, &d, 0, &wi);
        if (wi >= 0 && t < best) { best = t; kind = LEVEL_HIT_WALL; bi = -2 - wi; }
    }
    /* the floor under the blade (Y down: floor at level_ground, blade below it = buried) */
    if (d.vy > 0) {
        s32 g = level_ground(b->vx, b->vz, b->vy - 400);
        if (b->vy > g && a->vy <= g) {
            t = div12(g - a->vy, d.vy);
            if (t >= 0 && t < best) { best = t; kind = LEVEL_HIT_FLOOR; bi = -1; }
        }
    }
    if (kind == LEVEL_HIT_NONE) return kind;
    at->vx = a->vx + (d.vx * best >> 12);
    at->vy = a->vy + (d.vy * best >> 12);
    at->vz = a->vz + (d.vz * best >> 12);
    nrm->vx = nrm->vz = 0; nrm->vy = 0;
    if (kind == LEVEL_HIT_FLOOR) nrm->vy = -ONE;
    else if (bi <= -2) { nrm->vx = lab_walls[-2 - bi].nx; nrm->vz = lab_walls[-2 - bi].nz; }
    else {   /* the box face nearest the contact */
        const LabBox *bx = &lab_boxes[bi];
        s32 dl = at->vx - bx->x0, dr = bx->x1 - at->vx, df = at->vz - bx->z0, dk = bx->z1 - at->vz, dt = at->vy + bx->h;
        s32 m = dl; nrm->vx = -ONE;
        if (dr < m) { m = dr; nrm->vx = ONE; nrm->vz = 0; }
        if (df < m) { m = df; nrm->vx = 0; nrm->vz = -ONE; }
        if (dk < m) { m = dk; nrm->vx = 0; nrm->vz = ONE; }
        if (dt < m) { nrm->vx = 0; nrm->vz = 0; nrm->vy = -ONE; }
    }
    return kind;
}

int level_clip_segment(const VECTOR *a, const VECTOR *b, s32 m)
{
    VECTOR d;
    s32 best = 4096, t;
    int i;
    s32 minx = a->vx < b->vx ? a->vx : b->vx, maxx = a->vx ^ b->vx ^ minx;
    s32 minz = a->vz < b->vz ? a->vz : b->vz, maxz = a->vz ^ b->vz ^ minz;
    u16 near[96];
    int nn, k;
    d.vx = b->vx - a->vx; d.vy = b->vy - a->vy; d.vz = b->vz - a->vz;
    nn = m <= GRID_PAD / 2 ? grid_near(minx - m, minz - m, maxx + m, maxz + m, near, 96) : -1;
    for (k = 0; k < (nn < 0 ? LAB_NBOXES : nn); k++) {
        const LabBox *bx;
        s32 top, bot;
        if (nn < 0) i = k;
        else if (!(near[k] & 0x8000)) continue;   /* a sector: seg_walls() below */
        else i = near[k] & 0x7fff;
        bx = &lab_boxes[i];
        top = -bx->h - m; bot = -bx->lo + m;
        if (maxx < bx->x0 - m || minx > bx->x1 + m || maxz < bx->z0 - m || minz > bx->z1 + m) continue;   /* nowhere near the ray */
        if (a->vx >= bx->x0 - m && a->vx <= bx->x1 + m && a->vz >= bx->z0 - m && a->vz <= bx->z1 + m && a->vy >= top && a->vy <= bot)
            continue;                                   /* starting inside (standing on it) */
        t = seg_box(a, &d, bx->x0 - m, top, bx->z0 - m, bx->x1 + m, bot, bx->z1 + m);
        if (t < best) best = t;
    }
    {                     /* sector walls, then the ceiling over the start and the end */
        int wi, si;
        t = seg_walls(a, &d, m, &wi);
        if (t < best) best = t;
        for (i = 0; i < 2; i++) {
            const VECTOR *p = i ? b : a;
            si = level_sector_at(p->vx, p->vz, sec_hint);
            if (si < 0) continue;
            {
                s32 cy = -ceil_up(si, p->vx, p->vz) + m;
                if (b->vy < cy && d.vy < 0 && a->vy > cy) {
                    t = div12(cy - a->vy, d.vy);
                    if (t >= 0 && t < best) best = t;
                }
            }
        }
    }
    return (int)best;
}

void level_update(void) { frame_t++; }

/* ------------------------------------------------------------------ doors, lifts, pickups */
static LevelHooks hooks;
void level_set_hooks(const LevelHooks *h) { hooks = *h; }
int  level_thing_count(void) { gen_lab(); return lab.nthings; }
const LabThing *level_thing(int i) { return &lab.things[i]; }
static void lsound(int id, int vol, s32 x, s32 z, int pitch) { if (hooks.sound) hooks.sound(id, vol, x, z, pitch); }

#define DOOR_SPEED 96
#define LIFT_SPEED 40
/* doors and lifts: by default they work by themselves (a door opens as she comes near, a lift
   sets off once she's stood on it); in "use" mode (Deadbird) they wait for level_use - the
   interact button - except a lift already carrying her, which finishes its trip */
static int use_mode;
void level_set_use_mode(int on) { use_mode = on; }

static int door_secret(const LabSec *s) { return (s->flags & SF_SECRET) != 0; }

void level_think(s32 px, s32 py, s32 pz, u32 keys)
{
    int si, here;
    if (!LAB_NSECS) return;
    here = level_sector_at(px, pz, sec_hint);
    for (si = 0; si < LAB_NSECS && si < MAX_SECS; si++) {
        const LabSec *s = &lab_secs[si];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2;
        if (s->kind == 1) {
            int near = px > s->x0 - 2200 && px < s->x1 + 2200 && pz > s->z0 - 2200 && pz < s->z1 + 2200;
            int close = px > s->x0 - 1100 && px < s->x1 + 1100 && pz > s->z0 - 1100 && pz < s->z1 + 1100;
            int can = !s->lock || ((keys >> s->lock) & 1);
            int open = use_mode ? mover_dir[si] > 0 : (door_secret(s) ? close : near) && can;
            if (open) {
                if (sec_dyn[si] == 0) lsound(SFX_AMB_CLANK, 3000, cx, cz, 2600);
                sec_dyn[si] = (s16)(sec_dyn[si] + DOOR_SPEED > s->ka ? s->ka : sec_dyn[si] + DOOR_SPEED);
                if (near || here == si) mover_t[si] = 0;          /* stays open while she's about */
                else if (++mover_t[si] > 180) mover_dir[si] = 0;  /* then shuts behind her */
            } else {
                if (!use_mode && close && !can && !mover_flag[si] && !door_secret(s)) {
                    mover_flag[si] = 1;
                    lsound(SFX_ZAP, 3200, cx, cz, 1500);
                    if (hooks.locked) hooks.locked(s->lock);
                }
                if (!close) mover_flag[si] = 0;
                if (mover_t[si] < 1000) mover_t[si]++;
                if ((use_mode || mover_t[si] > 180) && sec_dyn[si] > 0 && here != si) {
                    if (sec_dyn[si] == s->ka) lsound(SFX_AMB_CLANK, 2500, cx, cz, 2200);
                    sec_dyn[si] = (s16)(sec_dyn[si] - DOOR_SPEED < 0 ? 0 : sec_dyn[si] - DOOR_SPEED);
                }
            }
        } else if (s->kind == 2) {
            s32 top = s->kb - s->ka, deck = -(s->ka + sec_dyn[si]);
            int on = here == si && py > deck - 120 && py < deck + 120;
            if (mover_dir[si]) {             /* moving: it carries her (her floor is its deck) */
                s32 v = sec_dyn[si] + mover_dir[si] * LIFT_SPEED;
                if (v <= 0 || v >= top) { v = v <= 0 ? 0 : top; mover_dir[si] = 0; mover_t[si] = -90; lsound(SFX_AMB_CLANK, 3500, cx, cz, 1800); }
                sec_dyn[si] = (s16)v;
                continue;
            }
            if (on) {
                /* use mode: only the level's opening ride sets off by itself */
                if ((!use_mode || mover_t[si] > 0) && ++mover_t[si] > 45) { mover_dir[si] = sec_dyn[si] ? -1 : 1; mover_t[si] = 0; lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400); }
            } else {
                if (mover_t[si] > 0) mover_t[si] = 0;
                else if (mover_t[si] < 0) mover_t[si]++;
                /* call it: she's beside it, standing at the end it isn't at */
                if (!use_mode && !mover_t[si] && px > s->x0 - 1800 && px < s->x1 + 1800 && pz > s->z0 - 1800 && pz < s->z1 + 1800) {
                    s32 lo = -s->ka, hi = -s->kb;
                    if (sec_dyn[si] == top && py > lo - 300 && py < lo + 300) { mover_dir[si] = -1; lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400); }
                    else if (sec_dyn[si] == 0 && py > hi - 300 && py < hi + 300) { mover_dir[si] = 1; lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400); }
                }
            }
        }
    }
}

/* the interact button: the lift she's on, a lift she's beside (it comes to her), else the
   nearest door (or secret door) in reach; LU_* says what happened */
static int use_at(s32 px, s32 py, s32 pz, u32 keys, int act);
int level_use(s32 px, s32 py, s32 pz, u32 keys) { return use_at(px, py, pz, keys, 1); }
int level_can_use(s32 px, s32 py, s32 pz) { return use_at(px, py, pz, 0, 0); }
static int use_at(s32 px, s32 py, s32 pz, u32 keys, int act)
{
    int si, here, best = -1;
    s32 bestd = 0x7fffffff;
    if (!LAB_NSECS) return LU_NONE;
    here = level_sector_at(px, pz, sec_hint);
    for (si = 0; si < LAB_NSECS && si < MAX_SECS; si++) {     /* lifts first */
        const LabSec *s = &lab_secs[si];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2, top, deck;
        if (s->kind != 2) continue;
        top = s->kb - s->ka; deck = -(s->ka + sec_dyn[si]);
        if (mover_dir[si]) { if (here == si && act) return LU_LIFT; continue; }
        if (here == si && py > deck - 200 && py < deck + 200) {
            if (!act) return LU_LIFT;
            mover_dir[si] = sec_dyn[si] ? -1 : 1; mover_t[si] = 0;
            lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400);
            return LU_LIFT;
        }
        if (px > s->x0 - 1800 && px < s->x1 + 1800 && pz > s->z0 - 1800 && pz < s->z1 + 1800) {
            s32 lo = -s->ka, hi = -s->kb;
            if (sec_dyn[si] == top && py > lo - 400 && py < lo + 400) { if (!act) return LU_LIFT; mover_dir[si] = -1; lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400); return LU_LIFT; }
            if (sec_dyn[si] == 0 && py > hi - 400 && py < hi + 400) { if (!act) return LU_LIFT; mover_dir[si] = 1; lsound(SFX_AMB_STEAM, 2600, cx, cz, 1400); return LU_LIFT; }
        }
    }
    for (si = 0; si < LAB_NSECS && si < MAX_SECS; si++) {     /* the nearest door in reach */
        const LabSec *s = &lab_secs[si];
        s32 dx, dz, d;
        if (s->kind != 1) continue;
        if (!act && sec_dyn[si] >= s->ka) continue;                          /* already open: nothing to do */
        if (px < s->x0 - 1500 || px > s->x1 + 1500 || pz < s->z0 - 1500 || pz > s->z1 + 1500) continue;
        if (py < -s->k0 - 1600 || py > -s->k0 + 600) continue;               /* not on its floor */
        dx = px < s->x0 ? s->x0 - px : px > s->x1 ? px - s->x1 : 0;
        dz = pz < s->z0 ? s->z0 - pz : pz > s->z1 ? pz - s->z1 : 0;
        d = dx + dz;
        if (d < bestd) { bestd = d; best = si; }
    }
    if (best >= 0) {
        const LabSec *s = &lab_secs[best];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2;
        if (!act) return door_secret(s) ? LU_NONE : LU_DOOR;               /* the button doesn't give a secret away */
        if (s->lock && !((keys >> s->lock) & 1)) {
            lsound(SFX_ZAP, 3200, cx, cz, 1500);
            if (hooks.locked) hooks.locked(s->lock);
            return LU_LOCKED;
        }
        mover_dir[best] = 1; mover_t[best] = 0;
        return door_secret(s) ? LU_SECRET : LU_DOOR;
    }
    return LU_NONE;
}

int level_secrets(int *found)
{
    int i, n = 0, f = 0;
    for (i = 0; i < lab.nthings && i < 256; i++)
        if (lab.things[i].type == 3 && lab.things[i].arg) { n++; f += taken[i]; }
    *found = f;
    return n;
}

int level_pickup(s32 px, s32 py, s32 pz, int *arg)
{
    int i;
    for (i = 0; i < lab.nthings && i < 256; i++) {
        const LabThing *t = &lab.things[i];
        s32 dx = px - t->x, dz = pz - t->z;
        if ((t->type != 2 && t->type != 3) || taken[i]) continue;
        if (dx > 700 || dx < -700 || dz > 700 || dz < -700 || dx * dx + dz * dz > 700 * 700) continue;
        if (py < level_ground(t->x, t->z, py) - 1500) continue;            /* jumping over it */
        taken[i] = 1;
        *arg = t->arg;
        return t->type;
    }
    return -1;
}

/* ------------------------------------------------------------------ drawing */
typedef struct { s32 x, y, z; s32 u, v, r, g, b; } CV;     /* view-space vertex for clipping */

static VECTOR cvv[MAXCV];
static s16 csx[MAXCV], csy[MAXCV];
static u8 cout_[MAXCV];        /* outcode: 1 behind near, 2..16 outside guard band */

static int pulse(int anim, int t)
{
    switch (anim) {
    case 0: return 4096;
    case 1: return ((t * 37) % 241 < 3) ? 3000 : 4096 - (isin(t * 60) * 120 >> 12);   /* screens: flicker */
    case 2: return 3600 + (isin(t * 22) * 900 >> 12);                                   /* main tank */
    case 3: return 3500 + (isin(t * 30) * 900 >> 12);
    case 4: return 3500 + (isin(t * 30 + 2048) * 900 >> 12);
    case 5: return (t & 32) ? 4096 : 900;                                                /* alarm blink */
    case 6: return 3400 + (isin(t * 50) * 1100 >> 12);                                  /* reactor plasma */
    default: return 3400 + (isin(t * 50 + 1365) * 1100 >> 12);
    }
}

#define FOG_K (4096 * 65536 / (FOG_FAR - FOG_NEAR))
static int fog(s32 z)
{
    if (z <= FOG_NEAR) return 4096;
    if (z >= FOG_FAR) return 0;
    return (int)(((FOG_FAR - z) * FOG_K) >> 16);         /* multiply, no divide (slow on the R3000) */
}

static int pulse_now[8];     /* pulse() for each animation, worked out once per frame */
static u16 lab_clut[512], lab_tpage[8][2];   /* CLUT_ID(n) and each page's tpage (half / additive blend) */
static void lab_tables(void)
{
    int i;
    for (i = 0; i < 512; i++) lab_clut[i] = CLUT_ID(i);
    for (i = 0; i < 8; i++) {
        lab_tpage[i][0] = get_tpage(TP_4BIT, SEMI_HALF, LAB_PAGE_X(i), LAB_PAGE_Y(i));
        lab_tpage[i][1] = get_tpage(TP_4BIT, SEMI_ADD, LAB_PAGE_X(i), LAB_PAGE_Y(i));
    }
}
static int screen_du;        /* animated screens: U offset of the current frame */

static inline u8 outcode(const VECTOR *c)
{
    u8 o = 0;
    if (c->vz < NEAR_Z) o |= 1;
    if (c->vx > c->vz * GUARD_X) o |= 2;
    if (c->vx < -c->vz * GUARD_X) o |= 4;
    if (c->vy > c->vz * GUARD_Y) o |= 8;
    if (c->vy < -c->vz * GUARD_Y) o |= 16;
    return o;
}

static inline s32 plane_d(const CV *v, int p)
{
    switch (p) {
    case 0: return v->z - NEAR_Z;
    case 1: return v->z * GUARD_X - v->x;
    case 2: return v->z * GUARD_X + v->x;
    case 3: return v->z * GUARD_Y - v->y;
    default: return v->z * GUARD_Y + v->y;
    }
}

static int clip_poly(CV *in, int n, CV *out, u8 planes)
{
    CV buf[12];
    CV *src = in, *dst = buf;
    int p, i, m = n;
    for (p = 0; p < 5; p++) {
        if (!(planes & (1 << p))) continue;
        {
            int k = 0;
            for (i = 0; i < m; i++) {
                const CV *a = &src[i], *b = &src[(i + 1) % m];
                s32 da = plane_d(a, p), db = plane_d(b, p);
                if (da >= 0) dst[k++] = *a;
                if ((da >= 0) != (db >= 0)) {
                    s32 t, ea = da, eb = db;
                    CV *c = &dst[k++];
                    /* 32-bit only: 64-bit divides are library calls costing ~1000 cycles on PS1 */
                    while (ea > 0x7ffff || ea < -0x7ffff || eb > 0x7ffff || eb < -0x7ffff) { ea >>= 1; eb >>= 1; }
                    t = ea == eb ? 0 : (ea << 12) / (ea - eb);
                    c->x = a->x + (((b->x - a->x) * t) >> 12);
                    c->y = a->y + (((b->y - a->y) * t) >> 12);
                    c->z = a->z + (((b->z - a->z) * t) >> 12);
                    c->u = a->u + (((b->u - a->u) * t) >> 12);
                    c->v = a->v + (((b->v - a->v) * t) >> 12);
                    c->r = a->r + (((b->r - a->r) * t) >> 12);
                    c->g = a->g + (((b->g - a->g) * t) >> 12);
                    c->b = a->b + (((b->b - a->b) * t) >> 12);
                }
                if (k >= 11) break;
            }
            m = k;
        }
        if (m < 3) return 0;
        src = dst;
        dst = (dst == buf) ? out : buf;
    }
    for (i = 0; i < m; i++) out[i] = src[i];
    return m;
}

static void emit(FrameBuf *fb, const LabFace *f, const s16 *x, const s16 *y, const u8 *u, const u8 *v,
                 const int *r, const int *g, const int *b, int n, s32 z)
{
    POLY *p = prim_alloc(fb);
    int k, otz;
    if (!p) return;
    p->code = PF_TEX | PF_GOURAUD | (n == 4 ? PF_QUAD : 0);
    if (f->flags & (F_ADD | F_HALF)) p->code |= PF_SEMI;
    {   /* each 64x64 tile has its own CLUT; animated screens use the current frame's tile */
        int du = (f->flags & F_ANIMUV) ? screen_du : 0, tile = (((f->uv[0] + du) & 255) >> 6) + (f->uv[1] >> 6) * 4;
        p->tpage = lab_tpage[f->page & 7][(f->flags & F_ADD) != 0];
        p->clut = lab_clut[(f->pal ? CL_EXTRA(f->pal - 1) : CL_LAB(f->page, tile)) & 511];
    }
    for (k = 0; k < n; k++) {
        p->v[k].x = x[k]; p->v[k].y = y[k]; p->v[k].u = u[k]; p->v[k].v = v[k];
        p->v[k].r = (u8)(r[k] > 255 ? 255 : r[k]); p->v[k].g = (u8)(g[k] > 255 ? 255 : g[k]); p->v[k].b = (u8)(b[k] > 255 ? 255 : b[k]);
    }
    otz = (int)(z >> 4) + ((f->flags & F_FLOOR) ? 8 : 0);
    ot_add(fb, otz + ot_bias, p);
    polys_drawn++;
}

static s16 vclamp[MAXCV];      /* door faces: how far a corner was pulled down to the doorway's top */

static void clip_face(FrameBuf *fb, const PROJ *pr, const LabFace *f, int n, u8 oo, int pm)
{
    int k;
    /* crosses the near plane / guard band: clip in view space, then draw the clipped polygon
       as quads (and a last triangle) fanned from its first corner */
    static const int order4[4] = { 0, 1, 3, 2 }, order3[3] = { 0, 1, 2 };
    const int *ord = n == 4 ? order4 : order3;
    CV in[4], out[12];
    s16 qx[12], qy[12];
    int m, j;
    if (!(f->flags & F_DOUBLE)) {
        /* facing away (the eye behind its plane): skip it before any clipping. The sign of
           det(P0, P1, P2) in view space is the sign the screen-space test would give. */
        const VECTOR *a = &cvv[f->v[0]], *b = &cvv[f->v[1]], *c = &cvv[f->v[2]];
        long long det = (long long)a->vx * ((long long)b->vy * c->vz - (long long)b->vz * c->vy)
                      - (long long)a->vy * ((long long)b->vx * c->vz - (long long)b->vz * c->vx)
                      + (long long)a->vz * ((long long)b->vx * c->vy - (long long)b->vy * c->vx);
        if (det <= 0) return;
    }
    for (k = 0; k < n; k++) {
        int c = ord[k], vi = f->v[c];
        in[k].x = cvv[vi].vx; in[k].y = cvv[vi].vy; in[k].z = cvv[vi].vz;
        in[k].u = f->uv[c * 2] << 8; in[k].v = (f->uv[c * 2 + 1] << 8) + (vclamp[vi] * 63 >> 3);
        in[k].r = f->rgb[c * 3] << 4; in[k].g = f->rgb[c * 3 + 1] << 4; in[k].b = f->rgb[c * 3 + 2] << 4;
    }
    lvl_st_clip++;
    m = clip_poly(in, n, out, oo);
    if (m < 3) return;
    if (f->flags & F_ANIMUV) {
        int du = screen_du * 256;
        for (j = 0; j < m; j++) out[j].u += du;
    }
    for (j = 0; j < m; j++) {      /* each corner projected once: one divide (z >= NEAR_Z here) */
        s32 inv = (pr->h << 14) / out[j].z;
        qx[j] = (s16)(pr->ofx + ((out[j].x * inv) >> 14));
        qy[j] = (s16)(pr->ofy + ((out[j].y * inv) >> 14));
    }
    for (j = 1; j + 1 < m; j += 2) {
        /* fan triangles (0, j, j+1) and (0, j+1, j+2) as one quad j, 0, j+1, j+2: the GPU
           splits a quad along its 2nd-3rd corner diagonal, so it draws exactly those two
           triangles (the same affine texture warp as before, one primitive instead of two) */
        int idx[4], c4 = j + 2 < m, nn = c4 ? 4 : 3;
        s16 x[4], y[4];
        u8 u[4], v[4];
        int r[4], g[4], b[4];
        s32 za;
        int fg;
        if (c4) { idx[0] = j; idx[1] = 0; idx[2] = j + 1; idx[3] = j + 2; }
        else { idx[0] = 0; idx[1] = j; idx[2] = j + 1; }
        for (k = 0; k < nn; k++) { x[k] = qx[idx[k]]; y[k] = qy[idx[k]]; }
        if (poly_off_screen(x, y, nn)) continue;
        za = c4 ? (out[0].z + out[j].z + out[j + 1].z + out[j + 2].z) >> 2
                : (((out[0].z + out[j].z + out[j + 1].z) >> 2) * 21845) >> 14;
        fg = fog(za) * pm >> 12;
        for (k = 0; k < nn; k++) {
            const CV *tv = &out[idx[k]];
            u[k] = (u8)(tv->u >> 8); v[k] = (u8)(tv->v >> 8);
            r[k] = (tv->r >> 4) * fg >> 12; g[k] = (tv->g >> 4) * fg >> 12; b[k] = (tv->b >> 4) * fg >> 12;
        }
        emit(fb, f, x, y, u, v, r, g, b, nn, za);
    }
}

#ifdef PSX_GTE
/*
 * The PS1 path for a chunk. The R3000 has no data cache, so the loops keep loads and stores to a
 * minimum: each vertex keeps its packed screen x,y, its depth and one word of clip flags; a face
 * is rejected by one AND (all corners behind the camera, or off the same screen edge) and drawn
 * after a GTE back-face test. Faces crossing the near plane or the guard band go to clip_face().
 * Guard-band flags come from the screen position (|x| > 2H, |y| > H from the centre is the same
 * test as outcode() for a vertex in front of the camera).
 */
enum { VF_NEAR = 1, VF_GUARD = 0x1e, VF_L = 0x20, VF_R = 0x40, VF_T = 0x80, VF_B = 0x100 };
static u32 vsxy[MAXCV];
static s32 vsz[MAXCV];
static u16 vfl[MAXCV];
typedef u32 __attribute__((may_alias)) u32a_l;

static void draw_chunk_gte(FrameBuf *fb, const PROJ *pr, const LabChunk *ch)
{
    int i, door = 0, nv = ch->nv, nf = ch->nf;
    s32 dy = ch->sec ? -sec_dyn[ch->sec - 1] : 0;
    s32 ytop = 0, ofx = pr->ofx, ofy = pr->ofy, gx = pr->h * GUARD_X, gy = pr->h * GUARD_Y, sw = SCREEN_W;
    const LabVert *lv = &lab_verts[ch->v0];
    const LabFace *f = &lab_faces[ch->f0];
    if (nv > MAXCV) return;
    if (ch->sec && lab_secs[ch->sec - 1].kind == 1) { door = 1; ytop = -(lab_secs[ch->sec - 1].k0 + lab_secs[ch->sec - 1].ka); }
    lvl_st_chunks++; lvl_st_verts += nv; lvl_st_faces += nf;
    for (i = 0; i < nv; i++, lv++) {
        s32 yy = lv->y + dy, z, x, y;
        u32 xy, fl;
        VECTOR *cv = &cvv[i];
        vclamp[i] = 0;
        if (door && yy < ytop) { vclamp[i] = (s16)(ytop - yy); yy = ytop; }
        __asm__ volatile(
            "mtc2  %2, $0\n\t" "mtc2  %3, $1\n\t"
            "nop\n\t" "nop\n\t"
            "cop2  0x0180001\n\t"            /* RTPS */
            "swc2  $25, 0(%4)\n\t"           /* view x, y, z for clipping */
            "swc2  $26, 4(%4)\n\t"
            "swc2  $27, 8(%4)\n\t"
            "mfc2  %0, $14\n\t"              /* SXY2 */
            "mfc2  %1, $27\n\t"              /* MAC3 */
            "nop\n\t"
            : "=r"(xy), "=r"(z)
            : "r"(((u32)lv->x & 0xffff) | ((u32)yy << 16)), "r"((s32)lv->z), "r"(cv)
            : "memory");
        if (z < NEAR_Z) fl = VF_NEAR;
        else {
            x = (s16)(xy & 0xffff); y = (s32)xy >> 16;
            fl = 0;
            if (x - ofx > gx) fl |= 2; else if (ofx - x > gx) fl |= 4;
            if (y - ofy > gy) fl |= 8; else if (ofy - y > gy) fl |= 16;
            if (x < 0) fl |= VF_L; else if (x >= sw) fl |= VF_R;
            if (y < 0) fl |= VF_T; else if (y >= SCREEN_H) fl |= VF_B;
        }
        vsxy[i] = xy; vsz[i] = z; vfl[i] = (u16)fl;
    }
    for (i = 0; i < nf; i++, f++) {
        const u16 *fv = f->v;
        int a = fv[0], b = fv[1], c = fv[2], d = fv[3], quad = (f->flags & F_QUAD) != 0;
        u32 fa = vfl[a] & vfl[b] & vfl[c], fo = vfl[a] | vfl[b] | vfl[c];
        int pm;
        if (quad) { fa &= vfl[d]; fo |= vfl[d]; }
        if (fa & (VF_NEAR | VF_L | VF_R | VF_T | VF_B)) continue;   /* behind, or off one screen edge */
        if (door && vclamp[a] && vclamp[b] && vclamp[c] && (!quad || vclamp[d])) continue;   /* up in the wall */
        pm = pulse_now[f->anim > 7 ? 7 : f->anim];
        if (!(fo & (VF_NEAR | VF_GUARD))) {
            u32 p0 = vsxy[a], p1 = vsxy[b], p2 = vsxy[c];
            s32 za, nc;
            int fg, du, k;
            POLY *p;
            za = quad ? (vsz[a] + vsz[b] + vsz[c] + vsz[d]) >> 2 : (((vsz[a] + vsz[b] + vsz[c]) >> 2) * 21845) >> 14;
            if (za > FOG_FAR) continue;
            if (!(f->flags & F_DOUBLE)) {
                __asm__ volatile(
                    "mtc2  %1, $12\n\t" "mtc2  %2, $13\n\t" "mtc2  %3, $14\n\t"
                    "nop\n\t" "nop\n\t"
                    "cop2  0x1400006\n\t"    /* NCLIP */
                    "mfc2  %0, $24\n\t"
                    "nop\n\t"
                    : "=r"(nc) : "r"(p0), "r"(p1), "r"(p2));
                if (nc <= 0) continue;
            }
            if (fb->nprims >= PRIM_MAX) return;
            p = &fb->prims[fb->nprims++];
            p->code = (u8)(PF_TEX | PF_GOURAUD | (quad ? PF_QUAD : 0) | ((f->flags & (F_ADD | F_HALF)) ? PF_SEMI : 0));
            du = (f->flags & F_ANIMUV) ? screen_du : 0;
            {
                int tile = (((f->uv[0] + du) & 255) >> 6) + (f->uv[1] >> 6) * 4;
                p->tpage = lab_tpage[f->page & 7][(f->flags & F_ADD) != 0];
                p->clut = lab_clut[(f->pal ? CL_EXTRA(f->pal - 1) : CL_LAB(f->page, tile)) & 511];
            }
            *(u32a_l *)&p->v[0].x = p0; *(u32a_l *)&p->v[1].x = p1; *(u32a_l *)&p->v[2].x = p2;
            if (quad) *(u32a_l *)&p->v[3].x = vsxy[d];
            fg = fog(za);
            if (pm != 4096) fg = fg * pm >> 12;
            for (k = 0; k < 3 + quad; k++) {
                const u8 *cc = &f->rgb[k * 3];
                p->v[k].u = (u8)(f->uv[k * 2] + du);
                p->v[k].v = (u8)(f->uv[k * 2 + 1] + (door ? vclamp[fv[k]] * 63 >> 11 : 0));
                if (fg == 4096) { p->v[k].r = cc[0]; p->v[k].g = cc[1]; p->v[k].b = cc[2]; }
                else if (fg < 4096) {
                    p->v[k].r = (u8)(cc[0] * fg >> 12); p->v[k].g = (u8)(cc[1] * fg >> 12); p->v[k].b = (u8)(cc[2] * fg >> 12);
                } else {
                    int r = cc[0] * fg >> 12, g = cc[1] * fg >> 12, bb = cc[2] * fg >> 12;
                    p->v[k].r = (u8)(r > 255 ? 255 : r); p->v[k].g = (u8)(g > 255 ? 255 : g); p->v[k].b = (u8)(bb > 255 ? 255 : bb);
                }
            }
            {
                int otz = (int)(za >> 4) + ((f->flags & F_FLOOR) ? 8 : 0) + ot_bias;
                if (otz < 0) otz = 0; else if (otz >= OT_LEN) otz = OT_LEN - 1;
                p->next = fb->ot[otz]; fb->ot[otz] = p;
            }
            polys_drawn++;
        } else {
            /* near plane / guard band: the true outcodes from the view-space corners */
            u8 oa = (u8)(outcode(&cvv[a]) & outcode(&cvv[b]) & outcode(&cvv[c])), oo = (u8)(outcode(&cvv[a]) | outcode(&cvv[b]) | outcode(&cvv[c]));
            if (quad) { u8 od = outcode(&cvv[d]); oa &= od; oo |= od; }
            if (oa) continue;
            clip_face(fb, pr, f, quad ? 4 : 3, oo, pm);
        }
    }
}
#endif

static void draw_chunk(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const LabChunk *ch, int t)
{
    int i, k, door = 0;
#ifdef PSX_GTE
    (void)view; (void)t; (void)i; (void)k; (void)door;
    draw_chunk_gte(fb, pr, ch);
    return;
#endif
    s32 dy = ch->sec ? -sec_dyn[ch->sec - 1] : 0;     /* a door or lift: moved up by how far it's gone */
    s32 ytop = 0;
    if (ch->nv > MAXCV) return;
    /* a door works like Doom's: its sector's ceiling rises, the face rides up on it with its texture
       and is cut off at the top of the doorway (where the wall above starts) instead of sliding up
       through that wall */
    if (ch->sec && lab_secs[ch->sec - 1].kind == 1) { door = 1; ytop = -(lab_secs[ch->sec - 1].k0 + lab_secs[ch->sec - 1].ka); }
    lvl_st_chunks++; lvl_st_verts += ch->nv; lvl_st_faces += ch->nf;
    for (i = 0; i < ch->nv; i++) {
        const LabVert *lv = &lab_verts[ch->v0 + i];
        s32 yy = lv->y + dy;
        vclamp[i] = 0;
        if (door && yy < ytop) { vclamp[i] = (s16)(ytop - yy); yy = ytop; }
#ifdef PSX_GTE
        u32 sxy;
        gte_xform(lv->x, (s16)yy, lv->z, &cvv[i], &sxy);     /* matrix + projection set in level_draw */
        cout_[i] = outcode(&cvv[i]);
        if (!cout_[i]) {
            if (cvv[i].vx > 32767 || cvv[i].vx < -32767 || cvv[i].vy > 32767 || cvv[i].vy < -32767) {
                csx[i] = (s16)(pr->ofx + cvv[i].vx * pr->h / cvv[i].vz);    /* past the GTE's 16-bit IR */
                csy[i] = (s16)(pr->ofy + cvv[i].vy * pr->h / cvv[i].vz);
            } else {
                csx[i] = (s16)(sxy & 0xffff);
                csy[i] = (s16)(sxy >> 16);
            }
        }
#else
        SVECTOR s; s.vx = lv->x; s.vy = (s16)yy; s.vz = lv->z;
        mat_apply(view, &s, &cvv[i]);
        cout_[i] = outcode(&cvv[i]);
        if (!cout_[i]) {
            csx[i] = (s16)(pr->ofx + cvv[i].vx * pr->h / cvv[i].vz);
            csy[i] = (s16)(pr->ofy + cvv[i].vy * pr->h / cvv[i].vz);
        }
#endif
    }
    for (i = 0; i < ch->nf; i++) {
        const LabFace *f = &lab_faces[ch->f0 + i];
        int n = (f->flags & F_QUAD) ? 4 : 3;
        u8 oa = 0xff, oo = 0;
        s32 zs = 0, zmax = 0;
        int pm;
        for (k = 0; k < n; k++) {
            int vi = f->v[k];
            oa &= cout_[vi]; oo |= cout_[vi];
            zs += cvv[vi].vz;
            if (cvv[vi].vz > zmax) zmax = cvv[vi].vz;
        }
        if (oa) continue;                              /* all corners outside one plane */
        if (zmax < NEAR_Z) continue;
        if (door) {                                    /* wholly above the doorway: gone into the wall */
            int up = 1;
            for (k = 0; k < n; k++) if (!vclamp[f->v[k]]) up = 0;
            if (up) continue;
        }
        pm = pulse_now[f->anim > 7 ? 7 : f->anim];
        if (!oo) {
            /* the common case, written straight into the primitive (no temporary copies: the R3000
               has no data cache, so every load and store to RAM costs) */
            const u16 *fv = f->v;
            s16 x0 = csx[fv[0]], y0 = csy[fv[0]], x1 = csx[fv[1]], y1 = csy[fv[1]], x2 = csx[fv[2]], y2 = csy[fv[2]];
            s32 za = n == 4 ? zs >> 2 : ((zs >> 2) * 21845) >> 14;    /* / 3 without a divide */
            int fg, du;
            POLY *p;
            if (za > FOG_FAR) continue;
            {   /* wholly off screen: discarded before any work */
                s16 x3 = n == 4 ? csx[fv[3]] : x2, y3 = n == 4 ? csy[fv[3]] : y2;
                if ((x0 < 0 && x1 < 0 && x2 < 0 && x3 < 0) || (y0 < 0 && y1 < 0 && y2 < 0 && y3 < 0) ||
                    (x0 >= SCREEN_W && x1 >= SCREEN_W && x2 >= SCREEN_W && x3 >= SCREEN_W) ||
                    (y0 >= SCREEN_H && y1 >= SCREEN_H && y2 >= SCREEN_H && y3 >= SCREEN_H)) continue;
            }
            if (!(f->flags & F_DOUBLE) && normal_clip(x0, y0, x1, y1, x2, y2) <= 0) continue;
            p = prim_alloc(fb);
            if (!p) continue;
            p->code = PF_TEX | PF_GOURAUD | (n == 4 ? PF_QUAD : 0);
            if (f->flags & (F_ADD | F_HALF)) p->code |= PF_SEMI;
            du = (f->flags & F_ANIMUV) ? screen_du : 0;
            {   /* each 64x64 tile has its own CLUT; animated screens use the current frame's tile
                   (tables: CLUT_ID divides by 13, too slow per face on the R3000) */
                int tile = (((f->uv[0] + du) & 255) >> 6) + (f->uv[1] >> 6) * 4;
                p->tpage = lab_tpage[f->page & 7][(f->flags & F_ADD) != 0];
                p->clut = lab_clut[(f->pal ? CL_EXTRA(f->pal - 1) : CL_LAB(f->page, tile)) & 511];
            }
            fg = fog(za);
            if (pm != 4096) fg = fg * pm >> 12;
            for (k = 0; k < n; k++) {
                int vi = fv[k];
                const u8 *c = &f->rgb[k * 3];
                p->v[k].x = csx[vi]; p->v[k].y = csy[vi];
                p->v[k].u = (u8)(f->uv[k * 2] + du);
                p->v[k].v = (u8)(f->uv[k * 2 + 1] + (door ? vclamp[vi] * 63 >> 11 : 0));
                if (fg == 4096) { p->v[k].r = c[0]; p->v[k].g = c[1]; p->v[k].b = c[2]; }   /* the baked colour as is */
                else if (fg < 4096) {
                    p->v[k].r = (u8)(c[0] * fg >> 12); p->v[k].g = (u8)(c[1] * fg >> 12); p->v[k].b = (u8)(c[2] * fg >> 12);
                } else {
                    int r = c[0] * fg >> 12, g = c[1] * fg >> 12, b = c[2] * fg >> 12;
                    p->v[k].r = (u8)(r > 255 ? 255 : r); p->v[k].g = (u8)(g > 255 ? 255 : g); p->v[k].b = (u8)(b > 255 ? 255 : b);
                }
            }
            {
                int otz = (int)(za >> 4) + ((f->flags & F_FLOOR) ? 8 : 0) + ot_bias;
                if (otz < 0) otz = 0; else if (otz >= OT_LEN) otz = OT_LEN - 1;
                p->next = fb->ot[otz]; fb->ot[otz] = p;
            }
            polys_drawn++;
        } else clip_face(fb, pr, f, n, oo, pm);
    }
}

/* camera-facing additive quad with a texture tile from page B */
static void billboard(FrameBuf *fb, const MATRIX *view, const PROJ *pr, s32 wx, s32 wy, s32 wz, s32 size,
                      int tile, int r, int g, int b, int bias)
{
    SVECTOR s; VECTOR c;
    POLY *p;
    s32 hs, x, y;
    int k, tu = (tile & 3) * 64, tv = (tile >> 2) * 64, fg;
    s.vx = (s16)wx; s.vy = (s16)wy; s.vz = (s16)wz;
    mat_apply(view, &s, &c);
    if (c.vz < NEAR_Z + size / 2 || c.vz > FOG_FAR) return;
    hs = size * pr->h / c.vz;
    if (hs < 1) return;
    if (hs > 400) hs = 400;
    x = pr->ofx + c.vx * pr->h / c.vz; y = pr->ofy + c.vy * pr->h / c.vz;
    if (x + hs < 0 || x - hs > SCREEN_W || y + hs < 0 || y - hs > SCREEN_H) return;
    p = prim_alloc(fb);
    if (!p) return;
    fg = fog(c.vz);
    p->code = PF_TEX | PF_QUAD | PF_SEMI;
    p->tpage = get_tpage(TP_4BIT, SEMI_ADD, LAB_PAGE_X(1), LAB_PAGE_Y(1));
    p->clut = CLUT_ID(CL_LAB(1, tile));
    for (k = 0; k < 4; k++) {
        p->v[k].x = (s16)(x + ((k & 1) ? hs : -hs));
        p->v[k].y = (s16)(y + ((k & 2) ? hs : -hs));
        p->v[k].u = (u8)(tu + ((k & 1) ? 63 : 0));
        p->v[k].v = (u8)(tv + ((k & 2) ? 63 : 0));
        p->v[k].r = (u8)(r * fg >> 12); p->v[k].g = (u8)(g * fg >> 12); p->v[k].b = (u8)(b * fg >> 12);
    }
    ot_add(fb, (int)(c.vz >> 4) + bias + ot_bias, p);
    polys_drawn++;
}

/* a world-space quad (corners in Z order), textured from page B, optional additive; clut 0 = the tile's */
static void world_quad_c(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const SVECTOR *w, int tile,
                         int r, int g, int b, int add, int bias, u16 clut)
{
    VECTOR c[4];
    POLY *p;
    int k, tu = (tile & 3) * 64, tv = (tile >> 2) * 64, fg;
    s32 z = 0;
#ifdef PSX_GTE
    u32 sxy[4];
    gte_load_matrix(view);       /* cheap, and whatever drew last may have left another matrix */
    gte_load_proj(pr);
    for (k = 0; k < 4; k++) {
        gte_xform(w[k].vx, w[k].vy, w[k].vz, &c[k], &sxy[k]);
        if (outcode(&c[k])) return;
        z += c[k].vz;
    }
#else
    for (k = 0; k < 4; k++) {
        mat_apply(view, &w[k], &c[k]);
        if (outcode(&c[k])) return;
        z += c[k].vz;
    }
#endif
    z >>= 2;
    if (z > FOG_FAR) return;
    p = prim_alloc(fb);
    if (!p) return;
    fg = fog(z);
    p->code = PF_TEX | PF_QUAD | PF_GOURAUD | (add ? PF_SEMI : 0);
    p->tpage = get_tpage(TP_4BIT, SEMI_ADD, LAB_PAGE_X(1), LAB_PAGE_Y(1));
    p->clut = clut ? clut : CLUT_ID(CL_LAB(1, tile));
    for (k = 0; k < 4; k++) {
#ifdef PSX_GTE
        if (c[k].vx <= 32767 && c[k].vx >= -32767 && c[k].vy <= 32767 && c[k].vy >= -32767) {
            p->v[k].x = (s16)(sxy[k] & 0xffff); p->v[k].y = (s16)(sxy[k] >> 16);
        } else
#endif
        {
            p->v[k].x = (s16)(pr->ofx + c[k].vx * pr->h / c[k].vz);
            p->v[k].y = (s16)(pr->ofy + c[k].vy * pr->h / c[k].vz);
        }
        p->v[k].u = (u8)(tu + ((k & 1) ? 63 : 0));
        p->v[k].v = (u8)(tv + ((k & 2) ? 63 : 0));
        p->v[k].r = (u8)(r * fg >> 12); p->v[k].g = (u8)(g * fg >> 12); p->v[k].b = (u8)(b * fg >> 12);
    }
    ot_add(fb, (int)(z >> 4) + bias + ot_bias, p);
    polys_drawn++;
}

static void world_quad(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const SVECTOR *w, int tile,
                       int r, int g, int b, int add, int bias)
{
    world_quad_c(fb, view, pr, w, tile, r, g, b, add, bias, 0);
}

/* keycards (a spinning card in the key colour) and soul shards (a glowing crystal), with halos */
static void draw_pickups(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    static const u8 KC[4][3] = { { 0, 0, 0 }, { 60, 220, 255 }, { 255, 170, 40 }, { 240, 40, 60 } };
    int i, k;
    for (i = 0; i < lab.nthings && i < 256; i++) {
        const LabThing *th = &lab.things[i];
        s32 gy, cy;
        int a = t * 40 + i * 700, ca = icos(a), sa = isin(a);
        SVECTOR w[4];
        if ((th->type != 2 && th->type != 3) || taken[i]) continue;
        gy = level_ground(th->x, th->z, 0x7fff0000);
        cy = gy - 900 - (isin(t * 50 + i * 900) * 90 >> 12);
        if (th->type == 2) {
            const u8 *c = KC[th->arg & 3];
            static const int cx_[4] = { -1, 1, -1, 1 }, cy_[4] = { -1, -1, 1, 1 };
            for (k = 0; k < 4; k++) {
                s32 lx = cx_[k] * 210, ly = cy_[k] * 140;
                w[k].vx = (s16)(th->x + (lx * ca >> 12)); w[k].vz = (s16)(th->z + (lx * sa >> 12)); w[k].vy = (s16)(cy + ly);
            }
            world_quad_c(fb, view, pr, w, 0, 128, 128, 128, 0, -2, CLUT_ID(CL_EXTRA(PAL_KEY(th->arg & 3))));
            billboard(fb, view, pr, th->x, cy, th->z, 520, 15, c[0] / 2, c[1] / 2, c[2] / 2, 2);
        } else {
            /* an octahedron: 4 upper and 4 lower triangles (quads with a repeated corner), additive */
            static const s16 R = 170, H = 320;
            for (k = 0; k < 8; k++) {
                int a0 = a + (k & 3) * 1024, a1 = a0 + 1024, up = k < 4;
                w[0].vx = th->x; w[0].vz = th->z; w[0].vy = (s16)(cy + (up ? -H : H));
                w[1].vx = (s16)(th->x + (R * icos(a0) >> 12)); w[1].vz = (s16)(th->z + (R * isin(a0) >> 12)); w[1].vy = (s16)cy;
                w[2].vx = (s16)(th->x + (R * icos(a1) >> 12)); w[2].vz = (s16)(th->z + (R * isin(a1) >> 12)); w[2].vy = (s16)cy;
                w[3] = w[2];
                world_quad(fb, view, pr, w, 5, 70, 60, 120, 1, -2);
            }
            billboard(fb, view, pr, th->x, cy, th->z, 600, 15, 70, 40, 110, 2);
        }
    }
}

/* the rooms the last level_draw could see (through the doorways from the camera's room) */
static u32 last_vis = 0xffffffffu;

/* can a ball at (x, y, z), radius r, be on screen? in a room that can be seen, in front of the
   camera, inside the screen's edges and nearer than the fog. The same test the level's own chunks
   pass, for the things in it (enemies, pickups) */
int level_ball_visible(const MATRIX *view, const PROJ *pr, s32 x, s32 y, s32 z, s32 r)
{
    SVECTOR s; VECTOR c;
    s32 hw = SCREEN_W / 2 + 8, hh = SCREEN_H / 2 + 8;
    int a = level_area_at(x, z);
    if (a >= 0 && a < 32 && !(last_vis & (1u << a))) return 0;
    s.vx = (s16)x; s.vy = (s16)y; s.vz = (s16)z;
    mat_apply(view, &s, &c);
    if (c.vz < -r || c.vz - r > FOG_FAR) return 0;
    if (c.vx * pr->h > (c.vz * hw) + r * (pr->h + hw)) return 0;
    if (-c.vx * pr->h > (c.vz * hw) + r * (pr->h + hw)) return 0;
    if (c.vy * pr->h > (c.vz * hh) + r * (pr->h + hh)) return 0;
    if (-c.vy * pr->h > (c.vz * hh) + r * (pr->h + hh)) return 0;
    return 1;
}

int level_point_seen(s32 x, s32 z)
{
    int a = level_area_at(x, z);
    return a < 0 || a >= 32 || (last_vis & (1u << a));
}

void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    int i, k;
    u32 seen = visible_mask(view, pr), vis = all_rooms ? 0xffffffffu : seen;
    last_vis = vis;
    for (i = 0; i < 8; i++) pulse_now[i] = pulse(i, t);
    if (!lab_clut[0] && !lab_clut[1]) lab_tables();
    screen_du = ((t / SCREEN_FRAME_TICKS) & 1) * 64;
#ifdef PSX_GTE
    gte_load_matrix(view);
    gte_load_proj(pr);
#endif
    polys_drawn = 0;
    lvl_st_verts = lvl_st_faces = lvl_st_clip = lvl_st_chunks = 0;
    for (i = 0; i < LAB_NCHUNKS; i++) {
        const LabChunk *ch = &lab_chunks[i];
        SVECTOR s; VECTOR c;
        s32 r = ch->r;
        if (!(vis & (1u << ch->area))) continue;
        if (ch->sec && lab_secs[ch->sec - 1].kind == 1 && sec_dyn[ch->sec - 1] >= lab_secs[ch->sec - 1].ka) continue;   /* door fully open */
        s.vx = ch->cx; s.vy = (s16)(ch->cy - (ch->sec ? sec_dyn[ch->sec - 1] : 0)); s.vz = ch->cz;
        mat_apply(view, &s, &c);
        if (c.vz < -r || c.vz - r > FOG_FAR) continue;
        /* frustum: the screen's half-widths over h, of z, padded by the radius */
        {
            s32 hw = SCREEN_W / 2 + 8, hh = SCREEN_H / 2 + 8;
            if (c.vx * pr->h > (c.vz * hw) + r * (pr->h + hw)) continue;
            if (-c.vx * pr->h > (c.vz * hw) + r * (pr->h + hw)) continue;
            if (c.vy * pr->h > (c.vz * hh) + r * (pr->h + hh)) continue;
            if (-c.vy * pr->h > (c.vz * hh) + r * (pr->h + hh)) continue;
        }
        draw_chunk(fb, view, pr, ch, t);
    }
    /* light halos */
    for (i = 0; i < LAB_NSPRITES; i++) {
        const LabSprite *s = &lab_sprites[i];
        int pm = pulse(s->anim, t);
        if (!(seen & (1u << s->area))) continue;
        billboard(fb, view, pr, s->x, s->y, s->z, s->size, 15, s->r * pm >> 13, s->g * pm >> 13, s->b * pm >> 13, -4);
    }
    /* energy rings rising through the tanks */
    for (i = 0; i < LAB_NRINGS; i++) {
        const LabRing *rg = &lab_rings[i];
        s32 span = rg->yb - rg->yt;                 /* positive: bottom is below top */
        int ring;
        if (!all_rooms && !level_point_seen(rg->x, rg->z)) continue;   /* its room can't be seen */
        {   /* skip the whole tank when it's behind the camera or in the fog */
            SVECTOR s; VECTOR c;
            s.vx = rg->x; s.vy = (s16)((rg->yt + rg->yb) / 2); s.vz = rg->z;
            mat_apply(view, &s, &c);
            if (c.vz < -(rg->r + span) || c.vz > FOG_FAR + rg->r) continue;
        }
        for (ring = 0; ring < 3; ring++) {
            s32 y = rg->yb - ((t * 14 + ring * span / 3) % span);
            int glow = 46 + (isin(t * 40 + ring * 1365) * 20 >> 12);
            for (k = 0; k < 12; k++) {
                SVECTOR w[4];
                int a0 = k * 4096 / 12, a1 = (k + 1) * 4096 / 12;
                s32 r = rg->r;
                w[0].vx = (s16)(rg->x + (icos(a0) * r >> 12)); w[0].vz = (s16)(rg->z + (isin(a0) * r >> 12)); w[0].vy = (s16)(y - 90);
                w[1].vx = (s16)(rg->x + (icos(a1) * r >> 12)); w[1].vz = (s16)(rg->z + (isin(a1) * r >> 12)); w[1].vy = (s16)(y - 90);
                w[2] = w[0]; w[2].vy = (s16)(y + 90);
                w[3] = w[1]; w[3].vy = (s16)(y + 90);
                world_quad(fb, view, pr, w, 5, glow / 2, glow + 30, glow + 40, 1, -2);
            }
        }
    }
    /* turbine fans spinning in the reactor */
    for (i = 0; i < LAB_NFANS; i++) {
        const LabFan *fn = &lab_fans[i];
        int a = t * 90 * fn->speed;
        if (!all_rooms && !level_point_seen(fn->x, fn->z)) continue;
        SVECTOR w[4];
        static const int cx[4] = { -1, 1, -1, 1 }, cy[4] = { -1, -1, 1, 1 };
        for (k = 0; k < 4; k++) {
            s32 lx = cx[k] * fn->r, ly = cy[k] * fn->r;
            w[k].vx = (s16)(fn->x + ((lx * icos(a) - ly * isin(a)) >> 12));
            w[k].vy = (s16)(fn->y + ((lx * isin(a) + ly * icos(a)) >> 12));
            w[k].vz = fn->z;
        }
        world_quad(fb, view, pr, w, 12, 110, 110, 110, 0, -1);
    }
    if (lab.nthings) draw_pickups(fb, view, pr, t);
}
