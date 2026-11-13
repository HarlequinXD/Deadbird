/*
 * PS1 GPU model: VRAM, primitives, ordering tables, draw environment.
 *
 * The game builds primitives into a packet buffer and links them into an ordering
 * table (OT) by depth, exactly like libgpu. On Android/PC, gpu_soft.c rasterises the
 * OT into the emulated 1 MB VRAM. On PS1 the same OT is handed to the real GPU.
 */
#ifndef GPU_H
#define GPU_H

#include "psx.h"

/* primitive flags (code) */
#define PF_QUAD     0x01   /* 4 vertices in PS1 "Z" order: 0,1,2 + 1,3,2 */
#define PF_TEX      0x02   /* textured (uses tpage/clut/uv) */
#define PF_GOURAUD  0x04   /* per-vertex colour, else vertex 0 colour */
#define PF_SEMI     0x08   /* semi-transparent (mode from tpage bits 5-6) */
#define PF_RAW      0x10   /* textured without colour modulation */

/* each corner is laid out like the PS1 GPU wants it - x,y as one word, r,g,b as one word, u,v as a
   halfword - so the PS1 backend builds packets with word copies */
typedef struct POLY {
    struct POLY *next;
    u8  code, pad0;
    u16 clut;
    u16 tpage, pad1;
    struct { s16 x, y; u8 r, g, b, pad; u8 u, v; u16 pad2; } v[4];
} POLY;

/* texture page / CLUT helpers (same encoding as libgpu getTPage/getClut) */
#define TP_4BIT   0
#define TP_8BIT   1
#define TP_16BIT  2
#define SEMI_HALF 0       /* B/2 + F/2 */
#define SEMI_ADD  1       /* B + F     */
#define SEMI_SUB  2       /* B - F     */
#define SEMI_QADD 3       /* B + F/4   */
static inline u16 get_tpage(int mode, int abr, int x, int y)
{
    return (u16)(((y & 0x100) >> 4) | ((x & 0x3ff) >> 6) | ((abr & 3) << 5) | ((mode & 3) << 7));
}
static inline u16 get_clut(int x, int y) { return (u16)((y << 6) | ((x >> 4) & 0x3f)); }

typedef struct { s16 x, y, w, h; } RECT;

/* ordering table + packet buffer for one frame */
#define OT_LEN     1024
#ifndef PRIM_MAX
#define PRIM_MAX   4096      /* the PS1 build passes a smaller value to fit 2 MB */
#endif
typedef struct {
    POLY *ot[OT_LEN];
    POLY  prims[PRIM_MAX];
    int   nprims;
} FrameBuf;

void  ot_clear(FrameBuf *fb);
POLY *prim_alloc(FrameBuf *fb);                 /* NULL when the packet buffer is full */
/* 1 when every corner is past the same screen edge: the polygon can't touch the screen */
static inline int poly_off_screen(const s16 *x, const s16 *y, int n)
{
    int k, l = 1, r = 1, t = 1, b = 1;
    for (k = 0; k < n; k++) {
        if (x[k] >= 0) l = 0;
        if (x[k] < SCREEN_W) r = 0;
        if (y[k] >= 0) t = 0;
        if (y[k] < SCREEN_H) b = 0;
    }
    return l | r | t | b;
}
/* links a primitive into the OT - unless it's wholly off screen, when it's dropped (no GPU work) */
static inline void ot_add(FrameBuf *fb, int z, POLY *p)
{
    {
        int k, n = (p->code & PF_QUAD) ? 4 : 3, l = 1, r = 1, t = 1, b = 1;
        for (k = 0; k < n; k++) {
            if (p->v[k].x >= 0) l = 0;
            if (p->v[k].x < SCREEN_W) r = 0;
            if (p->v[k].y >= 0) t = 0;
            if (p->v[k].y < SCREEN_H) b = 0;
        }
        if (l | r | t | b) return;
    }
    if (z < 0) z = 0;
    if (z >= OT_LEN) z = OT_LEN - 1;
    p->next = fb->ot[z];
    fb->ot[z] = p;
}

/* GPU */
void  gpu_init(void);
u16  *gpu_vram(void);                                        /* VRAM_W x VRAM_H BGR555 */
void  gpu_load_image(const RECT *r, const u16 *data);        /* LoadImage */
void  gpu_fill(const RECT *r, u8 cr, u8 cg, u8 cb);          /* FillRect (ClearImage) */
void  gpu_set_draw_env(int x, int y, int w, int h, int ofx, int ofy, int dither);
void  gpu_draw_ot(const FrameBuf *fb);                       /* draws OT_LEN-1 .. 0 (far to near) */

#endif
