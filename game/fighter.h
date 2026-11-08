/*
 * Fighting framework: fighters, moves, combo chains, hit detection and reactions.
 * Everything is driven by PS1 pad bits, so a human, the CPU and (later) a second pad
 * all control fighters the same way.
 */
#ifndef FIGHTER_H
#define FIGHTER_H

#include "model.h"
#include "anim.h"
#define DEMON_MESH_NO_DATA
#include "demon_mesh.h"

/* buttons */
#define BTN_LIGHT  PAD_SQUARE
#define BTN_HEAVY  PAD_TRIANGLE
#define BTN_GUARD  (PAD_CIRCLE | PAD_R1)

enum {
    ST_IDLE, ST_WALK_F, ST_WALK_B, ST_GUARD, ST_BLOCKSTUN, ST_ATTACK,
    ST_HITSTUN, ST_FALL, ST_LIE, ST_GETUP, ST_KO, ST_WIN, ST_TAUNT
};

enum { MV_JAB, MV_HOOK, MV_RAKE, MV_HAMMER, MV_HAYMAKER, MV_STOMP, MV_GORE, MV_COUNT };

enum { REACT_HIGH, REACT_BODY, REACT_DOWN };

typedef struct {
    const char *name;
    u8  clip;
    u8  active_from, active_to;   /* frames the hitbox is live */
    u8  cancel_from;              /* earliest frame the next chained move can start */
    u8  bone;                     /* hitbox bone */
    s16 hx, hy, hz;               /* hitbox offset in bone space */
    s16 radius;
    s16 damage;
    u8  hitstun, blockstun;
    u8  react;                    /* REACT_* */
    s16 push;                     /* knockback speed (units/frame) */
    s8  next_light, next_heavy;   /* combo chain, -1 = ends */
    u8  lunge_from, lunge_to;     /* frames the attacker drives forward */
    s16 lunge;                    /* forward speed while lunging */
} Move;
extern const Move moves[MV_COUNT];

typedef struct Fighter {
    int  side;                    /* 0 = player 1, 1 = player 2 */
    s32  x, z, vx;                /* position on the arena floor + knockback velocity */
    int  facing;                  /* +1 faces +X, -1 faces -X */
    int  state, t;                /* state and frames spent in it */
    int  move, move_hit, queued;  /* current move, connected yet, buffered next button */
    int  hp, trail;               /* health 0..1000 and the slow-draining damage trail */
    int  stun_len;                /* frames of hitstun/blockstun/lie */
    int  hitstop;                 /* freeze frames on impact */
    int  combo;                   /* hits landed in the current combo (shown on the HUD) */
    int  combo_show;
    int  wins;
    u32  pad, prev;
    /* animation */
    int  clip;
    s32  anim_t, rate;            /* 1/256 frames */
    AnimPose pose_now, pose_from;
    int  blend, blend_len;
    MATRIX bone_pose[DEMON_NBONES];
    MATRIX world[DEMON_NBONES];   /* world-space bone matrices (hitboxes + drawing) */
    u16  clut;
    const char *name;
} Fighter;

/* events reported to the game for effects/sound */
typedef struct { int hit, blocked, ko; VECTOR at; int power; } HitEvent;

void fighter_reset(Fighter *f, int side, s32 x, u16 clut, const char *name);
void fighter_input(Fighter *f, u32 pad);
void fighter_update(Fighter *f, const Fighter *opp, int frozen);
void fighter_pose(Fighter *f, const Model *m);       /* animation -> IK -> world bones */
/* checks f's active hitbox against target; applies the hit; returns 1 on contact */
int  fighter_check_hit(Fighter *f, Fighter *target, HitEvent *ev);
void fighter_set_state(Fighter *f, int st);
int  fighter_is_attacking(const Fighter *f);
int  fighter_can_act(const Fighter *f);

#endif
