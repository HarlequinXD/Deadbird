/*
 * Software PS1 GPU. Reproduces the PS1 rasteriser's look:
 *   - integer vertex coordinates (vertex snapping / wobble)
 *   - affine (non perspective-correct) texture mapping
 *   - 4/8/15-bit textures read from VRAM through CLUTs, texel 0x0000 = transparent
 *   - texture * colour / 128 modulation, 15-bit output, PS1 4x4 dither on shaded pixels
 *   - the four semi-transparency modes
 *   - top/left fill rule; bottom row and right column of a polygon are not drawn
 * This file is only used on Android/PC. The PS1 build uses the hardware GPU instead.
 */
#include "gpu.h"

static u16 vram[VRAM_W * VRAM_H];   /* the emulated VRAM (512 KB) */

static struct { int x0, y0, x1, y1, ofx, ofy, dither; } env;

static const s8 DITHER[4][4] = {
    { -4,  0, -3,  1 },
    {  2, -2,  3, -1 },
    { -3,  1, -4,  0 },
    {  3, -1,  2, -2 },
};

void gpu_init(void)
{
    memset(vram, 0, sizeof(vram));
    gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
}

u16 *gpu_vram(void) { return vram; }

void gpu_load_image(const RECT *r, const u16 *data)
{
    int y;
    for (y = 0; y < r->h; y++)
        memcpy(&vram[((r->y + y) & (VRAM_H - 1)) * VRAM_W + (r->x & (VRAM_W - 1))], data + y * r->w, (size_t)r->w * 2);
}

void gpu_fill(const RECT *r, u8 cr, u8 cg, u8 cb)
{
    u16 c = (u16)((cr >> 3) | ((cg >> 3) << 5) | ((cb >> 3) << 10));
    int x, y;
    for (y = r->y; y < r->y + r->h; y++)
        for (x = r->x; x < r->x + r->w; x++)
            vram[(y & (VRAM_H - 1)) * VRAM_W + (x & (VRAM_W - 1))] = c;
}

void gpu_set_draw_env(int x, int y, int w, int h, int ofx, int ofy, int dither)
{
    env.x0 = x; env.y0 = y; env.x1 = x + w; env.y1 = y + h;
    env.ofx = ofx; env.ofy = ofy; env.dither = dither;
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

/* ------------------------------------------------------------------ rasteriser */

typedef struct { s32 x, y, u, v, r, g, b; } RV;

typedef struct {
    int tex, mod, gouraud, semi, abr, tmode, dither;
    int tpx, tpy, clx, cly;
    int fr, fg, fb;                 /* flat colour */
} RState;

static inline int clamp31(int v) { return v < 0 ? 0 : (v > 31 ? 31 : v); }

static inline u16 blend(u16 bk, u16 fg, int abr)
{
    int br = bk & 31, bg = (bk >> 5) & 31, bb = (bk >> 10) & 31;
    int fr = fg & 31, fgg = (fg >> 5) & 31, fb = (fg >> 10) & 31;
    int r, g, b;
    switch (abr) {
    case SEMI_HALF: r = (br + fr) >> 1; g = (bg + fgg) >> 1; b = (bb + fb) >> 1; break;
    case SEMI_ADD:  r = br + fr; g = bg + fgg; b = bb + fb; break;
    case SEMI_SUB:  r = br - fr; g = bg - fgg; b = bb - fb; break;
    default:        r = br + (fr >> 2); g = bg + (fgg >> 2); b = bb + (fb >> 2); break;
    }
    r = clamp31(r); g = clamp31(g); b = clamp31(b);
    return (u16)(r | (g << 5) | (b << 10) | (fg & 0x8000));
}

static inline u16 texel(const RState *s, int u, int v)
{
    int ty = (s->tpy + (v & 255)) & (VRAM_H - 1);
    u &= 255;
    switch (s->tmode) {
    case TP_4BIT: {
        u16 w = vram[ty * VRAM_W + ((s->tpx + (u >> 2)) & (VRAM_W - 1))];
        return vram[s->cly * VRAM_W + s->clx + ((w >> ((u & 3) * 4)) & 15)];
    }
    case TP_8BIT: {
        u16 w = vram[ty * VRAM_W + ((s->tpx + (u >> 1)) & (VRAM_W - 1))];
        return vram[s->cly * VRAM_W + ((s->clx + ((u & 1) ? (w >> 8) : (w & 255))) & (VRAM_W - 1))];
    }
    default:
        return vram[ty * VRAM_W + ((s->tpx + u) & (VRAM_W - 1))];
    }
}

static void span(const RState *s, int y, int xa, int xb,
                 s32 u, s32 v, s32 r, s32 g, s32 b,
                 s32 dudx, s32 dvdx, s32 drdx, s32 dgdx, s32 dbdx)
{
    u16 *row = &vram[(y & (VRAM_H - 1)) * VRAM_W];
    const s8 *dt = DITHER[y & 3];
    int x;
    for (x = xa; x < xb; x++) {
        int cr = r >> 16, cg = g >> 16, cb = b >> 16;
        int d = s->dither ? dt[x & 3] : 0;
        u16 out;
        if (s->tex) {
            u16 t = texel(s, u >> 16, v >> 16);
            if (t == 0) goto next;                              /* transparent texel */
            if (s->mod) {
                int tr = t & 31, tg = (t >> 5) & 31, tb = (t >> 10) & 31;
                /* (texel * colour) / 128 in 8-bit space, dithered, back to 5 bits */
                tr = clamp31((((tr << 3) * cr >> 7) + d) >> 3);
                tg = clamp31((((tg << 3) * cg >> 7) + d) >> 3);
                tb = clamp31((((tb << 3) * cb >> 7) + d) >> 3);
                out = (u16)(tr | (tg << 5) | (tb << 10) | (t & 0x8000));
            } else {
                out = t;
            }
            if (s->semi && (t & 0x8000)) out = blend(row[x], out, s->abr);
        } else {
            out = (u16)(clamp31((cr + d) >> 3) | (clamp31((cg + d) >> 3) << 5) | (clamp31((cb + d) >> 3) << 10));
            if (s->semi) out = blend(row[x], out, s->abr);
        }
        row[x] = out;
    next:
        u += dudx; v += dvdx; r += drdx; g += dgdx; b += dbdx;
    }
}

/* attribute gradient over the triangle plane, 16.16 */
static void grad(const RV *a, const RV *b, const RV *c, s32 ka, s32 kb, s32 kc, int64_t area, s32 *ddx, s32 *ddy)
{
    int64_t dx = (int64_t)(kb - ka) * (c->y - a->y) - (int64_t)(kc - ka) * (b->y - a->y);
    int64_t dy = (int64_t)(kc - ka) * (b->x - a->x) - (int64_t)(kb - ka) * (c->x - a->x);
    *ddx = (s32)((dx * 65536) / area);
    *ddy = (s32)((dy * 65536) / area);
}

static void tri(const RState *s, RV a, RV b, RV c)
{
    int64_t area;
    s32 dudx, dudy, dvdx, dvdy, drdx, drdy, dgdx, dgdy, dbdx, dbdy;
    const RV *p0, *p1, *p2, *t;
    int y, ystart, yend;
    int minx, maxx;

    /* draw offset */
    a.x += env.ofx; b.x += env.ofx; c.x += env.ofx;
    a.y += env.ofy; b.y += env.ofy; c.y += env.ofy;

    /* PS1 drops polygons wider than 1023 or taller than 511 */
    minx = a.x < b.x ? (a.x < c.x ? a.x : c.x) : (b.x < c.x ? b.x : c.x);
    maxx = a.x > b.x ? (a.x > c.x ? a.x : c.x) : (b.x > c.x ? b.x : c.x);
    if (maxx - minx > 1023) return;

    area = (int64_t)(b.x - a.x) * (c.y - a.y) - (int64_t)(c.x - a.x) * (b.y - a.y);
    if (area == 0) return;

    grad(&a, &b, &c, a.u, b.u, c.u, area, &dudx, &dudy);
    grad(&a, &b, &c, a.v, b.v, c.v, area, &dvdx, &dvdy);
    if (s->gouraud) {
        grad(&a, &b, &c, a.r, b.r, c.r, area, &drdx, &drdy);
        grad(&a, &b, &c, a.g, b.g, c.g, area, &dgdx, &dgdy);
        grad(&a, &b, &c, a.b, b.b, c.b, area, &dbdx, &dbdy);
    } else {
        drdx = drdy = dgdx = dgdy = dbdx = dbdy = 0;
        a.r = s->fr; a.g = s->fg; a.b = s->fb;
    }

    /* sort by y */
    p0 = &a; p1 = &b; p2 = &c;
    if (p1->y < p0->y) { t = p0; p0 = p1; p1 = t; }
    if (p2->y < p0->y) { t = p0; p0 = p2; p2 = t; }
    if (p2->y < p1->y) { t = p1; p1 = p2; p2 = t; }
    if (p2->y - p0->y > 511) return;

    ystart = p0->y < env.y0 ? env.y0 : p0->y;
    yend = p2->y > env.y1 ? env.y1 : p2->y;

    for (y = ystart; y < yend; y++) {
        /* x on the long edge (p0->p2) and the active short edge, 16.16 */
        s32 xl = (p0->x << 16) + (s32)(((int64_t)(p2->x - p0->x) * (y - p0->y) << 16) / (p2->y - p0->y));
        s32 xs;
        int xa, xb;
        s32 base_x, base_y;
        if (y < p1->y)
            xs = (p0->x << 16) + (s32)(((int64_t)(p1->x - p0->x) * (y - p0->y) << 16) / (p1->y - p0->y));
        else if (p2->y != p1->y)
            xs = (p1->x << 16) + (s32)(((int64_t)(p2->x - p1->x) * (y - p1->y) << 16) / (p2->y - p1->y));
        else
            xs = p1->x << 16;
        if (xs < xl) { s32 tt = xs; xs = xl; xl = tt; }
        xa = (xl + 0xffff) >> 16;
        xb = (xs + 0xffff) >> 16;
        if (xa < env.x0) xa = env.x0;
        if (xb > env.x1) xb = env.x1;
        if (xa >= xb) continue;
        base_x = xa - a.x; base_y = y - a.y;
        span(s, y, xa, xb,
             (a.u << 16) + base_x * dudx + base_y * dudy,
             (a.v << 16) + base_x * dvdx + base_y * dvdy,
             (a.r << 16) + base_x * drdx + base_y * drdy,
             (a.g << 16) + base_x * dgdx + base_y * dgdy,
             (a.b << 16) + base_x * dbdx + base_y * dbdy,
             dudx, dvdx, drdx, dgdx, dbdx);
    }
}

static void draw_poly(const POLY *p)
{
    RState s;
    RV v[4];
    int i, n = (p->code & PF_QUAD) ? 4 : 3;

    s.tex = (p->code & PF_TEX) != 0;
    s.mod = s.tex && !(p->code & PF_RAW);
    s.gouraud = (p->code & PF_GOURAUD) != 0;
    s.semi = (p->code & PF_SEMI) != 0;
    s.abr = (p->tpage >> 5) & 3;
    s.tmode = (p->tpage >> 7) & 3;
    s.tpx = (p->tpage & 15) * 64;
    s.tpy = ((p->tpage >> 4) & 1) * 256;
    s.clx = (p->clut & 0x3f) * 16;
    s.cly = (p->clut >> 6) & (VRAM_H - 1);
    s.fr = p->v[0].r; s.fg = p->v[0].g; s.fb = p->v[0].b;
    /* PS1 dithers shaded (gouraud) and modulated-texture pixels when dithering is on */
    s.dither = env.dither && (s.gouraud || s.mod);

    for (i = 0; i < n; i++) {
        v[i].x = p->v[i].x; v[i].y = p->v[i].y;
        v[i].u = p->v[i].u; v[i].v = p->v[i].v;
        v[i].r = p->v[i].r; v[i].g = p->v[i].g; v[i].b = p->v[i].b;
    }
    tri(&s, v[0], v[1], v[2]);
    if (n == 4) tri(&s, v[1], v[3], v[2]);
}

void gpu_draw_ot(const FrameBuf *fb)
{
    int i;
    for (i = OT_LEN - 1; i >= 0; i--) {
        const POLY *p;
        for (p = fb->ot[i]; p; p = p->next)
            draw_poly(p);
    }
}
