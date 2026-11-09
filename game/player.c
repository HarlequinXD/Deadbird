/*
 * Player controller + animation. See player.h. Physics constants are Slayer from Hell's
 * (metres/second) converted at ~1100 units per metre, 60 frames per second, scaled 0.85 to fit
 * the test level; velocities are kept in 1/16 units per frame.
 */
#include "player.h"
#include "fkclips.h"
#include "level.h"
#include "ik.h"

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

/* chainsaw combo: each swing lunges her forward (she slides on planted hooves) */
typedef struct { int len, lunge_at, lunge, strike, chain; } Swing;
static const Swing SWINGS[3] = {
    { 26, 5, 50 * SUB, 9, 13 },           /* 1: horizontal slash, right to left */
    { 26, 5, 50 * SUB, 9, 13 },           /* 2: backhand, left to right */
    { 40, 9, 80 * SUB, 15, 99 },          /* 3: overhead slam (ends the combo) */
};

/* full-body footwork for each swing (model units; fwd + = ahead of her, out + = away from her centre,
   lift + = foot up, pel + = pelvis down / - = up). Swing 1 steps in with the left hoof, swing 2
   drives the right hoof through, swing 3 loads, hops and comes down in a deep split for the slam. */
typedef struct { u8 t; s16 lf, lo, lh, rf, ro, rh, pel; } Step;
static const Step STEPS1[] = {
    { 0, 60, 0, 0, -50, 0, 0, 20 },   { 3, 90, 0, 60, -80, 0, 0, -10 },  { 7, 250, 10, 40, -170, 0, 0, 70 },
    { 9, 290, 30, 0, -200, 10, 25, 140 }, { 16, 290, 30, 0, -200, 10, 10, 130 }, { 26, 230, 20, 0, -170, 0, 0, 80 } };
static const Step STEPS2[] = {
    { 0, 230, 20, 0, -170, 0, 0, 80 },  { 3, 220, 20, 0, -100, 0, 75, 30 },  { 7, 120, 10, 0, 200, 10, 35, 100 },
    { 9, 40, 0, 20, 270, 30, 0, 150 },  { 16, 40, 0, 10, 270, 30, 0, 140 },  { 26, 10, 0, 0, 220, 20, 0, 80 } };
static const Step STEPS3[] = {
    { 0, 10, 0, 0, 220, 20, 0, 80 },    { 5, 30, 10, 0, 150, 10, 0, 170 },   { 9, 110, 0, 130, 90, 0, 120, -130 },
    { 12, 200, 20, 80, -110, 20, 70, -60 }, { 15, 270, 60, 0, -190, 40, 0, 240 }, { 24, 270, 60, 0, -190, 40, 0, 225 },
    { 40, 60, 0, 0, -50, 0, 0, 20 } };
static const Step *const STEPS[3] = { STEPS1, STEPS2, STEPS3 };
static const int NSTEPS[3] = { 6, 6, 7 };

static void footwork(int stage, int t, s32 *out)
{
    const Step *k = STEPS[stage - 1];
    int n = NSTEPS[stage - 1], i = 0, j;
    s32 w;
    while (i + 1 < n && k[i + 1].t <= t) i++;
    if (i + 1 >= n) { w = 0; j = i; }
    else { j = i + 1; w = (t - k[i].t) * 4096 / (k[j].t - k[i].t); w = (3 * (w * w >> 12)) - (2 * ((w * w >> 12) * w >> 12)); }
    out[0] = k[i].lf + ((k[j].lf - k[i].lf) * w >> 12); out[1] = k[i].lo + ((k[j].lo - k[i].lo) * w >> 12);
    out[2] = k[i].lh + ((k[j].lh - k[i].lh) * w >> 12); out[3] = k[i].rf + ((k[j].rf - k[i].rf) * w >> 12);
    out[4] = k[i].ro + ((k[j].ro - k[i].ro) * w >> 12); out[5] = k[i].rh + ((k[j].rh - k[i].rh) * w >> 12);
    out[6] = k[i].pel + ((k[j].pel - k[i].pel) * w >> 12);
}


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


    /* chainsaw combo */
    p->atk_event = 0;
    if (p->atk) {
        const Swing *sw = &SWINGS[p->atk - 1];
        p->atk_t++;
        if ((press & BTN_ATTACK) && p->atk_t >= 3) p->atk_next = 1;
        if (p->atk_t == sw->lunge_at) {                 /* lunge: she skids forward on the swing */
            p->vx = sw->lunge * isin(p->yaw) >> 12; p->vz = sw->lunge * icos(p->yaw) >> 12;
        }
        if (p->atk_t == sw->strike) p->atk_event = 10 + p->atk;
        if (p->atk_next && p->atk < 3 && p->atk_t >= sw->chain) {
            p->atk_last = p->atk; p->atk_last_t = p->atk_t;
            p->atk++; p->atk_t = 0; p->atk_next = 0; p->atk_event = p->atk;
            if (input_mag > 300) p->yaw = input_yaw & 4095;       /* aim each swing with the stick */
        } else if (p->atk_t >= sw->len) {
            p->atk_last = p->atk; p->atk_last_t = p->atk_t; p->atk = 0;
        }
    } else if ((press & BTN_ATTACK) && p->grounded) {
        p->atk = 1; p->atk_t = 0; p->atk_next = 0; p->atk_event = 1;
        if (input_mag > 300) p->yaw = input_yaw & 4095;
    }

    if (input_mag > 300 && !p->atk) {
        wx = isin(input_yaw); wz = icos(input_yaw);
        if ((pad & BTN_RUN) || input_mag >= RUN_PUSH) wish = MAX_SPEED;
        else wish = WALK_SPEED * clamp(input_mag * 4096 / 2000, 1200, 4096) >> 12;
    }

    if (p->grounded && p->atk) {
        p->vx -= p->vx / 14; p->vz -= p->vz / 14;          /* long skid on the lunge */
    } else if (p->grounded) {
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
    {   /* wedged between two things (a gap narrower than she is): don't move this frame */
        s32 cx = nx >> 4, cz = nz >> 4;
        if (level_collide(&cx, &cz, p->y, RADIUS - 8, &cnx, &cnz)) {
            nx = p->x16; nz = p->z16;
            p->vx = 0; p->vz = 0;
        }
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

    /* animation clocks and weights (Slayer Hero.java): run / gaitAmp from speed, a stride that
       scales with speed below 1.2 m/s (so cadence stays natural when creeping), and a phase that
       advances by distance / stride so a planted foot stays put on the ground */
    {
        s32 sp = p->speed16, exc, stride;
        int run_t = smooth((sp - 30 * SUB) * 4096 / (26 * SUB));          /* 1.9 .. 3.6 m/s */
        int amp_t = smooth((sp - 20) * 4096 / (174 - 20));                /* 0.08 .. 0.7 m/s */
        if (!p->grounded) amp_t = p->amp;
        if (p->atk) { run_t = 0; amp_t = 0; }                              /* braced: hooves slide, no steps */
        p->runw += (run_t - p->runw) / 6;
        p->amp += (amp_t - p->amp) / 3;
        p->stance = 2580 + ((1680 - 2580) * p->runw >> 12);                /* 0.63 .. 0.41: heavy, long ground contact */
        exc = 330 + (80 * p->runw >> 12);                                  /* long, slow strides */
        exc = exc * clamp(sp * 4096 / 300, 0, 4096) >> 12;                  /* full stride from 1.2 m/s */
        p->exc = exc;
        if (exc < 24) exc = 24;
        stride = 2 * exc * 4096 / p->stance;                                 /* ground covered per cycle */
        if (p->grounded && !p->atk) p->phase += (int)(sp * 4096 / stride);  /* 65536 per cycle, sp in 1/16 */
        if (sp < 2 * SUB && p->grounded) {
            /* standing: settle to a double-support moment so the next start is clean */
            int m = p->phase & 32767;
            if (m) p->phase += m < 16384 ? -((m + 7) / 8) : (32768 - m + 7) / 8;
        }
        p->phase &= 65535;
        /* turning on the spot: little shuffle steps */
        if (p->amp < 2048 && p->grounded && (p->turn > 6 || p->turn < -6)) p->shuffle_amp += (4096 - p->shuffle_amp) / 4;
        else p->shuffle_amp -= p->shuffle_amp / 6;
        if (p->shuffle_amp > 200) p->shuffle_t = (p->shuffle_t + 260) & 8191;
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
        /* combo blend + how deep the lunge stance sinks (the slam drops lowest) */
        p->atk_w += ((p->atk ? 4096 : 0) - p->atk_w) / (p->atk ? 2 : 5);
        {   /* footwork: the current swing's keys (or the last swing's end while blending out), smoothed */
            s32 want[7];
            int st = p->atk ? p->atk : p->atk_last, k;
            if (st >= 1 && st <= 3) footwork(st, p->atk ? p->atk_t : p->atk_last_t, want);
            else for (k = 0; k < 7; k++) want[k] = 0;
            for (k = 0; k < 7; k++) p->atk_leg[k] += (want[k] - p->atk_leg[k]) / 2;
        }
    }
}

/* ---------------------------------------------------------------- pose */
static s32 mixi(s32 a, s32 b, int w) { return a + ((b - a) * w >> 12); }
static s32 sinpi(int q) { return isin(q >> 1); }   /* sin(pi * q), q 0..4096 */

/* upper body + pelvis tilt from the keyframed clips (legs are placed by foot IK below) */
static void build_pose(Player *p)
{
    FKPose a, b, tmp;
    FKPose *o = &p->pose;
    s32 t = ((p->phase + 32768) & 65535) >> 2;   /* clips: left foot lands at frame 0 */
    int w;

    fk_sample(&clip_idle, (s32)p->idle_t << 8, o);
    fk_sample(&clip_walk, t, &a);
    fk_sample(&clip_run, t, &b);
    fk_blend(&a, &b, p->runw, &tmp);
    fk_blend(o, &tmp, p->amp, o);
    if (p->airw > 0) {
        int at = (int)clamp((p->vy + 1400) * 32 / 2400, 0, 32);
        fk_sample(&clip_air, (s32)at << 8, &a);
        fk_blend(o, &a, p->airw, o);
    }
    p->landw = 0;
    if (p->land_t < 20) {
        w = p->land_t < 3 ? p->land_t * 4096 / 3 : (20 - p->land_t) * 4096 / 17;
        p->landw = smooth(w) * p->land_amp >> 12;
        fk_sample(&clip_land, 0, &a);
        fk_blend(o, &a, p->landw, o);
    }
    if (p->atk_w > 0) {
        static const FKClip *const ATK[3] = { &clip_atk1, &clip_atk2, &clip_atk3 };
        int st = p->atk ? p->atk : p->atk_last, at = p->atk ? p->atk_t : p->atk_last_t;
        if (st >= 1 && st <= 3) {
            fk_sample(ATK[st - 1], (s32)at << 8, &a);
            fk_blend(o, &a, p->atk_w, o);
        }
    }
    o->v[FK_PRX] += (s16)(p->lean / 16);
    o->v[FK_TRX] += (s16)(p->lean / 32);
    o->v[FK_PRZ] += (s16)(p->bank / 16);
    o->v[FK_HRY] -= (s16)(p->turn / 12);
    o->v[FK_PY] = 0;
}

static void bone_vec(const Model *m, int b, VECTOR *v) { v->vx = m->bones[b].ox; v->vy = m->bones[b].oy; v->vz = m->bones[b].oz; }

/* pelvis matrix in ground space (ground under her, facing +Z) */
static void pelvis_mat(const Model *m, const MATRIX *R, s32 px, s32 py, MATRIX *out)
{
    MATRIX l = *R;
    mat_translate(&l, px + m->bones[DEMON_BONE_PELVIS].ox, py + m->bones[DEMON_BONE_PELVIS].oy, m->bones[DEMON_BONE_PELVIS].oz);
    *out = l;
}

static void point(const MATRIX *M, const VECTOR *v, VECTOR *o)
{
    mat_rot_apply(M, v, o);
    o->vx += M->t[0]; o->vy += M->t[1]; o->vz += M->t[2];
}

/*
 * Foot IK, after Slayer from Hell's Hero.java: each foot is planted while in stance (moving back
 * under her exactly as fast as she moves forward, so it stays put on the ground) and swung through
 * on an arc with toe-off and heel-strike pitch. Every foot finds the floor under itself, so on
 * stairs each foot lands on its own step; the pelvis drops just enough for both legs to reach,
 * then a two-bone IK bends the knees (forward) to put the hooves on their targets.
 */
static void place_feet(Player *p, const Model *m)
{
    static const int legs[2][3] = {
        { DEMON_BONE_THIGH_L, DEMON_BONE_CALF_L, DEMON_BONE_HOOF_L },
        { DEMON_BONE_THIGH_R, DEMON_BONE_CALF_R, DEMON_BONE_HOOF_R } };
    MATRIX P, Rch, Rt, tmp;
    VECTOR an[2], tgt[2], pole, hip, v1, v2;
    s32 cy = icos(p->yaw), sy = isin(p->yaw);
    s32 root_y = p->y + p->step_off, need = 0, L;
    s32 mvx = 0, mvz = ONE, run = p->runw, amp = p->amp, air = p->airw;
    int si, pitch[2], mid, ph12;
    s32 bob, dz, lat;

    /* the way she's moving, in her own frame (lets her side-step / back-pedal with planted feet) */
    if (p->speed16 > 16) {
        s32 lx = (p->vx * cy - p->vz * sy) >> 12, lz = (p->vx * sy + p->vz * cy) >> 12;
        mvx = lx * ONE / p->speed16; mvz = lz * ONE / p->speed16;
    }

    for (si = 0; si < 2; si++) {
        int sg = si == 0 ? 1 : -1;                   /* +1 = her left (+X) */
        int ph = ((p->phase + (si == 0 ? 32768 : 0)) & 65535) >> 4;   /* 0..4095 */
        s32 off, lift = 0, fl, wx, wz, fwd = 0, floor_rel;
        int pt, swing, q;
        VECTOR r;
        /* rest ankle position */
        bone_vec(m, DEMON_BONE_PELVIS, &an[si]);
        bone_vec(m, legs[si][0], &r); an[si].vx += r.vx; an[si].vy += r.vy; an[si].vz += r.vz;
        bone_vec(m, legs[si][1], &r); an[si].vx += r.vx; an[si].vy += r.vy; an[si].vz += r.vz;
        bone_vec(m, legs[si][2], &r); an[si].vx += r.vx; an[si].vy += r.vy; an[si].vz += r.vz;

        if (ph < p->stance) {                        /* planted: slides back under her */
            q = ph * 4096 / p->stance;
            off = p->exc * (4096 - 2 * q) >> 12;
            swing = 0;
            pt = q < 614 ? -163 * (4096 - q * 4096 / 614) >> 12
                         : (q > 2785 ? (int)(358 * (((s32)(q - 2785) * 4096 / 1311) * ((s32)(q - 2785) * 4096 / 1311) >> 12) >> 12) : 0);
            lift = pt > 0 ? 170 * isin(pt) >> 12 : 0;  /* heel peels up before toe-off */
        } else {                                     /* swinging through on an arc */
            s32 h;
            q = (ph - p->stance) * 4096 / (4096 - p->stance);
            off = -p->exc + (2 * p->exc * smooth(q) >> 12);
            swing = 1;
            h = mixi(58, 112, run) * clamp(p->speed16 * 4096 / (22 * SUB) + 1229, 0, 4096) >> 12;
            lift = (h * sinpi(q) >> 12) + (run * 80 >> 12) * sinpi(q * 3 / 2 > 4096 ? 4096 : q * 3 / 2) / 4096;
            off -= (run * 70 >> 12) * sinpi(q * 7 / 5 > 4096 ? 4096 : q * 7 / 5) / 4096;   /* heel kicks up behind */
            pt = mixi(358, -117, smooth(q * 4096 / 3482));
        }
        off = off * amp >> 12; lift = lift * amp >> 12; pt = pt * amp >> 12;
        /* turning on the spot: alternate little steps */
        if (p->shuffle_amp > 0) {
            int sp = ((p->shuffle_t + (si ? 4096 : 0)) & 8191);
            if (sp < 4096) lift += (70 * sinpi(sp) >> 12) * p->shuffle_amp >> 12;
        }
        /* idle stance: left foot a touch forward; the chainsaw lunge splits the hooves wide */
        fwd = (sg > 0 ? 60 : -50) * (4096 - amp) >> 12;
        if (p->atk_w > 0) {                          /* combo footwork: step in, plant, push off */
            const s32 *L = &p->atk_leg[si == 0 ? 0 : 3];
            fwd = mixi(fwd, L[0], p->atk_w);
            lift += L[2] * p->atk_w >> 12;
            pt += (L[2] > 20 ? 160 : 0) * p->atk_w >> 12;   /* toe hangs as the hoof leaves the floor */
            an[si].vx += sg * (L[1] * p->atk_w >> 12);
        }
        fwd = fwd * (4096 - air) >> 12;
        /* airborne: tuck on the way up, reach down on the way down */
        off = mixi(off, sg > 0 ? 130 : -100, air);
        lift = mixi(lift, p->vy < 0 ? 300 : 100, air);
        pt = mixi(pt, p->vy < 0 ? 228 : 98, air);

        tgt[si].vx = an[si].vx + (mvx * off * 4 / 5 >> 12);
        if (sg > 0 && tgt[si].vx < 47) tgt[si].vx = 47;          /* side-steps never cross the feet */
        if (sg < 0 && tgt[si].vx > -47) tgt[si].vx = -47;
        tgt[si].vz = an[si].vz + (mvz * off >> 12) + fwd;

        /* the floor under this foot (and a little ahead of it, where the hoof reaches) */
        wx = p->x + ((tgt[si].vx * cy + tgt[si].vz * sy) >> 12);
        wz = p->z + ((tgt[si].vz * cy - tgt[si].vx * sy) >> 12);
        fl = level_ground(wx, wz, p->y);
        { s32 f2 = level_ground(wx + (sy * 85 >> 12), wz + (cy * 85 >> 12), p->y); if (f2 < fl) fl = f2; }
        if (!p->floor_ok) { p->foot_floor[0] = p->foot_floor[1] = fl; p->floor_ok = 1; }
        if (fl < p->foot_floor[si]) {                /* higher: planted feet take it at once */
            if (swing || air > 2048) p->foot_floor[si] += (fl - p->foot_floor[si]) / 2 - 1;
            else p->foot_floor[si] = fl;
            if (p->foot_floor[si] < fl) p->foot_floor[si] = fl;
        }
        if (swing && fl < p->foot_floor[si] - 30) p->foot_floor[si] = fl + 30;
        else if (fl > p->foot_floor[si]) p->foot_floor[si] += (fl - p->foot_floor[si]) / (air > 2048 ? 15 : 4) + 1;
        if (p->foot_floor[si] > fl && fl >= p->foot_floor[si] - 1) p->foot_floor[si] = fl;
        floor_rel = p->foot_floor[si] - root_y;
        if (swing && fl < p->foot_floor[si] - 20) lift += clamp(p->foot_floor[si] - fl, 0, 300) * 11 / 10;   /* clear the step */
        tgt[si].vy = an[si].vy - lift + (air > 2048 ? 0 : floor_rel * (4096 - air) >> 12);
        pitch[si] = pt;
        p->foot_swing[si] = swing;
    }

    /* pelvis: Slayer's bob / sway / run crouch / landing dip, then drop so both legs reach */
    ph12 = p->phase >> 4;
    mid = p->stance / 2;
    bob = mixi(32 * icos((ph12 - mid) * 2 & 4095) >> 12, 58 * icos((ph12 - mid - 1024) * 2 & 4095) >> 12, run) * amp >> 12;
    {   /* heavy footfalls: the body keeps sinking for a moment after each foot lands, then heaves up */
        int h = ph12 & 2047;
        if (h < 760) bob -= (s32)(mixi(26, 44, run) * isin(h * 2048 / 760) >> 12) * amp >> 12;
    }
    dz = 12 + (60 * run >> 12) * amp / 4096 - bob + (170 * p->landw >> 12) - (20 * air >> 12) + (p->atk_leg[6] * p->atk_w >> 12);
    lat = (-34 * icos((ph12 - mid) & 4095) >> 12) * amp / 4096 * (4096 - run * 6 / 10) / 4096;   /* weight rolls side to side */
    {
        MATRIX R;
        VECTOR th;
        fk_pelvis_rot(&p->pose, &R);
        pelvis_mat(m, &R, lat, dz, &P);
        L = (fk_thigh_len() + fk_calf_len()) * 985 / 1000;
        for (si = 0; si < 2; si++) {
            s32 ddx, ddy, ddz, h2;
            bone_vec(m, legs[si][0], &th);
            point(&P, &th, &hip);
            ddx = tgt[si].vx - hip.vx; ddy = tgt[si].vy - hip.vy; ddz = tgt[si].vz - hip.vz;
            h2 = ddx * ddx + ddz * ddz;
            if (ddy > 0 && h2 + ddy * ddy > L * L) {
                s32 z = h2 < L * L ? (s32)isqrt((u32)(L * L - h2)) : 0;
                if (ddy - z > need) need = ddy - z;
            }
        }
        if (need > 330) need = 330;
        p->drop += need > p->drop ? (need - p->drop) / 2 : (need - p->drop) / 8;
        P.t[1] += p->drop;
        p->pelvis_y = dz + p->drop;
        p->pelvis_x = lat;
    }

    /* legs: two-bone IK, knees forward and a touch out; hooves pitched, toes splayed */
    for (si = 0; si < 2; si++) {
        int sg = si == 0 ? 1 : -1;
        MATRIX rx, ry;
        pole.vx = sg * 330; pole.vy = 0; pole.vz = ONE;
        bone_vec(m, legs[si][0], &hip);
        bone_vec(m, legs[si][1], &v1);
        bone_vec(m, legs[si][2], &v2);
        {
            VECTOR tl, d, pl;
            d.vx = tgt[si].vx - P.t[0]; d.vy = tgt[si].vy - P.t[1]; d.vz = tgt[si].vz - P.t[2];
            mat_rot_tapply(&P, &d, &tl);
            mat_rot_tapply(&P, &pole, &pl);
            ik_two_bone(&hip, &v1, &v2, &tl, &pl, &p->bone_pose[legs[si][0]], &p->bone_pose[legs[si][1]]);
        }
        /* hoof: undo the chain's rotation, then splay and pitch it in ground space */
        mat_mul(&P, &p->bone_pose[legs[si][0]], &tmp);
        mat_mul(&tmp, &p->bone_pose[legs[si][1]], &Rch);
        Rch.t[0] = Rch.t[1] = Rch.t[2] = 0;
        mat_transpose(&Rch, &Rt);
        mat_rot_y(&ry, sg * 46);
        mat_rot_x(&rx, -pitch[si]);
        mat_mul(&ry, &rx, &tmp);
        mat_mul(&Rt, &tmp, &p->bone_pose[legs[si][2]]);
    }
}

void player_pose(Player *p, const Model *m)
{
    VECTOR off;
    MATRIX root, ry;
    build_pose(p);
    fk_bones(m, &p->pose, p->bone_pose, &off);
    place_feet(p, m);
    mat_rot_y(&ry, p->yaw);
    mat_translate(&ry, p->x, p->y + p->step_off, p->z);
    mat_identity(&root);
    mat_translate(&root, p->pelvis_x, p->pelvis_y, 0);
    mat_mul(&ry, &root, &root);
    model_skeleton(m, &root, p->bone_pose, p->world);
}
