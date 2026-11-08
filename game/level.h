/*
 * Test level: floor, boundary walls, low platform, stairs, ramp, pillars, skull totems.
 * World space: Y down (up is negative), floor at y = 0, units = model units (1024 per metre-ish).
 */
#ifndef LEVEL_H
#define LEVEL_H

#include "gte.h"
#include "gpu.h"

#define STEP_HEIGHT 220          /* tallest ledge you can walk up without jumping */

typedef struct { s16 cx, cz, hx, hz, h; u32 col; } Box;     /* axis-aligned block, top at y = -h */

typedef struct {
    s32 x, z, y;                 /* base position (y = top of whatever it stands on) */
    int hp, flash, dead_t;
} Totem;
#define NTOTEMS 5
extern Totem totems[NTOTEMS];

void level_init(void);
/* highest walkable ground at (x,z) for something currently at height cur_y */
s32  level_ground(s32 x, s32 z, s32 cur_y);
/* push a circle out of blocking geometry; returns 1 if it was blocked, with push normal (4.12) */
int  level_collide(s32 *x, s32 *z, s32 y, s32 radius, s32 *nx, s32 *nz);
/* does a segment hit solid geometry or a totem? returns 1=world, 2+i=totem i, 0=nothing */
int  level_ray(const VECTOR *a, const VECTOR *b, VECTOR *hit);
void level_update(void);
void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);
int  level_totems_destroyed(void);

/* generic shaded box (used by the level and the gun): corners = m * (local box) */
void draw_box(FrameBuf *fb, const MATRIX *cam, s32 x0, s32 y0, s32 z0, s32 x1, s32 y1, s32 z1,
              u32 col, const PROJ *pr, int bias, int subdiv);

/* projectiles */
void shots_fire(const VECTOR *from, s32 dx, s32 dy, s32 dz);
void shots_update(void);
void shots_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);

#endif
