#include "player.h"
#include "rig.h"
#include "ik.h"
#include "level.h"

#define WALK_SPEED  18
#define RUN_SPEED   42
#define TURN_MAX    260          /* fastest turn toward the pressed direction (per frame) */
#define HIP_HALF    118          /* feet stand this far either side of the root */
#define ANKLE_H     164          /* ankle above the ground */
#define JUMP_V      -80
#define GRAVITY     5

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

/* where a foot wants to be: beside the root, pushed ahead by the stride */
static void foot_ideal(const Player *p, int i, s32 lead, s32 *x, s32 *y, s32 *z)
{
    s32 dx, dz;
    rotate(p->yaw, i == 0 ? HIP_HALF : -HIP_HALF, lead, &dx, &dz);
    *x = p->x + dx; *z = p->z + dz;
    *y = level_ground(*x, *z, p->y) - ANKLE_H;
}

static void plant_feet(Player *p)
{
    int i;
    for (i = 0; i < 2; i++) {
        Foot *f = &p->foot[i];
        foot_ideal(p, i, 0, &f->x, &f->y, &f->z);
        f->swing = 0;
    }
}

void player_reset(Player *p, s32 x, s32 z, int yaw)
{
    memset(p, 0, sizeof(*p));
    p->x = x; p->z = z; p->yaw = yaw;
    p->y = level_ground(x, z, 0);
    p->grounded = 1;
    plant_feet(p);
}

static int swing_len(s32 speed)
{
    s32 a = iabs(speed);
    return (int)clamp(20 - a / 3, 8, 18);
}

/* ---------------------------------------------------------------- locomotion (foot locking) */
static void update_feet(Player *p)
{
    int len = swing_len(p->speed), i, busy = -1;
    s32 lead = p->speed * len * 9 / 16;              /* plant ahead by about half the travel of one step */
    s32 ix[2], iy[2], iz[2], err[2];

    for (i = 0; i < 2; i++) foot_ideal(p, i, lead, &ix[i], &iy[i], &iz[i]);

    /* advance swinging feet; the landing spot tracks the body so turns and speed changes stay in sync */
    for (i = 0; i < 2; i++) {
        Foot *f = &p->foot[i];
        if (!f->swing) continue;
        f->swing--;
        {
            int prog = (f->swing_len - f->swing) * ONE / f->swing_len, e = smooth(prog);
            s32 lift = 60 + iabs(p->speed) * 2;
            s32 up = f->sy - iy[i];                   /* stepping up a ledge: lift higher */
            if (up > 0) lift += up;
            f->x = f->sx + ((ix[i] - f->sx) * e >> 12);
            f->z = f->sz + ((iz[i] - f->sz) * e >> 12);
            f->y = f->sy + ((iy[i] - f->sy) * e >> 12) - (isin(prog / 2) * lift >> 12);
            if (f->swing == 0) { f->x = ix[i]; f->y = iy[i]; f->z = iz[i]; }
            else busy = i;
        }
    }

    /* pick the foot that has fallen furthest behind and step it */
    for (i = 0; i < 2; i++) {
        s32 dx = p->foot[i].x - ix[i], dz = p->foot[i].z - iz[i], dy = p->foot[i].y - iy[i];
        err[i] = (s32)isqrt((u32)(dx * dx + dz * dz)) + iabs(dy) / 2;
    }
    {
        int can = busy < 0;
        /* running: the next step may start before the other foot lands (flight phase) */
        if (busy >= 0 && iabs(p->speed) > 30) {
            Foot *b = &p->foot[busy];
            can = (b->swing_len - b->swing) * 10 > b->swing_len * 6;
        }
        if (can) {
            int pick = busy >= 0 ? 1 - busy : (err[0] >= err[1] ? 0 : 1);
            s32 thr = iabs(p->speed) < 3 && iabs(p->turn) == 0 ? 45 : 40 + iabs(p->speed) * 2;
            Foot *f = &p->foot[pick];
            if (!f->swing && err[pick] > thr) {
                f->sx = f->x; f->sy = f->y; f->sz = f->z;
                f->swing_len = f->swing = iabs(p->speed) < 3 ? 12 : len;
            }
        }
    }
}

static void air_feet(Player *p)
{
    int i;
    s32 tuck = p->vy < 0 ? 230 : 230 - clamp(p->vy * 4, 0, 200);   /* tuck rising, reach down falling */
    for (i = 0; i < 2; i++) {
        s32 dx, dz;
        rotate(p->yaw, i == 0 ? HIP_HALF : -HIP_HALF, i == 0 ? 90 : -40, &dx, &dz);
        p->foot[i].x = p->x + dx; p->foot[i].z = p->z + dz;
        p->foot[i].y = p->y - ANKLE_H - tuck;
        p->foot[i].swing = 0;
    }
}

/* ---------------------------------------------------------------- body pose */
static void build_pose(Player *p)
{
    s16 *v = p->pose.v;
    s32 fx[2], fy[2], fz[2], py, drop, trx, prz, pry, tryv;
    int i, k, prog = 0, swinging = 0;
    int bump = p->bump_t * ONE / 22, land = p->land_t * ONE / 16;
    int run = iabs(p->speed) > 26;

    for (i = 0; i < 2; i++) {
        unrotate(p->yaw, p->foot[i].x - p->x, p->foot[i].z - p->z, &fx[i], &fz[i]);
        fy[i] = p->foot[i].y - p->y;
        if (p->foot[i].swing) { swinging = 1; prog = (p->foot[i].swing_len - p->foot[i].swing) * ONE / p->foot[i].swing_len; }
    }

    /* pelvis height: bent knees, gait bob (highest mid-step), drop to reach the lower foot, landing dip */
    py = run ? 110 : 60;
    if (swinging && p->grounded) py -= isin(prog / 2) * (run ? 45 : 26) >> 12;
    drop = (fy[0] > fy[1] ? fy[0] : fy[1]) + ANKLE_H;          /* > 0: a foot is below the root */
    if (drop > 0) py += drop;
    py += land * 170 >> 12;
    py += bump * 40 >> 12;
    if (!p->grounded) py -= 40;
    p->bob = py;

    /* lean: into speed and acceleration, back when braking; bank into turns */
    trx = -(p->speed * 5) - p->accel * 30 - (land * 220 >> 12) + (bump * 300 >> 12);
    if (!p->grounded) trx -= 60;
    trx = clamp(trx, -420, 320);
    prz = clamp(-p->turn * p->speed / 30, -200, 200);
    /* stride twist: hips follow the forward foot, chest counter-rotates */
    pry = clamp((fz[0] - fz[1]) * 3 / 8, -140, 140);
    tryv = -pry * 3 / 4;

    for (k = 0; k < AP_COUNT; k++) v[k] = 0;
    v[AP_PX] = 0; v[AP_PY] = (s16)py; v[AP_PZ] = (s16)(-(bump * 90 >> 12));
    v[AP_PRX] = 0; v[AP_PRY] = (s16)pry; v[AP_PRZ] = (s16)prz;
    v[AP_TRX] = (s16)trx; v[AP_TRY] = (s16)tryv; v[AP_TRZ] = (s16)(prz / 2);
    v[AP_HRX] = (s16)(-trx * 6 / 10 + (bump * 220 >> 12)); v[AP_HRY] = (s16)(-pry - tryv);

    /* arms: counter-swing with the legs, pumping when running */
    for (i = 0; i < 2; i++) {
        int sg = i == 0 ? 1 : -1;
        s32 hx = sg * (run ? 165 : 235), hy = (run ? -1260 : -1010) + py, hz = (run ? 230 : 40) - fz[i] * (run ? 9 : 7) / 10;
        if (!p->grounded) { hx = sg * 330; hy = -1660 + py; hz = 120; }
        /* bump: brace forward */
        hx += ((sg * 170 - hx) * bump) >> 12;
        hy += ((-1480 + py - hy) * bump) >> 12;
        hz += ((300 - hz) * bump) >> 12;
        v[i == 0 ? AP_LHX : AP_RHX] = (s16)hx;
        v[i == 0 ? AP_LHY : AP_RHY] = (s16)hy;
        v[i == 0 ? AP_LHZ : AP_RHZ] = (s16)hz;
        v[i == 0 ? AP_LFX : AP_RFX] = (s16)fx[i];
        v[i == 0 ? AP_LFY : AP_RFY] = (s16)fy[i];
        v[i == 0 ? AP_LFZ : AP_RFZ] = (s16)fz[i];
    }

}

/* d-pad direction -> angle (4096 = 360), 0 = away from the camera */
static int pad_angle(u32 pad, int *ok)
{
    int dx = ((pad & PAD_RIGHT) != 0) - ((pad & PAD_LEFT) != 0);
    int dz = ((pad & PAD_UP) != 0) - ((pad & PAD_DOWN) != 0);
    static const s16 ang[3][3] = {          /* [dz+1][dx+1] */
        { 2560, 2048, 1536 },
        { 3072,    0, 1024 },
        { 3584,    0,  512 },
    };
    *ok = dx || dz;
    return ang[dz + 1][dx + 1];
}

void player_update(Player *p, u32 pad, int cam_yaw)
{
    u32 press;
    s32 target, d, step, nx, nz, cnx = 0, cnz = 0, gy;
    int moving, want, diff, rate;

    p->prev = p->pad; p->pad = pad;
    press = p->pad & ~p->prev;

    if (p->bump_t) p->bump_t--;
    if (p->land_t) p->land_t--;

    /* camera-relative: turn toward the pressed direction, then move forward */
    want = (cam_yaw + pad_angle(pad, &moving)) & 4095;
    p->turn = 0;
    target = 0;
    if (moving && p->bump_t < 12) {
        diff = ((want - p->yaw + 2048) & 4095) - 2048;
        rate = p->grounded ? TURN_MAX : TURN_MAX / 3;
        p->turn = (int)clamp(diff * 3 / 8, -rate, rate);
        if (p->turn == 0 && diff) p->turn = diff > 0 ? 1 : -1;
        p->yaw = (p->yaw + p->turn) & 4095;
        target = (pad & BTN_RUN) ? RUN_SPEED : WALK_SPEED;
        /* slow down while swinging round a sharp corner */
        if (diff > 1200 || diff < -1200) target = target / 4;
        else if (diff > 600 || diff < -600) target = target / 2;
    }
    if (p->land_t > 8 && p->grounded) target = target / 2;
    d = target - p->speed;
    step = d / 6 + (d > 0) - (d < 0);
    if (!p->grounded) step /= 3;
    p->speed += step;
    p->accel = step;

    if ((press & BTN_JUMP) && p->grounded && p->bump_t < 10) {
        p->vy = JUMP_V - iabs(p->speed) / 2;
        p->grounded = 0; p->air_t = 0;
    }

    /* move and collide */
    rotate(p->yaw, 0, p->speed, &nx, &nz);
    nx += p->x; nz += p->z;
    if (level_collide(&nx, &nz, p->y, 200, &cnx, &cnz)) {
        s32 fwx, fwz, into;
        rotate(p->yaw, 0, p->speed >= 0 ? ONE : -ONE, &fwx, &fwz);
        into = (fwx * cnx + fwz * cnz) >> 12;
        if (into < -2000 && iabs(p->speed) > 8 && p->bump_t == 0) {
            p->bump_t = 22;                               /* hit reaction: recoil off the wall */
            p->speed = -p->speed / 3;
        }
    }
    p->x = nx; p->z = nz;

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

    if (p->grounded) update_feet(p); else air_feet(p);

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
