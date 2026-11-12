/* The Studio's Geometry tab: Deadbird's level editor, embedded, on the project's .map (geom.c). */
#ifndef GEOM_H
#define GEOM_H

#include "gpu.h"

enum { GEOM_STAY, GEOM_PLAY, GEOM_EXIT };

int  geom_open(const char *mapfile);                 /* 0: none / unreadable */
int  geom_create(const char *mapfile, int from_level1);
void geom_save(void);
void geom_resume(void);                              /* back from a play-test */
int  geom_loaded(void);
const char *geom_file(void);
/* draw the tab in x,y,w,h (canvas units); *wants_create: 1 copy level 1, 2 empty room */
void geom_tab(float x, float y, float w, float h, int *wants_create);
int  geom_tick(void);                                /* each engine tick on the tab: GEOM_* */
void geom_draw(FrameBuf *fb, int t);                 /* the editor's picture (the App's draw) */
int  geom_pointer(int id, int ev, float x, float y, int kind);   /* canvas units; 1: the editor took it */
int  geom_back(void);                                /* Android's back: the editor's O; 1: leave it */
void geom_set_toast(void (*say)(const char *));
int  geom_load_level(const char *name);              /* sector_world's loader for play-tests */

#endif
