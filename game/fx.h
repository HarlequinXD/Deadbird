/* Particles (hit sparks, blood, embers) and the hell backdrop shared by the title and the arena. */
#ifndef FX_H
#define FX_H

#include "gpu.h"
#include "gte.h"

u32  fx_rand(void);
void fx_reset(void);
void fx_hit(const VECTOR *at, int power, int blocked);
/* burst of particles pushed along (dx,dy,dz): kind 0 hot blood mist, 1 dark blood, 2 sparks,
   3 blue laser sparks, 4 heavy blood drops (fall and splat) */
void fx_spray(const VECTOR *at, int kind, int count, s32 dx, s32 dy, s32 dz, int spread);
void fx_blood_pool(s32 x, s32 y, s32 z, s32 radius);   /* spreads to radius on the floor */
void fx_update(void);
/* world particles, projected with the camera */
void fx_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr);

/* screen-space hell sky: gradient, blood moon, spires (parallax by camx), embers */
void sky_draw(FrameBuf *fb, int t, int camx);
void embers_draw(FrameBuf *fb, int t, int ot);

/* hell arena floor: basalt tiles with pulsing lava cracks */
void arena_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);
void blob_shadow(FrameBuf *fb, const MATRIX *view, const PROJ *pr, s32 x, s32 y, s32 z);

#endif
