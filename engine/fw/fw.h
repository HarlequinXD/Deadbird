/*
 * The gameplay framework: actors, the player, cameras, damage, events, variables, sound and HUD
 * for first-person and third-person games, all integer maths (PS1-safe).
 *
 * Everything a game does goes through these calls, and every call that makes sense on its own is
 * also an FSM action (fw/actions.c): a state machine built in the editor runs exactly the same
 * code a C game would call. The world (a sector level) plugs in through FwWorld.
 *
 * Units: world Y points DOWN (up is negative), 1024 units = 1 metre, angles 4096 = 360 degrees,
 * speeds in units per tick (60 ticks a second).
 */
#ifndef FW_H
#define FW_H

#include "engine.h"
#include "gte.h"

#define FW_ACTORS   96        /* live actors, the player included */
#define FW_PROTOS   32        /* actor types */
#define FW_VARS     8         /* per-actor integer variables (v0..v7) */
#define FW_GLOBALS  16        /* game-wide variables (g0..g15) */
#define FW_EVENTS   32        /* event ids (built-in + the project's own) */
#define FW_TEXTS    32        /* message strings */

/* ------------------------------------------------------------------ the world */
typedef struct {
    void (*load)(const char *level);                       /* "" = the default level */
    void (*spawn)(s32 *x, s32 *z, int *yaw);               /* where the player starts */
    s32  (*ground)(s32 x, s32 z, s32 y);                   /* floor height (world y) under (x,z) for something at y */
    int  (*collide)(s32 *x, s32 *z, s32 y, s32 radius, s32 *nx, s32 *nz);   /* push a circle out of walls */
    int  (*clip)(const VECTOR *a, const VECTOR *b, s32 margin);              /* free fraction of a->b, 0..4096 */
    void (*update)(s32 px, s32 py, s32 pz);                /* doors, lifts... once a tick */
    void (*draw)(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);
    int  (*thing_count)(void);                             /* the level's placed things (spawn points) */
    int  (*thing)(int i, int *type, int *arg, s32 *x, s32 *z, int *yaw);   /* arg: the thing's number (key colour ...) -> v0 */
    int  (*thing_type)(const char *name);                  /* a thing's name -> its type (-1 unknown) */
} FwWorld;
void fw_set_world(const FwWorld *w);
const FwWorld *fw_world(void);

/* ------------------------------------------------------------------ actor types */
enum { SHAPE_BOX, SHAPE_ORB, SHAPE_PILLAR, SHAPE_NONE };
enum {                                       /* FwProto.flags / FwActor.flags */
    FA_SOLID = 1, FA_GRAVITY = 2, FA_VISIBLE = 4, FA_PROJECTILE = 8, FA_PICKUP = 16,
    FA_GROUNDED = 32, FA_HITWALL = 64, FA_DEAD = 128
};
typedef struct {
    char name[16];
    u8   r, g, b, shape;
    s16  radius, height, size;               /* size: how big it's drawn */
    s32  hp;
    u16  flags;
    u32  tags;                               /* bit set: what it is (enemy, item ...) for targeting */
    s16  speed;                              /* a default for movement actions (0 = use the action's) */
    s16  damage;                             /* projectiles / touch damage */
    s16  life;                               /* ticks before it removes itself (0 = forever) */
    s16  fsm;                                /* its state machine (-1 = none) */
    s16  thing;                              /* the map thing type it replaces (-1 = none) */
} FwProto;

/* ------------------------------------------------------------------ actors */
typedef struct {
    u8   used, proto;
    u16  flags;
    s32  x, y, z;                            /* feet */
    s32  vx, vy, vz;                         /* velocity, units / tick (vy: + = falling) */
    int  yaw;
    s16  radius, height;
    s32  hp, hp_max;
    u32  tags;
    s32  var[FW_VARS];
    s16  owner;                              /* who fired / spawned it (-1 none) */
    s16  life, flash, damage;
    u8   r, g, b, shape;
    s16  size;
    /* the state machine */
    s16  fsm, state;
    u16  state_t, step;                      /* ticks in this state, next "do" action */
    s32  wait;                               /* a running action's counter */
    u32  events;                             /* events raised for the next tick */
} FwActor;

#define FW_PLAYER 0                          /* the player is always actor 0 */

FwActor *fw_actor(int i);                    /* 0 if unused */
int  fw_actor_index(const FwActor *a);
int  fw_spawn(int proto, s32 x, s32 y, s32 z, int yaw);                  /* -> actor index or -1 */
void fw_destroy(FwActor *a);
int  fw_proto_find(const char *name);
int  fw_proto_add(const char *name);          /* finds or adds one with defaults */
FwProto *fw_proto(int i);
int  fw_proto_count(void);

/* movement (applied by the physics each tick) */
void fw_move_forward(FwActor *a, int speed);                  /* along its facing */
void fw_move_toward(FwActor *a, s32 x, s32 z, int speed);
void fw_turn(FwActor *a, int rate);
void fw_turn_toward(FwActor *a, s32 x, s32 z, int rate);      /* rate: angle a tick, 4096 = turn at once */
void fw_jump(FwActor *a, int speed);
void fw_stop(FwActor *a);
/* senses */
s32  fw_dist(const FwActor *a, const FwActor *b);             /* ground distance */
int  fw_can_see(const FwActor *a, const FwActor *b, s32 range, int fov);  /* fov: half angle, 4096 = 360 */
int  fw_angle_to(const FwActor *a, s32 x, s32 z);             /* signed turn to face it, -2048..2047 */
/* combat */
void fw_damage(FwActor *a, int amount, int from);             /* raises HIT, or DIED at 0 hp */
void fw_heal(FwActor *a, int amount);
int  fw_shoot(FwActor *a, int proto, int speed, int damage, int at_player);   /* -> projectile index */
int  fw_hitscan(FwActor *a, s32 range, int damage, int pitch); /* -> actor hit or -1 */
int  fw_melee(FwActor *a, s32 range, int damage);              /* -> actors hit */
/* events: built-in names first, then the project's own */
enum { EV_FINISHED, EV_HIT, EV_DIED, EV_TOUCH, EV_LANDED, EV_WALL, EV_SPAWNED, EV_TIMER, EV_USER };
int  fw_event_id(const char *name);          /* finds or adds; -1 when full */
const char *fw_event_name(int id);
void fw_event(FwActor *a, int ev);           /* raise on one actor */
void fw_event_all(int ev, u32 tags);         /* every actor with any of these tags (0 = all) */
/* variables */
extern s32 fw_global[FW_GLOBALS];
/* the game */
typedef struct {
    s32 score, kills, keys;
    int won, lost;                            /* set by WinGame / LoseGame or the player dying */
} FwGame;
extern FwGame fw_game;
void fw_message(const char *text, int ticks);
void fw_shake(int amount);
void fw_flash(int r, int g, int b);
void fw_sound(int id, int vol, s32 x, s32 z);                 /* positional: louder and panned near the player */
int  fw_text_add(const char *s);             /* project strings -> index */
const char *fw_text(int i);

/* ------------------------------------------------------------------ the player and camera */
enum { CAM_FIRST_PERSON, CAM_THIRD_PERSON };
typedef struct {
    int  mode;                                /* CAM_* */
    s16  walk, run, jump, gravity;            /* speeds: units a tick; gravity: added to vy a tick */
    s16  eye;                                 /* first person: eye height */
    s16  cam_dist, cam_height;                /* third person: camera behind / above */
    s16  turn;                                /* stick turn rate */
    /* the weapon */
    s16  fire_rate;                           /* ticks between shots */
    s16  damage;
    s32  range;                               /* hitscan range (0 = fires shot_proto) */
    s16  shot_proto, shot_speed;
} FwPlayerCfg;
extern FwPlayerCfg fw_player_cfg;
void fw_player_reset(void);                   /* to the level's spawn, full health */
void fw_look(int dyaw);                       /* turn the camera (touch look drag) */

/* ------------------------------------------------------------------ running it */
void fw_init(void);                           /* clears everything (protos, FSMs, actors) */
void fw_start(void);                          /* load the world, spawn the player and the level's things */
void fw_tick(void);                           /* input, FSMs, physics, collisions: once a tick */
void fw_draw(FrameBuf *fb);                   /* world, actors, weapon, HUD */
int  fw_ram_used(void);

/* the built-in 6x8 font (fw/font.c): uploaded by fw_init at FW_FONT_X/Y in VRAM */
void fw_text_draw(FrameBuf *fb, const char *s, int x, int y, u32 rgb, int ot);
int  fw_text_width(const char *s);

#endif
