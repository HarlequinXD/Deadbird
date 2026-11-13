/* Deadbird - the game's own interface beyond the engine's App (engine/app.h): its flow state and
   hooks the headless tests use. Platforms talk to the engine (engine/engine.h), not to this. */
#ifndef DEADBIRD_H
#define DEADBIRD_H

#include "psx.h"

/* flow states returned by game_phase() */
enum { GS_TITLE, GS_CONTROLS, GS_PLAY, GS_PAUSE, GS_END, GS_EDITOR };
int  game_phase(void);

/* tests */
void game_debug_near_enemy(int i, s32 dist);
int  game_debug_enemy_state(int i);       /* state * 1000 + hp */
void game_debug_setup_duel(int i, s32 x, s32 z, s32 dist, int ang);
s32  game_debug_player_x(void);
int  game_debug_fatality(void);           /* cutscene frame, -2 prompt showing, -1 none */
int  game_debug_player_hp(void);
int  game_debug_goto_exit(void);          /* puts her by the level's exit (0 = it has none) */
int  game_debug_state(void);              /* gs */
void game_debug_side_cam(int on);         /* side-on camera */
void game_debug_hitbox(int on);           /* draw the chainsaw's hitbox (its edges and the nose's sweep) */
int  game_debug_player_yaw(void);         /* player facing (4096 = 360) */

#endif
