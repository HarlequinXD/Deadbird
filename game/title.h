/* Title screen: hell sky, embers, DEADBIRD logo, menu. */
#ifndef TITLE_H
#define TITLE_H

#include "gpu.h"

#ifdef GEN_BAKED
enum { MENU_PLAY, MENU_CONTROLS, MENU_COUNT, MENU_EDITOR = 99 };
#else
enum { MENU_PLAY, MENU_EDITOR, MENU_CONTROLS, MENU_COUNT };   /* the level editor isn't on the PS1 */
#endif

void title_init(void);
void title_reset(void);
/* returns the chosen menu item once confirmed (after the fade-out), else -1 */
int  title_update(u32 pad, u32 press);
void title_draw(FrameBuf *fb, int t);
/* touch: tap a menu row (screen coords) */
void title_tap(int x, int y);
#endif
