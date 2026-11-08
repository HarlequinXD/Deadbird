#include "level.h"
#include "gte.h"
#include "fx.h"

/* ------------------------------------------------------------------ layout */
#define ARENA 4000
static const Box boxes[] = {
    /* boundary walls */
    {     0, -ARENA - 100, ARENA + 200, 100, 1400, 0x3a1416 },
    {     0,  ARENA + 100, ARENA + 200, 100, 1400, 0x3a1416 },
    { -ARENA - 100,     0, 100, ARENA, 1400, 0x34121a },
    {  ARENA + 100,     0, 100, ARENA, 1400, 0x34121a },
    /* low platform (step up onto it) */
    {  1600,  1200, 700, 700, 150, 0x5a2a1e },
    /* stairs up to a high platform */
    { -2200,     0, 500, 200, 130, 0x4e2420 },
    { -2200,   400, 500, 200, 260, 0x4e2420 },
    { -2200,   800, 500, 200, 390, 0x4e2420 },
    { -2200,  1200, 500, 200, 520, 0x4e2420 },
    { -2200,  2100, 500, 700, 520, 0x5a2a1e },
    /* platform at the top of the ramp */
    {  2900, -3350, 500, 450, 520, 0x5a2a1e },
    /* pillars and a wall to run into */
    {   600, -1500, 250, 250, 2200, 0x2e1014 },
    {  -900, -2400, 250, 250, 2200, 0x2e1014 },
    {  2600,  -600, 300, 300, 2200, 0x2e1014 },
    {     0,  2700, 1200, 150, 900, 0x3e161a },
};
#define NBOX ((int)(sizeof(boxes) / sizeof(boxes[0])))

/* ramp: x 2400..3400, rising from y 0 at z = -1600 to y -520 at z = -2900 */
#define RAMP_X0 2400
#define RAMP_X1 3400
#define RAMP_Z0 -1600
#define RAMP_Z1 -2900
#define RAMP_H  520

Totem totems[NTOTEMS];
static const s16 totem_pos[NTOTEMS][2] = { { -2200, 2300 }, { 1600, 1200 }, { 2900, -3400 }, { -3000, -2800 }, { 3200, 2900 } };
#define TOTEM_R 140
#define TOTEM_H 760

static s32 ramp_height(s32 x, s32 z)
{
    if (x < RAMP_X0 || x > RAMP_X1 || z > RAMP_Z0 || z < RAMP_Z1) return 1;      /* 1 = not on the ramp */
    return -(RAMP_H * (RAMP_Z0 - z) / (RAMP_Z0 - RAMP_Z1));
}

s32 level_ground(s32 x, s32 z, s32 cur_y)
{
    s32 best = 0, r;
    int i;
    for (i = 0; i < NBOX; i++) {
        const Box *b = &boxes[i];
        if (x < b->cx - b->hx || x > b->cx + b->hx || z < b->cz - b->hz || z > b->cz + b->hz) continue;
        if (-b->h >= cur_y - STEP_HEIGHT && -b->h < best) best = -b->h;
    }
    r = ramp_height(x, z);
    if (r <= 0 && r >= cur_y - STEP_HEIGHT && r < best) best = r;
    return best;
}

static int push_box(s32 *x, s32 *z, s32 r, s32 bx0, s32 bz0, s32 bx1, s32 bz1, s32 *nx, s32 *nz)
{
    s32 qx = *x < bx0 ? bx0 : (*x > bx1 ? bx1 : *x);
    s32 qz = *z < bz0 ? bz0 : (*z > bz1 ? bz1 : *z);
    s32 dx = *x - qx, dz = *z - qz, d2 = dx * dx + dz * dz, d;
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
    for (i = 0; i < NBOX; i++) {
        const Box *b = &boxes[i];
        if (-b->h >= y - STEP_HEIGHT) continue;        /* low enough to step onto */
        hit |= push_box(x, z, r, b->cx - b->hx, b->cz - b->hz, b->cx + b->hx, b->cz + b->hz, nx, nz);
    }
    for (i = 0; i < NTOTEMS; i++) {
        Totem *t = &totems[i];
        if (t->dead_t) continue;
        hit |= push_box(x, z, r, t->x - TOTEM_R, t->z - TOTEM_R, t->x + TOTEM_R, t->z + TOTEM_R, nx, nz);
    }
    return hit;
}

void level_init(void)
{
    int i;
    for (i = 0; i < NTOTEMS; i++) {
        totems[i].x = totem_pos[i][0]; totems[i].z = totem_pos[i][1];
        totems[i].y = level_ground(totems[i].x, totems[i].z, -2000);
        totems[i].hp = 3; totems[i].flash = 0; totems[i].dead_t = 0;
    }
}

int level_totems_destroyed(void)
{
    int i, n = 0;
    for (i = 0; i < NTOTEMS; i++) n += totems[i].dead_t > 0;
    return n;
}

void level_update(void)
{
    int i;
    for (i = 0; i < NTOTEMS; i++) {
        Totem *t = &totems[i];
        if (t->flash > 0) t->flash--;
        if (t->dead_t > 0 && ++t->dead_t > 600) { t->dead_t = 0; t->hp = 3; }   /* respawn */
    }
}

static int point_in_box(const VECTOR *p, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1)
{
    return p->vx >= x0 && p->vx <= x1 && p->vy >= y0 && p->vy <= y1 && p->vz >= z0 && p->vz <= z1;
}

int level_ray(const VECTOR *a, const VECTOR *b, VECTOR *hit)
{
    /* marched test (8 samples) - cheap and good enough for fast projectiles */
    int s, i;
    for (s = 1; s <= 8; s++) {
        VECTOR p;
        p.vx = a->vx + (b->vx - a->vx) * s / 8;
        p.vy = a->vy + (b->vy - a->vy) * s / 8;
        p.vz = a->vz + (b->vz - a->vz) * s / 8;
        for (i = 0; i < NTOTEMS; i++) {
            const Totem *t = &totems[i];
            if (!t->dead_t && point_in_box(&p, t->x - TOTEM_R, t->y - TOTEM_H, t->z - TOTEM_R, t->x + TOTEM_R, t->y, t->z + TOTEM_R)) {
                *hit = p; return 2 + i;
            }
        }
        for (i = 0; i < NBOX; i++) {
            const Box *bx = &boxes[i];
            if (point_in_box(&p, bx->cx - bx->hx, -bx->h, bx->cz - bx->hz, bx->cx + bx->hx, 0, bx->cz + bx->hz)) { *hit = p; return 1; }
        }
        if (p.vy >= 0 || (ramp_height(p.vx, p.vz) <= 0 && p.vy >= ramp_height(p.vx, p.vz))) { *hit = p; return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------------ drawing */
#define FLOOR_BIAS 10000     /* floor sits under everything: sort it at the far end of the OT */
static int fog_scale(s32 z)
{
    int f = 4096 - (z - 2500) * 4096 / 11000;
    return f < 300 ? 300 : (f > 4096 ? 4096 : f);
}

/* one face of a box, subdivided into cells so big walls don't vanish at the near plane */
static void draw_face(FrameBuf *fb, const MATRIX *cam, const VECTOR *o, const VECTOR *du, const VECTOR *dv,
                      int nu, int nv, int shade, u32 col, const PROJ *pr, int bias)
{
    static s16 sx[9][9], sy[9][9];
    static s32 sz[9][9];
    static u8 ok[9][9];
    int i, j, k;
    int cr = (int)(col >> 16) & 255, cg = (int)(col >> 8) & 255, cb = (int)col & 255;
    if (nu > 8) nu = 8;
    if (nv > 8) nv = 8;
    for (j = 0; j <= nv; j++)
        for (i = 0; i <= nu; i++) {
            SVECTOR w; VECTOR c;
            w.vx = (s16)(o->vx + du->vx * i / nu + dv->vx * j / nv);
            w.vy = (s16)(o->vy + du->vy * i / nu + dv->vy * j / nv);
            w.vz = (s16)(o->vz + du->vz * i / nu + dv->vz * j / nv);
            mat_apply(cam, &w, &c);
            ok[j][i] = (u8)rot_trans_pers(pr, &c, &sx[j][i], &sy[j][i]);
            sz[j][i] = c.vz;
        }
    for (j = 0; j < nv; j++)
        for (i = 0; i < nu; i++) {
            static const int ci[4] = { 0, 1, 0, 1 }, cj[4] = { 0, 0, 1, 1 };
            POLY *p;
            s32 z;
            if (!ok[j][i] || !ok[j][i + 1] || !ok[j + 1][i] || !ok[j + 1][i + 1]) continue;
            if (normal_clip(sx[j][i], sy[j][i], sx[j][i + 1], sy[j][i + 1], sx[j + 1][i], sy[j + 1][i]) <= 0) continue;
            p = prim_alloc(fb);
            if (!p) return;
            memset(p, 0, sizeof(*p));
            p->code = PF_QUAD | PF_GOURAUD;
            for (k = 0; k < 4; k++) {
                int x = i + ci[k], y = j + cj[k];
                int f = fog_scale(sz[y][x]) * shade >> 12;
                /* checker every other cell for a hint of tiling */
                int chk = ((i + j) & 1) ? 4096 : 3600;
                f = f * chk >> 12;
                p->v[k].x = sx[y][x]; p->v[k].y = sy[y][x];
                p->v[k].r = (u8)(cr * f >> 12); p->v[k].g = (u8)(cg * f >> 12); p->v[k].b = (u8)(cb * f >> 12);
            }
            z = (sz[j][i] + sz[j][i + 1] + sz[j + 1][i] + sz[j + 1][i + 1]) >> 2;
            ot_add(fb, bias == FLOOR_BIAS ? OT_LEN - 20 : (z >> 4) + bias, p);
        }
}

static int cells(s32 len, int subdiv) { int n = subdiv ? (int)(len / 700) + 1 : 1; return n > 8 ? 8 : n; }

void draw_box(FrameBuf *fb, const MATRIX *cam, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1,
              u32 col, const PROJ *pr, int bias, int sub)
{
    VECTOR o, u, v;
    s32 w = x1 - x0, h = y1 - y0, d = z1 - z0;
    /* top (y0 is the upper face in Y-down space) */
    o.vx = x0; o.vy = y0; o.vz = z1; u.vx = w; u.vy = 0; u.vz = 0; v.vx = 0; v.vy = 0; v.vz = -d;
    draw_face(fb, cam, &o, &u, &v, cells(w, sub), cells(d, sub), 4096, col, pr, bias);
    /* front (+z) */
    o.vx = x1; o.vy = y0; o.vz = z1; u.vx = -w; u.vy = 0; u.vz = 0; v.vx = 0; v.vy = h; v.vz = 0;
    draw_face(fb, cam, &o, &u, &v, cells(w, sub), cells(h, sub), 2900, col, pr, bias);
    /* back (-z) */
    o.vx = x0; o.vy = y0; o.vz = z0; u.vx = w; u.vy = 0; u.vz = 0; v.vx = 0; v.vy = h; v.vz = 0;
    draw_face(fb, cam, &o, &u, &v, cells(w, sub), cells(h, sub), 2300, col, pr, bias);
    /* left (-x) */
    o.vx = x0; o.vy = y0; o.vz = z1; u.vx = 0; u.vy = 0; u.vz = -d; v.vx = 0; v.vy = h; v.vz = 0;
    draw_face(fb, cam, &o, &u, &v, cells(d, sub), cells(h, sub), 3300, col, pr, bias);
    /* right (+x) */
    o.vx = x1; o.vy = y0; o.vz = z0; u.vx = 0; u.vy = 0; u.vz = d; v.vx = 0; v.vy = h; v.vz = 0;
    draw_face(fb, cam, &o, &u, &v, cells(d, sub), cells(h, sub), 2600, col, pr, bias);
}

void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    int i, j;
    /* floor: 10x10 grid of big tiles, glowing lava seams on a few */
    for (j = 0; j < 10; j++)
        for (i = 0; i < 10; i++) {
            VECTOR o, u, v;
            int lava = ((i * 7 + j * 3) % 9) == 0;
            int glow = 150 + (isin(t * 40 + i * 900 + j * 500) * 70 >> 12);
            o.vx = -ARENA + i * 800; o.vy = 0; o.vz = -ARENA + (j + 1) * 800;
            u.vx = 800; u.vy = 0; u.vz = 0; v.vx = 0; v.vy = 0; v.vz = -800;
            draw_face(fb, view, &o, &u, &v, 2, 2, 4096,
                      lava ? (u32)((glow << 16) | ((glow / 4) << 8) | 12) : ((i + j) & 1 ? 0x3c1a1a : 0x321416),
                      pr, FLOOR_BIAS);
        }
    for (i = 0; i < NBOX; i++) {
        const Box *b = &boxes[i];
        draw_box(fb, view, b->cx - b->hx, -b->h, b->cz - b->hz, b->cx + b->hx, 0, b->cz + b->hz, b->col, pr, -8, 1);
    }
    /* ramp: sloped top + side walls */
    {
        VECTOR o, u, v;
        o.vx = RAMP_X0; o.vy = 0; o.vz = RAMP_Z0;
        u.vx = RAMP_X1 - RAMP_X0; u.vy = 0; u.vz = 0;
        v.vx = 0; v.vy = -RAMP_H; v.vz = RAMP_Z1 - RAMP_Z0;
        draw_face(fb, view, &o, &u, &v, 2, 3, 3900, 0x5a2a1e, pr, -8);
        o.vx = RAMP_X1; o.vy = 0; o.vz = RAMP_Z0; u.vx = 0; u.vy = -RAMP_H; u.vz = RAMP_Z1 - RAMP_Z0; v.vx = 0; v.vy = RAMP_H; v.vz = 0;
        draw_face(fb, view, &o, &u, &v, 3, 1, 2600, 0x4e2420, pr, -8);
        {
            /* left side wall: triangle approximated by a quad strip */
            VECTOR o2, u2, v2;
            o2.vx = RAMP_X0; o2.vy = -RAMP_H; o2.vz = RAMP_Z1; u2.vx = 0; u2.vy = RAMP_H; u2.vz = RAMP_Z0 - RAMP_Z1; v2.vx = 0; v2.vy = RAMP_H; v2.vz = 0;
            draw_face(fb, view, &o2, &u2, &v2, 3, 1, 3300, 0x4e2420, pr, -8);
        }
    }
    /* skull totems: bone pillar with a skull block and glowing eyes */
    for (i = 0; i < NTOTEMS; i++) {
        const Totem *tt = &totems[i];
        u32 base = tt->flash ? 0xfff0c0 : 0x8a7a64, head = tt->flash ? 0xffffff : 0xb8a888;
        s32 sink;
        if (tt->dead_t) {                                /* crumbles into the floor */
            sink = tt->dead_t * 24;
            if (sink > TOTEM_H) continue;
        } else sink = 0;
        draw_box(fb, view, tt->x - 110, tt->y - 520 + sink, tt->z - 110, tt->x + 110, tt->y, tt->z + 110, base, pr, -4, 0);
        draw_box(fb, view, tt->x - 150, tt->y - TOTEM_H + sink, tt->z - 150, tt->x + 150, tt->y - 520 + sink, tt->z + 150, head, pr, -4, 0);
        if (!tt->dead_t) {
            /* eyes on all four faces, additive glow */
            for (j = 0; j < 4; j++) {
                static const s8 dx[4] = { 0, 0, 1, -1 }, dz[4] = { 1, -1, 0, 0 };
                int e;
                for (e = -1; e <= 1; e += 2) {
                    SVECTOR w; VECTOR c; s16 sx, sy;
                    POLY *q;
                    int s, k;
                    w.vx = (s16)(tt->x + dx[j] * 152 + (dz[j] ? e * 60 : 0));
                    w.vz = (s16)(tt->z + dz[j] * 152 + (dx[j] ? e * 60 : 0));
                    w.vy = (s16)(tt->y - 660);
                    mat_apply(view, &w, &c);
                    if (!rot_trans_pers(pr, &c, &sx, &sy)) continue;
                    s = 9000 / (c.vz > 300 ? c.vz : 300) + 1;
                    q = prim_alloc(fb);
                    if (!q) return;
                    memset(q, 0, sizeof(*q));
                    q->code = PF_QUAD | PF_SEMI;
                    q->tpage = get_tpage(0, SEMI_ADD, 0, 0);
                    q->v[0].x = (s16)(sx - s); q->v[0].y = (s16)(sy - s);
                    q->v[1].x = (s16)(sx + s); q->v[1].y = (s16)(sy - s);
                    q->v[2].x = (s16)(sx - s); q->v[2].y = (s16)(sy + s);
                    q->v[3].x = (s16)(sx + s); q->v[3].y = (s16)(sy + s);
                    k = 200 + (isin(t * 90 + i * 700) * 50 >> 12);
                    q->v[0].r = (u8)k; q->v[0].g = (u8)(k / 3); q->v[0].b = 0;
                    ot_add(fb, (c.vz >> 4) - 6, q);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ projectiles */
#define NSHOT 16
typedef struct { s32 x, y, z, vx, vy, vz; int life; } Shot;
static Shot shots[NSHOT];

void shots_fire(const VECTOR *from, s32 dx, s32 dy, s32 dz)
{
    int i;
    for (i = 0; i < NSHOT; i++) if (shots[i].life <= 0) break;
    if (i == NSHOT) return;
    shots[i].x = from->vx; shots[i].y = from->vy; shots[i].z = from->vz;
    shots[i].vx = dx * 150 >> 12; shots[i].vy = dy * 150 >> 12; shots[i].vz = dz * 150 >> 12;
    shots[i].life = 60;
}

void shots_update(void)
{
    int i;
    for (i = 0; i < NSHOT; i++) {
        Shot *s = &shots[i];
        VECTOR a, b, hit;
        int r;
        if (s->life <= 0) continue;
        a.vx = s->x; a.vy = s->y; a.vz = s->z;
        b.vx = s->x + s->vx; b.vy = s->y + s->vy; b.vz = s->z + s->vz;
        r = level_ray(&a, &b, &hit);
        if (r) {
            fx_hit(&hit, r >= 2 ? 90 : 40, 0);
            if (r >= 2) {
                Totem *t = &totems[r - 2];
                t->flash = 6;
                if (--t->hp <= 0) { t->dead_t = 1; fx_hit(&hit, 200, 0); }
            }
            s->life = 0;
            continue;
        }
        s->x = b.vx; s->y = b.vy; s->z = b.vz;
        s->life--;
    }
}

void shots_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t)
{
    int i, k;
    for (i = 0; i < NSHOT; i++) {
        Shot *s = &shots[i];
        if (s->life <= 0) continue;
        for (k = 0; k < 3; k++) {                     /* head + two fading trail blobs */
            SVECTOR w; VECTOR c; s16 sx, sy;
            POLY *q;
            int sz, b;
            w.vx = (s16)(s->x - s->vx * k / 2); w.vy = (s16)(s->y - s->vy * k / 2); w.vz = (s16)(s->z - s->vz * k / 2);
            mat_apply(view, &w, &c);
            if (!rot_trans_pers(pr, &c, &sx, &sy)) continue;
            sz = (14000 - k * 3500) / (c.vz > 300 ? c.vz : 300) + 1;
            b = 255 - k * 70 - (isin(t * 300 + i * 800) * 30 >> 12);
            q = prim_alloc(fb);
            if (!q) return;
            memset(q, 0, sizeof(*q));
            q->code = PF_QUAD | PF_SEMI;
            q->tpage = get_tpage(0, SEMI_ADD, 0, 0);
            q->v[0].x = (s16)(sx - sz); q->v[0].y = (s16)(sy - sz);
            q->v[1].x = (s16)(sx + sz); q->v[1].y = (s16)(sy - sz);
            q->v[2].x = (s16)(sx - sz); q->v[2].y = (s16)(sy + sz);
            q->v[3].x = (s16)(sx + sz); q->v[3].y = (s16)(sy + sz);
            q->v[0].r = (u8)b; q->v[0].g = (u8)(b * 2 / 5); q->v[0].b = (u8)(b / 8);
            ot_add(fb, (c.vz >> 4) - 3, q);
        }
    }
}
