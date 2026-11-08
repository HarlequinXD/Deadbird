#include "level.h"
#include "tim.h"
#include "ik.h"
#include "lab_data.h"
#include "gte_hw.h"

/* face flags (tools/build_lab.py) */
#define F_QUAD   1
#define F_ADD    2
#define F_HALF   4
#define F_DOUBLE 8
#define F_PULSE  16
#define F_FLOOR  32
#define F_EMIT   64
#define F_ANIMUV 128          /* animated screen: U steps along a 4-frame strip */
#define SCREEN_FRAME_TICKS 8

#ifndef FOG_FAR
#define FOG_NEAR 8000
#define FOG_FAR  18000
#endif
#define NEAR_Z   140
#define GUARD    3                 /* guard band: x,y within +-3z keeps screen coords inside 11 bits */
#define MAXCV    1400

static TimInfo page_tim[LAB_NPAGES];
static const unsigned char *const page_data[LAB_NPAGES] = { lab_tim0, lab_tim1, lab_tim2, lab_tim3, lab_tim4, lab_tim5 };
static const int page_size[LAB_NPAGES] = { LAB_TIM0_SIZE, LAB_TIM1_SIZE, LAB_TIM2_SIZE, LAB_TIM3_SIZE, LAB_TIM4_SIZE, LAB_TIM5_SIZE };
static int uploaded, polys_drawn, frame_t, flat;
int lvl_st_verts, lvl_st_faces, lvl_st_clip, lvl_st_chunks;

void level_debug_flat(int on) { flat = on; }

void level_init(void)
{
    if (!uploaded) {
        int k;
        for (k = 0; k < LAB_NPAGES; k++) tim_upload(page_data[k], page_size[k], &page_tim[k]);
        uploaded = 1;
    }
    frame_t = 0;
}

void level_spawn(s32 *x, s32 *z, int *yaw) { *x = LAB_SPAWN_X; *z = LAB_SPAWN_Z; *yaw = LAB_SPAWN_YAW; }
void level_tank(VECTOR *c) { c->vx = LAB_TANK_X; c->vy = LAB_TANK_Y; c->vz = LAB_TANK_Z; }
int  level_polys_drawn(void) { return polys_drawn; }
int  level_data_bytes(void)
{
    return (int)(sizeof(lab_verts) + sizeof(lab_faces) + sizeof(lab_chunks) + sizeof(lab_boxes) + sizeof(lab_sprites)
                 + sizeof(lab_fans) + sizeof(lab_rings) + sizeof(lab_pods) + sizeof(lab_ceil) + sizeof(lab_areas));
}
int  level_area_count(void) { return LAB_NAREAS; }
const LabArea *level_area(int i) { return &lab_areas[i]; }
int  level_view_count(void) { return LAB_NVIEWS; }
int  level_route_count(void) { return LAB_NROUTE; }
void level_route_point(int i, s32 *x, s32 *z) { *x = lab_route[i][0]; *z = lab_route[i][1]; }
const LabView *level_view(int i) { return &lab_views[i]; }
int  level_area_at(s32 x, s32 z)
{
    int i;
    for (i = 0; i < LAB_NAREAS; i++) {
        const LabArea *a = &lab_areas[i];
        if (x >= a->x0 && x <= a->x1 && z >= a->z0 && z <= a->z1) return i;
    }
    return -1;
}

/* rooms worth drawing from a camera position: the PVS of every area within a doorway's
   reach of it (so standing in a doorway shows both sides) */
static u32 visible_mask(const MATRIX *view)
{
    static u32 last = 0xffffffffu;
    VECTOR t, c;
    u32 m = 0;
    int i;
    t.vx = -view->t[0]; t.vy = -view->t[1]; t.vz = -view->t[2];
    mat_rot_tapply(view, &t, &c);
    for (i = 0; i < LAB_NAREAS; i++) {
        const LabArea *a = &lab_areas[i];
        if (c.vx >= a->x0 - 500 && c.vx <= a->x1 + 500 && c.vz >= a->z0 - 500 && c.vz <= a->z1 + 500) m |= a->pvs;
    }
    if (m) last = m;
    return last;
}

/* ------------------------------------------------------------------ collision */
s32 level_ground(s32 x, s32 z, s32 cur_y)
{
    s32 best = 0;
    int i;
    if (flat) return 0;
    for (i = 0; i < LAB_NBOXES; i++) {
        const LabBox *b = &lab_boxes[i];
        if (x < b->x0 || x > b->x1 || z < b->z0 || z > b->z1) continue;
        if (-b->h >= cur_y - STEP_HEIGHT && -b->h < best) best = -b->h;
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

int level_collide(s32 *x, s32 *z, s32 y, s32 r, s32 *nx, s32 *nz)
{
    int i, hit = 0;
    if (flat) return 0;
    for (i = 0; i < LAB_NBOXES; i++) {
        const LabBox *b = &lab_boxes[i];
        if (-b->h >= y - STEP_HEIGHT) continue;        /* low enough to step onto */
        hit |= push_box(x, z, r, b->x0, b->z0, b->x1, b->z1, nx, nz);
    }
    return hit;
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
            s32 t0 = (s32)(((long long)(mn[k] - p[k]) << 12) / dd[k]);
            s32 t1 = (s32)(((long long)(mx[k] - p[k]) << 12) / dd[k]);
            if (t0 > t1) { s32 s = t0; t0 = t1; t1 = s; }
            if (t0 > lo) lo = t0;
            if (t1 < hi) hi = t1;
            if (lo > hi) return 4096;
        }
    }
    return lo;
}

int level_clip_segment(const VECTOR *a, const VECTOR *b, s32 m)
{
    VECTOR d;
    s32 best = 4096, t;
    int i;
    d.vx = b->vx - a->vx; d.vy = b->vy - a->vy; d.vz = b->vz - a->vz;
    for (i = 0; i < LAB_NBOXES; i++) {
        const LabBox *bx = &lab_boxes[i];
        s32 top = -bx->h - m;
        if (a->vx >= bx->x0 - m && a->vx <= bx->x1 + m && a->vz >= bx->z0 - m && a->vz <= bx->z1 + m && a->vy >= top)
            continue;                                   /* starting inside (standing on it) */
        t = seg_box(a, &d, bx->x0 - m, top, bx->z0 - m, bx->x1 + m, 0, bx->z1 + m);
        if (t < best) best = t;
    }
    /* ceiling of the room the segment starts in */
    for (i = 0; i < LAB_NCEIL; i++) {
        const LabBox *c = &lab_ceil[i];
        s32 cy = -c->h + m;
        if (a->vx < c->x0 || a->vx > c->x1 || a->vz < c->z0 || a->vz > c->z1) continue;
        if (b->vy < cy && d.vy < 0) {
            t = (s32)(((long long)(cy - a->vy) << 12) / d.vy);
            if (t >= 0 && t < best) best = t;
        }
        break;
    }
    return (int)best;
}

void level_update(void) { frame_t++; }

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
static int screen_du;        /* animated screens: U offset of the current frame */

static u8 outcode(const VECTOR *c)
{
    u8 o = 0;
    if (c->vz < NEAR_Z) o |= 1;
    if (c->vx > c->vz * GUARD) o |= 2;
    if (c->vx < -c->vz * GUARD) o |= 4;
    if (c->vy > c->vz * GUARD) o |= 8;
    if (c->vy < -c->vz * GUARD) o |= 16;
    return o;
}

static s32 plane_d(const CV *v, int p)
{
    switch (p) {
    case 0: return v->z - NEAR_Z;
    case 1: return v->z * GUARD - v->x;
    case 2: return v->z * GUARD + v->x;
    case 3: return v->z * GUARD - v->y;
    default: return v->z * GUARD + v->y;
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
    p->tpage = get_tpage(TP_8BIT, (f->flags & F_ADD) ? SEMI_ADD : SEMI_HALF, lab_page_xy[f->page][0], lab_page_xy[f->page][1]);
    p->clut = get_clut(0, 485 + f->page);
    for (k = 0; k < n; k++) {
        p->v[k].x = x[k]; p->v[k].y = y[k]; p->v[k].u = u[k]; p->v[k].v = v[k];
        p->v[k].r = (u8)(r[k] > 255 ? 255 : r[k]); p->v[k].g = (u8)(g[k] > 255 ? 255 : g[k]); p->v[k].b = (u8)(b[k] > 255 ? 255 : b[k]);
    }
    otz = (int)(z >> 4) + ((f->flags & F_FLOOR) ? 8 : 0);
    ot_add(fb, otz, p);
    polys_drawn++;
}

static void draw_chunk(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const LabChunk *ch, int t)
{
    int i, k;
    if (ch->nv > MAXCV) return;
    lvl_st_chunks++; lvl_st_verts += ch->nv; lvl_st_faces += ch->nf;
    for (i = 0; i < ch->nv; i++) {
        const LabVert *lv = &lab_verts[ch->v0 + i];
#ifdef PSX_GTE
        u32 sxy;
        gte_xform(lv->x, lv->y, lv->z, &cvv[i], &sxy);     /* matrix + projection set in level_draw */
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
        SVECTOR s; s.vx = lv->x; s.vy = lv->y; s.vz = lv->z;
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
        pm = pulse_now[f->anim > 7 ? 7 : f->anim];
        if (!oo) {
            s16 x[4], y[4];
            int r[4], g[4], b[4];
            s32 za = n == 4 ? zs >> 2 : zs / 3;
            int fg;
            if (za > FOG_FAR) continue;
            for (k = 0; k < n; k++) { x[k] = csx[f->v[k]]; y[k] = csy[f->v[k]]; }
            if (!(f->flags & F_DOUBLE) && normal_clip(x[0], y[0], x[1], y[1], x[2], y[2]) <= 0) continue;
            fg = fog(za) * pm >> 12;
            for (k = 0; k < n; k++) {
                r[k] = f->rgb[k * 3] * fg >> 12; g[k] = f->rgb[k * 3 + 1] * fg >> 12; b[k] = f->rgb[k * 3 + 2] * fg >> 12;
            }
            {
                u8 u[4], v[4];
                int du = (f->flags & F_ANIMUV) ? screen_du : 0;
                for (k = 0; k < n; k++) { u[k] = (u8)(f->uv[k * 2] + du); v[k] = f->uv[k * 2 + 1]; }
                emit(fb, f, x, y, u, v, r, g, b, n, za);
            }
        } else {
            /* crosses the near plane / guard band: clip in view space, draw as a fan */
            static const int order4[4] = { 0, 1, 3, 2 }, order3[3] = { 0, 1, 2 };
            const int *ord = n == 4 ? order4 : order3;
            CV in[4], out[12];
            int m, j;
            for (k = 0; k < n; k++) {
                int c = ord[k], vi = f->v[c];
                in[k].x = cvv[vi].vx; in[k].y = cvv[vi].vy; in[k].z = cvv[vi].vz;
                in[k].u = f->uv[c * 2] << 8; in[k].v = f->uv[c * 2 + 1] << 8;
                in[k].r = f->rgb[c * 3] << 4; in[k].g = f->rgb[c * 3 + 1] << 4; in[k].b = f->rgb[c * 3 + 2] << 4;
            }
            lvl_st_clip++;
            m = clip_poly(in, n, out, oo);
            if (f->flags & F_ANIMUV) {
                int du = screen_du * 256;
                for (j = 0; j < m; j++) out[j].u += du;
            }
            for (j = 1; j + 1 < m; j++) {
                const CV *tv[3];
                s16 x[3], y[3];
                u8 u[3], v[3];
                int r[3], g[3], b[3];
                s32 za;
                int fg;
                tv[0] = &out[0]; tv[1] = &out[j]; tv[2] = &out[j + 1];
                for (k = 0; k < 3; k++) {
                    x[k] = (s16)(pr->ofx + tv[k]->x * pr->h / tv[k]->z);
                    y[k] = (s16)(pr->ofy + tv[k]->y * pr->h / tv[k]->z);
                }
                if (!(f->flags & F_DOUBLE) && normal_clip(x[0], y[0], x[1], y[1], x[2], y[2]) <= 0) continue;
                za = (tv[0]->z + tv[1]->z + tv[2]->z) / 3;
                fg = fog(za) * pm >> 12;
                for (k = 0; k < 3; k++) {
                    u[k] = (u8)(tv[k]->u >> 8); v[k] = (u8)(tv[k]->v >> 8);
                    r[k] = (tv[k]->r >> 4) * fg >> 12; g[k] = (tv[k]->g >> 4) * fg >> 12; b[k] = (tv[k]->b >> 4) * fg >> 12;
                }
                emit(fb, f, x, y, u, v, r, g, b, 3, za);
            }
        }
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
    p->tpage = get_tpage(TP_8BIT, SEMI_ADD, 384 + 128, 0);
    p->clut = get_clut(0, 486);
    for (k = 0; k < 4; k++) {
        p->v[k].x = (s16)(x + ((k & 1) ? hs : -hs));
        p->v[k].y = (s16)(y + ((k & 2) ? hs : -hs));
        p->v[k].u = (u8)(tu + ((k & 1) ? 63 : 0));
        p->v[k].v = (u8)(tv + ((k & 2) ? 63 : 0));
        p->v[k].r = (u8)(r * fg >> 12); p->v[k].g = (u8)(g * fg >> 12); p->v[k].b = (u8)(b * fg >> 12);
    }
    ot_add(fb, (int)(c.vz >> 4) + bias, p);
    polys_drawn++;
}

/* a world-space quad (corners in Z order), textured from page B, optional additive */
static void world_quad(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const SVECTOR *w, int tile,
                       int r, int g, int b, int add, int bias)
{
    VECTOR c[4];
    POLY *p;
    int k, tu = (tile & 3) * 64, tv = (tile >> 2) * 64, fg;
    s32 z = 0;
    for (k = 0; k < 4; k++) {
        mat_apply(view, &w[k], &c[k]);
        if (outcode(&c[k])) return;
        z += c[k].vz;
    }
    z >>= 2;
    if (z > FOG_FAR) return;
    p = prim_alloc(fb);
    if (!p) return;
    fg = fog(z);
    p->code = PF_TEX | PF_QUAD | PF_GOURAUD | (add ? PF_SEMI : 0);
    p->tpage = get_tpage(TP_8BIT, SEMI_ADD, 384 + 128, 0);
    p->clut = get_clut(0, 486);
    for (k = 0; k < 4; k++) {
        p->v[k].x = (s16)(pr->ofx + c[k].vx * pr->h / c[k].vz);
        p->v[k].y = (s16)(pr->ofy + c[k].vy * pr->h / c[k].vz);
        p->v[k].u = (u8)(tu + ((k & 1) ? 63 : 0));
        p->v[k].v = (u8)(tv + ((k & 2) ? 63 : 0));
        p->v[k].r = (u8)(r * fg >> 12); p->v[k].g = (u8)(g * fg >> 12); p->v[k].b = (u8)(b * fg >> 12);
    }
    ot_add(fb, (int)(z >> 4) + bias, p);
    polys_drawn++;
}

void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    int i, k;
    u32 vis = visible_mask(view);
    for (i = 0; i < 8; i++) pulse_now[i] = pulse(i, t);
    screen_du = ((t / SCREEN_FRAME_TICKS) & 3) * 64;
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
        s.vx = ch->cx; s.vy = ch->cy; s.vz = ch->cz;
        mat_apply(view, &s, &c);
        if (c.vz < -r || c.vz - r > FOG_FAR) continue;
        /* frustum: half-widths 160/h and 120/h of z, padded by the radius */
        if (c.vx * pr->h > (c.vz * 160) + r * (pr->h + 160)) continue;
        if (-c.vx * pr->h > (c.vz * 160) + r * (pr->h + 160)) continue;
        if (c.vy * pr->h > (c.vz * 120) + r * (pr->h + 120)) continue;
        if (-c.vy * pr->h > (c.vz * 120) + r * (pr->h + 120)) continue;
        draw_chunk(fb, view, pr, ch, t);
    }
    /* light halos */
    for (i = 0; i < LAB_NSPRITES; i++) {
        const LabSprite *s = &lab_sprites[i];
        int pm = pulse(s->anim, t);
        if (!(vis & (1u << s->area))) continue;
        billboard(fb, view, pr, s->x, s->y, s->z, s->size, 15, s->r * pm >> 13, s->g * pm >> 13, s->b * pm >> 13, -4);
    }
    /* energy rings rising through the tanks */
    for (i = 0; i < LAB_NRINGS; i++) {
        const LabRing *rg = &lab_rings[i];
        s32 span = rg->yb - rg->yt;                 /* positive: bottom is below top */
        int ring;
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
}
