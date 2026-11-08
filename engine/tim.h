/* TIM (PS1 texture image) loader: uploads pixels + CLUT to VRAM at the positions stored in the file. */
#ifndef TIM_H
#define TIM_H

#include "psx.h"

typedef struct {
    int mode;          /* TP_4BIT / TP_8BIT / TP_16BIT */
    int has_clut;
    int px, py, pw, ph; /* image rect in VRAM (pw in 16-bit units) */
    int cx, cy;        /* CLUT position */
    u16 tpage, clut;   /* ready-to-use primitive values */
} TimInfo;

/* returns 0 on success */
int tim_upload(const u8 *data, int size, TimInfo *out);

#endif
