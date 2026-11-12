/* The Studio's Geometry tab: the level's sector map, edited (geom.c). */
#ifndef GEOM_H
#define GEOM_H

#include "gpu.h"

int  geom_open(const char *mapfile);                 /* 0: none / unreadable */
int  geom_create(const char *mapfile, int from_level1);
void geom_save(void);
int  geom_loaded(void);
const char *geom_file(void);
int  geom_in3d(void);
/* draw the tab in x,y,w,h (canvas units); *wants_create: 1 copy level 1, 2 empty room */
void geom_tab(float x, float y, float w, float h, int *wants_create);
void geom_draw3d(FrameBuf *fb, int t);                /* the 3D view (from the App's draw) */
void geom_set_toast(void (*say)(const char *));
int  geom_load_level(const char *name);              /* sector_world's loader for play-tests */

#endif
