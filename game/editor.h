/*
 * The in-game level editor (not on the PS1): a level list, then a 2D sector map editor built
 * around Deadbird's own .map format (game/edmap.h) and a 3D view for painting tiles, all at the
 * PS1's 320x240 and driven by a gamepad first (touch and mouse too).
 */
#ifndef EDITOR_H
#define EDITOR_H

#include "gpu.h"
#include "map.h"

/* what the editor wants the game to do next */
enum { ED_STAY, ED_EXIT, ED_PLAYTEST };

void editor_open(int pad_style, int touch);    /* opens the level list */
void editor_resume(void);                      /* back from a play test */
void editor_style(int pad_style, int touch);   /* button names and the touch d-pad, each frame */
int  editor_update(u32 pad, u32 press, int lx, int ly, int rx, int ry);
void editor_draw(FrameBuf *fb, int t);
const MapDef *editor_playtest_map(void);       /* the compiled map after ED_PLAYTEST */
int  editor_in_3d(void);
/* input from the platform (via deadbird.c) */
enum { EP_DOWN, EP_MOVE, EP_UP, EP_HOVER };
enum { EK_TOUCH, EK_MOUSE_L, EK_MOUSE_R };
void editor_pointer(int id, int ev, int x, int y, int kind);
void editor_wheel(int steps);
void editor_char(int ch);

/* storage: engine/files.h (plain .map text files in the levels folder) */
#include "files.h"

#endif
