/* Ragdolls, Hitman: Codename 47 style (Thomas Jakobsen's Verlet particles).
 *
 * A body is 21 points: the 16 joints of the shared character skeleton (Deadbird and the troopers
 * have the same bones) plus the far ends of the head, fists and hooves. Sticks of fixed length
 * join each joint to its parent, the torso's joints are braced into one rigid frame, the head is
 * braced to the shoulders, and arms and legs can't fold up shorter than half their length.
 * Each tick the points fall under gravity, the sticks are pulled back to length a few times, and
 * the floor and walls push the points out (with friction on the floor).
 * The bones are rebuilt from the points: each bone keeps the frame two of the points made with it
 * at the moment it went limp, so the hand-over from the animated pose doesn't pop.
 * A body that has stopped moving freezes and costs nothing more. */
#ifndef RAGDOLL_H
#define RAGDOLL_H
#include "gte.h"
#include "model.h"

#define RAG_NP 22                    /* 16 joints + head, fist and hoof ends + a point braced to the hips */
#define RAG_NS 72

typedef struct {
    s32 p[RAG_NP][3], o[RAG_NP][3];  /* position and last tick's, world units x 16 */
    s16 lb[16][3][3];                /* each bone's frame from its two reference points, in its own space */
    u16 len[RAG_NS];                 /* stick rest lengths (world units) */
    u8  kind[RAG_NS];                /* 0 rigid, 1 "at least this long" */
    u8  sa[RAG_NS], sb[RAG_NS];      /* the points each stick joins */
    int ns;
    int still;                       /* ticks it has barely moved */
    int frozen;
    int impact;                      /* set by rag_step: the fastest point that just hit the floor (units/tick) */
    MATRIX last[16];                 /* the bones as last rebuilt (kept when a frame can't be made) */
} Ragdoll;

/* goes limp from the skeleton `world` (the model gives the head / fist / hoof ends), moving at
   (vx, vy, vz) world units a tick, the upper body thrown along (tx, tz) (4096 = a hard shove) */
void rag_start(Ragdoll *r, const Model *m, const MATRIX *world, s32 vx, s32 vy, s32 vz, s32 tx, s32 tz);
/* cut in two at the waist (a fatality): the upper body (torso, arms, head) is let go of the legs
   and thrown at (vx, vy, vz), its top along (tx, tz) more, so it tumbles */
void rag_split(Ragdoll *r, s32 vx, s32 vy, s32 vz, s32 tx, s32 tz);
int  rag_upper(int point);                 /* 1: a point of the upper half */
int  rag_step(Ragdoll *r);                 /* one tick; returns 0 once it has settled (frozen) */
void rag_bones(Ragdoll *r, MATRIX *world); /* the 16 bone matrices from the points */
void rag_pelvis(const Ragdoll *r, s32 *x, s32 *y, s32 *z);

#endif
