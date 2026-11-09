/*
 * The level, generated at start-up: textures (gen_labtex.c) and geometry with baked lighting
 * (gen_lab.c). The game reads it through game/lab_data.h.
 */
#ifndef GEN_LAB_H
#define GEN_LAB_H

#include "map.h"

#define LAB_NPAGES 5

/* texture tiles: page = id / 16, tile = id % 16 */
enum {
    LT_BASE_PLAIN, LT_BASE_PIPES, LT_BASE_VENT, LT_BASE_HAZARD, LT_MID_PANEL, LT_MID_POSTER_POWER, LT_MID_POSTER_DANGER,
    LT_MID_AUTHORIZED, LT_MID_VPIPES, LT_MID_MONITOR, LT_MID_ARROW, LT_TOP_CIRCUIT, LT_TOP_BEAM, LT_DOOR_JAMB_L,
    LT_DOOR_JAMB_R, LT_DOOR_LINTEL,
    LT_MACHINE_PANEL, LT_SCREEN_GRAPH, LT_SCREEN_VITALS, LT_PROPS_FREE, LT_GLASS, LT_ENERGY, LT_COIL, LT_CRATE,
    LT_CANISTER, LT_MACHINE_BASE, LT_KEYBOARD, LT_CAGE_BARS, LT_FAN, LT_SHINI_BODY, LT_SHINI_SKULL, LT_LAMP_GLOW,
    LT_DOCK_PLATE, LT_DOCK_NUMBER, LT_CHECKPOINT_PANEL, LT_PROCESSING_PANEL, LT_EXTRACTION_PANEL, LT_CELL_WALL,
    LT_CELL_WARNING, LT_LAB_PANEL, LT_LAB_POSTER, LT_VAULT_PANEL, LT_GALLERY_PIPES, LT_TURBINE_PANEL, LT_REACTOR_PLATE,
    LT_REACTOR_WARNING, LT_HALL_WALL, LT_HALL_WALL_TOP,
    LT_FLOOR_PLATE, LT_FLOOR_GRATE, LT_FLOOR_EDGE, LT_FLOOR_HAZARD, LT_FLOOR_ARROW, LT_FLOOR_STRIP, LT_FLOOR_DRAIN,
    LT_FLOOR_CONCRETE, LT_CEILING_PANEL, LT_CEILING_LIGHT, LT_CEILING_CABLES, LT_STAIR_TREAD, LT_FLOOR_LAB,
    LT_FLOOR_REACTOR, LT_FLOOR_CELL, LT_FLOOR_DOCK,
    LT_DARK_METAL, LT_HAZARD, LT_PIPE, LT_BULKHEAD, LT_BEAM, LT_CABLES, LT_FLOOR_LIGHT, LT_PLATE_SIDE,
    LT_WALL_GRAPH, LT_WALL_GRAPH_F1, LT_WALL_VITALS, LT_WALL_VITALS_F1, LT_SCR_GRAPH, LT_SCR_GRAPH_F1,
    LT_SCR_VITALS, LT_SCR_VITALS_F1,
    LT_COUNT
};
/* tiles that were exact copies of others */
#define LT_SERVER_RACK    LT_VAULT_PANEL
#define LT_CATWALK        LT_FLOOR_GRATE
#define LT_GENERATOR_SIDE LT_TURBINE_PANEL
/* animated screens: 2-frame strips, frame 1 sits 64 texels to the right of frame 0 */
#define LT_ANIMATED(t) ((t) >= LT_WALL_GRAPH)
#define SCREEN_FRAMES 2

/* 4bpp pages (256x256 texels = 128 bytes a row) and one 16-colour CLUT per tile */
typedef struct { unsigned char pix[LAB_NPAGES][256 * 128]; unsigned short clut[LAB_NPAGES * 16][16]; } LabTex;
extern LabTex *lab_tex;
void gen_lab_textures(float *preview_rgb);     /* fills lab_tex; preview: LAB_NPAGES x 256x256 RGB or 0 */
void gen_lab_geometry(void);                   /* fills the LabData that game/lab_data.h exposes */
void gen_lab(void);                            /* both, once */
void gen_lab_rebuild(const MapDef *m);         /* the geometry again from another map (0 = level 1) */
void lab_upload_textures(void);                /* pages + CLUTs into VRAM (game/vram.h); skips unused tiles */

#endif
