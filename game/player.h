/*
 * Third-person player: tank controls, walk / run / jump / aim / fire, and procedural
 * full-body IK locomotion:
 *   - each foot is locked to the ground in world space while planted, so it cannot slide
 *   - a foot steps only when the body has moved/turned away from it; step length and timing
 *     are computed from the actual velocity, so the gait always keeps up with movement
 *   - foot IK onto steps, ramps and platforms (pelvis drops so both feet reach)
 *   - lean into acceleration and speed, bank into turns, arms counter-swing
 *   - jump tuck + landing dip, bump reaction when walking into walls, aim + recoil
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "model.h"
#include "anim.h"
#define DEMON_MESH_NO_DATA
#include "demon_mesh.h"

#define BTN_JUMP  PAD_CROSS
#define BTN_FIRE  PAD_SQUARE
#define BTN_RUN   PAD_CIRCLE
#define BTN_AIM   (PAD_TRIANGLE | PAD_R1)

typedef struct {
    s32 x, y, z;               /* ankle position in world space */
    s32 sx, sy, sz;            /* where the current swing started */
    int swing, swing_len;      /* frames left in the swing (0 = planted) */
} Foot;

typedef struct {
    s32 x, y, z;               /* root: on the ground, between the feet */
    int yaw;                   /* 4096 = 360 deg, 0 faces +Z */
    s32 speed, vy;             /* forward speed (units/frame), vertical velocity (Y down) */
    int turn;                  /* yaw rate this frame */
    s32 accel;
    int grounded, air_t, land_t, bump_t, recoil_t, aiming, fire_cool;
    int bob;                   /* pelvis bob from the gait */
    Foot foot[2];              /* 0 = left, 1 = right */
    u32 pad, prev;
    AnimPose pose;
    MATRIX bone_pose[DEMON_NBONES];
    MATRIX world[DEMON_NBONES];
    VECTOR muzzle;             /* hand-cannon muzzle (world) */
    s32 aim_dx, aim_dy, aim_dz;
} Player;

void player_reset(Player *p, s32 x, s32 z, int yaw);
/* returns 1 on the frame a shot is fired */
int  player_update(Player *p, u32 pad);
void player_pose(Player *p, const Model *m);
/* hand cannon on the right fist */
void player_draw_gun(const Player *p, FrameBuf *fb, const MATRIX *view, const PROJ *pr);

#endif
