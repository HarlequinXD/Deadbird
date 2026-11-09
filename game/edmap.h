/*
 * The level editor's map: the contents of a maps/NAME.map file held in memory (the format
 * tools/make_map.py documents), so the editor works on exactly what Deadbird's sector builder
 * reads. em_parse reads .map text, em_write writes it back, em_compile turns it into the MapDef
 * gen_lab builds the level from (the same steps as make_map.py: portals, tile specs, props).
 *
 * Units are the map's: 1 m = 1024, x east, z north, heights up-positive. Every sector's loop is
 * kept counter-clockwise. Not on the PS1 (GEN_BAKED): it has no level builder at run time.
 */
#ifndef EDMAP_H
#define EDMAP_H

#include "psx.h"
#include "map.h"

#define EM_SV      48          /* points a sector (the builder clips floors in 60-point buffers) */
#define EM_SECS    320
#define EM_ROWS    768         /* row specs (walls / lows / ups lines) */
#define EM_WCELLS  2048        /* single wall tiles */
#define EM_FCELLS  3072        /* single floor / ceiling tiles */
#define EM_THINGS  200
#define EM_LIGHTS  256
#define EM_SPRITES 16
#define EM_PROPS   400
#define EM_ROOMS   32
#define EM_NROWS   6
#define EM_ROWLEN  8
#define EM_RULES   6
#define EM_LIMIT   32000       /* +-31.25 m: the builder's vertices are s16 */

/* a ROWS spec: tile rows bottom-up (each row's tiles repeat along the wall), top = 255 for none */
typedef struct { u8 nrows, top, n[EM_NROWS], t[EM_NROWS][EM_ROWLEN]; } EmRows;
typedef struct { s16 mid, low, up; u8 flags; } EmWall;                 /* row spec indices, -1 = default */
typedef struct { u8 tile, sx, sz, ox, oz; } EmRule;
typedef struct { u8 def, nrules; EmRule r[EM_RULES]; } EmFlat;
enum { EM_MID, EM_LOW, EM_UP };
typedef struct { u16 sec; u8 wall, part, col, row, tile; } EmWCell;   /* wall I [low|up] cell COL,ROW TILE */
typedef struct { u16 sec; u8 ceil; s8 cx, cz; u8 tile; } EmFCell;     /* fcell / ccell */
typedef struct {
    char name[16];
    u8 room, kind, lock, np;
    s16 x[EM_SV], z[EM_SV];
    EmWall w[EM_SV];
    s16 floor, ceil, ka, kb;          /* door: ka = open height; lift: ka, kb = low, high floor */
    s8 fslope, cslope;                /* hinge wall, -1 = level */
    s16 fslope_h, cslope_h;
    EmFlat f, c;
    s16 walls, lows, ups;             /* the sector's default row specs, -1 = none */
} EmSector;
typedef struct { u8 type, arg; s16 x, z, yaw, ax, az; } EmThing;      /* TH_*; key: arg colour; shard: arg 1 = secret */
/* light x,z,h R G B RADIUS (colour x100); grid = 1: lightgrid x,z x1,z1 h STEP R G B RADIUS */
typedef struct { u8 grid, r, g, b; s16 x, z, x1, z1, h, step, rad; } EmLight;
typedef struct { s16 x, z, h, size; u8 r, g, b, anim; } EmSprite;
enum { PK_BOX, PK_BOXF, PK_CRATE, PK_CAN, PK_PILLAR, PK_CONSOLE, PK_MACHINE, PK_POD, PK_RACK, PK_FAN,
       PK_RAIL, PK_CYL, PK_TANK, PK_CORE, PK_CELL, PK_COUNT };
/* one prop line; which fields mean what depends on the kind (edmap.c em_prop_fields) */
typedef struct {
    u8 kind, dir, flags, t0, t1, t2, rep, axis;  /* dir: 0 n 1 e 2 s 3 w; t*: tiles (255 none) */
    s16 x0, z0, x1, z1, h0, h1, h2;              /* points and heights */
    s16 v0, v1, v2;                              /* counts, speeds, emit... */
} EmProp;
typedef struct { char id[16], name[24]; } EmRoom;

typedef struct {
    char name[24];
    int nsecs, nrows, nwcells, nfcells, nthings, nlights, nsprites, nprops, nrooms;
    EmRoom rooms[EM_ROOMS];
    EmSector secs[EM_SECS];
    EmRows rows[EM_ROWS];
    EmWCell wcells[EM_WCELLS];
    EmFCell fcells[EM_FCELLS];
    EmThing things[EM_THINGS];
    EmLight lights[EM_LIGHTS];
    EmSprite sprites[EM_SPRITES];
    EmProp props[EM_PROPS];
} EmMap;

extern const char *const em_tile_names[];   /* LT_* without the prefix (gen/map_src.c) */
extern const int em_tile_count;
extern const char em_src_e1[];              /* maps/e1.map as shipped (comments stripped) */
extern const char *const em_prop_names[PK_COUNT];
extern const char *const em_thing_names[];  /* by TH_* */

/* text <-> map. em_parse returns 1, or 0 with em_error set ("line 12: ..."). */
int  em_parse(EmMap *m, const char *text);
int  em_write(const EmMap *m, char *out, int cap);      /* bytes written, -1 if it didn't fit */
extern char em_error[96];
void em_clear(EmMap *m, const char *name);              /* an empty map with one room */
void em_new_level(EmMap *m, const char *name);          /* a starter room with a start point */

/* checks the map against the builder's rules; 0 = fine, else em_error says what (and *sec, *wall
   point at the trouble when they can) */
int  em_check(const EmMap *m, int *sec, int *wall);
int  em_check_geom(const EmMap *m, int *sec, int *wall);  /* the sectors only (not things) */
/* builds the MapDef gen_lab reads (arrays owned by edmap.c, valid until the next compile);
   strict: the whole map must check out (to play it), else just the sectors (to look at it) */
const MapDef *em_compile(const EmMap *m, int strict);

/* geometry helpers */
int  em_sector_at(const EmMap *m, s32 x, s32 z);          /* -1 if none */
int  em_inside(const EmSector *s, s32 x, s32 z);
s32  em_area2(const EmSector *s);                        /* twice the area (positive: CCW) */
void em_fix_winding(EmSector *s);                        /* counter-clockwise, remapping per-wall data */
int  em_floor_at(const EmMap *m, int si, s32 x, s32 z);  /* the floor height there (slopes too) */
int  em_ceil_at(const EmMap *m, int si, s32 x, s32 z);
/* splits walls that have another sector's corner on them (the builder needs walls that meet to
   share both points); returns how many points were added */
int  em_weld(EmMap *m);
/* how sector si's wall w resolves (spec index or -1) for part EM_MID/LOW/UP */
int  em_wall_rows(const EmMap *m, int si, int w, int part);
int  em_find_tile(const char *name);                     /* tile id, -1 unknown */
int  em_rows_new(EmMap *m, const EmRows *r);             /* -1 if full */
void em_rows_gc(EmMap *m);                               /* drops unused row specs */
/* point editing (keeps wall cells and slopes pointing at the right walls) */
int  em_insert_point(EmMap *m, int si, int at, s32 x, s32 z);   /* new point at index at */
void em_delete_point(EmMap *m, int si, int at);
void em_delete_sector(EmMap *m, int si);
int  em_add_sector(EmMap *m, const s32 *x, const s32 *z, int n, int like);   /* like: copy settings from it (-1 = defaults) */
/* editing (each returns 0 / -1 with em_error set when it can't; callers check em_check_geom after
   and undo on failure) */
int  em_near_point(const EmMap *m, s32 x, s32 z, s32 r, int *si, int *pi);
int  em_near_wall(const EmMap *m, s32 x, s32 z, s32 r, int *si, int *wi);
int  em_move_points(EmMap *m, const s32 *px, const s32 *pz, int n, s32 dx, s32 dz);
int  em_delete_corner(EmMap *m, s32 x, s32 z);
int  em_cut(EmMap *m, int si, s32 ax, s32 az, s32 bx, s32 bz);
int  em_carve(EmMap *m, int si, const s32 *px, const s32 *pz, int n);   /* returns the new sector */
int  em_extrude(EmMap *m, int si, int w, s32 dist);                     /* returns the new sector */
int  em_stairs(EmMap *m, int si, int dir, s32 to, int steps);
int  em_join(EmMap *m, int si, int w);                                  /* returns the joined sector */
int  em_prop_xz(const EmProp *p, s32 *x, s32 *z);       /* a prop's centre */
void em_prop_move(EmProp *p, s32 dx, s32 dz);
int  em_prop_defaults(EmProp *p, int kind, s32 x, s32 z);

#endif
