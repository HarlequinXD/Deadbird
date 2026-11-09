/*
 * VRAM map: 512 x 512 halfwords = 512 KB (half the PS1's 1 MB; the software GPU allocates only this).
 *
 *   x 0..319     framebuffers 320x240 at (0,0) and (0,240)
 *   x 320..511   texture pages, 4bpp (64 halfwords = 256 texels wide each):
 *                (320,0) lab WALL1  (384,0) lab PROPS  (448,0) lab WALL2
 *                (320,256) lab FLOOR (384,256) lab TRIM+ANIM  (448,256) characters + UI
 *   rows 480..511 under the second framebuffer (x 0..111): font, icons and menu plates (32 rows tall),
 *                read through texture pages (0,256) / (64,256) at v 224..255
 *   rows 480..511, x 112..319: CLUT slots of 16 colours (13 per row, 416 in all)
 * The title painting borrows the lab pages (320..399, 0..239) while the menu is up.
 */
#ifndef GAME_VRAM_H
#define GAME_VRAM_H

#define FB_W 320
#define FB_H 240

#define LAB_PAGE_X(k) ((k) < 3 ? 320 + 64 * (k) : 320 + 64 * ((k) - 3))
#define LAB_PAGE_Y(k) ((k) < 3 ? 0 : 256)
#define CHR_PAGE_X 448
#define CHR_PAGE_Y 256
#define STRIP_Y    480                 /* 32-row strip under the second framebuffer */

/* CLUT slots */
#define CLUT_X(n) (112 + ((n) % 13) * 16)
#define CLUT_Y(n) (480 + (n) / 13)
#define CLUT_ID(n) ((u16)((CLUT_Y(n) << 6) | (CLUT_X(n) >> 4)))

#define CL_LAB(page, tile) ((page) * 16 + (tile))   /* 0..79 */
#define CL_DEMON    80
#define CL_SOLDIER  81
#define CL_SAW      82
#define CL_PISTOL   83
#define CL_FONT     84
#define CL_UI       85                 /* up to 16 UI cells */
#define CL_TITLE    101                /* 20 title tiles */
#define CL_EXTRA(n) (121 + (n))        /* tinted copies of lab tiles' CLUTs: */
enum { PAL_KEY_CYAN, PAL_KEY_AMBER, PAL_KEY_CRIMSON, PAL_DOOR_CYAN, PAL_DOOR_AMBER, PAL_DOOR_CRIMSON, PAL_COUNT };
#define PAL_KEY(c)  (PAL_KEY_CYAN + (c) - 1)     /* key colour 1..3: the machine-panel tile tinted */
#define PAL_DOOR(c) (PAL_DOOR_CYAN + (c) - 1)    /* the bulkhead tile tinted */

/* character page layout (texels within the page at CHR_PAGE_X/Y) */
#define CHR_DEMON_U    0
#define CHR_DEMON_V    0               /* 128x128 */
#define CHR_SOLDIER_U  128
#define CHR_SOLDIER_V  0               /* 128x128 */
#define CHR_SAW_U      0
#define CHR_SAW_V      128             /* 128x96 */
#define CHR_PISTOL_U   128
#define CHR_PISTOL_V   128             /* 64x64 */

#endif
