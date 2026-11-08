/*
 * Player locomotion. Two layers, both pure fixed point:
 *
 *  1. Gait cycle (moving): a phase clock advanced by distance travelled. Each foot spends
 *     `duty` of the cycle planted (locked in world space - it cannot slide) and the rest
 *     swinging toward where the hip will be when it lands. Walk: long stance, low lift.
 *     Run: short stance with a flight phase, longer stride, high knee lift and heel kick.
 *     Stride length, cadence, bob and arm pump all come from the actual speed.
 *  2. Step planner (standing / turning on the spot): a foot steps only when the body has
 *     turned or drifted away from it.
 *
 * Foot targets sample the level height (foot IK); the pelvis drops so both feet reach.
 */
#include "player.h"
#include "rig.h"
#include "ik.h"
#include "level.h"

#define WALK_SPEED  17
#define RUN_SPEED   44
#define TURN_MOVE   190          /* turn rate while moving (4096 = 360 per frame units) */
#define TURN_PIVOT  320          /* turn rate standing / skidding round */
#define HIP_HALF    112          /* feet stand this far either side of the root */
#define ANKLE_H     164          /* ankle above the ground */
#define JUMP_V      -82
#define GRAVITY     5
#define GAIT_MIN    4            /* below this speed the step planner takes over */

static void rotate(int yaw, s32 lx, s32 lz, s32 *wx, s32 *wz)
{
    s32 s = isin(yaw), c = icos(yaw);
    *wx = (c * lx + s * lz) >> 12;
    *wz = (-s * lx + c * lz) >> 12;
}

static void unrotate(int yaw, s32 wx, s32 wz, s32 *lx, s32 *lz)
{
    s32 s = isin(yaw), c = icos(yaw);
    *lx = (c * wx - s * wz) >> 12;
    *lz = (s * wx + c * wz) >> 12;
}

static int smooth(int t) { s32 tt = (s32)t * t >> 12; return (int)(3 * tt - (2 * (tt * t >> 12))); }  /* 0..4096 */
static s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
static s32 iabs(s32 v) { return v < 0 ? -v : v; }

/* angle of (dx right, dz forward) in 4096 units, 0 = forward, 1024 = right */
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

/* gait parameters for a speed: cycle distance, stance fraction (4096 = whole cycle) */
static s32 cycle_dist(s32 v) { return 540 + v * 19; }
static s32 duty(s32 v)
{
    s32 d = 2600 - (v - 12) * (2600 - 1450) / (RUN_SPEED - 12);
    return clamp(d, 1450, 2600);
}

/* where a foot stands relative to the root (beside the hip, pushed ahead by `lead`) */
static void foot_spot(const Player *p, int i, s32 rx, s32 rz, s32 lead, s32 *x, s32 *y, s32 *z)
{
    s32 dx, dz;
    rotate(p->yaw, i == 0 ? HIP_HALF : -HIP_HALF, lead, &dx, &dz);
    *x = rx + dx; *z = rz + dz;
    *y = level_ground(*x, *z, p->y) - ANKLE_H;
}

static void plant_feet(Player *p)
{
    int i;
    for (i = 0; i < 2; i++) {
        Foot *f = &p->foot[i];
        foot_spot(p, i, p->x, p->z, 0, &f->x, &f->y, &f->z);
        f->swing = 0;
    }
    p->gait = 0;
}

void player_reset(Player *p, s32 x, s32 z, int yaw)
{
    memset(p, 0, sizeof(*p));
    p->x = x; p->z = z; p->yaw = yaw;
    p->y = level_ground(x, z, 0);
    p->grounded = 1;
    plant_feet(p);
}

/* ---------------------------------------------------------------- 1. gait cycle */
static void gait_start(Player *p)
{
    /* begin with the foot that is further behind lifting off */
    s32 lz0, lz1, lx, du = duty(p->speed);
    unrotate(p->yaw, p->foot[0].x - p->x, p->foot[0].z - p->z, &lx, &lz0);
    unrotate(p->yaw, p->foot[1].x - p->x, p->foot[1].z - p->z, &lx, &lz1);
    p->phase = lz0 <= lz1 ? du : (du + 2048) & 4095;
    p->foot[0].swing = p->foot[1].swing = 0;
    p->gait = 1;
}

static void gait_update(Player *p)
{
    s32 v = p->speed, D = cycle_dist(v), du = duty(v);
    s32 frames_cycle = D / (v > 1 ? v : 1);
    int i;

    p->phase = (p->phase + (int)(v * 4096 / D)) & 4095;
    for (i = 0; i < 2; i++) {
        Foot *f = &p->foot[i];
        s32 fi = (p->phase + i * 2048) & 4095;
        if (fi < du) {                                   /* stance: locked to the ground */
            if (f->swing) {                              /* just landed */
                f->swing = 0;
                f->y = level_ground(f->x, f->z, p->y) - ANKLE_H;
            }
            continue;
        }
        if (!f->swing) { f->swing = 1; f->sx = f->x; f->sy = f->y; f->sz = f->z; }   /* lift off */
        {
            s32 s = (fi - du) * ONE / (ONE - du);        /* swing progress 0..4096 */
            s32 left = (ONE - fi) * frames_cycle >> 12;  /* frames until touchdown */
            s32 lead = (du * D >> 12) / 2;               /* land half a stance ahead of the hip */
            s32 px, pz, tx, ty, tz, e = smooth((int)s), arc = isin((int)(s / 2));
            s32 runw = clamp((v - WALK_SPEED) * ONE / (RUN_SPEED - WALK_SPEED), 0, ONE);
            s32 lift = 85 + (runw * 70 >> 12), kx, kz, back, up;
            rotate(p->yaw, 0, v * left, &px, &pz);       /* predicted root at touchdown */
            foot_spot(p, i, p->x + px, p->z + pz, lead, &tx, &ty, &tz);
            if (f->sy - ty > 0) lift += f->sy - ty;      /* stepping up: clear the ledge */
            f->x = f->sx + ((tx - f->sx) * e >> 12);
            f->z = f->sz + ((tz - f->sz) * e >> 12);
            f->y = f->sy + ((ty - f->sy) * e >> 12) - (arc * lift >> 12);
            /* running: heel kicks up behind early in the swing, then the knee drives through */
            if (runw) {
                s32 early = isin((int)clamp(s * 2, 0, ONE) / 2);           /* peak at 1/4 swing */
                s32 mid = isin((int)clamp(s * 3 / 2 - 1024, 0, ONE) / 2);  /* knee drive later */
                back = (300 * early >> 12) - (90 * mid >> 12);
                up = (300 * early >> 12) + (160 * mid >> 12);
                back = back * runw >> 12; up = up * runw >> 12;
                rotate(p->yaw, 0, -back, &kx, &kz);
                f->x += kx; f->z += kz; f->y -= up;
            }
        }
    }
}

/* ---------------------------------------------------------------- 2. step planner */
static void step_update(Player *p)
{
    int i, busy = -1;
    s32 ix[2], iy[2], iz[2], err[2];
    int pivot = iabs(p->turn) > 20;

    for (i = 0; i < 2; i++) foot_spot(p, i, p->x, p->z, 0, &ix[i], &iy[i], &iz[i]);
    for (i = 0; i < 2; i++) {
        Foot *f = &p->foot[i];
        if (!f->swing) continue;
        if (--f->swing == 0) { f->x = ix[i]; f->y = iy[i]; f->z = iz[i]; continue; }
        {
            int prog = (f->swing_len - f->swing) * ONE / f->swing_len, e = smooth(prog);
            s32 lift = 70 + (f->sy - iy[i] > 0 ? f->sy - iy[i] : 0);
            f->x = f->sx + ((ix[i] - f->sx) * e >> 12);
            f->z = f->sz + ((iz[i] - f->sz) * e >> 12);
            f->y = f->sy + ((iy[i] - f->sy) * e >> 12) - (isin(prog / 2) * lift >> 12);
            busy = i;
        }
    }
    for (i = 0; i < 2; i++) {
        s32 dx = p->foot[i].x - ix[i], dz = p->foot[i].z - iz[i];
        err[i] = (s32)isqrt((u32)(dx * dx + dz * dz)) + iabs(p->foot[i].y - iy[i]) / 2;
    }
    if (busy < 0 || (pivot && p->foot[busy].swing * 2 < p->foot[busy].swing_len)) {
        int pick = busy >= 0 ? 1 - busy : (err[0] >= err[1] ? 0 : 1);
        Foot *f = &p->foot[pick];
        if (!f->swing && err[pick] > (pivot ? 28 : 50)) {
            f->sx = f->x; f->sy = f->y; f->sz = f->z;
            f->swing_len = f->swing = pivot ? 7 : 11;
        }
    }
}

static void air_feet(Player *p)
{
    int i;
    s32 tuck = p->vy < 0 ? 240 : 240 - clamp(p->vy * 4, 0, 210);   /* tuck rising, reach down falling */
    for (i = 0; i < 2; i++) {
        s32 dx, dz;
        rotate(p->yaw, i == 0 ? HIP_HALF : -HIP_HALF, i == 0 ? 110 : -60, &dx, &dz);
        p->foot[i].x = p->x + dx; p->foot[i].z = p->z + dz;
        p->foot[i].y = p->y - ANKLE_H - tuck;
        p->foot[i].swing = 0;
    }
    p->gait = 0;
}

/* ---------------------------------------------------------------- body pose */
extern const Clip clip_walk, clip_run, clip_idle;

static void build_pose(Player *p)
{
    s16 *v = p->pose.v;
    AnimPose idle, walk, run, base;
    s32 fx[2], fy[2], fz[2], py, drop, trx, prz;
    s32 spd = p->speed;
    int i;
    int bump = p->bump_t > 16 ? (24 - p->bump_t) * ONE / 8 : p->bump_t * ONE / 16, land = p->land_t * ONE / 16;
    int runw = (int)clamp((spd - WALK_SPEED) * ONE / (RUN_SPEED - WALK_SPEED), 0, ONE);   /* walk -> run */
    int movew = p->gait ? (int)clamp(spd * ONE / 10, 0, ONE) : 0;                       /* idle -> moving */

    /* authored cycles: idle on its own clock, walk/run locked to the gait phase */
    anim_sample(&clip_idle, (s32)p->idle_t << 6, &idle);   /* one breath every ~4 s */
    anim_sample(&clip_walk, (s32)p->phase << 2, &walk);
    anim_sample(&clip_run, (s32)p->phase << 2, &run);
    anim_blend(&walk, &run, runw, &base);
    anim_blend(&idle, &base, movew, &base);
    for (i = 0; i < AP_COUNT; i++) v[i] = base.v[i];

    for (i = 0; i < 2; i++) {
        unrotate(p->yaw, p->foot[i].x - p->x, p->foot[i].z - p->z, &fx[i], &fz[i]);
        fy[i] = p->foot[i].y - p->y;
    }

    /* pelvis: clip bob + drop to reach the lower foot + landing dip */
    py = v[AP_PY];
    drop = (fy[0] > fy[1] ? fy[0] : fy[1]) + ANKLE_H;          /* > 0: a foot is below the root */
    if (drop > 0) py += drop;
    py += land * 190 >> 12;
    py += bump * 40 >> 12;
    if (!p->grounded) py -= 40;
    p->bob = py;

    /* dynamic lean on top of the clip: acceleration, landing, bump; bank into turns */
    trx = v[AP_TRX] - p->accel * 28 - (land * 220 >> 12) + (bump * 340 >> 12);
    if (!p->grounded) trx -= 60;
    trx = clamp(trx, -480, 320);
    prz = v[AP_PRZ] + clamp(p->turn * spd / 22, -220, 220);

    v[AP_PY] = (s16)py; v[AP_PZ] = (s16)(-(bump * 90 >> 12));
    v[AP_PRZ] = (s16)prz;
    v[AP_TRX] = (s16)trx; v[AP_TRZ] = (s16)(prz / 2);
    v[AP_HRX] = (s16)(v[AP_HRX] + (bump * 300 >> 12) - ((trx - base.v[AP_TRX]) * 6 / 10));
    v[AP_HRY] = (s16)(-v[AP_PRY] - v[AP_TRY]);                 /* keep the skull facing forward */

    for (i = 0; i < 2; i++) {
        int sg = i == 0 ? 1 : -1;
        int hx = i == 0 ? AP_LHX : AP_RHX, hy = hx + 1, hz = hx + 2;
        s32 x = v[hx], y = v[hy] + py, z = v[hz];
        if (!p->grounded) { x = sg * 330; y = -1660 + py; z = 120; }
        x += ((sg * 330 - x) * bump) >> 12;                    /* bump: arms fly up and out */
        y += ((-1560 + py - y) * bump) >> 12;
        z += ((180 - z) * bump) >> 12;
        v[hx] = (s16)x; v[hy] = (s16)y; v[hz] = (s16)z;
        v[i == 0 ? AP_LFX : AP_RFX] = (s16)fx[i];
        v[i == 0 ? AP_LFY : AP_RFY] = (s16)fy[i];
        v[i == 0 ? AP_LFZ : AP_RFZ] = (s16)fz[i];
    }
}

/* ---------------------------------------------------------------- update */
void player_update(Player *p, u32 pad, int input_yaw, int input_mag)
{
    u32 press;
    s32 target = 0, d, step, nx, nz, cnx = 0, cnz = 0, gy;

    p->prev = p->pad; p->pad = pad;
    press = p->pad & ~p->prev;
    if (p->bump_t) p->bump_t--;
    if (p->land_t) p->land_t--;
    p->idle_t = (p->idle_t + 1) & 255;

    /* turn toward the stick direction; skid round when reversing */
    p->turn = 0;
    if (input_mag > 0 && p->bump_t < 8) {
        int diff = ((input_yaw - p->yaw + 2048) & 4095) - 2048;
        int rate = p->speed < 8 || diff > 1500 || diff < -1500 ? TURN_PIVOT : TURN_MOVE;
        if (!p->grounded) rate /= 3;
        p->turn = (int)clamp(diff / 2, -rate, rate);
        if (p->turn == 0 && diff) p->turn = diff > 0 ? 1 : -1;
        p->yaw = (p->yaw + p->turn) & 4095;
        /* speed from how far the stick is pushed (4096 = full); run button forces a sprint */
        target = (pad & BTN_RUN) ? RUN_SPEED : (WALK_SPEED * 2 > RUN_SPEED * input_mag >> 12 ? (input_mag < 2600 ? WALK_SPEED * input_mag / 2600 : WALK_SPEED + (RUN_SPEED - WALK_SPEED) * (input_mag - 2600) / 1496) : RUN_SPEED);
        if (target < 6) target = 6;
        if (diff > 1500 || diff < -1500) target /= 3;
    }
    if (p->land_t > 8 && p->grounded) target /= 2;
    d = target - p->speed;
    step = d > 0 ? d / 7 + 1 : d / 4 - 1;                 /* brake harder than you accelerate */
    if (d == 0) step = 0;
    if (!p->grounded) step /= 3;
    p->speed += step;
    if (p->speed < 0) p->speed = 0;
    p->accel = step;

    if ((press & BTN_JUMP) && p->grounded && p->bump_t < 10) {
        p->vy = JUMP_V - p->speed / 3;
        p->grounded = 0; p->air_t = 0;
    }

    /* move and collide */
    rotate(p->yaw, 0, p->speed, &nx, &nz);
    nx += p->x; nz += p->z;
    if (level_collide(&nx, &nz, p->y, 200, &cnx, &cnz)) {
        s32 fwx, fwz, into;
        rotate(p->yaw, 0, ONE, &fwx, &fwz);
        into = (fwx * cnx + fwz * cnz) >> 12;
        if (into < -2400 && p->speed > 12 && p->bump_t == 0) {
            s32 k = 6 + p->speed / 3;                     /* hit reaction: knocked back off the wall */
            p->bump_t = 24;
            p->kx = cnx * k >> 12; p->kz = cnz * k >> 12;
            p->speed = 0;
        } else if (into < -2400) {
            p->speed = p->speed * 3 / 4;                  /* pressing into a wall slowly: just stop */
        }
    }
    p->x = nx; p->z = nz;
    if (p->kx || p->kz) {
        s32 kx = p->x + p->kx, kz = p->z + p->kz;
        level_collide(&kx, &kz, p->y, 200, &cnx, &cnz);
        p->x = kx; p->z = kz;
        p->kx = p->kx * 13 / 16; p->kz = p->kz * 13 / 16;
    }

    /* vertical */
    if (p->grounded) {
        gy = level_ground(p->x, p->z, p->y);
        if (gy > p->y + 60) { p->grounded = 0; p->vy = 0; }          /* walked off a ledge */
        else { p->y += (gy - p->y) / 3; if (iabs(gy - p->y) < 4) p->y = gy; }
    }
    if (!p->grounded) {
        p->vy += GRAVITY;
        p->y += p->vy;
        p->air_t++;
        gy = level_ground(p->x, p->z, p->y - p->vy);
        if (p->y >= gy && p->vy >= 0) {
            p->land_t = p->vy > 40 ? 16 : 10;
            p->y = gy; p->vy = 0; p->grounded = 1;
            plant_feet(p);
        }
    }

    /* feet: gait cycle while moving, step planner while standing / pivoting */
    if (!p->grounded) air_feet(p);
    else if (p->speed > GAIT_MIN && p->bump_t == 0) {
        if (!p->gait) gait_start(p);
        gait_update(p);
    } else {
        if (p->gait) {
            /* hand over: a foot in mid-swing finishes as a short planner step */
            int i;
            for (i = 0; i < 2; i++)
                if (p->foot[i].swing) {
                    Foot *f = &p->foot[i];
                    f->sx = f->x; f->sy = f->y; f->sz = f->z;
                    f->swing_len = f->swing = 6;
                }
            p->gait = 0;
        }
        step_update(p);
    }
    build_pose(p);
}

void player_pose(Player *p, const Model *m)
{
    VECTOR off;
    MATRIX root, ry;
    rig_solve(m, &p->pose, p->bone_pose, &off);
    mat_rot_y(&ry, p->yaw);
    mat_translate(&ry, p->x, p->y, p->z);
    mat_identity(&root);
    mat_translate(&root, off.vx, off.vy, off.vz);
    mat_mul(&ry, &root, &root);
    model_skeleton(m, &root, p->bone_pose, p->world);
}

int player_input_angle(s32 dx, s32 dz) { return iatan2(dx, dz); }
