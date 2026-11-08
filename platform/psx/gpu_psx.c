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

#define PACKET_WORDS (PRIM_MAX * 14 + 64)
static uint32_t packets[2][PACKET_WORDS];
static int pk_buf;
static DRAWENV draw;
static int draw_dither = 1;

void gpu_init(void)
{
    /* ResetGraph / video mode are done by the platform layer before game_init() */
}

u16 *gpu_vram(void) { return 0; }            /* VRAM is not CPU-visible on the real console */

void gpu_load_image(const ENGINE_RECT *r, const u16 *data)
{
    static uint32_t buf[VRAM_W / 2];          /* DMA needs 4-byte aligned source data */
    RECT rr;
    int n = r->w * r->h;
    if (n > VRAM_W) n = VRAM_W;
    memcpy(buf, data, (size_t)n * 2);
    rr.x = r->x; rr.y = r->y; rr.w = r->w; rr.h = r->h;
    LoadImage(&rr, buf);
    DrawSync(0);
}

void gpu_fill(const ENGINE_RECT *r, u8 cr, u8 cg, u8 cb)
{
    static FILL f;
    setFill(&f);
    setRGB0(&f, cr, cg, cb);
    setXY0(&f, r->x, r->y);
    setWH(&f, r->w, r->h);
    termPrim(&f);
    DrawPrim(&f);
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
    DrawSync(0);
    PutDrawEnv(&draw);
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
    if (pk_prev) setaddr(pk_prev, p);
    pk_prev = p;
    return p;
}

#define COL(dst, k, pr) setRGB##k(dst, (pr)->v[k].r, (pr)->v[k].g, (pr)->v[k].b)

static void convert(const POLY *p)
{
    int quad = (p->code & PF_QUAD) != 0, tex = (p->code & PF_TEX) != 0;
    int gour = (p->code & PF_GOURAUD) != 0, semi = (p->code & PF_SEMI) != 0;
    int raw = (p->code & PF_RAW) != 0;

    if (!tex && semi) {                          /* blend mode for untextured primitives */
        DR_TPAGE *t = (DR_TPAGE *)emit(2);
        if (!t) return;
        setDrawTPage(t, 0, draw_dither, p->tpage);
    }
    if (tex) {
        if (gour && quad) {
            POLY_GT4 *g = (POLY_GT4 *)emit(13);
            if (!g) return;
            setPolyGT4(g);
            COL(g, 0, p); COL(g, 1, p); COL(g, 2, p); COL(g, 3, p);
            setXY4(g, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y, p->v[3].x, p->v[3].y);
            setUV4(g, p->v[0].u, p->v[0].v, p->v[1].u, p->v[1].v, p->v[2].u, p->v[2].v, p->v[3].u, p->v[3].v);
            g->tpage = p->tpage; g->clut = p->clut;
            if (semi) setSemiTrans(g, 1);
            if (raw) setShadeTex(g, 1);
        } else if (gour) {
            POLY_GT3 *g = (POLY_GT3 *)emit(10);
            if (!g) return;
            setPolyGT3(g);
            COL(g, 0, p); COL(g, 1, p); COL(g, 2, p);
            setXY3(g, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y);
            setUV3(g, p->v[0].u, p->v[0].v, p->v[1].u, p->v[1].v, p->v[2].u, p->v[2].v);
            g->tpage = p->tpage; g->clut = p->clut;
            if (semi) setSemiTrans(g, 1);
            if (raw) setShadeTex(g, 1);
        } else if (quad) {
            POLY_FT4 *f = (POLY_FT4 *)emit(10);
            if (!f) return;
            setPolyFT4(f);
            COL(f, 0, p);
            setXY4(f, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y, p->v[3].x, p->v[3].y);
            setUV4(f, p->v[0].u, p->v[0].v, p->v[1].u, p->v[1].v, p->v[2].u, p->v[2].v, p->v[3].u, p->v[3].v);
            f->tpage = p->tpage; f->clut = p->clut;
            if (semi) setSemiTrans(f, 1);
            if (raw) setShadeTex(f, 1);
        } else {
            POLY_FT3 *f = (POLY_FT3 *)emit(8);
            if (!f) return;
            setPolyFT3(f);
            COL(f, 0, p);
            setXY3(f, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y);
            setUV3(f, p->v[0].u, p->v[0].v, p->v[1].u, p->v[1].v, p->v[2].u, p->v[2].v);
            f->tpage = p->tpage; f->clut = p->clut;
            if (semi) setSemiTrans(f, 1);
            if (raw) setShadeTex(f, 1);
        }
    } else if (gour) {
        if (quad) {
            POLY_G4 *g = (POLY_G4 *)emit(9);
            if (!g) return;
            setPolyG4(g);
            COL(g, 0, p); COL(g, 1, p); COL(g, 2, p); COL(g, 3, p);
            setXY4(g, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y, p->v[3].x, p->v[3].y);
            if (semi) setSemiTrans(g, 1);
        } else {
            POLY_G3 *g = (POLY_G3 *)emit(7);
            if (!g) return;
            setPolyG3(g);
            COL(g, 0, p); COL(g, 1, p); COL(g, 2, p);
            setXY3(g, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y);
            if (semi) setSemiTrans(g, 1);
        }
    } else {
        if (quad) {
            POLY_F4 *f = (POLY_F4 *)emit(6);
            if (!f) return;
            setPolyF4(f);
            COL(f, 0, p);
            setXY4(f, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y, p->v[3].x, p->v[3].y);
            if (semi) setSemiTrans(f, 1);
        } else {
            POLY_F3 *f = (POLY_F3 *)emit(5);
            if (!f) return;
            setPolyF3(f);
            COL(f, 0, p);
            setXY3(f, p->v[0].x, p->v[0].y, p->v[1].x, p->v[1].y, p->v[2].x, p->v[2].y);
            if (semi) setSemiTrans(f, 1);
        }
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
    DrawSync(0);                                 /* previous frame's list must be finished */
    DrawOTag(head);
}
