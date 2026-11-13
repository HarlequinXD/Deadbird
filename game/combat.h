/*
 * Combat: one hit system for the player now and enemies later.
 *
 *   Targets  - anything that can be hurt registers a CombatTarget: a standing cylinder (feet
 *              position, radius, height) with hit points, a team and an on_hit callback that
 *              applies the damage, knockback, stagger and its own reaction.
 *   Attacks  - a weapon swing is an Attack: a HITBOX, an oriented box fitted around the blade
 *              (clamp -> nose, the blade's depth across it, its thickness through it) that the
 *              attacker moves every frame while the swing is "live". attack_update sweeps the box
 *              from last frame's pose to this one in steps no longer than it is thick (fast swings
 *              can't skip through things). What it reaches first reports:
 *                COMBAT_HIT_TARGET  a target of another team (each target once per swing)
 *                COMBAT_HIT_WALL / _PROP / _FLOOR  level geometry (once per swing)
 *              with the contact point and surface normal, so the attacker can play sparks, blood,
 *              hit-stop, recoil and sounds.
 * Everything is fixed point and static (no malloc), PS1-friendly.
 */
#ifndef COMBAT_H
#define COMBAT_H

#include "gte.h"

#define COMBAT_MAX_TARGETS 32
enum { TEAM_PLAYER, TEAM_ENEMY, TEAM_NEUTRAL };
enum { COMBAT_HIT_NONE, COMBAT_HIT_WALL, COMBAT_HIT_PROP, COMBAT_HIT_FLOOR, COMBAT_HIT_TARGET };

typedef struct CombatTarget CombatTarget;

typedef struct {
    int damage;                /* hit points */
    int knockback;             /* push speed, 1/16 units per frame */
    int stagger;               /* frames the victim should be stunned */
    int move;                  /* attack id (e.g. combo swing 1..3) */
    int team;                  /* attacker's team */
    VECTOR at;                 /* contact point (world) */
    s32 dir_x, dir_z;          /* push direction, 4096 = 1 (attacker -> victim / along the swing) */
} Hit;

struct CombatTarget {
    s32 x, y, z;               /* feet (world, Y down: y is the floor it stands on) */
    s32 radius, height;
    int hp, max_hp, team;
    int alive;
    int flash;                 /* frames of hit flash left (set by combat on every hit) */
    void (*on_hit)(CombatTarget *t, const Hit *h);   /* react; default (0) just takes the damage */
    void *user;
};

typedef struct {
    int kind;                  /* COMBAT_HIT_* */
    VECTOR at, nrm;            /* contact point and surface normal (targets: from their axis) */
    CombatTarget *target;
} CombatEvent;

typedef struct {
    int live, team, move, damage, knockback, stagger;
    s32 radius;
    VECTOR a, b;               /* last frame's capsule (for the sweep) */
    int have_prev, world_done;
    s32 ox, oz;                /* attacker's position: knockback pushes away from here (set each frame) */
    u32 hit_mask;              /* targets already hit by this swing */
    VECTOR up;                 /* last frame's "up" across the blade (have_up: given) */
    int have_up;
    int rays;                  /* this frame's box edges + nose path (debug drawing, level tests) */
    VECTOR ray[7][2];
} Attack;

void combat_reset(void);
int  combat_add(CombatTarget *t);              /* returns the slot, -1 if full */
void combat_remove(CombatTarget *t);
void combat_update(void);                       /* per tick: hit flashes etc. */
int  combat_count(void);
CombatTarget *combat_target(int i);

void attack_begin(Attack *at, int team, int move, int damage, int knockback, int stagger, s32 radius);
/* move the live capsule to a->b this frame (set at->ox/oz to the attacker's position first);
   returns the number of events written (<= max) */
/* the swing reached target t without a ray finding it (she was lined up on him): hit him now, once
   per swing like any other; returns 1 if it landed (fills ev) */
int  attack_force(Attack *at, CombatTarget *t, const VECTOR *where, CombatEvent *ev);
int  attack_update(Attack *at, const VECTOR *a, const VECTOR *b, CombatEvent *ev, int max);
/* the same with the blade's "up" (across its depth, 4096 = 1) so the hitbox turns with the weapon */
int  attack_update_box(Attack *at, const VECTOR *a, const VECTOR *b, const VECTOR *up, CombatEvent *ev, int max);
void attack_end(Attack *at);

#endif
