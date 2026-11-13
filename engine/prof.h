/*
 * A frame profiler for the real console: how long each part of a frame takes, in TV scanlines
 * (the PS1's timer 1 counting hblanks: 263 a frame at 60 fps, so 263 = one whole frame). Shown
 * with the frame-rate readout (SELECT on the PS1). Costs nothing on the other platforms.
 */
#ifndef PROF_H
#define PROF_H
#include "psx.h"

enum { PROF_TICK, PROF_LEVEL, PROF_ENEMY, PROF_PLAYER, PROF_FX, PROF_HUD, PROF_OT,
       /* inside the tick: the camera, targeting, the player's movement, the enemies */
       PROF_T_CAM, PROF_T_LOCK, PROF_T_PLAYER, PROF_T_ENEMY,
       /* scratch slots for narrowing a hotspot down (shown after the tick's parts) */
       PROF_X0, PROF_X1, PROF_X2, PROF_X3, PROF_N };
#define PROF_LINE1 7                  /* the first line shows the frame's parts, the second the tick's */

#ifdef PSX_GTE
#define PROF_TIMER1 (*(volatile u32 *)0x1F801110)
extern u32 prof_acc[PROF_N];
#define PROF_BEGIN(id) u32 prof_t0_##id = PROF_TIMER1
#define PROF_END(id) (prof_acc[id] += (PROF_TIMER1 - prof_t0_##id) & 0xffff)
#else
#define PROF_BEGIN(id) ((void)0)
#define PROF_END(id) ((void)0)
#endif

void prof_init(void);                 /* start the timer (PS1) */
void prof_frame(void);                /* once a frame: every 30 frames the averages are published */
const char *prof_text(void);          /* "TK 40 LV 120 ..." (empty off the PS1) */
const char *prof_text2(void);         /* the tick's parts */

#endif
