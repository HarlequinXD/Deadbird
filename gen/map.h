/*
 * Sector maps (Build-style): the compiled form of a maps/NAME.map file. tools/make_map.py writes it
 * as gen/map_<name>.c; gen/gen_sector.c turns it into the baked, lit, tiled mesh at start-up.
 *
 * Units: world units (1 m = 1024), x east, z north. Heights are stored up-positive here; the
 * world is Y-down, so the builder negates them.
 *
 * A sector is a loop of points (counter-clockwise seen from above, so its inside is on the left
 * of every wall) with a floor and a ceiling. Wall i runs from point i to point i+1. A wall shared
 * with another sector (same two points, the other way round) is a portal: the builder draws the
 * lower wall below the neighbour's floor and the upper wall above its ceiling.
 *
 * Tile specs live in one byte blob (spec[]); a wall or flat names its spec by offset (0xffff = none).
 *   wall spec:  nrows, top tile (255 = none), then per row: n, tiles[n]; then noverride, (col, row, tile)...
 *               rows run bottom-up in 2 m steps, each row's tiles repeat along the wall; a top tile
 *               takes the 2 m under the ceiling. The last row repeats if the wall is taller.
 *   flat spec:  default tile, nrules, (tile, step x, step z, at x, at z)..., ncells, (cell x, cell z, tile)...
 *               cells are the world's 2 m grid (cell x = floor(x / 2048)); rules match cell % step == at.
 */
#ifndef GEN_MAP_H
#define GEN_MAP_H

#include "psx.h"

#define MAP_UNIT 1024                 /* world units per metre */
#define MAP_TILE 2048                 /* one texture tile = 2 m */
#define MAP_NONE 0xffff

enum { SK_ROOM, SK_DOOR, SK_LIFT };                    /* sector kinds */
enum { WF_GLASS = 1, WF_BARS = 2, WF_ALIGN_END = 4, WF_ALIGN_MID = 8, WF_NOSHADOW = 16 };
enum { TH_START, TH_TROOPER, TH_KEY, TH_SHARD, TH_EXIT, TH_WAY };
enum { KEY_NONE, KEY_CYAN, KEY_AMBER, KEY_CRIMSON };

/* placement-script opcodes (props, lights) - see place_props() in gen_lab.c */
enum { OP_END, OP_CUR, OP_BOX, OP_BOXF, OP_STAIRS, OP_RAILX, OP_RAILZ, OP_CRATE, OP_CAN, OP_CEIL, OP_LIGHT, OP_SPRITE,
       OP_CONSOLE, OP_MACHINE, OP_CYL, OP_QUAD, OP_POD, OP_PILLAR, OP_RACK, OP_FAN, OP_SPECIAL, OP_BASE };
enum { ND_NONE, ND_PX, ND_NX, ND_PY, ND_NY, ND_PZ, ND_NZ };
enum { SPECIAL_TANK, SPECIAL_CELLS, SPECIAL_CORE, SPECIAL_CELL };

typedef struct { s16 x, z; } MapPt;
typedef struct {
    u16 next;                         /* neighbour sector + 1, 0 = solid wall */
    u16 mid, low, up;                 /* wall specs (offsets into spec[]) */
    u8 flags;
} MapWall;
typedef struct {
    u16 p0, np;                       /* points p0 .. p0+np-1; walls share the same indices */
    s16 floor, ceil;                  /* heights (up-positive) at the hinge, or level */
    s8 fslope, cslope;                /* hinge wall of a sloped floor / ceiling, -1 = level */
    s16 fslope_h, cslope_h;           /* height at the point furthest from the hinge */
    u16 ftex, ctex;                   /* flat specs */
    u8 room, kind, lock, pad;
    s16 ka, kb;                       /* door: open height above the floor; lift: low, high floor */
} MapSector;
typedef struct { u8 type, arg; s16 x, z, yaw; s16 ax, az; } MapThing;   /* arg: key colour; ax/az: patrol point */

typedef struct {
    const char *name;
    const MapPt *pts; int npts;
    const MapWall *walls;             /* one per point */
    const MapSector *secs; int nsecs;
    const u8 *spec;
    const s16 *props;                 /* placement script, gen_lab.c opcodes (OP_END-terminated) */
    const MapThing *things; int nthings;
    const char *const *rooms; int nrooms;
} MapDef;

extern const MapDef map_test;
extern const MapDef map_e1;

/* build the level from this map instead of the built-in plant (call before gen_lab(); 0 = plant) */
void gen_level_select(const MapDef *m);
const MapDef *gen_level_map(void);

#endif
