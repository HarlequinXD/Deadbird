/*
 * Third-person player, ported from the Slayer from Hell controller:
 *   - Quake-style ground physics: friction, acceleration toward the wished velocity,
 *     stop speed, limited air control, gravity + jump
 *   - camera-relative stick: half push walks, full push (or the run button) runs
 *   - walls stop you or slide you along them (no hit reaction)
 *   - step-up smoothing and a landing dip
 *   - the body turns toward where she is actually moving, leans into acceleration and
 *     banks into turns
 * Animation: the upper body plays keyframed joint-angle clips (fk.h) blended by speed; the legs
 * are placed by Slayer-style foot IK (planted feet, swing arcs, a floor per foot, pelvis drop,
 * two-bone knees), on a gait clock driven by distance so planted feet don't slide.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "model.h"
#include "fk.h"
#define DEMON_MESH_NO_DATA
#include "demon_mesh.h"

#define BTN_JUMP  PAD_CROSS
#define BTN_DODGE  PAD_CIRCLE                /* dodge roll (keep holding to sprint out of it) */
#define PIVOT_FRAMES 14
#define BTN_SPRINT PAD_R2                    /* hold to sprint; a long full-stick run turns into one anyway */
#define BTN_ATTACK PAD_SQUARE                 /* chainsaw: press again during a swing to chain the combo */
#define BTN_CAM_L PAD_L1
#define BTN_CAM_R PAD_R1

typedef struct {
    s32 x, y, z;               /* root on the ground, world units (Y down) */
    s32 x16, y16, z16;         /* same, 1/16 units (sub-unit precision) */
    s32 vx, vz, vy;            /* velocity, 1/16 units per frame */
    int yaw;                   /* body facing, 4096 = 360, 0 faces +Z */
    s32 speed;                 /* horizontal speed, units per frame */
    s32 speed16, prev_speed16;
    int turn;                  /* yaw change this frame */
    int grounded, air_t, pushing;  /* pushing = touching a wall this frame */
    int land_t, land_amp;      /* landing dip */
    s32 step_off;              /* visual offset after stepping up/down a ledge */
    int phase;                 /* gait clock, 65536 per cycle (right foot lands at 0, left at 32768) */
    int stance;                /* share of the cycle a foot is planted, 4096 = all */
    s32 exc;                   /* how far a foot travels ahead / behind the hip */
    int shuffle_t, shuffle_amp;/* little steps when turning on the spot */
    int landw;
    s32 foot_floor[2];         /* floor height each foot is standing on / heading for */
    int floor_ok, foot_swing[2];
    s32 pelvis_x, pelvis_y;
    int idle_t;
    int runw, amp, airw;       /* blend weights 0..4096 */
    int lean, bank;            /* degrees * 16 */
    s32 drop;                  /* smoothed grounding drop */
    int atk;                   /* chainsaw combo swing 1..3, 0 = not attacking */
    int atk_t, atk_next;       /* frames into the swing; another press is buffered */
    int atk_w, atk_last, atk_last_t;   /* blend weight 0..4096, the clip to blend out of */
    int atk_event;             /* this frame: 1..3 = swing started, 11..13 = swing struck */
    int atk_active;            /* the blade is live this frame (inside the swing's hit window) */
    int hitstop;               /* frames frozen after a hit lands (weight / impact) */
    s32 atk_leg[7];            /* combo footwork, smoothed: L fwd/out/lift, R fwd/out/lift, pelvis drop */
    /* locomotion extras */
    int aim, aim_yaw;          /* set by AI: face aim_yaw and move independently (strafe, back-pedal) */
    int roll_t, roll_yaw, rollw;   /* dodge roll: frames in (0 = none), its heading, pose weight */
    int sprint, sprintw, run_hold;  /* sprinting, its blend weight, frames of full-stick running */
    int pivot_t, pivot_yaw;    /* plant-and-turn when the stick is thrown the other way at speed */
    u32 pad, prev;
    FKPose pose;
    MATRIX bone_pose[DEMON_NBONES];
    MATRIX world[DEMON_NBONES];
} Player;

void player_init(const Model *m);
void player_reset(Player *p, s32 x, s32 z, int yaw);
/* input_yaw: world heading the stick points at (camera-relative, resolved by the caller);
   input_mag: 0 = stick released, 4096 = fully pushed */
void player_update(Player *p, u32 pad, int input_yaw, int input_mag);
/* angle of (dx right, dz forward), 4096 = 360, 0 = forward */
int  player_input_angle(s32 dx, s32 dz);
void player_pose(Player *p, const Model *m);
/* the same in two halves, so another character can edit the keyframed pose (p->pose) in between */
void player_pose_begin(Player *p);
void player_pose_end(Player *p, const Model *m);
int  player_invulnerable(const Player *p);   /* mid-roll */
/* the blade struck something: freeze for a few frames (hit-stop); push back off a surface
   (normal nx, nz, 4096 = 1) by push (1/16 units per frame, 0 = no recoil) */
void player_hit_react(Player *p, s32 nx, s32 nz, int push, int stop);
/* cutscenes: hold her in combo swing `stage` at frame t with blend w (stage 0 = let go) */
void player_script(Player *p, int stage, int t, int w);

#endif
