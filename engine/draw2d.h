/* Untextured 2D primitives in screen pixels: gradients, triangles. All PS1 primitives. */
#ifndef DRAW2D_H
#define DRAW2D_H

#include "gpu.h"

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define NO_SEMI (-1)               /* opaque; else SEMI_HALF / SEMI_ADD / SEMI_SUB / SEMI_QADD */

/* a rectangle shaded from top colour to bottom colour */
POLY *quad_g(FrameBuf *fb, int x0, int y0, int x1, int y1, u32 top, u32 bottom, int semi_mode, int ot);
/* a flat triangle */
POLY *tri_f(FrameBuf *fb, int x0, int y0, int x1, int y1, int x2, int y2, u32 rgb, int semi_mode, int ot);

#endif
