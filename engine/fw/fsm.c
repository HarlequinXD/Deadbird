/* State machines: building them and running them on actors (see fsm.h). */
#include "fsm.h"

Fsm      fsm_m[FSM_MAX];
FsmState fsm_s[FSM_STATES];
FsmAct   fsm_a[FSM_ACTS];
FsmTrans fsm_t[FSM_TRANS];
int fsm_nm, fsm_ns, fsm_na, fsm_nt;

/* transitions name their state before it may exist: kept here until fsm_end */
static char pend_name[FSM_TRANS][16];
static int  pend_first;

static void copy_name(char *d, const char *s) { int i; for (i = 0; i < 15 && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static int  same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

void fsm_clear(void) { fsm_nm = fsm_ns = fsm_na = fsm_nt = 0; }

int fsm_begin(const char *name)
{
    Fsm *m;
    if (fsm_nm >= FSM_MAX) return -1;
    m = &fsm_m[fsm_nm];
    copy_name(m->name, name);
    m->s0 = (u16)fsm_ns; m->ns = 0;
    pend_first = fsm_nt;
    return fsm_nm++;
}

int fsm_state(const char *name)
{
    FsmState *s;
    if (fsm_nm == 0 || fsm_ns >= FSM_STATES) return -1;
    s = &fsm_s[fsm_ns];
    copy_name(s->name, name);
    s->a0 = (u16)fsm_na; s->na = 0; s->t0 = (u16)fsm_nt; s->nt = 0;
    fsm_m[fsm_nm - 1].ns++;
    return fsm_ns++;
}

int fsm_add_action(int action, int every, const s32 *args)
{
    FsmAct *a;
    int k;
    if (fsm_ns == 0 || fsm_na >= FSM_ACTS || action < 0 || action >= fw_action_count) return -1;
    a = &fsm_a[fsm_na];
    a->action = (u16)action; a->every = (u8)(every != 0);
    for (k = 0; k < FSM_ARGS; k++) a->arg[k] = args ? args[k] : 0;
    fsm_s[fsm_ns - 1].na++;
    return fsm_na++;
}

int fsm_add_trans(int event, const char *to)
{
    if (fsm_ns == 0 || fsm_nt >= FSM_TRANS || event < 0) return -1;
    fsm_t[fsm_nt].event = (u8)event; fsm_t[fsm_nt].to = 0;
    copy_name(pend_name[fsm_nt], to);
    fsm_s[fsm_ns - 1].nt++;
    return fsm_nt++;
}

int fsm_state_find(int machine, const char *name)
{
    int i;
    if (machine < 0 || machine >= fsm_nm) return -1;
    for (i = 0; i < fsm_m[machine].ns; i++) if (same(fsm_s[fsm_m[machine].s0 + i].name, name)) return fsm_m[machine].s0 + i;
    return -1;
}

int fsm_end(void)
{
    int i, ok = 1, m = fsm_nm - 1;
    for (i = pend_first; i < fsm_nt; i++) {
        int s = fsm_state_find(m, pend_name[i]);
        if (s < 0) { ok = 0; s = fsm_m[m].s0; }
        fsm_t[i].to = (u16)s;
    }
    return ok;
}

int fsm_find(const char *name)
{
    int i;
    for (i = 0; i < fsm_nm; i++) if (same(fsm_m[i].name, name)) return i;
    return -1;
}

void fsm_enter(FwActor *a, int state)
{
    a->state = (s16)state;
    a->state_t = 0; a->step = 0; a->wait = 0;
}

void fsm_tick(FwActor *a)
{
    const FsmState *s;
    int i, start;
    u32 ev;
    if (a->fsm < 0 || a->state < 0) return;
    start = a->state;
    s = &fsm_s[a->state];
    /* the "do" sequence: as far as it gets this tick; FINISHED when the last one is done */
    if (a->step < s->na) {
        int has_do = 0;
        for (i = 0; i < s->na; i++) if (!fsm_a[s->a0 + i].every) has_do = 1;
        while (a->step < s->na) {
            const FsmAct *x = &fsm_a[s->a0 + a->step];
            if (x->every) { a->step++; continue; }
            if (!fw_actions[x->action].fn(a, x->arg)) break;          /* still running: again next tick */
            if (!a->used || a->state != start) return;                  /* destroyed, or it changed state */
            a->wait = 0;
            a->step++;
        }
        if (a->step >= s->na && has_do) a->events |= 1u << EV_FINISHED;
    }
    /* the "every" actions */
    for (i = 0; i < s->na; i++) {
        const FsmAct *x = &fsm_a[s->a0 + i];
        if (!x->every) continue;
        fw_actions[x->action].fn(a, x->arg);
        if (!a->used || a->state != start) return;
    }
    if (a->state_t < 65535) a->state_t++;
    /* transitions: the first raised event this state listens for */
    ev = a->events;
    a->events = 0;
    if (!ev) return;
    for (i = 0; i < s->nt; i++) {
        const FsmTrans *t = &fsm_t[s->t0 + i];
        if (ev & (1u << t->event)) { fsm_enter(a, t->to); return; }
    }
}
