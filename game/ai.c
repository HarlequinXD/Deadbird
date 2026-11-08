#include "ai.h"

static int rnd(AI *ai, int n)
{
    ai->rng = ai->rng * 1103515245u + 12345u;
    return (int)((ai->rng >> 16) % (u32)n);
}

void ai_reset(AI *ai, int aggression, int passive, u32 seed)
{
    memset(ai, 0, sizeof(*ai));
    ai->aggression = aggression;
    ai->passive = passive;
    ai->rng = seed;
    ai->think = 30;
}

static void pick_combo(AI *ai, s32 dist)
{
    static const u32 strings[][4] = {
        { BTN_LIGHT, BTN_LIGHT, BTN_LIGHT, 0 },
        { BTN_LIGHT, BTN_LIGHT, BTN_HEAVY, 0 },
        { BTN_HEAVY, BTN_HEAVY, 0, 0 },
        { BTN_LIGHT, 0, 0, 0 },
        { BTN_LIGHT, BTN_HEAVY, BTN_HEAVY, 0 },
        { BTN_HEAVY, 0, 0, 0 },
    };
    int i, s = rnd(ai, 6);
    (void)dist;
    ai->nplan = 0;
    for (i = 0; i < 4 && strings[s][i]; i++) ai->plan[ai->nplan++] = strings[s][i];
    ai->step = 0;
}

u32 ai_update(AI *ai, const Fighter *me, const Fighter *op)
{
    s32 dist = op->x > me->x ? op->x - me->x : me->x - op->x;
    u32 fwd = me->facing > 0 ? PAD_RIGHT : PAD_LEFT;
    u32 back = me->facing > 0 ? PAD_LEFT : PAD_RIGHT;
    u32 out = 0;
    int opp_attacking = fighter_is_attacking(op);

    /* continuing a combo string: press the next button once the move is past startup */
    if (me->state == ST_ATTACK) {
        if (ai->step < ai->nplan && me->t == 4 + rnd(ai, 3)) out |= ai->plan[ai->step++];
        return out;
    }
    if (!fighter_can_act(me)) { ai->nplan = 0; return 0; }

    /* react to an incoming attack by guarding (decided once per enemy attack) */
    if (opp_attacking && !ai->last_opp_attack && dist < 1100) {
        if (rnd(ai, 100) < 30 + ai->aggression / 3) ai->guard_timer = 14 + rnd(ai, 12);
    }
    ai->last_opp_attack = opp_attacking;
    if (ai->guard_timer > 0) { ai->guard_timer--; return BTN_GUARD; }

    if (--ai->think > 0) return ai->hold;
    ai->think = 6 + rnd(ai, 10);
    ai->hold = 0;

    if (ai->passive) {
        ai->hold = dist < 500 ? back : 0;
        return ai->hold;
    }
    if (dist > 1150) {
        if (rnd(ai, 100) < 12) return fwd | BTN_HEAVY;          /* horn-gore lunge from range */
        ai->hold = fwd;
    } else if (dist > 760) {
        ai->hold = rnd(ai, 100) < 70 ? fwd : 0;
    } else if (dist < 420 && rnd(ai, 100) < 40) {
        ai->hold = back;
        ai->think = 10;
    } else if (rnd(ai, 100) < ai->aggression) {
        pick_combo(ai, dist);
        ai->step = 1;
        ai->think = 1;
        return ai->plan[0];
    } else if (rnd(ai, 100) < 30) {
        ai->hold = BTN_GUARD;
    }
    return ai->hold;
}
