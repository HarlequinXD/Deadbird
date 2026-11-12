/*
 * Studio UI: an immediate-mode UI kit for editors (Android first, then Windows), in pixel art.
 * It draws on a canvas about one pixel per dp (a phone in landscape is ~840 x 390 of them), which
 * the platform scales up with nearest filtering: chunky, crisp pixels like the games it edits.
 * Sprites, a bitmap font and a 9-slice skin come from the app's atlas (sui_set_atlas / _font /
 * _skin). Each frame: sui_begin, draw panels and widgets (they read the pointer), sui_end.
 */
#ifndef SUI_H
#define SUI_H

#include "hires.h"

/* colours: 0xAARRGGBB as written in the design (converted for the GPU inside) */
#define SUI_RGB(c) (0xff000000u | (u32)(c))
/* text styles: the white font tinted by the colour (all four look alike in the pixel font), or fire */
enum { FONT_REGULAR, FONT_SEMIBOLD, FONT_BOLD, FONT_MONO, FONT_FIRE };

/* the app's atlas (RGBA, bytes r,g,b,a; texel (0,0) white) */
void  sui_set_atlas(const u32 *rgba, int w, int h);
/* a bitmap font in the atlas: chars[i] for i < nbase sits at (x0 + (i % 16) * cw, y0 + (i / 16) * ch),
   the rest in one row at (x0 + j * cw, extra_y); fire_dx: where the coloured copy is, relative */
void  sui_set_font(const char *chars, int nbase, int cw, int ch, int advance, int x0, int y0, int extra_y, int fire_dx);
/* 9-slice cells for the widgets: atlas rect, corner size, face gradient (centre drawn as colour) */
typedef struct { s16 x, y, w, h, corner; u32 face_top, face_bottom; u8 hollow; } SuiCell;
enum { SK_PANEL, SK_BUTTON, SK_BUTTON_LIT, SK_BUTTON_HOT, SK_BAR, SK_ROW, SK_FIELD, SK_SELECT, SK_PLAQUE, SK_COUNT };
void  sui_set_skin(const SuiCell *cells);
void  sui_nine(int cell, float x, float y, float w, float h);
void  sui_sprite(float x, float y, float w, float h, int u, int v, int uw, int vh, u32 tint);

void  sui_begin(int w_px, int h_px, int density);      /* density: px per dp x 100 */
const HrList *sui_end(u32 clear_rgb);
float sui_w(void);                                      /* screen size in dp */
float sui_h(void);

/* input: platform events in screen pixels (id = finger; 0 = the mouse / first finger) */
void  sui_pointer(int id, int ev, int x, int y);
void  sui_frame_input(void);                            /* take this frame's input: call before drawing */
typedef struct {
    float x, y;                  /* where the pointer is (dp) */
    int   down;                  /* held */
    int   pressed, released;     /* went down / up this frame */
    float sx, sy;                /* where it went down */
    float dx, dy;                /* moved since last frame */
    int   moved;                 /* dragged further than a tap allows since it went down */
    int   fingers;               /* fingers on the screen */
    float pinch;                 /* two fingers: ratio of their spread to last frame's (1 = none) */
} SuiPtr;
const SuiPtr *sui_ptr(void);
int   sui_in(float x, float y, float w, float h);       /* the pointer is inside */
void  sui_mute(int on);                                 /* 1: widgets see no pointer (under a modal) */
/* a tap that started and ended inside the box (id: anything unique and stable) */
int   sui_tap(const void *id, float x, float y, float w, float h);
int   sui_pressing(const void *id);                     /* this id holds the pointer */
void  sui_capture(const void *id);                      /* drags belong to this id until release */
const void *sui_captured(void);

/* shapes */
void  sui_rect(float x, float y, float w, float h, u32 c);
void  sui_rrect(float x, float y, float w, float h, float r, u32 c);
void  sui_rrect_line(float x, float y, float w, float h, float r, float t, u32 c);
void  sui_gradient(float x, float y, float w, float h, u32 top, u32 bottom);
void  sui_circle(float cx, float cy, float r, u32 c);
void  sui_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c);
void  sui_line(float x0, float y0, float x1, float y1, float t, u32 c);
void  sui_bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float t, u32 c);
void  sui_polyline(const float *xy, int n, float t, u32 c);     /* dp points */
void  sui_bezier_point(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float k, float *x, float *y);
void  sui_frame_image(float x, float y, float w, float h);   /* the engine's PS1 frame (game view) */
/* text: size picks the pixel scale (under 18: 1x, else 2x...), y = top; upper case; returns the width */
float sui_text(int font, float size, float x, float y, const char *s, u32 c);
float sui_text_w(int font, float size, const char *s);
void  sui_text_c(int font, float size, float cx, float y, const char *s, u32 c);   /* centred on cx */
/* clipping (nests) */
void  sui_clip(float x, float y, float w, float h);
void  sui_unclip(void);

/* widgets in the Studio's look */
int   sui_button(const void *id, float x, float y, float w, float h, const char *label, int style);   /* 1 = tapped */
enum { BTN_PLAIN, BTN_PRIMARY, BTN_GHOST, BTN_TAB, BTN_TAB_ON, BTN_DANGER, BTN_CHIP, BTN_CHIP_ON };
void  sui_label(float x, float y, const char *s);       /* small caps section label */

/* the palette: Deadbird's hell - black-red faces, bone text, molten accents */
#define C_BG      0x0B0606
#define C_PANEL   0x1A0E0C
#define C_PANEL2  0x26120E
#define C_RAISED  0x3A1A12
#define C_LINE    0x4A2418
#define C_LINE2   0x6E3622
#define C_TEXT    0xE8D2B2
#define C_MUTED   0xA8887A
#define C_ACCENT  0xFF6A2B
#define C_ACCENT2 0xFFB060
#define C_ONACC   0x2A0A04
#define C_SEL     0x3A1608
#define C_RED     0xFF5A4A
#define C_GREEN   0x9AD86A
#define C_BLUE    0x6AA8FF

#endif
