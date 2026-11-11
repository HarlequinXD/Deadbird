/* Hell-themed 2D layer: fire font, bars, panels. All PS1 primitives (no floats). */
#ifndef HUD_H
#define HUD_H

#include "gpu.h"
#include "draw2d.h"

void hud_init(void);                              /* uploads the font TIM */
int  text_width(const char *s, int scale);
/* draws text at (x,y) top-left; brightness 128 = normal, up to 255 = hot */
void text(FrameBuf *fb, const char *s, int x, int y, int scale, int bright, int ot);
void text_rgb(FrameBuf *fb, const char *s, int x, int y, u32 rgb, int ot);   /* scale 1, any colour */
void text_center(FrameBuf *fb, const char *s, int cx, int y, int scale, int bright, int ot);
/* fiery text with drop shadow and per-letter heat shimmer (t = frame counter) */
void text_hell(FrameBuf *fb, const char *s, int cx, int y, int scale, int t, int ot);


/* fight HUD */
void hud_health(FrameBuf *fb, int side, int hp, int trail, const char *name, int wins, int t);
void hud_timer(FrameBuf *fb, int seconds, int t);
void hud_combo(FrameBuf *fb, int side, int hits, int life);

#endif
