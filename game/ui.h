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

enum { UI_ICON_JUMP, UI_ICON_SPRINT, UI_ICON_CAM_L, UI_ICON_CAM_R, UI_ICON_PAUSE };
void ui_button(FrameBuf *fb, int cx, int cy, int r, int icon, int lit, int t);
void ui_stick(FrameBuf *fb, int ox, int oy, int kx, int ky, int radius, int active, int t);

#endif
