/*
 * Player controller + animation. See player.h. Physics constants are Slayer from Hell's
 * (metres/second) converted at ~1100 units per metre, 60 frames per second, scaled 0.85 to fit
 * the test level; velocities are kept in 1/16 units per frame.
 */
#include "player.h"
#include "fkclips.h"
#include "level.h"

#define SUB        16
#define MAX_SPEED  (70 * SUB)     /* full run (Slayer 4.3 m/s) */
#define WALK_SPEED (26 * SUB)     /* half-pushed stick (Slayer 1.45 m/s) */
#define STOP_SPEED (9 * SUB)      /* friction never works on less than this (0.5 m/s) */
#define FRICTION   7              /* per second */
#define ACCEL      10             /* per second */
#define AIR_ACCEL  8
#define AIR_CAP    (17 * SUB)     /* air control can only add this much (0.9 m/s) */
#define GRAVITY    90             /* 18 m/s^2 */
#define JUMP_V     (-1626)        /* 5.4 m/s */
#define RADIUS     200
#define RUN_PUSH   2870           /* stick past 70% runs */

static s32 d_walk, d_run;         /* ground covered per cycle by the walk / run clips */

static s32 iabs(s32 v) { return v < 0 ? -v : v; }
static s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int smooth(s32 t) { t = clamp(t, 0, ONE); return (int)((3 * (t * t >> 12)) - (2 * ((t * t >> 12) * t >> 12))); }
static int wrap(int a) { return ((a + 2048) & 4095) - 2048; }

/* angle of (dx right/+X, dz forward/+Z), 4096 units, 0 = +Z, 1024 = +X */
static int iatan2(s32 dx, s32 dz)
{
    s32 ax = iabs(dx), az = iabs(dz), z, a;
    if (ax == 0 && az == 0) return 0;
    if (ax <= az) { z = ax * ONE / az; a = (512 * z >> 12) + (178 * ((z * (ONE - z)) >> 12) >> 12); }
    else          { z = az * ONE / ax; a = 1024 - ((512 * z >> 12) + (178 * ((z * (ONE - z)) >> 12) >> 12)); }
    if (dz < 0) a = 2048 - a;
    if (dx < 0) a = -a;
    return a & 4095;
}
int player_input_angle(s32 dx, s32 dz) { return iatan2(dx, dz); }

void player_init(const Model *m)
{
    fk_init(m);
    d_walk = fk_cycle_distance(&clip_walk);
    d_run = fk_cycle_distance(&clip_run);
}

void player_reset(Player *p, s32 x, s32 z, int yaw)
{
    s32 y = level_ground(x, z, 0);
    int i;
    char *c = (char *)p;
    for (i = 0; i < (int)sizeof(*p); i++) c[i] = 0;
    p->x = x; p->y = y; p->z = z;
    p->x16 = x * SUB; p->y16 = y * SUB; p->z16 = z * SUB;
    p->yaw = yaw & 4095;
    p->grounded = 1;
    p->land_t = 99;
}

/* ---------------------------------------------------------------- physics (Slayer Player.java) */
static void friction(Player *p)
{
    s32 sp = (s32)isqrt((u32)(p->vx * p->vx + p->vz * p->vz)), control, ns;
    if (sp == 0) return;
    control = sp < STOP_SPEED ? STOP_SPEED : sp;
    ns = sp - control * FRICTION / 60;
    if (ns < 0) ns = 0;
    p->vx = p->vx * ns / sp; p->vz = p->vz * ns / sp;
}

static void accelerate(Player *p, s32 wx, s32 wz, s32 wish, s32 cap, int accel)
{
    s32 cur = (p->vx * wx + p->vz * wz) >> 12;
    s32 add = (wish < cap ? wish : cap) - cur, as;
    if (add <= 0) return;
    as = wish * accel / 60;
    if (as < 1) as = 1;
    if (as > add) as = add;
    p->vx += as * wx >> 12; p->vz += as * wz >> 12;
}

void player_update(Player *p, u32 pad, int input_yaw, int input_mag)
{
    u32 press = pad & ~p->prev;
    s32 wx = 0, wz = 0, wish = 0, nx, nz, ox, oz, cnx, cnz, gy;
    int i;
    p->prev = pad; p->pad = pad;
    ox = oz = 0;


    if (input_mag > 300) {
        wx = isin(input_yaw); wz = icos(input_yaw);
        if ((pad & BTN_RUN) || input_mag >= RUN_PUSH) wish = MAX_SPEED;
        else wish = WALK_SPEED * clamp(input_mag * 4096 / 2000, 1200, 4096) >> 12;
    }

    if (p->grounded) {
        friction(p);
        if (wish) accelerate(p, wx, wz, wish, wish, ACCEL);
        if (press & BTN_JUMP) {
            p->vy = JUMP_V; p->grounded = 0; p->air_t = 0;
        }
    } else if (wish) {
        accelerate(p, wx, wz, wish, AIR_CAP, AIR_ACCEL);
    }

    /* move, then slide along walls (a few passes so corners don't jitter) */
    nx = p->x16 + p->vx; nz = p->z16 + p->vz;
    p->pushing = 0;
    for (i = 0; i < 3; i++) {
        s32 cx, cz;
        ox = nx >> 4; oz = nz >> 4; cx = ox; cz = oz;
        if (!level_collide(&cx, &cz, p->y, RADIUS, &cnx, &cnz)) break;
        nx += (cx - ox) * SUB; nz += (cz - oz) * SUB;
        {
            s32 into = -((p->vx * cnx + p->vz * cnz) >> 12);
            if (into > 0) { p->vx += cnx * into >> 12; p->vz += cnz * into >> 12; }   /* slide along it */
        }
        p->pushing = 1;                                   /* touching a wall this frame */
    }
    p->x16 = nx; p->z16 = nz;
    p->x = nx >> 4; p->z = nz >> 4;

    /* vertical */
    if (p->grounded) {
        gy = level_ground(p->x, p->z, p->y);
        /* stairs and small drops: stay glued to the ground; only a real ledge makes you fall */
        if (gy > p->y + STEP_HEIGHT) { p->grounded = 0; p->vy = 0; p->air_t = 0; }
        else if (gy != p->y) { p->step_off += p->y - gy; p->y = gy; p->y16 = gy * SUB; }
    }
    if (!p->grounded) {
        p->vy += GRAVITY;
        p->y16 += p->vy; p->y = p->y16 >> 4;
        p->air_t++;
        gy = level_ground(p->x, p->z, p->y - (p->vy >> 4) - 1);
        if (p->y >= gy && p->vy >= 0) {
            p->land_amp = (int)clamp(p->vy * 4096 / 1700, 900, 4096);
            if (p->air_t < 6) p->land_amp /= 3;
            p->land_t = 0;
            p->y = gy; p->y16 = gy * SUB; p->vy = 0; p->grounded = 1;
        }
    }
    p->step_off = p->step_off * 3 / 4;
    if (p->step_off > -4 && p->step_off < 4) p->step_off = 0;

    /* facing: turn toward the direction of travel (Slayer: 9/s) */
    p->prev_speed16 = p->speed16;
    p->speed16 = (s32)isqrt((u32)(p->vx * p->vx + p->vz * p->vz));
    p->speed = p->speed16 >> 4;
    p->turn = 0;
    {
        /* turn toward where the stick points (MediEvil / Slayer: snappy, ~0.2 of the gap a frame);
           with the stick released she keeps her facing instead of spinning toward a slide */
        if (wish) {
            /* face where she is actually going when that is roughly where you push (so she runs
               along a wall she is sliding on); otherwise snap round toward the stick */
            int want = input_yaw, d;
            if (p->speed16 > 8 * SUB && ((p->vx * wx + p->vz * wz) >> 12) > p->speed16 / 4)
                want = iatan2(p->vx, p->vz);
            d = wrap(want - p->yaw);
            p->turn = d / 5;
            if (p->turn == 0 && d) p->turn = d > 0 ? 1 : -1;
            p->yaw = (p->yaw + p->turn) & 4095;
        }
    }

    /* animation clocks and weights (Slayer Hero.java) */
    {
        s32 sp = p->speed16, d, deff;
        int run_t = smooth((sp - 36 * SUB) * 4096 / (32 * SUB));
        int amp_t = sp < 2 * SUB ? 0 : (int)clamp(sp * 4096 / (22 * SUB), 0, 4096);
        p->runw += (run_t - p->runw) / 6;
        p->amp += (amp_t - p->amp) / 4;
        if (!p->grounded) {
            /* keep the legs going in the air but stop the clock */
        } else if (sp >= 2 * SUB) {
            d = d_walk + ((d_run - d_walk) * p->runw >> 12);
            deff = d * (p->amp < 700 ? 700 : p->amp) >> 12;
            p->phase += (int)(sp * 1024 / deff);
        } else {
            /* settle to the nearest double-support moment so the next start is clean */
            int m = p->phase & 8191;
            if (m) p->phase += m < 4096 ? -((m + 7) / 8) : (8192 - m + 7) / 8;
        }
        p->phase &= 16383;
        p->idle_t++;
        p->airw += ((p->grounded ? 0 : 4096) - p->airw) / (p->grounded ? 2 : 4);
        /* lean into acceleration, bank into turns */
        {
            /* (a wall stopping you is not braking: no lean back off it) */
            s32 acc = p->pushing ? 0 : p->speed16 - p->prev_speed16;
            int lean_t = (int)clamp(-acc * 16 * 16 / 12, -16 * 16, 12 * 16);
            int bank_t = (int)clamp((s32)p->turn * p->speed * 16 / 280, -22 * 16, 22 * 16);
            if (!p->grounded) { lean_t = 0; bank_t /= 2; }
            p->lean += (lean_t - p->lean) / 5;
            p->bank += (bank_t - p->bank) / 5;
        }
        if (p->land_t < 99) p->land_t++;
    }
}

/* ---------------------------------------------------------------- pose */
static void build_pose(Player *p)
{
    FKPose a, b, tmp;
    FKPose *o = &p->pose;
    s32 reach, r2;
    int t, w;

    /* idle -> walk -> run, all on the same clock */
    fk_sample(&clip_idle, (s32)p->idle_t << 8, o);
    fk_sample(&clip_walk, p->phase, &a);
    fk_sample(&clip_run, p->phase, &b);
    fk_blend(&a, &b, p->runw, &tmp);
    fk_blend(o, &tmp, p->amp, o);

    /* airborne: tuck on the way up, reach for the ground on the way down */
    if (p->airw > 0) {
        t = (int)clamp((p->vy + 1400) * 32 / 2400, 0, 32);
        fk_sample(&clip_air, (s32)t << 8, &a);
        fk_blend(o, &a, p->airw, o);
    }
    /* landing dip: quick crouch, slow recovery, scaled by impact */
    if (p->land_t < 20) {
        w = p->land_t < 3 ? p->land_t * 4096 / 3 : (20 - p->land_t) * 4096 / 17;
        w = smooth(w) * p->land_amp >> 12;
        fk_sample(&clip_land, 0, &a);
        fk_blend(o, &a, w, o);
    }

    /* procedural layers on top of the clips */
    o->v[FK_PRX] += (s16)(p->lean / 16);
    o->v[FK_TRX] += (s16)(p->lean / 32);
    o->v[FK_PRZ] += (s16)(p->bank / 16);
    o->v[FK_HRY] -= (s16)(p->turn / 12);         /* look into the turn */

    /* grounding: drop the body until the lowest foot reaches the floor (no joint is changed) */
    reach = fk_leg_reach(o->v[FK_LHP] + o->v[FK_PRX], o->v[FK_LKN]);
    r2 = fk_leg_reach(o->v[FK_RHP] + o->v[FK_PRX], o->v[FK_RKN]);
    if (r2 > reach) reach = r2;
    reach = fk_rest_reach() - reach;
    p->drop += (reach - p->drop) / 2;
    o->v[FK_PY] += (s16)((p->drop * (4096 - p->airw) >> 12) + p->step_off);
}

void player_pose(Player *p, const Model *m)
{
    VECTOR off;
    MATRIX root, ry;
    build_pose(p);
    fk_bones(m, &p->pose, p->bone_pose, &off);
    mat_rot_y(&ry, p->yaw);
    mat_translate(&ry, p->x, p->y, p->z);
    mat_identity(&root);
    mat_translate(&root, off.vx, off.vy, off.vz);
    mat_mul(&ry, &root, &root);
    model_skeleton(m, &root, p->bone_pose, p->world);
}
