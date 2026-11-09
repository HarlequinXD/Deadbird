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

static s32 clampi(s32 v, s32 lo, s32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* closest approach (squared, in 1/16 units to stay in 32 bits) of segment p->q to a vertical axis
   (cx, y0..y1, cz); writes the closest point on the segment */
static s32 seg_axis_dist2(const VECTOR *p, const VECTOR *q, s32 cx, s32 cz, s32 y0, s32 y1, VECTOR *on)
{
    s32 dx = q->vx - p->vx, dz = q->vz - p->vz, l2, t, ex, ez, y;
    s32 sdx = dx >> 4, sdz = dz >> 4;
    l2 = sdx * sdx + sdz * sdz;
    t = l2 > 0 ? (s32)((((long long)((cx - p->vx) >> 4) * sdx + (long long)((cz - p->vz) >> 4) * sdz) << 12) / l2) : 0;
    t = clampi(t, 0, 4096);
    on->vx = p->vx + (s32)((long long)dx * t >> 12);
    on->vy = p->vy + (s32)((long long)(q->vy - p->vy) * t >> 12);
    on->vz = p->vz + (s32)((long long)dz * t >> 12);
    on->pad = 0;
    y = on->vy;
    if (y < y0 || y > y1) return 0x7fffffff;                 /* above its head / below its feet */
    ex = (on->vx - cx) >> 4; ez = (on->vz - cz) >> 4;
    return ex * ex + ez * ez;
}

int attack_update(Attack *at, const VECTOR *a, const VECTOR *b, CombatEvent *ev, int max)
{
    VECTOR seg[4][2];
    int nseg = 0, n = 0, i, k;
    if (!at->live) return 0;
    /* sweep: last frame's blade, the blade halfway, this frame's blade, and the tip's path */
    if (at->have_prev) {
        seg[nseg][0] = at->a; seg[nseg][1] = at->b; nseg++;
        seg[nseg][0].vx = (at->a.vx + a->vx) / 2; seg[nseg][0].vy = (at->a.vy + a->vy) / 2; seg[nseg][0].vz = (at->a.vz + a->vz) / 2;
        seg[nseg][1].vx = (at->b.vx + b->vx) / 2; seg[nseg][1].vy = (at->b.vy + b->vy) / 2; seg[nseg][1].vz = (at->b.vz + b->vz) / 2;
        nseg++;
        seg[nseg][0] = at->b; seg[nseg][1] = *b; nseg++;
    }
    seg[nseg][0] = *a; seg[nseg][1] = *b; nseg++;
    at->a = *a; at->b = *b; at->have_prev = 1;

    /* targets of other teams, each once per swing */
    for (i = 0; i < COMBAT_MAX_TARGETS && n < max; i++) {
        CombatTarget *t = targets[i];
        s32 best = 0x7fffffff, r;
        VECTOR on, best_on;
        memset(&on, 0, sizeof(on)); memset(&best_on, 0, sizeof(best_on));
        if (!t || !t->alive || t->team == at->team || (at->hit_mask & (1u << i))) continue;
        r = (t->radius + at->radius) >> 4;
        for (k = 0; k < nseg; k++) {
            s32 d2 = seg_axis_dist2(&seg[k][0], &seg[k][1], t->x, t->z, t->y - t->height, t->y, &on);
            if (d2 < best) { best = d2; best_on = on; }
        }
        if (best > r * r) continue;
        {
            Hit h;
            s32 dx = t->x - at->ox, dz = t->z - at->oz, l = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
            at->hit_mask |= 1u << i;
            memset(&h, 0, sizeof(h));
            h.damage = at->damage; h.knockback = at->knockback; h.stagger = at->stagger; h.move = at->move; h.team = at->team;
            h.at = best_on;
            h.dir_x = l ? dx * 4096 / l : 0; h.dir_z = l ? dz * 4096 / l : 4096;
            t->flash = 8;
            if (t->on_hit) t->on_hit(t, &h);
            else { t->hp -= h.damage; if (t->hp <= 0) { t->hp = 0; t->alive = 0; } }
            ev[n].kind = COMBAT_HIT_TARGET; ev[n].at = best_on; ev[n].target = t;
            ev[n].nrm.vx = -h.dir_x; ev[n].nrm.vy = 0; ev[n].nrm.vz = -h.dir_z;
            n++;
        }
    }
    /* level: walls, props, the floor (the first contact of the swing) */
    if (!at->world_done && n < max) {
        for (k = nseg - 1; k >= 0; k--) {
            VECTOR p, nr, s0 = seg[k][0], s1 = seg[k][1];
            int kind;
            s0.vy += at->radius; s1.vy += at->radius;          /* the blade's lower edge, not its centre line */
            kind = level_hit_segment(&s0, &s1, &p, &nr);
            if (kind == LEVEL_HIT_NONE) continue;
            ev[n].kind = kind == LEVEL_HIT_WALL ? COMBAT_HIT_WALL : kind == LEVEL_HIT_PROP ? COMBAT_HIT_PROP : COMBAT_HIT_FLOOR;
            ev[n].at = p; ev[n].nrm = nr; ev[n].target = 0;
            n++;
            at->world_done = 1;
            break;
        }
    }
    return n;
}
