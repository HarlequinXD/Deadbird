/* Deadbird - platform-independent game interface. Platforms call these; nothing here knows about Android. */
#ifndef DEADBIRD_H
#define DEADBIRD_H

#include "psx.h"

void game_init(void);
/* advance one frame with the current pad state and render it into VRAM */
void game_frame(u32 pad);
/* VRAM rectangle (320x240) that should be shown this frame */
void game_display_area(int *x, int *y);
/* draw the on-screen touch pad (Android) into the frame */
void game_set_touch_ui(int on);

/* touch layout in 320x240 screen space, shared with the platform's hit testing */
typedef struct { s16 x, y, r; u32 button; } TouchButton;
#define TOUCH_DPAD_X 46
#define TOUCH_DPAD_Y 192
#define TOUCH_DPAD_R 38
extern const TouchButton touch_buttons[];
extern const int touch_button_count;
u32 touch_hit(int x, int y);       /* screen point -> pad bits */

/* static memory accounting against the 2 MB PS1 RAM budget */
int game_ram_used(void);

#endif
