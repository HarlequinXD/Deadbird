/*
 * Enemies: sci-fi troopers (tools/build_soldier.py) with a laser pistol (tools/build_pistol.py)
 * attached to the right hand. They patrol, spot Deadbird, keep their distance, strafe and fire
 * bursts of blue laser bolts. The chainsaw staggers them (combat.h); at low health they stagger
 * about dazed, which is the moment for a FATALITY (driven from deadbird.c): the cut splits the
 * body at the waist and both halves fall as rigid pieces.
 */
#ifndef ENEMY_H
#define ENEMY_H

#include "gpu.h"
#include "model.h"
#include "combat.h"
#include "player.h"

#define MAX_ENEMIES 24
enum { E_IDLE, E_FIGHT, E_STAGGER, E_DAZED, E_DEAD, E_FATAL, E_CUT };

typedef struct {
    CombatTarget tgt;
    s32 x, y, z, vx, vz;          /* feet; velocity in 1/16 units per frame */
    int yaw, state, t, alive;
    int shoot_cd, burst, flash, strafe, strafe_t, los, los_t;
    int phase, amp, stagger_t, stagger_w, fall_t, wob;
    int hurt_t;                   /* frames since last hurt (hit flash) */
    /* fatality / cut in half */
    int flail;                    /* 0..4096 flailing while the saw grinds */
    int cut, cut_t;
    s32 cut_px, cut_py, cut_pz;   /* world point the upper half pivots about (the waist) */
    s32 up_x, up_y, up_z, up_vx, up_vy, up_vz;
    int up_rot, up_rv, up_landed, lo_rot, lo_rv, lo_landed;
    /* locomotion: Deadbird's controller (physics, gait, foot IK, strafing with planted feet) */
    Player body;
    s32 home_x, home_z;           /* patrol: walks out from here and back */
    int patrol_leg, patrol_wait, patrol_yaw, step_half;
    FKPose pose;
    MATRIX bpose[16], world[16];
} Enemy;

/* hooks the game supplies: player damage and sounds/effects for enemy events */
typedef struct {
    void (*hurt_player)(int damage, s32 dir_x, s32 dir_z);
    void (*sound)(int sfx, int vol, s32 x, s32 z, int pitch);
} EnemyHooks;

void enemy_init(const EnemyHooks *hooks);                          /* uploads the trooper + pistol textures */
void enemy_reset(void);                                            /* places the level's troopers, registers targets */
void enemy_update(Player *pl, int frozen);                         /* AI, lasers, collisions (frozen: cutscene) */
void enemy_draw(FrameBuf *fb, const MATRIX *view, const DrawCtx *ctx);
int  enemy_count(void);
Enemy *enemy_get(int i);
/* the dazed enemy close enough to finish, or -1 */
int  enemy_fatality_target(const Player *pl);
/* fatality script: start (stops it), per-frame step (the saw is grinding when flail > 0),
   and the cut that splits it in two (the cut physics keeps running in enemy_update) */
void enemy_fatal_begin(Enemy *e, int face_yaw);
void enemy_fatal_step(Enemy *e, int flail);
void enemy_cut(Enemy *e, s32 dir_x, s32 dir_z);
void enemy_cut_step(Enemy *e);
/* the waist (cut height) in world space */
void enemy_waist(const Enemy *e, VECTOR *out);

#endif
