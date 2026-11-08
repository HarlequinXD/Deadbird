/* CPU opponent: produces pad bits, so it plays through exactly the same inputs as a human. */
#ifndef AI_H
#define AI_H

#include "fighter.h"

typedef struct {
    u32 plan[4];        /* buttons of the combo string being performed */
    int nplan, step;
    int guard_timer;    /* frames left holding guard */
    int think;          /* frames until the next decision */
    int aggression;     /* 0..100 */
    int passive;        /* training dummy: never attacks */
    u32 rng;
    u32 hold;           /* held direction / guard */
    int last_opp_attack;
} AI;

void ai_reset(AI *ai, int aggression, int passive, u32 seed);
u32  ai_update(AI *ai, const Fighter *me, const Fighter *opp);

#endif
