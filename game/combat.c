#include "combat.h"
#include "level.h"

static CombatTarget *targets[COMBAT_MAX_TARGETS];

void combat_reset(void) { memset(targets, 0, sizeof(targets)); }

int combat_add(CombatTarget *t)
{
    int i;
    for (i = 0; i < COMBAT_MAX_TARGETS; i++) if (!targets[i]) { targets[i] = t; t->alive = t->hp > 0; return i; }
    return -1;
}

void combat_remove(CombatTarget *t)
{
    int i;
    for (i = 0; i < COMBAT_MAX_TARGETS; i++) if (targets[i] == t) targets[i] = 0;
}

int combat_count(void) { return COMBAT_MAX_TARGETS; }
CombatTarget *combat_target(int i) { return (i >= 0 && i < COMBAT_MAX_TARGETS) ? targets[i] : 0; }

void combat_update(void)
{
    int i;
    for (i = 0; i < COMBAT_MAX_TARGETS; i++) if (targets[i] && targets[i]->flash > 0) targets[i]->flash--;
}

void attack_begin(Attack *at, int team, int move, int damage, int knockback, int stagger, s32 radius)
{
    memset(at, 0, sizeof(*at));
    at->live = 1; at->team = team; at->move = move; at->damage = damage;
    at->knockback = knockback; at->stagger = stagger; at->radius = radius;
}

void attack_end(Attack *at) { at->live = 0; at->have_prev = 0; }

static s32 iabs32(s32 v) { return v < 0 ? -v : v; }

/*
 * The hitbox: an oriented box fitted around the weapon. Its X axis runs along the bar (a -> b),
 * Y across the bar's depth (the weapon's own "up", from its bone), Z through its thickness.
 * Every frame the box sweeps from last frame's pose to this one in steps no longer than its
 * thickness, so a fast swing can't pass through anything. Targets (standing cylinders) are hit
 * where the box touches them; the level is tested along the box's four long edges and the path
 * of its nose.
 */
typedef struct { s32 c[3]; s32 ax[3][3]; s32 h[3]; } OBox;   /* centre, unit axes (4096 = 1), half sizes */

static s32 len3(s32 x, s32 y, s32 z)
{
    return (s32)isqrt((u32)((x >> 2) * (x >> 2) + (y >> 2) * (y >> 2) + (z >> 2) * (z >> 2))) << 2;
}

static void box_build(OBox *o, const VECTOR *a, const VECTOR *b, const VECTOR *up, s32 hy, s32 hz)
{
    s32 ex = b->vx - a->vx, ey = b->vy - a->vy, ez = b->vz - a->vz, L = len3(ex, ey, ez), d, ux, uy, uz, l;
    if (L < 8) { ex = 0; ey = 0; ez = 4096; L = 8; } else { ex = ex * 4096 / L; ey = ey * 4096 / L; ez = ez * 4096 / L; }
    o->c[0] = (a->vx + b->vx) / 2; o->c[1] = (a->vy + b->vy) / 2; o->c[2] = (a->vz + b->vz) / 2;
    o->ax[0][0] = ex; o->ax[0][1] = ey; o->ax[0][2] = ez;
    if (up) { ux = up->vx; uy = up->vy; uz = up->vz; } else { ux = 0; uy = 4096; uz = 0; }
    d = (ux * ex + uy * ey + uz * ez) >> 12;
    ux -= ex * d >> 12; uy -= ey * d >> 12; uz -= ez * d >> 12;          /* up, square to the bar */
    l = len3(ux, uy, uz);
    if (l < 400) {                                                       /* up along the bar: any square axis */
        ux = ez; uy = 0; uz = -ex; l = len3(ux, uy, uz);
        if (l < 400) { ux = 4096; uy = 0; uz = 0; l = 4096; }
    }
    ux = ux * 4096 / l; uy = uy * 4096 / l; uz = uz * 4096 / l;
    o->ax[1][0] = ux; o->ax[1][1] = uy; o->ax[1][2] = uz;
    o->ax[2][0] = (ey * uz - ez * uy) >> 12;                             /* thickness: bar x up */
    o->ax[2][1] = (ez * ux - ex * uz) >> 12;
    o->ax[2][2] = (ex * uy - ey * ux) >> 12;
    o->h[0] = L / 2; o->h[1] = hy; o->h[2] = hz;
}

/* box-local coordinates of a world point (relative positions stay small: callers reject far ones) */
static void to_local(const OBox *o, s32 x, s32 y, s32 z, s32 *q)
{
    int k;
    x -= o->c[0]; y -= o->c[1]; z -= o->c[2];
    for (k = 0; k < 3; k++) q[k] = (x * o->ax[k][0] + y * o->ax[k][1] + z * o->ax[k][2]) >> 12;
}

static void to_world(const OBox *o, const s32 *q, VECTOR *w)
{
    w->vx = o->c[0] + ((q[0] * o->ax[0][0] + q[1] * o->ax[1][0] + q[2] * o->ax[2][0]) >> 12);
    w->vy = o->c[1] + ((q[0] * o->ax[0][1] + q[1] * o->ax[1][1] + q[2] * o->ax[2][1]) >> 12);
    w->vz = o->c[2] + ((q[0] * o->ax[0][2] + q[1] * o->ax[1][2] + q[2] * o->ax[2][2]) >> 12);
    w->pad = 0;
}

static s32 clamps(s32 v, s32 h) { return v < -h ? -h : (v > h ? h : v); }

/* squared distance (in 1/16 units) from point p (box-local) to the box; q = the nearest box point */
static s32 pt_box2(const OBox *o, const s32 *p, s32 *q)
{
    s32 d = 0;
    int k;
    for (k = 0; k < 3; k++) {
        s32 e;
        q[k] = clamps(p[k], o->h[k]);
        e = (p[k] - q[k]) >> 4;
        d += e * e;
    }
    return d;
}

/* does the box touch the standing cylinder (axis cx,cz from y top..bottom, radius r)? The axis
   segment's nearest point to the box is found by narrowing in on it (the distance is convex
   along the segment); fills the contact point on the box */
static int box_cylinder(const OBox *o, s32 cx, s32 cz, s32 top, s32 bottom, s32 r, VECTOR *at)
{
    s32 p0[3], p1[3], lo = 0, hi = 4096, q[3], best[3], p[3];
    int it, k;
    if (iabs32(cx - o->c[0]) > o->h[0] + r + 600 || iabs32(cz - o->c[2]) > o->h[0] + r + 600) return 0;
    to_local(o, cx, top, cz, p0);
    to_local(o, cx, bottom, cz, p1);
    for (it = 0; it < 12; it++) {
        s32 m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3, a[3], b[3];
        for (k = 0; k < 3; k++) { a[k] = p0[k] + ((p1[k] - p0[k]) * m1 >> 12); b[k] = p0[k] + ((p1[k] - p0[k]) * m2 >> 12); }
        if (pt_box2(o, a, q) <= pt_box2(o, b, q)) hi = m2; else lo = m1;
    }
    for (k = 0; k < 3; k++) p[k] = p0[k] + ((p1[k] - p0[k]) * ((lo + hi) / 2) >> 12);
    if (pt_box2(o, p, best) > (r >> 4) * (r >> 4)) return 0;
    to_world(o, best, at);
    return 1;
}

static void apply_hit(Attack *at, int i, CombatTarget *t, const VECTOR *where)
{
    Hit h;
    s32 dx = t->x - at->ox, dz = t->z - at->oz, l = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    at->hit_mask |= 1u << i;
    memset(&h, 0, sizeof(h));
    h.damage = at->damage; h.knockback = at->knockback; h.stagger = at->stagger; h.move = at->move; h.team = at->team;
    h.at = *where;
    h.dir_x = l ? dx * 4096 / l : 0; h.dir_z = l ? dz * 4096 / l : 4096;
    t->flash = 8;
    if (t->on_hit) t->on_hit(t, &h);
    else { t->hp -= h.damage; if (t->hp <= 0) { t->hp = 0; t->alive = 0; } }
}

int attack_force(Attack *at, CombatTarget *t, const VECTOR *where, CombatEvent *ev)
{
    int i;
    if (!at->live || !t || !t->alive) return 0;
    for (i = 0; i < COMBAT_MAX_TARGETS; i++) if (targets[i] == t) break;
    if (i == COMBAT_MAX_TARGETS || (at->hit_mask & (1u << i))) return 0;
    apply_hit(at, i, t, where);
    ev->kind = COMBAT_HIT_TARGET; ev->at = *where; ev->target = t;
    ev->nrm.vx = ev->nrm.vy = ev->nrm.vz = 0;
    return 1;
}

int attack_update(Attack *at, const VECTOR *a, const VECTOR *b, CombatEvent *ev, int max)
{
    return attack_update_box(at, a, b, 0, ev, max);
}

int attack_update_box(Attack *at, const VECTOR *a, const VECTOR *b, const VECTOR *up, CombatEvent *ev, int max)
{
    OBox box[9];
    int nb = 0, n = 0, i, k, s;
    s32 hy = at->radius, hz = at->radius / 3 + 4;          /* depth half (radius), thickness half */
    if (!at->live) return 0;
    {   /* the sweep: last frame's pose -> this frame's, in steps no longer than the box is thick */
        int steps = 1;
        if (at->have_prev) {
            s32 mv = len3(b->vx - at->b.vx, b->vy - at->b.vy, b->vz - at->b.vz);
            s32 mv2 = len3(a->vx - at->a.vx, a->vy - at->a.vy, a->vz - at->a.vz);
            if (mv2 > mv) mv = mv2;
            steps = 1 + mv / (2 * hz);
            if (steps > 8) steps = 8;
        }
        for (s = 1; s <= steps; s++) {
            VECTOR pa = *a, pb = *b, pu;
            const VECTOR *u = up;
            if (at->have_prev && s < steps) {
                s32 t = s * 4096 / steps;
                pa.vx = at->a.vx + ((a->vx - at->a.vx) * t >> 12); pa.vy = at->a.vy + ((a->vy - at->a.vy) * t >> 12); pa.vz = at->a.vz + ((a->vz - at->a.vz) * t >> 12);
                pb.vx = at->b.vx + ((b->vx - at->b.vx) * t >> 12); pb.vy = at->b.vy + ((b->vy - at->b.vy) * t >> 12); pb.vz = at->b.vz + ((b->vz - at->b.vz) * t >> 12);
                if (up && at->have_up) {
                    pu.vx = at->up.vx + ((up->vx - at->up.vx) * t >> 12); pu.vy = at->up.vy + ((up->vy - at->up.vy) * t >> 12);
                    pu.vz = at->up.vz + ((up->vz - at->up.vz) * t >> 12);
                    u = &pu;
                }
            }
            box_build(&box[nb++], &pa, &pb, u, hy, hz);
        }
    }
    {   /* (for debug drawing) the box's four long edges this frame, and its nose's path */
        const OBox *o = &box[nb - 1];
        static const s8 E[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        at->rays = 0;
        for (k = 0; k < 4; k++) {
            s32 q0[3], q1[3];
            q0[0] = -o->h[0]; q1[0] = o->h[0];
            q0[1] = q1[1] = E[k][0] * o->h[1]; q0[2] = q1[2] = E[k][1] * o->h[2];
            to_world(o, q0, &at->ray[at->rays][0]); to_world(o, q1, &at->ray[at->rays][1]); at->rays++;
        }
        if (at->have_prev) {   /* the nose's path: its centre and both depth corners, last frame -> now */
            OBox pv;
            static const s8 Y[3] = { 0, 1, -1 };
            box_build(&pv, &at->a, &at->b, at->have_up ? &at->up : 0, o->h[1], o->h[2]);
            for (k = 0; k < 3; k++) {
                s32 q[3];
                q[0] = pv.h[0]; q[1] = Y[k] * pv.h[1]; q[2] = 0;
                to_world(&pv, q, &at->ray[at->rays][0]);
                q[0] = o->h[0]; q[1] = Y[k] * o->h[1];
                to_world(o, q, &at->ray[at->rays][1]);
                at->rays++;
            }
        }
    }
    at->a = *a; at->b = *b; at->have_prev = 1;
    if (up) { at->up = *up; at->have_up = 1; }

    /* targets of other teams, each once per swing: the first step of the sweep that touches them */
    for (i = 0; i < COMBAT_MAX_TARGETS && n < max; i++) {
        CombatTarget *t = targets[i];
        VECTOR hit;
        if (!t || !t->alive || t->team == at->team || (at->hit_mask & (1u << i))) continue;
        for (s = 0; s < nb; s++)
            if (box_cylinder(&box[s], t->x, t->z, t->y - t->height, t->y, t->radius, &hit)) break;
        if (s == nb) continue;
        apply_hit(at, i, t, &hit);
        ev[n].kind = COMBAT_HIT_TARGET; ev[n].at = hit; ev[n].target = t;
        {
            s32 ex = hit.vx - t->x, ez = hit.vz - t->z, l = len3(ex, 0, ez);
            ev[n].nrm.vx = l ? ex * 4096 / l : 0; ev[n].nrm.vy = 0; ev[n].nrm.vz = l ? ez * 4096 / l : 0; ev[n].nrm.pad = 0;
        }
        n++;
    }
    /* level: the first wall, prop or floor along the box's long edges or its nose's path (once per
       swing). The nose's path goes first: it's the motion */
    if (!at->world_done && n < max) {
        s32 best = 0x7fffffff;
        int best_kind = LEVEL_HIT_NONE;
        VECTOR bp, bn;
        for (k = at->rays - 1; k >= 0; k--) {
            VECTOR p, nr;
            int kind;
            s32 d;
            kind = level_hit_segment(&at->ray[k][0], &at->ray[k][1], &p, &nr);
            if (kind == LEVEL_HIT_NONE) continue;
            d = iabs32(p.vx - at->ray[k][0].vx) + iabs32(p.vy - at->ray[k][0].vy) + iabs32(p.vz - at->ray[k][0].vz);
            if (k < 4) d += 1 << 20;                            /* the swept path first */
            if (d < best) { best = d; best_kind = kind; bp = p; bn = nr; }
        }
        if (best_kind != LEVEL_HIT_NONE) {
            ev[n].kind = best_kind == LEVEL_HIT_WALL ? COMBAT_HIT_WALL : best_kind == LEVEL_HIT_PROP ? COMBAT_HIT_PROP : COMBAT_HIT_FLOOR;
            ev[n].at = bp; ev[n].nrm = bn; ev[n].target = 0;
            n++;
            at->world_done = 1;
        }
    }
    return n;
}
