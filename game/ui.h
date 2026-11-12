/* Sci-fi hell UI: textured touch controls, menu plates and the title backdrop (tools/make_ui.py,
   tools/make_title_bg.py). */
#ifndef UI_H
#define UI_H

#include "gpu.h"

void ui_init(void);                 /* uploads the UI atlas */
void ui_title_bg_upload(void);      /* puts the title backdrop in VRAM (borrows the level's pages) */
void ui_title_bg(FrameBuf *fb, int bright, int ot);
/* sprite from the atlas: (u,v,uw,vh) texels scaled into (x,y,w,h); flip mirrors U; add = additive */
void ui_sprite(FrameBuf *fb, int x, int y, int w, int h, int u, int v, int uw, int vh, int r, int g, int b, int add, int flip, int ot);
void ui_sprite_rot(FrameBuf *fb, int cx, int cy, int size, int u, int v, int uw, int vh, int ang, int r, int g, int b, int add, int ot);
/* a menu plate centred on cx (width w), lit = selected; t = time for flicker */
void ui_plate(FrameBuf *fb, int cx, int y, int w, int lit, int t, int ot);

enum { UI_ICON_JUMP, UI_ICON_SPRINT, UI_ICON_CAM_L, UI_ICON_CAM_R, UI_ICON_PAUSE, UI_ICON_SAW, UI_ICON_NONE };
void ui_button(FrameBuf *fb, int cx, int cy, int r, int icon, int lit, int t);
/* the UI kit (tools/make_kit.py): HUD pieces and 9-slice panels / buttons in the touch controls' style */
enum { KIT_HB_FRAME, KIT_HB_FILL, KIT_HB_SHINE, KIT_HB_BOLT, KIT_HB_EYE, KIT_KEY_SLOT, KIT_KEY_CARD,
       KIT_PANEL, KIT_BTN, KIT_BTN_LIT, KIT_BTN_HOT, KIT_BAR, KIT_ROW, KIT_FIELD, KIT_SEL, KIT_PLAQUE, KIT_COUNT };
int  ui_kit_ok(int cell);
/* part of a cell (u0..u1, v0..v1 in the cell's own texels) stretched over x0..x1, y0..y1; flip mirrors it */
void ui_kit_part(FrameBuf *fb, int cell, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, int r, int g, int b, int flip, int ot);
void ui_kit(FrameBuf *fb, int cell, int x, int y, int r, int g, int b, int ot);          /* whole cell, 1:1 */
/* a 9-slice cell stretched over a rectangle (corners stay 1:1); 0 if the build lacks the kit */
int  ui_nine(FrameBuf *fb, int cell, int x0, int y0, int x1, int y1, int bright, int ot);
void ui_stick(FrameBuf *fb, int ox, int oy, int kx, int ky, int radius, int active, int t);

#endif
