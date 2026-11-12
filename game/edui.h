/* The level editor's 2D kit: lines, clipped polygons, hell panels, buttons, pad-button badges
   and touch/mouse hit areas, all as PS1 primitives at 320x240. */
#ifndef EDUI_H
#define EDUI_H

#include "gpu.h"
#include "hud.h"
#include "ui.h"

/* draw order (OT index: lower = in front) */
enum { L_TOP = 2, L_POP_TEXT = 6, L_POP = 10, L_POP_BG = 14, L_BAR_TEXT = 20, L_BAR = 24, L_MAP_TEXT = 40,
       L_CURSOR = 50, L_THING = 70, L_VERT = 90, L_WALL = 110, L_SEL = 105, L_PROP = 120, L_FILL = 200,
       L_GRID = 300, L_BG = 1000 };

typedef struct { int x0, y0, x1, y1; } ERect;
extern ERect eu_clip;                         /* lines and polygons are clipped to this */

void eu_begin(FrameBuf *fb);                  /* new frame: clears the hit areas */
FrameBuf *eu_fb(void);
void eu_line(int x0, int y0, int x1, int y1, u32 rgb, int w, int ot);          /* clipped */
void eu_line_semi(int x0, int y0, int x1, int y1, u32 rgb, int ot);           /* additive */
void eu_rect(int x0, int y0, int x1, int y1, u32 rgb, int ot);                 /* filled, unclipped */
void eu_rect_g(int x0, int y0, int x1, int y1, u32 top, u32 bot, int ot);
void eu_rect_semi(int x0, int y0, int x1, int y1, u32 rgb, int mode, int ot);
void eu_frame(int x0, int y0, int x1, int y1, u32 rgb, int ot);                /* 1 px outline */
void eu_poly(const s32 *x, const s32 *y, int n, u32 rgb, int semi, int ot);    /* convex or concave fan-safe: triangles in, clipped */
void eu_tri(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgb, int semi, int ot);   /* clipped */
void eu_disc(int cx, int cy, int r, u32 rgb, int semi, int ot);
void eu_ring(int cx, int cy, int r, u32 rgb, int ot);
void eu_text(const char *s, int x, int y, u32 rgb, int ot);
void eu_text_c(const char *s, int cx, int y, u32 rgb, int ot);
int  eu_text_w(const char *s);
/* a 9-slice kit sprite (ui.h KIT_PANEL, KIT_BTN ...) over a rectangle */
void eu_kit(int cell, int x0, int y0, int x1, int y1, int ot);
/* hell panel: forged iron and bone round a dark ember face */
void eu_panel(int x0, int y0, int x1, int y1, int ot);
/* a button (lit = selected); returns its width */
void eu_button(int x0, int y0, int x1, int y1, const char *label, int lit, int ot);
/* pad button badges: which PAD_* bit, style 0 PlayStation shapes, 1 Xbox letters */
void eu_badge(u32 button, int x, int y, int style, int ot);
int  eu_badge_w(u32 button, int style);
/* textured quad from VRAM (texture tiles in the palette and the 3D view's HUD) */
void eu_tex(int x0, int y0, int x1, int y1, u16 tpage, u16 clut, int u0, int v0, int u1, int v1, int ot);

/* high resolution (the Studio): with eu_hires set, what's drawn at the UI's depths (bars, rails,
   buttons, panels, text: ot <= EU_HI_OT) is recorded in frame coordinates instead of drawn into the
   PS1 frame, for the Studio to draw in its own crisp UI over the frame; the level stays in the
   frame. Texture tiles are still drawn into the frame and recorded as a window onto it. */
enum { EU_HI_OT = L_MAP_TEXT + 1 };
enum { EUC_RECT, EUC_TRI, EUC_QUAD, EUC_RECT_G, EUC_DISC, EUC_RING, EUC_TEXT, EUC_TEXT_C, EUC_KIT, EUC_TEX };
typedef struct {
    u8  kind;
    s8  semi;                     /* -1 opaque, else SEMI_* */
    s16 ot;
    s16 x[4], y[4];               /* rect: x0 y0 x1 y1 in x[0] y[0] x[1] y[1]; disc / ring: centre, x[1] = r */
    u32 c, c2;                    /* 0xRRGGBB; c2: gradient bottom; kit: the cell */
    u16 str;                      /* text: offset in eu_hi_text */
} EuCmd;
extern int eu_hires;
extern int eu_ncmd;
extern EuCmd eu_cmd[];
extern char eu_hi_text[];

/* hit areas: drawn things register a rectangle and an id; a tap is looked up against the last frame */
void eu_hit(int x0, int y0, int x1, int y1, int id);
int  eu_hit_at(int x, int y);                 /* -1 if none */

/* number text helpers (no printf) */
char *eu_int(char *o, long v);                /* writes, returns the end */
char *eu_m(char *o, long units);              /* metres, up to 2 decimals */
char *eu_str(char *o, const char *s);

#endif
