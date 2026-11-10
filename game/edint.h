/* Level editor internals shared by editor.c (map, menus, panels, files) and ed3d.c (3D view). */
#ifndef EDINT_H
#define EDINT_H

#include "editor.h"
#include "edmap.h"
#include "edui.h"
#include "gte.h"

extern EmMap ED;                     /* the map being edited */
extern int ed_pad_style, ed_touch, ed_t;
extern int ed_lx, ed_ly, ed_rx, ed_ry;          /* analog sticks this frame (-4096..4096) */
extern s32 ed_cx, ed_cz;                        /* the 2D cursor (map units, snapped) */
extern int ed_dirty;

/* pointer events (mouse, touch) queued by the platform this frame */
typedef struct { int id, ev, x, y, kind; } EdPtr;
extern EdPtr ed_ptr[16]; extern int ed_nptr;
extern int ed_wheel;
extern int ed_mouse_x, ed_mouse_y, ed_mouse_on;  /* the mouse over the screen (PC) */

void ed_toast(const char *msg);
void ed_undo_push(void);
void ed_changed(void);              /* after an edit: dirty, 3D needs a rebuild */
int  ed_rebuild3d(void);            /* compile + rebuild the level for the 3D view; 0 if the map won't build */
void ed_prompts(const u32 *buttons, const char *const *labels, int n);
u32  ed_prompt_taps(void);          /* buttons tapped on the prompt bar (touch / mouse) */
void ed_open_tiles(int current, void (*done)(int tile));
void ed_tile_tex(int tile, u16 *tpage, u16 *clut, int *u, int *v);
int  ed_overlay_busy(void);
int  ed_height(int sec, int ceil, int d);   /* floor / ceiling a step up (d 1) or down (-1) */
void ed_height_done(void);                   /* the next height change is a new undo step */

/* 3D view (ed3d.c) */
void ed3d_enter(s32 x, s32 z);
void ed3d_update(u32 pad, u32 press);   /* returns when the view should close via ed3d_closing */
void ed3d_draw(FrameBuf *fb, int t);
int  ed3d_closing(void);
extern int ed3d_tile;                   /* the paint tile */

#endif
