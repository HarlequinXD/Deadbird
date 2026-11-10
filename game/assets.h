/* Packed assets (skins, meshes, UI, font, title painting): see game/assets.c */
#ifndef ASSETS_H
#define ASSETS_H

#include "model.h"

void assets_init(void);                    /* decode + upload everything except the title (once) */
extern Model asset_demon, asset_soldier, asset_saw, asset_pistol;   /* verts, prims, tpage, clut */
extern u16 asset_font_tpage, asset_font_clut;
#define FONT_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!.:-'/+?<> "
#define FONT_V0 224                        /* the font sits at v 224..255 of its page */
/* a rectangle of the authored UI atlas -> its VRAM home (tpage, clut, uv offset); 0 if unknown */
int  ui_cell(int u, int v, u16 *tpage, u16 *clut, int *du, int *dv);
/* a UI kit cell (see ui.h KIT_*): its VRAM home and size; 0 if this build doesn't have it */
int  kit_cell(int i, u16 *tpage, u16 *clut, int *u, int *v, int *w, int *h, int *add);
void assets_title_upload(void);           /* puts the title painting over the lab pages */
u16  assets_title_tpage(int tx);
u16  assets_title_clut(int tx, int ty);

#endif
