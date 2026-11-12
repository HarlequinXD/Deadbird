/*
 * Full-resolution UI over (or instead of) the PS1 frame: editors and tools need crisp text and
 * shapes at the screen's real resolution. An app that wants it fills an HrList each frame
 * (engine/studio/sui.c builds them); the platform draws it with the GPU (Android) or a software
 * rasteriser (headless, Windows). Coordinates are screen pixels.
 */
#ifndef HIRES_H
#define HIRES_H

#include "psx.h"

typedef struct { float x, y, u, v; u32 rgba; } HrVert;          /* rgba: 0xAABBGGRR (bytes r,g,b,a) */
enum { HR_TEX_ATLAS, HR_TEX_FRAME };                             /* the UI atlas, or the engine's frame */
typedef struct { u16 tex, pad; s16 clip[4]; int first, count; } HrCmd;   /* clip x0,y0,x1,y1 px (x1 <= x0: none) */
typedef struct {
    const HrVert *v;
    const HrCmd  *cmd;
    int ncmd;
    u32 clear;                                                    /* background, 0xAABBGGRR */
    const u32 *atlas; int atlas_w, atlas_h;                       /* RGBA texels (bytes r, g, b, a) */
    int atlas_serial;                                             /* changes when the atlas does */
    int vw, vh;                                                   /* pixel art: drawn on a vw x vh canvas, scaled up
                                                                     with nearest filtering to the screen (0 = native) */
} HrList;

#endif
