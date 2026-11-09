/* Deadbird - platform-independent game interface. Platforms call these; nothing here knows about Android. */
#ifndef DEADBIRD_H
#define DEADBIRD_H

#include "psx.h"

void game_init(void);
/* advance one frame with the current pad state and render it into VRAM */
void game_frame(u32 pad);
/* advance one 60 Hz tick without drawing (catch-up when a frame took longer than 1/60 s) */
void game_step(u32 pad);
void game_show_fps(int fps);          /* >0: show this frame rate in the HUD (0 hides it) */
/* VRAM rectangle (320x240) that should be shown this frame */
void game_display_area(int *x, int *y);
/* draw the on-screen touch pad (Android) into the frame */
void game_set_touch_ui(int on);
/* names of the buttons in prompts: 0 = PlayStation, 1 = Xbox controller / keyboard */
void game_set_pad_style(int style);
/* tests */
void game_debug_near_enemy(int i, s32 dist);
int  game_debug_enemy_state(int i);       /* state * 1000 + hp */
int  game_debug_fatality(void);           /* cutscene frame, -2 prompt showing, -1 none */
int  game_debug_player_hp(void);
/* floating analog stick in screen pixels: origin where the thumb landed, current position */
void game_set_stick(int active, int ox, int oy, int x, int y);
/* gamepad analog sticks (Xbox controller etc.), each axis -4096..4096: left stick moves
   (ly > 0 = forward), right stick swings the camera (rx > 0 = turn right). Dead zones are the
   platform's job; pass 0,0 when centred. */
void game_set_analog(int lx, int ly, int rx, int ry);
/* controller rumble wanted this frame, 0..255 (hard landings) */
int  game_rumble(void);
/* flow states returned by game_phase() */
enum { GS_TITLE, GS_CONTROLS, GS_PLAY, GS_PAUSE };
/* look finger dragged horizontally by dx screen pixels: swings the camera */
void game_cam_drag(int dx);
/* a finger went down at a screen point (menus) */
void game_touch_tap(int x, int y);
/* current flow state (title, intro, fight, ko, ...) for tests */
int  game_phase(void);
/* test hook: side-on camera */
void game_debug_side_cam(int on);
/* test hook: player facing (4096 = 360) */
int  game_debug_player_yaw(void);

/* touch layout in 320x240 screen space, shared with the platform's hit testing */
typedef struct { s16 x, y, r; u32 button; } TouchButton;
extern const TouchButton touch_buttons[];
extern const int touch_button_count;
u32 touch_hit(int x, int y);       /* screen point -> pad bits */

/* static memory accounting against the 2 MB PS1 RAM budget */
int game_ram_used(void);

#endif
