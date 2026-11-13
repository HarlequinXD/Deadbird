#include "enemy.h"
#include "level.h"
#include "fx.h"
#include "fk.h"
#include "fkclips.h"
#include "snd.h"
#include "sfx_ids.h"
#include "soldier_mesh.h"
#include "assets.h"
#include "pistol_mesh.h"

#define E_RADIUS   230
#define E_HEIGHT   1900
#define E_HP       120
#define E_DAZE_HP  36            /* at or below this the trooper reels, open to a fatality */
#define SEE_DIST   9500
#define LASER_SPEED 150          /* units per frame (~9 m/s): a roll or a sidestep beats it */
#define LASER_DMG  4
#define MAX_LASERS 16

PSX_STATIC_ASSERT(same_skeleton, SOLDIER_NBONES == DEMON_NBONES);

static Model soldier, soldier_far, pistol;   /* soldier_far: the same mesh less its small details */
static Enemy enemies[MAX_ENEMIES];
static int n_enemies;
static EnemyHooks hooks;

typedef struct { int live, life; s32 x, y, z, vx, vy, vz; Attack atk; } Laser;
static Laser lasers[MAX_LASERS];

static s32 clampi(s32 v, s32 lo, s32 hi) { return v < lo ? lo : (v > hi ? hi : v); }
static s32 iabs_(s32 v) { return v < 0 ? -v : v; }
static int wrapa(int a) { return ((a + 2048) & 4095) - 2048; }
static int rnd(int n) { return (int)(fx_rand() % (u32)n); }
static s32 len2(s32 dx, s32 dz) { return (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4; }

static void sound(int id, int vol, s32 x, s32 z, int pitch) { if (hooks.sound) hooks.sound(id, vol, x, z, pitch); }

/* ------------------------------------------------------------------ hits from the chainsaw */
static void on_hit(CombatTarget *t, const Hit *h)
{
    Enemy *e = (Enemy *)t->user;
    if (e->state == E_FATAL || e->state == E_CUT || e->state == E_DEAD) return;
    e->tgt.hp -= h->damage;
    e->hurt_t = 0;
    e->body.vx += h->dir_x * (h->knockback / 16) >> 12 << 4;            /* knocked back: slides on his feet */
    e->body.vz += h->dir_z * (h->knockback / 16) >> 12 << 4;
    e->yaw = player_input_angle(-h->dir_x, -h->dir_z) & 4095;        /* turns toward whoever hit him */
    e->body.aim = 1; e->body.aim_yaw = e->yaw;
    {   /* blood out of the far side */
        fx_spray(&h->at, 4, 10, h->dir_x * 26 >> 12, -14, h->dir_z * 26 >> 12, 22);
        fx_spray(&h->at, 1, 8, h->dir_x * 14 >> 12, -10, h->dir_z * 14 >> 12, 18);
    }
    if (e->tgt.hp <= 0) {
        e->tgt.hp = 0; e->tgt.alive = 0; e->state = E_DEAD; e->fall_t = 0; e->t = 0;
        fx_blood_pool(e->x - (h->dir_x * 600 >> 12), e->y, e->z - (h->dir_z * 600 >> 12), 520);
        sound(SFX_GORE, 3400, e->x, e->z, 3600);
    } else if (e->tgt.hp <= E_DAZE_HP) {
        e->state = E_DAZED; e->t = 0; e->stagger_t = h->stagger; e->stagger_w = 4096;
    } else {
        e->state = E_STAGGER; e->t = 0; e->stagger_t = h->stagger; e->stagger_w = 4096;
    }
}

/* ------------------------------------------------------------------ setup */
void enemy_init(const EnemyHooks *h)
{
    hooks = *h;
    assets_init();
    soldier = asset_soldier;
    soldier.bones = (const DBONE *)soldier_bones; soldier.nbones = SOLDIER_NBONES;
    soldier_far = soldier; soldier_far.bones = (const DBONE *)soldier_bones_lod;
    pistol = asset_pistol;
    pistol.bones = (const DBONE *)pistol_bones; pistol.nbones = 1;
}

static void place(s32 x, s32 z, int yaw)
{
    Enemy *e;
    if (n_enemies >= MAX_ENEMIES) return;
    e = &enemies[n_enemies++];
    memset(e, 0, sizeof(*e));
    e->x = x; e->z = z; e->y = level_ground(x, z, 0); e->yaw = yaw & 4095;
    e->alive = 1; e->state = E_IDLE; e->shoot_cd = 60 + rnd(60); e->strafe = 1;
    player_reset(&e->body, x, z, yaw);
    e->y = e->body.y;
    e->home_x = x; e->home_z = z; e->patrol_wait = 60 + rnd(120); e->patrol_yaw = yaw & 4095;
    e->tgt.x = x; e->tgt.y = e->y; e->tgt.z = z; e->tgt.radius = E_RADIUS; e->tgt.height = E_HEIGHT;
    e->tgt.hp = e->tgt.max_hp = E_HP; e->tgt.team = TEAM_ENEMY; e->tgt.on_hit = on_hit; e->tgt.user = e;
    e->pose_stale = 1;
    combat_add(&e->tgt);
}

void enemy_reset(void)
{
    /* troopers along the route through the plant, facing back toward the way Deadbird comes in */
    static const s16 spots[][3] = {
        { -2600, -22800, 2048 + 600 },   /* security checkpoint */
        { -1200, -9500, 2048 },          /* processing hall */
        { -8000, 3600, 1024 },           /* extraction hall */
        { -19500, 16800, 2048 + 900 },   /* research lab */
        { 14000, -6000, 3072 },          /* turbine hall */
        { 20500, 1500, 2048 },           /* reactor core */
    };
    int i, from_map = 0;
    n_enemies = 0;
    memset(lasers, 0, sizeof(lasers));
    for (i = 0; i < level_thing_count(); i++) {          /* sector maps place their own troopers */
        const LabThing *t = level_thing(i);
        if (t->type == 1) { place(t->x, t->z, t->yaw); from_map = 1; }
    }
    if (from_map || level_thing_count()) return;
    for (i = 0; i < (int)(sizeof(spots) / sizeof(spots[0])); i++) place(spots[i][0], spots[i][1], spots[i][2]);
}

int enemy_count(void) { return n_enemies; }
Enemy *enemy_get(int i) { return (i >= 0 && i < n_enemies) ? &enemies[i] : 0; }

/* ------------------------------------------------------------------ animation */
static void set(FKPose *p, int ch, int v) { p->v[ch] = (s16)v; }
static void mixch(FKPose *p, int ch, int v, int w) { p->v[ch] = (s16)(p->v[ch] + (((s32)v - p->v[ch]) * w >> 12)); }

/* the trooper's own upper body over whatever the locomotion pose is */
static void upper_body(Enemy *e, FKPose *o)
{
    int s1 = isin(e->t * 61), s2 = isin(e->t * 37 + 900), k;
    /* upper body: low ready on patrol, aimed with a two-hand grip in a fight */
    if (e->state == E_IDLE) {
        set(o, FK_RSD, 80); set(o, FK_RSS, 34); set(o, FK_REL, 34); set(o, FK_RFR, -70);
        set(o, FK_LSD, 78); set(o, FK_LSS, 18); set(o, FK_LEL, 30);
        set(o, FK_HRY, (isin(e->t * 9) * 30) >> 12);
    } else {
        set(o, FK_RSD, 88); set(o, FK_RSS, 84); set(o, FK_REL, 4); set(o, FK_RFR, -90);
        set(o, FK_LSD, 84); set(o, FK_LSS, 66); set(o, FK_LEL, 52);
        set(o, FK_TRY, -10); set(o, FK_HRY, 8); set(o, FK_TRX, -3);
        if (e->flash > 0) { set(o, FK_RSS, 92); set(o, FK_TRX, 2); }            /* recoil */
    }
    if (e->stagger_w > 0 && e->state != E_DAZED) {                             /* reeling from a hit */
        int sw = e->stagger_w;
        mixch(o, FK_TRX, 22, sw); mixch(o, FK_HRX, 26, sw); mixch(o, FK_PRX, 8, sw);
        mixch(o, FK_RSS, -8, sw); mixch(o, FK_LSS, -26, sw); mixch(o, FK_RSD, 64, sw); mixch(o, FK_LSD, 62, sw);
        mixch(o, FK_LKN, 22, sw); mixch(o, FK_RKN, 30, sw);
    }
    if (e->state == E_DAZED || e->state == E_FATAL) {                          /* staggering, half out of it */
        set(o, FK_TRX, -20 + (s1 * 6 >> 12)); set(o, FK_HRX, -24 + (s2 * 14 >> 12)); set(o, FK_HRY, s1 * 18 >> 12);
        set(o, FK_TRZ, s2 * 12 >> 12); set(o, FK_PRZ, s1 * 6 >> 12); set(o, FK_PRX, -6);
        set(o, FK_RSD, 84 + (s2 * 4 >> 12)); set(o, FK_RSS, 6 + (s1 * 8 >> 12)); set(o, FK_REL, 26); set(o, FK_RFR, -60);
        set(o, FK_LSD, 80); set(o, FK_LSS, -6 + (s2 * 10 >> 12)); set(o, FK_LEL, 20);
        set(o, FK_LKN, o->v[FK_LKN] + 22); set(o, FK_RKN, o->v[FK_RKN] + 28);
        set(o, FK_LHP, o->v[FK_LHP] + 10); set(o, FK_RHP, o->v[FK_RHP] + 12);
    }
    if (e->state == E_FATAL) {
        /* he sees it coming: head snaps to her, arms come up to fend off the saw, he leans away;
           then the chain bites and he folds over it */
        int t = e->t, w = t < 10 ? 0 : t < 24 ? (t - 10) * 4096 / 14 : t < 33 ? 4096 : 0;
        if (w) {
            mixch(o, FK_HRX, -2, w); mixch(o, FK_HRY, 0, w); mixch(o, FK_TRX, 12, w); mixch(o, FK_PRX, 6, w);
            mixch(o, FK_LSD, 56, w); mixch(o, FK_LSS, 74, w); mixch(o, FK_LEL, 104, w);
            mixch(o, FK_RSD, 60, w); mixch(o, FK_RSS, 58, w); mixch(o, FK_REL, 96, w); mixch(o, FK_TRY, -10, w);
        }
        if (t >= 31 && t < 40) {                                               /* the hit: doubles over it */
            int k = t < 34 ? (t - 30) * 1365 : (40 - t) * 683;
            mixch(o, FK_TRX, -34, k); mixch(o, FK_HRX, -30, k); mixch(o, FK_PRX, -10, k);
        }
    }
    if (e->flail > 0) {                                                        /* the saw is in him */
        int f = e->flail, a = isin(e->t * 230), b = isin(e->t * 190 + 1300);
        mixch(o, FK_TRX, 20, f); mixch(o, FK_HRX, 34 + (a * 8 >> 12), f); mixch(o, FK_PRX, 6, f);
        mixch(o, FK_RSD, 40 + (a * 26 >> 12), f); mixch(o, FK_RSS, 70 + (b * 40 >> 12), f); mixch(o, FK_REL, 60, f);
        mixch(o, FK_LSD, 34 + (b * 26 >> 12), f); mixch(o, FK_LSS, 80 + (a * 40 >> 12), f); mixch(o, FK_LEL, 70, f);
        mixch(o, FK_TRZ, b * 10 >> 12, f);
    }
    if (e->state == E_DEAD) {                                                  /* limp */
        set(o, FK_HRX, -30); set(o, FK_RSD, 40); set(o, FK_LSD, 40); set(o, FK_RSS, 30); set(o, FK_LSS, 30);
        set(o, FK_LKN, 30); set(o, FK_RKN, 20);
    }
    for (k = 0; k < FK_COUNT; k++) if (k == FK_PY) o->v[k] = 0;
}

static void enemy_pose(Enemy *e)
{
    fk_sample(&clip_idle, (s32)(e->t % 96) << 8, &e->pose);
    upper_body(e, &e->pose);
}

static void enemy_skeleton(Enemy *e)
{
    MATRIX root, ry, fall;
    VECTOR off;
    e->posed = 1; e->pose_x = e->x; e->pose_y = e->y; e->pose_z = e->z;
    if (e->state != E_DEAD) {               /* alive (or being cut): the full locomotion rig */
        e->body.yaw = e->yaw;
        player_pose_begin(&e->body);
        upper_body(e, &e->body.pose);
        player_pose_end(&e->body, &soldier);
        memcpy(e->world, e->body.world, sizeof(e->world));
        e->pose = e->body.pose;
        return;
    }
    enemy_pose(e);
    fk_bones(&soldier, &e->pose, e->bpose, &off);
    mat_rot_y(&ry, e->yaw);
    if (e->state == E_DEAD) {                                  /* falls on his back about his heels */
        int a = e->fall_t < 26 ? (e->fall_t * e->fall_t * 1010 / (26 * 26)) : 1010;
        mat_rot_x(&fall, -a);
        mat_mul(&ry, &fall, &ry);
    }
    mat_translate(&ry, e->x, e->y, e->z);
    mat_identity(&root);
    mat_translate(&root, 0, off.vy, 0);
    mat_mul(&ry, &root, &root);
    model_skeleton(&soldier, &root, e->bpose, e->world);
}

/* the skeleton, worked out when something needs it: drawing him, his gun's muzzle, his waist. On the
   PS1 posing (foot IK probing the floor) every trooper every tick cost more than a whole frame */
#ifdef PSX_GTE
#define PS1_POSE_ON_TWOS 1           /* the console poses far troopers every other frame */
#define PS1_FAR_LOD 1                /* ... and draws them with fewer faces */
#else
#define PS1_POSE_ON_TWOS 0
#define PS1_FAR_LOD 0
#endif
static void pose_now(Enemy *e)
{
    if (e->pose_stale) { e->pose_stale = 0; enemy_skeleton(e); }
}

void enemy_waist(const Enemy *e, VECTOR *out)
{
    SVECTOR s; s.vx = 0; s.vy = 0; s.vz = 0;
    pose_now((Enemy *)e);
    mat_apply(&e->world[DEMON_BONE_TORSO], &s, out);
}

/* ------------------------------------------------------------------ lasers */
static void fire(Enemy *e, const Player *pl)
{
    SVECTOR m; VECTOR mz;
    Laser *L = 0;
    s32 dx, dy, dz, d;
    int i;
    for (i = 0; i < MAX_LASERS; i++) if (!lasers[i].live) { L = &lasers[i]; break; }
    if (!L) return;
    pose_now(e);
    m.vx = (s16)(SOLDIER_MOUNT_X + PISTOL_MUZZLE_X); m.vy = (s16)(SOLDIER_MOUNT_Y + PISTOL_MUZZLE_Y); m.vz = (s16)(SOLDIER_MOUNT_Z + PISTOL_MUZZLE_Z);
    mat_apply(&e->world[DEMON_BONE_FIST_R], &m, &mz);
    dx = pl->x + rnd(601) - 300 - mz.vx; dz = pl->z + rnd(601) - 300 - mz.vz;   /* not every bolt is on her */
    dy = pl->y - 1150 + rnd(301) - 150 - mz.vy;
    d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    if (d < 64) return;
    memset(L, 0, sizeof(*L));
    L->live = 1; L->life = 90;
    L->x = mz.vx; L->y = mz.vy; L->z = mz.vz;
    L->vx = dx * LASER_SPEED / d; L->vy = dy * LASER_SPEED / d; L->vz = dz * LASER_SPEED / d;
    attack_begin(&L->atk, TEAM_ENEMY, 0, LASER_DMG, 0, 0, 40);
    e->flash = 4;
    sound(SFX_LASER, 3000, mz.vx, mz.vz, 3700 + rnd(700));
}

static void lasers_update(void)
{
    int i, k;
    for (i = 0; i < MAX_LASERS; i++) {
        Laser *L = &lasers[i];
        VECTOR a, b;
        CombatEvent ev[2];
        int n;
        if (!L->live) continue;
        a.vx = L->x; a.vy = L->y; a.vz = L->z;
        L->x += L->vx; L->y += L->vy; L->z += L->vz;
        b.vx = L->x; b.vy = L->y; b.vz = L->z;
        L->atk.ox = a.vx; L->atk.oz = a.vz;
        L->atk.have_prev = 0; L->atk.world_done = 0;
        n = attack_update(&L->atk, &a, &b, ev, 2);
        for (k = 0; k < n; k++) {
            fx_spray(&ev[k].at, 3, 10, 0, -6, 0, 26);
            sound(SFX_ZAP, ev[k].kind == COMBAT_HIT_TARGET ? 3600 : 2400, ev[k].at.vx, ev[k].at.vz, 4096 + rnd(600));
            L->live = 0;
        }
        if (--L->life <= 0) L->live = 0;
    }
}

/* ------------------------------------------------------------------ AI */
/* crowd tiers (the PS1 only; Android and PC run every trooper in full): a trooper near Deadbird, or
   in any state but patrolling / fighting, gets his full movement update every tick; 5-12 m away
   every 2nd tick, further every 4th (each on his own tick). Not when he moves so fast that the
   glide could carry him into a wall before the next full update. */
#if defined(PSX_GTE) || defined(CROWD_TIERS)
static int crowd_tick;
static int crowd_glide(const Enemy *e, s32 d, int i)
{
    int every;
    s32 sp = e->body.speed16 >> 4;                       /* units a tick */
    if (e->state != E_IDLE && e->state != E_FIGHT) return 0;
    if (!e->body.grounded || e->hurt_t < 30 || d < 5000) return 0;
    every = d < 12000 ? 2 : 4;
    if (sp * (every - 1) > 110) return 0;
    return ((crowd_tick + i) & (every - 1)) != 0;
}
#else
static int crowd_glide(const Enemy *e, s32 d, int i) { (void)e; (void)d; (void)i; return 0; }
#endif

void enemy_update(Player *pl, int frozen)
{
    int i;
#if defined(PSX_GTE) || defined(CROWD_TIERS)
    crowd_tick++;
#endif
    if (!frozen) lasers_update();
    for (i = 0; i < n_enemies; i++) {
        Enemy *e = &enemies[i];
        s32 dx = pl->x - e->x, dz = pl->z - e->z, d;
        int face, speed = 0, sx = 0, run = 0, walk_yaw = 0, walk_mag = 0;
        if (e->state == E_IDLE && !frozen && (dx > 22000 || dx < -22000 || dz > 22000 || dz < -22000)) {
            e->t++; e->dist = 0x7fffffff; continue;                          /* far away: sleep (cheap test first) */
        }
        d = len2(dx, dz);
        level_hint(&e->body.sec_hint);
        e->dist = d;
        if (e->state == E_CUT) { enemy_cut_step(e); continue; }
        if (frozen || e->state == E_FATAL) { e->t++; enemy_skeleton(e); continue; }
        if (d > 22000 && e->state == E_IDLE) { e->t++; continue; }           /* far away: sleep */
        face = player_input_angle(dx, dz) & 4095;
        e->t++; e->hurt_t++;
        if (e->flash > 0) e->flash--;
        if (e->stagger_w > 0) e->stagger_w -= 4096 / 20;
        /* line of sight, a few times a second */
        if (--e->los_t <= 0) {
            VECTOR a, b;
            a.vx = e->x; a.vy = e->y - 1650; a.vz = e->z;
            b.vx = pl->x; b.vy = pl->y - 1200; b.vz = pl->z;
            e->los = d < SEE_DIST && level_clip_segment(&a, &b, 10) >= 4000;
            e->los_t = 8 + (i & 3);
        }
        /* a patrol in a room the camera can't see stands still (nobody can tell) until he sees her
           or is hurt: walking and posing him was most of the PS1's frame */
        if (e->state == E_IDLE && !e->los && e->hurt_t >= 60 && !level_point_seen(e->x, e->z)) continue;
        switch (e->state) {
        case E_IDLE:                                                           /* patrol: out and back */
            e->body.aim = 0;
            if (e->patrol_wait > 0) e->patrol_wait--;
            else {
                s32 tx = e->home_x + (e->patrol_leg ? isin(e->patrol_yaw) * 2400 >> 12 : 0);
                s32 tz = e->home_z + (e->patrol_leg ? icos(e->patrol_yaw) * 2400 >> 12 : 0);
                s32 ex = tx - e->x, ez = tz - e->z;
                if (len2(ex, ez) < 260 || e->body.pushing) {
                    e->patrol_leg ^= 1; e->patrol_wait = 90 + rnd(120);
                } else { walk_yaw = player_input_angle(ex, ez); walk_mag = 1700; }
            }
            if (e->los || e->hurt_t < 60) { e->state = E_FIGHT; e->t = 0; e->shoot_cd = 70 + rnd(60); sound(SFX_UI_MOVE, 2000, e->x, e->z, 2600); }
            break;
        case E_FIGHT: {
            e->body.aim = 1; e->body.aim_yaw = face;                            /* gun on her, legs free */
            if (d > 9500) run = 1;                                             /* far: run at her */
            if (d > 6800 || !e->los) speed = 20;                               /* close in */
            else if (d < 3000) speed = -15;                                    /* back off */
            if (--e->strafe_t <= 0) { e->strafe = rnd(3) - 1; e->strafe_t = 50 + rnd(90); }
            sx = e->strafe * 13;
            if (e->los && iabs_(wrapa(face - e->yaw)) < 160) {
                if (--e->shoot_cd <= 0) {
                    fire(e, pl);
                    if (++e->burst >= 2) { e->burst = 0; e->shoot_cd = 120 + rnd(90); } else e->shoot_cd = 12;
                }
            }
            if (!e->los && d > SEE_DIST + 3000) e->state = E_IDLE;
            break;
        }
        case E_STAGGER:
            if (--e->stagger_t <= 0) { e->state = E_FIGHT; e->shoot_cd = 60 + rnd(40); }
            break;
        case E_DAZED:                                                          /* stumbling about */
            if (e->stagger_t > 0) e->stagger_t--;
            else {
                if (e->t % 50 == 0) e->wob = rnd(4096);
                sx = (isin(e->wob) * 6) >> 12; speed = (icos(e->wob) * 5) >> 12;
                e->body.aim_yaw = (e->body.aim_yaw + (isin(e->t * 23) * 6 >> 12)) & 4095;
            }
            if (e->t > 60 * 9) { e->tgt.hp = E_DAZE_HP + 20; e->state = E_FIGHT; e->shoot_cd = 50; }   /* shakes it off */
            break;
        case E_DEAD:
            if (e->fall_t < 60) {
                e->fall_t++;
                if (e->fall_t == 26) { VECTOR a; a.vx = e->x; a.vy = e->y - 100; a.vz = e->z; sound(SFX_LAND, 3000, e->x, e->z, 3400); fx_spray(&a, 4, 6, 0, -10, 0, 16); }
            }
            break;
        }
        /* move: the AI's wish (forward / back / strafe, relative to his facing, or a patrol heading)
           drives Deadbird's controller, so he accelerates, turns, steps and plants his feet like her */
        if (e->state != E_DEAD) {
            s32 fx_ = isin(e->yaw), fz = icos(e->yaw);
            s32 wx = fx_ * speed + fz * sx, wz = fz * speed - fx_ * sx;
            s32 m = (s32)isqrt((u32)(speed * speed + sx * sx));
            int iy = walk_yaw, im = walk_mag;
            if (m > 2) { iy = player_input_angle(wx, wz); im = run ? 4096 : (int)clampi(m * 2000 / 26, 300, 2800); }
            if (e->state == E_STAGGER) im = 0;
            e->body.x = e->x; e->body.z = e->z;
            if (crowd_glide(e, d, i)) {
                /* crowd tier: away from the fight his full movement update runs on some ticks only;
                   in between he glides on at his velocity, his stride clock ticking on */
                e->body.x16 += e->body.vx; e->body.z16 += e->body.vz;
                e->body.x = e->body.x16 >> 4; e->body.z = e->body.z16 >> 4;
                e->body.phase += e->dphase;
            } else {
                int ph0 = e->body.phase;
                player_update(&e->body, 0, iy, im);
                e->dphase = e->body.phase - ph0;
            }
            e->x = e->body.x; e->y = e->body.y; e->z = e->body.z; e->yaw = e->body.yaw;
            e->amp = e->body.amp; e->phase = e->body.phase;
            {   /* his boots on the grating */
                int half = (e->body.phase >> 15) & 1;
                if (half != e->step_half && e->body.amp > 1200 && e->body.grounded)
                    sound(e->body.runw > 2048 ? SFX_STEP_RUN : SFX_STEP, 1300 + (e->body.runw >> 2), e->x, e->z, 5000 + rnd(500));
                e->step_half = half;
            }
        }
        /* keep Deadbird and the trooper from walking through each other */
        if (e->state != E_DEAD && d < E_RADIUS + 260 && d > 0) {
            s32 push = (E_RADIUS + 260 - d) / 2;
            pl->x += dx * push / d; pl->z += dz * push / d;
            pl->x16 = pl->x * 16; pl->z16 = pl->z * 16;
            e->x -= dx * push / d; e->z -= dz * push / d;
            e->body.x = e->x; e->body.z = e->z; e->body.x16 = e->x * 16; e->body.z16 = e->z * 16;
        }
        e->tgt.x = e->x; e->tgt.y = e->y; e->tgt.z = e->z;
        e->pose_stale = 1;                                                     /* posed when he's drawn */
    }
    level_hint(0);
}

int enemy_fatality_target(const Player *pl)
{
    int i, best = -1;
    s32 bd = 3600;
    for (i = 0; i < n_enemies; i++) {
        const Enemy *e = &enemies[i];
        s32 d;
        if (e->state != E_DAZED) continue;
        d = len2(pl->x - e->x, pl->z - e->z);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* ------------------------------------------------------------------ fatality */
void enemy_fatal_begin(Enemy *e, int face_yaw)
{
    e->body.vx = e->body.vz = 0; e->body.aim = 1; e->body.aim_yaw = face_yaw & 4095;
    e->state = E_FATAL; e->t = 0; e->vx = e->vz = 0; e->yaw = face_yaw & 4095;
    e->flail = 0; e->amp = 0; e->stagger_w = 0;
    e->tgt.alive = 0;
}

void enemy_fatal_step(Enemy *e, int flail)
{
    e->flail = flail; e->t++;
    /* knees buckle under him while the saw grinds, and he convulses */
    e->body.drop_extra += ((flail ? 230 : 0) - e->body.drop_extra) / 6;
    e->body.shake = flail ? 3400 : 0;
    enemy_skeleton(e);
}

void enemy_cut(Enemy *e, s32 dir_x, s32 dir_z)
{
    VECTOR w;
    enemy_waist(e, &w);
    e->state = E_CUT; e->cut = 1; e->cut_t = 0;
    e->cut_px = w.vx; e->cut_py = w.vy; e->cut_pz = w.vz;
    e->up_x = w.vx; e->up_y = w.vy; e->up_z = w.vz;
    /* the upper half is thrown back along the swing, spinning back; the legs stand, then buckle */
    e->up_vx = dir_x * 9 >> 12; e->up_vz = dir_z * 9 >> 12; e->up_vy = -16;
    e->up_rot = 0; e->up_rv = 34; e->lo_rot = 0; e->lo_rv = 0;
    e->up_landed = e->lo_landed = 0;
}

void enemy_cut_step(Enemy *e)
{
    s32 floor = e->y;
    e->cut_t++;
    if (!e->up_landed) {
        e->up_x += e->up_vx; e->up_z += e->up_vz;
        e->up_vy += 5; e->up_y += e->up_vy;
        e->up_rot += e->up_rv; if (e->up_rot > 1010) e->up_rot = 1010;
        if (e->up_y > floor - 150) {
            e->up_y = floor - 150; e->up_landed = 1; e->up_rot = 1010;
            fx_blood_pool(e->up_x, floor, e->up_z, 640);
            { VECTOR a; a.vx = e->up_x; a.vy = e->up_y; a.vz = e->up_z; fx_spray(&a, 4, 18, 0, -16, 0, 30); }
            sound(SFX_LAND, 4096, e->up_x, e->up_z, 3000);
            sound(SFX_GORE, 3600, e->up_x, e->up_z, 3300);
        }
    }
    if (e->cut_t > 34 && !e->lo_landed) {                       /* the legs buckle and topple forward */
        e->lo_rv += 3; e->lo_rot += e->lo_rv;
        if (e->lo_rot >= 1000) {
            e->lo_rot = 1000; e->lo_landed = 1;
            fx_blood_pool(e->x + (isin(e->yaw) * 500 >> 12), floor, e->z + (icos(e->yaw) * 500 >> 12), 480);
            sound(SFX_LAND, 3600, e->x, e->z, 3600);
        }
    }
    if (e->cut_t < 90 && (e->cut_t & 1)) {                      /* both halves keep pumping blood */
        VECTOR a; a.vx = e->up_x; a.vy = e->up_y + 60; a.vz = e->up_z;
        fx_spray(&a, 4, e->cut_t < 30 ? 4 : 2, 0, -8, 0, 14);
        if (!e->lo_landed) { a.vx = e->cut_px; a.vy = e->cut_py; a.vz = e->cut_pz; fx_spray(&a, 4, 2, 0, -18, 0, 12); }
    }
    enemy_skeleton(e);
}

/* ------------------------------------------------------------------ drawing */
static int is_upper(int b) { return b >= DEMON_BONE_TORSO && b <= DEMON_BONE_FIST_R; }

/* rigid transform for a cut half: rotate by ang (about the trooper's side axis) around pivot p0,
   then move the pivot to p1 */
static void half_xform(const Enemy *e, int ang, s32 p0x, s32 p0y, s32 p0z, s32 p1x, s32 p1y, s32 p1z, MATRIX *out)
{
    MATRIX ry, ryi, rx, t;
    SVECTOR p; VECTOR rp;
    mat_rot_y(&ry, e->yaw); mat_rot_y(&ryi, -e->yaw); mat_rot_x(&rx, ang);
    mat_mul(&ry, &rx, &t); mat_mul(&t, &ryi, out);
    out->t[0] = out->t[1] = out->t[2] = 0;
    p.vx = (s16)p0x; p.vy = (s16)p0y; p.vz = (s16)p0z;
    mat_apply(out, &p, &rp);
    out->t[0] = p1x - rp.vx; out->t[1] = p1y - rp.vy; out->t[2] = p1z - rp.vz;
}

static void health_bar(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const Enemy *e)
{
    SVECTOR s; VECTOR c; s16 x, y;
    int w, k;
    s.vx = (s16)e->x; s.vy = (s16)(e->y - 2150); s.vz = (s16)e->z;
    mat_apply(view, &s, &c);
    if (!rot_trans_pers(pr, &c, &x, &y) || c.vz > 12000) return;
    w = 34; k = e->tgt.hp * w / e->tgt.max_hp;
    {
        POLY *p;
        int i;
        for (i = 0; i < 2; i++) {
            int x0 = x - w / 2 - (i ? 1 : 0), x1 = i ? x + w / 2 + 1 : x - w / 2 + k;
            u8 r = i ? 20 : (e->state == E_DAZED ? (u8)(160 + (isin(e->t * 200) * 90 >> 12)) : 200);
            u8 g = i ? 6 : (e->state == E_DAZED ? 30 : 40), b = i ? 8 : 30;
            p = prim_alloc(fb);
            if (!p) return;
            memset(p, 0, sizeof(*p));
            p->code = PF_QUAD;
            p->v[0].x = (s16)x0; p->v[0].y = (s16)(y - 2); p->v[1].x = (s16)x1; p->v[1].y = (s16)(y - 2);
            p->v[2].x = (s16)x0; p->v[2].y = (s16)(y + 2); p->v[3].x = (s16)x1; p->v[3].y = (s16)(y + 2);
            p->v[0].r = r; p->v[0].g = g; p->v[0].b = b;
            ot_add(fb, i ? 3 : 2, p);
        }
    }
}

static void draw_laser(FrameBuf *fb, const MATRIX *view, const PROJ *pr, const Laser *L)
{
    SVECTOR s; VECTOR c0, c1;
    s16 x0, y0, x1, y1;
    int pass;
    s.vx = (s16)L->x; s.vy = (s16)L->y; s.vz = (s16)L->z;
    mat_apply(view, &s, &c0);
    s.vx = (s16)(L->x - L->vx * 3); s.vy = (s16)(L->y - L->vy * 3); s.vz = (s16)(L->z - L->vz * 3);
    mat_apply(view, &s, &c1);
    if (!rot_trans_pers(pr, &c0, &x0, &y0) || !rot_trans_pers(pr, &c1, &x1, &y1)) return;
    for (pass = 0; pass < 2; pass++) {               /* blue glow, then the white-hot core */
        POLY *p = prim_alloc(fb);
        s32 dx = x1 - x0, dy = y1 - y0, l = (s32)isqrt((u32)(dx * dx + dy * dy)), hw = pass ? 1 : 3;
        s32 nx, ny;
        int k;
        if (!p) return;
        if (l < 1) l = 1;
        nx = -dy * hw / l; ny = dx * hw / l;
        if (!nx && !ny) nx = hw;
        memset(p, 0, sizeof(*p));
        p->code = PF_QUAD | PF_SEMI | PF_GOURAUD;
        p->tpage = get_tpage(0, SEMI_ADD, 0, 0);
        p->v[0].x = (s16)(x0 + nx); p->v[0].y = (s16)(y0 + ny); p->v[1].x = (s16)(x0 - nx); p->v[1].y = (s16)(y0 - ny);
        p->v[2].x = (s16)(x1 + nx); p->v[2].y = (s16)(y1 + ny); p->v[3].x = (s16)(x1 - nx); p->v[3].y = (s16)(y1 - ny);
        for (k = 0; k < 4; k++) {
            int tail = k >= 2;
            p->v[k].r = (u8)(pass ? (tail ? 40 : 190) : (tail ? 0 : 30));
            p->v[k].g = (u8)(pass ? (tail ? 90 : 235) : (tail ? 10 : 110));
            p->v[k].b = (u8)(pass ? (tail ? 160 : 255) : (tail ? 60 : 255));
        }
        ot_add(fb, (int)(c0.vz >> 4) - 3 - pass, p);
    }
}

void enemy_draw(FrameBuf *fb, const MATRIX *view, const DrawCtx *ctx)
{
    MATRIX cam[16], up, lo, tmp;
    int i, b;
    s32 ox, oy, oz;
    static int draw_n;
    draw_n++;
    for (i = 0; i < n_enemies; i++) {
        Enemy *e = &enemies[i];
        SVECTOR s; VECTOR c;
        DrawCtx dc = *ctx;
        (void)s; (void)c;
        /* only troopers that can be seen: in a room the level can see, on screen, before the fog
           (a sliced one's halves spread out, so a bigger ball) */
        if (!level_ball_visible(view, &ctx->proj, e->x, e->y - 900, e->z, e->state == E_CUT ? 2400 : 1300)) continue;
        /* a trooper away from the fight is posed every other frame (animation at 15 Hz, a PS1
           classic); his skeleton still moves with him every frame, shifted to where he is now */
        ox = oy = oz = 0;
        if (e->state != E_CUT) {
            if (PS1_POSE_ON_TWOS && e->pose_stale && e->posed && e->dist > 3000 && ((draw_n + i) & 1)) {
                ox = e->x - e->pose_x; oy = e->y - e->pose_y; oz = e->z - e->pose_z;
            } else pose_now(e);
        }
        if (e->state != E_CUT && e->hurt_t < 6) { dc.r = 200; dc.g = 110; dc.b = 110; }   /* hit flash */
        if (e->state == E_CUT) {
            half_xform(e, -e->up_rot, e->cut_px, e->cut_py, e->cut_pz, e->up_x, e->up_y, e->up_z, &up);
            half_xform(e, e->lo_rot, e->x, e->y, e->z, e->x, e->y, e->z, &lo);
            for (b = 0; b < SOLDIER_NBONES; b++) {
                mat_mul(is_upper(b) ? &up : &lo, &e->world[b], &tmp);
                mat_mul(view, &tmp, &cam[b]);
            }
        } else {
            for (b = 0; b < SOLDIER_NBONES; b++) {
                tmp = e->world[b];
                tmp.t[0] += ox; tmp.t[1] += oy; tmp.t[2] += oz;
                mat_mul(view, &tmp, &cam[b]);
            }
        }
        model_draw_mats(PS1_FAR_LOD && e->dist > 4000 ? &soldier_far : &soldier, cam, &dc, fb);   /* far: fewer faces */
        {   /* the pistol in his right hand */
            MATRIX mount, pm;
            mat_identity(&mount);
            mat_translate(&mount, SOLDIER_MOUNT_X, SOLDIER_MOUNT_Y, SOLDIER_MOUNT_Z);
            mat_mul(&cam[DEMON_BONE_FIST_R], &mount, &pm);
            model_draw_mats(&pistol, &pm, &dc, fb);
        }
        if (e->state == E_FIGHT || e->state == E_STAGGER || e->state == E_DAZED)
            if (e->tgt.hp < e->tgt.max_hp) health_bar(fb, view, &ctx->proj, e);
        if (e->flash > 2) {                                                  /* muzzle flash */
            SVECTOR m; VECTOR mz;
            m.vx = (s16)(SOLDIER_MOUNT_X + PISTOL_MUZZLE_X); m.vy = (s16)(SOLDIER_MOUNT_Y + PISTOL_MUZZLE_Y); m.vz = (s16)(SOLDIER_MOUNT_Z + PISTOL_MUZZLE_Z);
            mat_apply(&e->world[DEMON_BONE_FIST_R], &m, &mz);
            mz.vx += ox; mz.vy += oy; mz.vz += oz;
            fx_spray(&mz, 3, 3, 0, 0, 0, 10);
        }
    }
    for (i = 0; i < MAX_LASERS; i++) if (lasers[i].live) draw_laser(fb, view, &ctx->proj, &lasers[i]);
}
