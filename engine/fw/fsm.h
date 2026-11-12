/*
 * Finite state machines for actors, built from actions (fw/actions.c).
 *
 * A machine is a list of states. Each state has:
 *   - "do" actions, run in order when the state is entered. An action can take several ticks
 *     (Wait, TurnToward ...); when the last one is done the state raises FINISHED.
 *   - "every" actions, run every tick while in the state (movement, senses).
 *   - transitions: event -> state.
 * Events come from actions (IfPlayerNear ... SEEN), from the engine (HIT, DIED, TOUCH, LANDED,
 * WALL) and from other actors (SendEvent). A tick runs the actions, then takes the first
 * transition whose event was raised.
 */
#ifndef FSM_H
#define FSM_H

#include "fw.h"

#define FSM_MAX       32          /* machines */
#define FSM_STATES    256         /* states, all machines */
#define FSM_ACTS      1024        /* actions in states */
#define FSM_TRANS     512
#define FSM_ARGS      4

typedef struct { u16 action; u8 every, pad; s32 arg[FSM_ARGS]; } FsmAct;
typedef struct { u8 event, pad; u16 to; } FsmTrans;            /* to: state index within the machine */
typedef struct { char name[16]; u16 a0, na, t0, nt; } FsmState;
typedef struct { char name[16]; u16 s0, ns; } Fsm;

/* the pools (filled by the project loader or by C) */
extern Fsm      fsm_m[FSM_MAX];
extern FsmState fsm_s[FSM_STATES];
extern FsmAct   fsm_a[FSM_ACTS];
extern FsmTrans fsm_t[FSM_TRANS];
extern int fsm_nm, fsm_ns, fsm_na, fsm_nt;

void fsm_clear(void);
/* building: begin a machine, add states, then actions / transitions to the last state */
int  fsm_begin(const char *name);
int  fsm_state(const char *name);
int  fsm_add_action(int action, int every, const s32 *args);
int  fsm_add_trans(int event, const char *to_state);           /* resolved by fsm_end */
int  fsm_end(void);                                             /* 0 if a transition names no state */
int  fsm_find(const char *name);
int  fsm_state_find(int machine, const char *name);

void fsm_enter(FwActor *a, int state);
void fsm_tick(FwActor *a);

/* ------------------------------------------------------------------ the action library */
enum { ARG_NUM, ARG_EVENT, ARG_PROTO, ARG_SOUND, ARG_BUTTON, ARG_TEXT, ARG_VAR, ARG_GLOBAL, ARG_OP, ARG_TARGET, ARG_STATE };
typedef int (*FwActionFn)(FwActor *a, const s32 *arg);          /* 1 = done, 0 = still running */
typedef struct {
    const char *name;
    FwActionFn  fn;
    u8          nargs;
    u8          type[FSM_ARGS];                                 /* ARG_* (for the editor and the loader) */
    const char *arg_names;                                      /* "speed" / "dist event" ... */
    const char *help;
    const char *group;                                          /* menu group in the editor */
} FwActionDef;
extern const FwActionDef fw_actions[];
extern const int fw_action_count;
int fw_action_find(const char *name);
/* comparison ops (ARG_OP) */
enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE };
/* SendEvent targets (ARG_TARGET) */
enum { TGT_SELF, TGT_PLAYER, TGT_ALL, TGT_ENEMIES, TGT_OWNER };

#endif
