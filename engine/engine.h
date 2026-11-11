/*
 * The engine runtime: one API for the platforms below it and the game above it.
 *
 * Platforms (engine/platform/<name>) call eng_init once, then eng_frame at a fixed 60 Hz (and
 * eng_step for missed ticks), show eng_display_area of the VRAM, and feed input through the
 * eng_set_* / eng_touch / eng_pointer / eng_key calls. Games (see app.h) read the controls
 * with eng_in() inside their tick.
 */
#ifndef ENGINE_H
#define ENGINE_H

#include "app.h"

/* the controls as the game sees them during a tick */
typedef struct {
    u32 pad;                     /* buttons held (PAD_*): pad, keyboard and touch buttons merged */
    u32 press;                   /* buttons that went down this tick */
    int lx, ly, rx, ry;          /* analog sticks -4096..4096 (ly > 0 = up / forward) */
    int stick_on;                /* the floating touch stick: where the thumb landed, where it is */
    int stick_ox, stick_oy, stick_x, stick_y;
    int touch_ui;                /* 1 on touch screens: draw your on-screen controls */
    int pad_style;               /* button names to show: 0 PlayStation, 1 Xbox / keyboard */
    int fps;                     /* > 0: the platform's frame-rate readout is on */
    u32 ticks;                   /* ticks since start */
} Input;

/* ---- for games ---- */
const Input *eng_in(void);
void eng_rumble(int strength);           /* controller rumble this tick, 0..255 */
void eng_tap(u32 bits);                  /* press buttons for the next tick (menu taps) */

/* ---- for platforms ---- */
void eng_init(void);
void eng_frame(u32 pad);                 /* one tick + draw (pad: the platform's buttons) */
void eng_step(u32 pad);                  /* one tick, no drawing (catching up) */
void eng_display_area(int *x, int *y);   /* VRAM top-left of the frame to show (SCREEN_W x SCREEN_H) */
const App *eng_app(void);

void eng_set_touch_ui(int on);
void eng_set_pad_style(int style);
void eng_show_fps(int fps);
void eng_set_analog(int lx, int ly, int rx, int ry);     /* gamepad sticks, dead zones applied */
int  eng_rumble_level(void);                             /* 0..255, for the controller */
/* touches in frame pixels: id = finger. In pointer mode they go to the game raw; otherwise they
   drive the touch controls (buttons, floating stick, look drag). */
void eng_touch(int id, int ev, int x, int y);
void eng_touch_cancel(void);
/* mouse (id 0) and the wheel / typed text: to the game in pointer mode */
int  eng_pointer_mode(void);
void eng_pointer(int id, int ev, int x, int y, int kind);
void eng_wheel(int steps);
void eng_char(int ch);
/* keyboard: key down / up (app.h KEY_* or upper-case ASCII), mapped by the game's KeyBinds */
void eng_key(int key, int down);
void eng_keys_clear(void);
/* tests / tools: drive the touch stick directly */
void eng_set_stick(int active, int ox, int oy, int x, int y);
int  eng_ram_used(void);

#endif
