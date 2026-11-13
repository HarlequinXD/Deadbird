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
#include "map.h"

#define STEP_HEIGHT 512          /* tallest ledge you can walk up without jumping: 16 texels (a texture tile
                                    is 64 texels over 2 m), half a metre */
#define STEP_DOWN   220          /* drops up to this keep her glued to the ground; deeper ones she falls */

typedef struct { s16 x, y, z; } LabVert;
typedef struct { u16 v[4]; u8 uv[8]; u8 rgb[12]; u8 page, flags, anim, pal; } LabFace;   /* pal: extra CLUT + 1 */
typedef struct { s16 cx, cy, cz; u16 r; u16 v0, nv, f0, nf; u8 area; u16 sec; } LabChunk;   /* sec: moving sector + 1 */
typedef struct { s16 x0, z0, x1, z1, h, lo; } LabBox;      /* a solid prop: from height lo up to h (up-positive) */
typedef struct { s16 x, y, z, size; u8 r, g, b, anim, area; } LabSprite;
typedef struct { s16 x, y, z, r, speed; } LabFan;
typedef struct { s16 x, z, r, yt, yb; } LabRing;
typedef struct { s16 x, z, r; } LabPod;
typedef struct { s16 x0, z0, x1, z1; u32 pvs; const char *name; } LabArea;   /* room or hall */
typedef struct { const char *name; s32 x, y, z; int yaw, pitch; } LabView;
/* sector maps: a sector's bounds, floor and ceiling planes (up-positive height = h + ((x - ax) * kx +
   (z - az) * kz) >> 12) and its walls; a wall runs (x0,z0) -> (x1,z1) with the sector on its left
   (inward normal nx,nz, 4.12) and next = neighbour + 1 (0 = solid) */
typedef struct { s16 h, ax, az, kx, kz; } LabPlane;
typedef struct { s16 x0, z0, x1, z1; LabPlane f, c; u16 w0, nw; u8 room, kind, lock, flags; s16 ka, kb, k0; } LabSec;
#ifndef SF_SECRET
#define SF_SECRET 1                       /* sector flag: a secret door (gen/map.h) */
#endif   /* lift: low, high, start floor; door: open height */
typedef struct { s16 x0, z0, x1, z1, nx, nz; u16 next, sec; } LabWall;
typedef struct { u8 type, arg; s16 x, z, yaw, ax, az; } LabThing;        /* map things (gen/map.h TH_*) */

void level_init(void);
void level_reload_textures(void);
void level_load_map(const MapDef *m);      /* rebuild from another sector map (0 = level 1); not on PS1 */
enum { LEVEL_HIT_NONE, LEVEL_HIT_WALL, LEVEL_HIT_PROP, LEVEL_HIT_FLOOR };
int  level_hit_segment(const VECTOR *a, const VECTOR *b, VECTOR *at, VECTOR *nrm);   /* re-upload the texture pages (after the title borrowed them) */
/* highest walkable ground at (x,z) for something currently at height cur_y */
s32  level_ground(s32 x, s32 z, s32 cur_y);
/* push a circle out of blocking geometry; returns 1 if it was blocked, with push normal (4.12) */
int  level_collide(s32 *x, s32 *z, s32 y, s32 radius, s32 *nx, s32 *nz);
/* how far along a -> b (0..4096) before solid geometry or the ceiling: for the camera */
int  level_clip_segment(const VECTOR *a, const VECTOR *b, s32 margin);
void level_spawn(s32 *x, s32 *z, int *yaw);
int  level_tank(VECTOR *centre);   /* 0 when the level has no energy tank */
void level_update(void);
void level_draw(FrameBuf *fb, const MATRIX *view, const PROJ *pr, int t);
void level_set_ot_bias(int b);            /* added to every OT index the level uses (the editor's 3D view) */
/* polygons drawn last frame (for the budget readout) */
int  level_polys_drawn(void);
int  level_data_bytes(void);
/* rooms / halls, and a camera spot per room for screenshots */
int  level_area_count(void);
const LabArea *level_area(int i);
int  level_area_at(s32 x, s32 z);
/* a ball (x, y, z, radius r) that could be on screen: in a room the last level_draw saw, inside the
   screen's edges, nearer than the fog */
int  level_point_seen(s32 x, s32 z);
int *level_hint(int *h);                  /* whose sector hint level queries use (0 = shared); returns the old one */          /* in a room the last level_draw could see */
int  level_ball_visible(const MATRIX *view, const PROJ *pr, s32 x, s32 y, s32 z, s32 r);
/* waypoints from the spawn through every room to the reactor (route test) */
int  level_route_count(void);
void level_route_point(int i, s32 *x, s32 *z);
int  level_view_count(void);
const LabView *level_view(int i);
void level_extra_views(const LabView *v, int n);   /* tools: more camera spots after the rooms' */
/* sector maps: the sector at (x,z) (-1 = none; hint = a guess, e.g. last frame's), its floor and
   ceiling (world y, Y down) */
int  level_sector_at(s32 x, s32 z, int hint);
s32  level_sector_floor(int sec, s32 x, s32 z);
s32  level_sector_ceil(int sec, s32 x, s32 z);
/* doors, lifts and pickups (sector maps): run once a frame with the player's feet and keys (bit k =
   key colour k); hooks report sounds and what she ran into */
typedef struct {
    void (*sound)(int id, int vol, s32 x, s32 z, int pitch);
    void (*locked)(int colour);               /* she walked up to a door she has no key for */
} LevelHooks;
void level_set_hooks(const LevelHooks *h);
void level_think(s32 px, s32 py, s32 pz, u32 keys);
void level_set_use_mode(int on);          /* doors and lifts wait for level_use (else they work by themselves) */
enum { LU_NONE, LU_DOOR, LU_SECRET, LU_LOCKED, LU_LIFT };
int  level_use(s32 px, s32 py, s32 pz, u32 keys);    /* the interact button: opens / calls / starts (LU_*) */
int  level_can_use(s32 px, s32 py, s32 pz);          /* what A would do here (no effect; secret doors: LU_NONE) */
/* a pickup she's touching (taken once): returns its LabThing type or -1, its arg in *arg */
int  level_pickup(s32 px, s32 py, s32 pz, int *arg);
int  level_thing_count(void);
int  level_secrets(int *found);                 /* secret shards: how many, *found taken */
const LabThing *level_thing(int i);
/* test hook: an endless flat floor with nothing on it */
void level_debug_flat(int on);
void level_debug_all_rooms(int on);           /* test hook: draw every room (no portal visibility) */

#endif
