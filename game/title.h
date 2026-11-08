/* Title screen: simulated PS1 fire rising from the bottom, hell sky, DEADBIRD logo, menu. */
#ifndef TITLE_H
#define TITLE_H

#include "gpu.h"

enum { MENU_VERSUS, MENU_TRAINING, MENU_MOVES, MENU_COUNT };

void title_init(void);
void title_reset(void);
/* returns the chosen menu item once confirmed (after the fade-out), else -1 */
int  title_update(u32 pad, u32 press);
void title_draw(FrameBuf *fb, int t);
/* touch: tap a menu row (screen coords) */
void title_tap(int x, int y);
/* the fire on its own (also used behind the K.O. / victory screens) */
void fire_step(int intensity);
void fire_draw(FrameBuf *fb, int y_top, int alpha);

#endif
