/*
 * The level: the shinigami energy-extraction plant (built by tools/build_lab.py) - ten rooms
 * joined by halls, from the loading dock to the reactor core.
 *
 * A pre-lit, textured, chunked mesh: every face carries its texture page/UVs and three baked
 * vertex colours (lighting, shadows, ambient occlusion), so drawing it is exactly what a PS1
 * does - transform, project, gouraud-textured quads into the ordering table, GPU dither.
 * Only the rooms visible from where the camera is (its room, its halls and the rooms at the
 * far ends of them) are considered; chunks outside the view are skipped, polygons crossing the near plane are clipped, and
 * distance fog darkens toward black. A few things move: energy rings in the extraction tank,
 * turbine fans, light halos and pulsing screens / plasma.
 *
 * World space: Y down (up is negative), floor at y = 0.
 */
#ifndef LEVEL_H
#define LEVEL_H

#include "gte.h"
#include "gpu.h"

#define STEP_HEIGHT 220          /* tallest ledge you can walk up without jumping */

typedef struct { s16 x, y, z; } LabVert;
typedef struct { u16 v[4]; u8 uv[8]; u8 rgb[12]; u8 page, flags, anim; } LabFace;
typedef struct { s16 cx, cy, cz; u16 r; u16 v0, nv, f0, nf; u8 area; } LabChunk;
typedef struct { s16 x0, z0, x1, z1, h; } LabBox;          /* solid from the floor up to y = -h */
typedef struct { s16 x, y, z, size; u8 r, g, b, anim, area; } LabSprite;
typedef struct { s16 x, y, z, r, speed; } LabFan;
typedef struct { s16 x, z, r, yt, yb; } LabRing;
typedef struct { s16 x, z, r; } LabPod;
typedef struct { s16 x0, z0, x1, z1; u32 pvs; const char *name; } LabArea;   /* room or hall */
typedef struct { const char *name; s32 x, y, z; int yaw, pitch; } LabView;

void level_init(void);
void level_reload_textures(void);
enum { LEVEL_HIT_NONE, LEVEL_HIT_WALL, LEVEL_HIT_PROP, LEVEL_HIT_FLOOR };
int  level_hit_segment(const VECTOR *a, const VECTOR *b, VECTOR *at, VECTOR *nrm);   /* re-upload the texture pages (after the title borrowed them) */
/* highest walkable ground at (x,z) for something currently at height cur_y */
s32  level_ground(s32 x, s32 z, s32 cur_y);
/* push a circle out of blocking geometry; returns 1 if it was blocked, with push normal (4.12) */
int  level_collide(s32 *x, s32 *z, s32 y, s32 radius, s32 *nx, s32 *nz);
/* how far along a -> b (0..4096) before solid geometry or the ceiling: for the camera */
int  level_clip_segment(const VECTOR *a, const VECTOR *b, s32 margin);
void level_spawn(s32 *x, s32 *z, int *yaw);
void level_tank(VECTOR *centre);
void level_update(void);
void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);
/* polygons drawn last frame (for the budget readout) */
int  level_polys_drawn(void);
int  level_data_bytes(void);
/* rooms / halls, and a camera spot per room for screenshots */
int  level_area_count(void);
const LabArea *level_area(int i);
int  level_area_at(s32 x, s32 z);
/* waypoints from the spawn through every room to the reactor (route test) */
int  level_route_count(void);
void level_route_point(int i, s32 *x, s32 *z);
int  level_view_count(void);
const LabView *level_view(int i);
void level_extra_views(const LabView *v, int n);   /* tools: more camera spots after the rooms' */
/* test hook: an endless flat floor with nothing on it */
void level_debug_flat(int on);

#endif
