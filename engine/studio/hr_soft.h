/* Software rasteriser for HrLists (hr_soft.c). Output pixels: bytes r, g, b, a. */
#ifndef HR_SOFT_H
#define HR_SOFT_H
#include "hires.h"
typedef struct { const u16 *vram; int stride, x, y, w, h; } HrFrameSrc;   /* the engine frame in VRAM */
void hr_soft_draw(const HrList *l, u32 *px, int w, int h, const HrFrameSrc *frame);
#endif
