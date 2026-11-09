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
 * Raycasts. The blade is a row of RAYS points from the clamp to the nose; every frame each point
 * casts a ray from where it was last frame to where it is now (the path the chain actually cut
 * through the air), and one more ray runs along the bar itself. A target is hit where the first
 * ray enters its cylinder; the level where the first ray meets a wall, prop or the floor.
 */
#define RAYS 6
#define EDGE 36                /* half the bar's thickness: the rays are this much fatter */

/* ray p -> p + d (t 0..4096) against a standing cylinder; returns t of entry or -1 */
static s32 ray_cylinder(const VECTOR *p, const VECTOR *q, s32 cx, s32 cz, s32 r, s32 top, s32 bottom, VECTOR *hit, VECTOR *nrm)
{
    /* in 1/16 units so the squares stay well inside 64 bits */
    long long px = (p->vx - cx) >> 4, pz = (p->vz - cz) >> 4, dx = (q->vx - p->vx) >> 4, dz = (q->vz - p->vz) >> 4;
    long long rr = (long long)(r >> 4) * (r >> 4), a = dx * dx + dz * dz, b = px * dx + pz * dz, c = px * px + pz * pz - rr;
    s32 t = -1, y;
    if (c <= 0) t = 0;                                         /* starts inside it */
    else if (a > 0 && b < 0) {
        long long disc = b * b - a * c;
        if (disc >= 0) {
            long long sq = (long long)isqrt((u32)(disc >> 16 < 0xffffffffLL ? (disc >> 16) : 0xffffffffLL)) << 8;
            long long tn = ((-b - sq) << 12) / a;
            if (tn >= 0 && tn <= 4096) t = (s32)tn;
        }
    }
    if (t >= 0) {
        y = p->vy + (s32)((long long)(q->vy - p->vy) * t >> 12);
        if (y < top || y > bottom) t = -1;                     /* passes over its head / under its feet */
    }
    if (t < 0 && q->vy != p->vy && p->vy < top) {              /* coming down onto the top of it (the slam) */
        s32 tt = (s32)(((long long)(top - p->vy) << 12) / (q->vy - p->vy));
        if (tt >= 0 && tt <= 4096) {
            long long hx = px + (dx * tt >> 12), hz = pz + (dz * tt >> 12);
            if (hx * hx + hz * hz <= rr) t = tt;
        }
    }
    if (t < 0) return -1;
    hit->vx = p->vx + (s32)((long long)(q->vx - p->vx) * t >> 12);
    hit->vy = p->vy + (s32)((long long)(q->vy - p->vy) * t >> 12);
    hit->vz = p->vz + (s32)((long long)(q->vz - p->vz) * t >> 12);
    hit->pad = 0;
    {
        s32 ex = hit->vx - cx, ez = hit->vz - cz, l = (s32)isqrt((u32)((ex >> 4) * (ex >> 4) + (ez >> 4) * (ez >> 4))) << 4;
        nrm->vx = l ? ex * 4096 / l : 0; nrm->vy = 0; nrm->vz = l ? ez * 4096 / l : 0; nrm->pad = 0;
    }
    return t;
}

static void lerpv(const VECTOR *a, const VECTOR *b, int i, VECTOR *o)
{
    o->vx = a->vx + (b->vx - a->vx) * i / (RAYS - 1);
    o->vy = a->vy + (b->vy - a->vy) * i / (RAYS - 1);
    o->vz = a->vz + (b->vz - a->vz) * i / (RAYS - 1);
    o->pad = 0;
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
    VECTOR ray[RAYS + 1][2];
    int nray = 0, n = 0, i, k;
    if (!at->live) return 0;
    if (at->have_prev)                                         /* each blade point: last frame -> now */
        for (k = 0; k < RAYS; k++) { lerpv(&at->a, &at->b, k, &ray[nray][0]); lerpv(a, b, k, &ray[nray][1]); nray++; }
    ray[nray][0] = *a; ray[nray][1] = *b; nray++;              /* along the bar */
    at->a = *a; at->b = *b; at->have_prev = 1;
    at->rays = nray;
    memcpy(at->ray, ray, sizeof(ray[0]) * (size_t)nray);

    /* targets of other teams, each once per swing: the earliest ray to enter them */
    for (i = 0; i < COMBAT_MAX_TARGETS && n < max; i++) {
        CombatTarget *t = targets[i];
        s32 best = 0x7fffffff;
        VECTOR hit, nrm, best_at, best_n;
        if (!t || !t->alive || t->team == at->team || (at->hit_mask & (1u << i))) continue;
        for (k = 0; k < nray; k++) {
            s32 tt = ray_cylinder(&ray[k][0], &ray[k][1], t->x, t->z, t->radius + EDGE, t->y - t->height, t->y, &hit, &nrm);
            if (tt >= 0 && tt < best) { best = tt; best_at = hit; best_n = nrm; }
        }
        if (best == 0x7fffffff) continue;
        apply_hit(at, i, t, &best_at);
        ev[n].kind = COMBAT_HIT_TARGET; ev[n].at = best_at; ev[n].target = t; ev[n].nrm = best_n;
        n++;
    }
    /* level: the first wall, prop or floor any ray reaches (once per swing); the rays run along
       the chain's lower edge for this, so a slam meets the floor with the teeth, not the bar's middle */
    if (!at->world_done && n < max) {
        s32 best = 0x7fffffff;
        int best_kind = LEVEL_HIT_NONE;
        VECTOR bp, bn;
        for (k = 0; k < nray; k++) {
            VECTOR p, nr, s0 = ray[k][0], s1 = ray[k][1];
            int kind;
            s32 d;
            s0.vy += at->radius; s1.vy += at->radius;
            kind = level_hit_segment(&s0, &s1, &p, &nr);
            if (kind == LEVEL_HIT_NONE) continue;
            d = iabs32(p.vx - s0.vx) + iabs32(p.vy - s0.vy) + iabs32(p.vz - s0.vz);   /* how far along its ray */
            if (k == nray - 1) d += 1 << 20;                    /* sweep rays first: they're the motion */
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
