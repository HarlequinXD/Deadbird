/*
 * Deadbird - the real PS1 GPU backend (replaces engine/gpu_soft.c in the PS1 build).
 *
 * The engine and game build the same POLY ordering table on every platform. Here, instead of
 * rasterising it in software, gpu_draw_ot() walks the OT far-to-near and writes each POLY out
 * as a real GPU packet (POLY_F3/F4/G3/G4/FT3/FT4/GT3/GT4) into a packet buffer, chains the
 * packets into one display list and hands it to the GPU with DrawOTag. Untextured
 * semi-transparent primitives get a DR_TPAGE in front so the blend mode is right.
 */
#include <stdint.h>
#include <string.h>
#include <psxgpu.h>

/* the engine's RECT has the same layout as libgpu's; keep the names apart */
#define RECT ENGINE_RECT
#include "gpu.h"
#undef RECT

#define PACKET_WORDS (PRIM_MAX * 11 + 64)     /* average packet ~10 words (GT4 13, FT4 10, F3 5); overflow just drops prims */
static uint32_t packets[2][PACKET_WORDS];
static int pk_buf;
static DRAWENV draw;
static int draw_dither = 1;
static uint32_t *pending;

void gpu_init(void)
{
    /* ResetGraph / video mode are done by the platform layer before eng_init() */
}

u16 *gpu_vram(void) { return 0; }            /* VRAM is not CPU-visible on the real console */

void gpu_load_image(const ENGINE_RECT *r, const u16 *data)
{
    static uint32_t buf[2048];                /* DMA needs 4-byte aligned source data: 4096 halfwords */
    int y = 0, rows = r->w > 0 ? 4096 / r->w : 1;
    if (rows < 1) rows = 1;
    while (y < r->h) {                        /* big blocks go up in bands */
        RECT rr;
        int n = r->h - y < rows ? r->h - y : rows;
        memcpy(buf, data + (size_t)y * r->w, (size_t)(n * r->w) * 2);
        rr.x = r->x; rr.y = (short)(r->y + y); rr.w = r->w; rr.h = (short)n;
        LoadImage(&rr, buf);
        DrawSync(0);
        y += n;
    }
}

void gpu_fill(const ENGINE_RECT *r, u8 cr, u8 cg, u8 cb)
{
    static FILL f;
    setFill(&f);
    setRGB0(&f, cr, cg, cb);
    setXY0(&f, r->x, r->y);
    setWH(&f, r->w, r->h);
    termPrim(&f);
    DrawPrim((const uint32_t *)&f);
    DrawSync(0);
}

void gpu_set_draw_env(int x, int y, int w, int h, int ofx, int ofy, int dither)
{
    SetDefDrawEnv(&draw, x, y, w, h);
    draw.ofs[0] = (int16_t)(ofx - x);
    draw.ofs[1] = (int16_t)(ofy - y);
    draw.dtd = (uint8_t)(dither ? 1 : 0);
    draw.isbg = 0;
    draw_dither = dither;
    /* applied by gpu_psx_submit() together with this frame's list, so the CPU never waits here */
}

void ot_clear(FrameBuf *fb)
{
    memset(fb->ot, 0, sizeof(fb->ot));
    fb->nprims = 0;
}

POLY *prim_alloc(FrameBuf *fb)
{
    if (fb->nprims >= PRIM_MAX) return 0;
    return &fb->prims[fb->nprims++];
}

/* append a packet of n words (tag included) and link the previous one to it */
static uint32_t *pk, *pk_end, *pk_prev;
static uint32_t *emit(int words)
{
    uint32_t *p;
    if (pk + words > pk_end) return 0;
    p = pk;
    pk += words;
    p[0] = (uint32_t)(words - 1) << 24;          /* the tag: length now, the next packet's address later */
    if (pk_prev) setaddr(pk_prev, p);
    pk_prev = p;
    return p;
}

/*
 * POLY -> GPU packet with word copies: a corner's x,y word, its r,g,b word (the GPU ignores the 4th
 * byte except in the first, where the command goes) and its u,v halfword sit in the POLY just as
 * the packet wants them (engine/gpu.h).
 */
typedef uint32_t __attribute__((may_alias)) w32;
typedef uint16_t __attribute__((may_alias)) w16;
#define XY(p, k)  (*(const w32 *)&(p)->v[k].x)
#define RGB(p, k) (*(const w32 *)&(p)->v[k].r & 0xffffff)
#define UV(p, k)  ((uint32_t)*(const w16 *)&(p)->v[k].u)

static void convert(const POLY *p)
{
    int quad = (p->code & PF_QUAD) != 0, tex = (p->code & PF_TEX) != 0;
    int gour = (p->code & PF_GOURAUD) != 0, semi = (p->code & PF_SEMI) != 0;
    uint32_t flags = (semi ? 0x02 : 0) | ((tex && (p->code & PF_RAW)) ? 0x01 : 0);
    uint32_t *o;
    if (!tex && semi) {                          /* blend mode for untextured primitives */
        DR_TPAGE *t = (DR_TPAGE *)emit(2);
        if (!t) return;
        setDrawTPage(t, 0, draw_dither, p->tpage);
    }
    if (tex) {
        uint32_t clut = (uint32_t)p->clut << 16, tpage = (uint32_t)p->tpage << 16;
        if (gour) {
            if (!(o = emit(quad ? 13 : 10))) return;
            o[1] = RGB(p, 0) | ((0x34 | (quad ? 0x08 : 0) | flags) << 24);
            o[2] = XY(p, 0); o[3] = UV(p, 0) | clut;
            o[4] = RGB(p, 1); o[5] = XY(p, 1); o[6] = UV(p, 1) | tpage;
            o[7] = RGB(p, 2); o[8] = XY(p, 2); o[9] = UV(p, 2);
            if (quad) { o[10] = RGB(p, 3); o[11] = XY(p, 3); o[12] = UV(p, 3); }
        } else {
            if (!(o = emit(quad ? 10 : 8))) return;
            o[1] = RGB(p, 0) | ((0x24 | (quad ? 0x08 : 0) | flags) << 24);
            o[2] = XY(p, 0); o[3] = UV(p, 0) | clut;
            o[4] = XY(p, 1); o[5] = UV(p, 1) | tpage;
            o[6] = XY(p, 2); o[7] = UV(p, 2);
            if (quad) { o[8] = XY(p, 3); o[9] = UV(p, 3); }
        }
    } else if (gour) {
        if (!(o = emit(quad ? 9 : 7))) return;
        o[1] = RGB(p, 0) | ((0x30 | (quad ? 0x08 : 0) | flags) << 24); o[2] = XY(p, 0);
        o[3] = RGB(p, 1); o[4] = XY(p, 1);
        o[5] = RGB(p, 2); o[6] = XY(p, 2);
        if (quad) { o[7] = RGB(p, 3); o[8] = XY(p, 3); }
    } else {
        if (!(o = emit(quad ? 6 : 5))) return;
        o[1] = RGB(p, 0) | ((0x20 | (quad ? 0x08 : 0) | flags) << 24);
        o[2] = XY(p, 0); o[3] = XY(p, 1); o[4] = XY(p, 2);
        if (quad) o[5] = XY(p, 3);
    }
}

void gpu_draw_ot(const FrameBuf *fb)
{
    uint32_t *head;
    int z;
    pk_buf ^= 1;
    pk = packets[pk_buf];
    pk_end = pk + PACKET_WORDS;
    pk_prev = 0;
    /* a no-op packet as the list head, so an empty frame is still a valid list */
    head = emit(1);
    setlen(head, 0);
    for (z = OT_LEN - 1; z >= 0; z--) {
        const POLY *p;
        for (p = fb->ot[z]; p; p = p->next) convert(p);
    }
    termPrim(pk_prev);
    pending = head;          /* main.c submits it after the previous frame is finished and shown */
}

/* start the GPU on the list built by gpu_draw_ot(): the GPU draws it while the CPU builds the next
   frame (the packet buffers are double buffered, so the CPU never writes the list being drawn) */
void gpu_psx_submit(void)
{
    if (!pending) return;
    DrawOTagEnv(pending, &draw);
    pending = 0;
}
