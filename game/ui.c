#include "ui.h"
#include "gte.h"
#include "hud.h"
#include "assets.h"

void ui_init(void) { assets_init(); }
void ui_title_bg_upload(void) { assets_title_upload(); }

static POLY *tex_quad(FrameBuf *fb, u16 tpage, u16 clut, int add, int ot)
{
    POLY *p = prim_alloc(fb);
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = PF_TEX | PF_QUAD | (add ? PF_SEMI : 0);
    p->tpage = add ? (u16)((tpage & ~0x60) | (SEMI_ADD << 5)) : tpage;
    p->clut = clut;
    ot_add(fb, ot, p);
    return p;
}

static void corners(POLY *p, int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, int r, int g, int b)
{
    int k;
    /* a cell that ends at the page edge would wrap to 0: stop at texel 255 (<1 texel short) */
    if (u0 > 255) u0 = 255;
    if (u1 > 255) u1 = 255;
    if (v0 > 255) v0 = 255;
    if (v1 > 255) v1 = 255;
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0; p->v[0].u = (u8)u0; p->v[0].v = (u8)v0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y0; p->v[1].u = (u8)u1; p->v[1].v = (u8)v0;
    p->v[2].x = (s16)x0; p->v[2].y = (s16)y1; p->v[2].u = (u8)u0; p->v[2].v = (u8)v1;
    p->v[3].x = (s16)x1; p->v[3].y = (s16)y1; p->v[3].u = (u8)u1; p->v[3].v = (u8)v1;
    for (k = 0; k < 4; k++) { p->v[k].r = (u8)r; p->v[k].g = (u8)g; p->v[k].b = (u8)b; }
}

void ui_title_bg(FrameBuf *fb, int bright, int ot)
{
    int tx, ty;         /* 5 x 4 tiles of 64x60, each with its own CLUT */
    for (ty = 0; ty < 4; ty++) for (tx = 0; tx < 5; tx++) {
        POLY *p = tex_quad(fb, assets_title_tpage(tx), assets_title_clut(tx, ty), 0, ot);
        int u0 = (tx & 3) * 64;
        if (!p) return;
        corners(p, tx * 64, ty * 60, tx * 64 + 64, ty * 60 + 60, u0, ty * 60, u0 + 64, ty * 60 + 60, bright, bright, bright);
    }
}

void ui_sprite(FrameBuf *fb, int x, int y, int w, int h, int u, int v, int uw, int vh, int r, int g, int b, int add, int flip, int ot)
{
    u16 tp, cl; int du, dv;
    POLY *p;
    if (!ui_cell(u, v, &tp, &cl, &du, &dv)) return;
    p = tex_quad(fb, tp, cl, add, ot);
    if (!p) return;
    u += du; v += dv;
    if (flip) corners(p, x, y, x + w, y + h, u + uw, v, u, v + vh, r, g, b);
    else corners(p, x, y, x + w, y + h, u, v, u + uw, v + vh, r, g, b);
}

void ui_sprite_rot(FrameBuf *fb, int cx, int cy, int size, int u, int v, int uw, int vh, int ang, int r, int g, int b, int add, int ot)
{
    static const int sx[4] = { -1, 1, -1, 1 }, sy[4] = { -1, -1, 1, 1 };
    u16 tp, cl; int du, dv;
    POLY *p;
    s32 c = icos(ang), s = isin(ang), h = size / 2;
    int k;
    if (!ui_cell(u, v, &tp, &cl, &du, &dv)) return;
    p = tex_quad(fb, tp, cl, add, ot);
    if (!p) return;
    u += du; v += dv;
    corners(p, 0, 0, 0, 0, u, v, u + uw, v + vh, r, g, b);
    for (k = 0; k < 4; k++) {
        s32 lx = sx[k] * h, ly = sy[k] * h;
        p->v[k].x = (s16)(cx + ((lx * c - ly * s) >> 12));
        p->v[k].y = (s16)(cy + ((lx * s + ly * c) >> 12));
    }
}

void ui_plate(FrameBuf *fb, int cx, int y, int w, int lit, int t, int ot)
{
    int x0 = cx - w / 2, x1 = cx + w / 2, cu = lit ? 40 : 0, mu = lit ? 64 : 24;
    int b = lit ? 112 + (isin(t * 150) * 16 >> 12) : 128;
    ui_sprite(fb, x0, y, 24, 32, cu, 208, 24, 32, b, b, b, 0, 0, ot);                 /* left cap: horn points left */
    ui_sprite(fb, x0 + 24, y, w - 48, 32, mu, 208, 16, 32, b, b, b, 0, 0, ot);        /* stretched middle */
    ui_sprite(fb, x1 - 24, y, 24, 32, cu, 208, 24, 32, b, b, b, 0, 1, ot);            /* right cap, mirrored */
    if (lit) {
        int d = isin(t * 120) * 3 >> 12;
        ui_sprite(fb, x0 - 30 - d, y, 24, 32, 80, 208, 24, 32, 128, 128, 128, 0, 0, ot - 1);   /* claws close in */
        ui_sprite(fb, x1 + 6 + d, y, 24, 32, 80, 208, 24, 32, 128, 128, 128, 0, 1, ot - 1);
    }
}

void ui_button(FrameBuf *fb, int cx, int cy, int r, int icon, int lit, int t)
{
    static const u8 iu[6] = { 0, 32, 64, 96, 0, 0 }, iv[6] = { 112, 112, 112, 112, 144, 176 };
    int s = (r + 7) * 2 + (lit ? 4 : 0), is = s * 3 / 4;
    int b = lit ? 128 : 112;
    if (lit) {
        int g = 110 + (isin(t * 200) * 30 >> 12);
        ui_sprite(fb, cx - s, cy - s, s * 2, s * 2, 32, 144, 32, 32, g, g / 2, g / 3, 1, 0, 2);
    }
    if (lit) ui_sprite(fb, cx - s / 2, cy - s / 2 - 2, s, s, 0, 64, 48, 48, 255, 150, 90, 0, 0, 1);      /* burning */
    else ui_sprite(fb, cx - s / 2, cy - s / 2 - 2, s, s, 0, 64, 48, 48, b, b, b, 0, 0, 1);
    ui_sprite(fb, cx - is / 2, cy - is / 2, is, is, iu[icon], iv[icon], 32, 32, lit ? 128 : 100, lit ? 128 : 92, lit ? 128 : 88, 0, 0, 0);
}

void ui_stick(FrameBuf *fb, int ox, int oy, int kx, int ky, int radius, int active, int t)
{
    int size = radius * 2 * 32 / 23, b = active ? 128 : 80, rb = active ? 150 : 60;
    ui_sprite_rot(fb, ox, oy, size * 52 / 64, 64, 144, 64, 64, -t * 6, rb / 3, rb / 6, rb / 8, 1, 3);      /* sigil */
    ui_sprite_rot(fb, ox, oy, size, 64, 0, 64, 64, t * 9, rb, rb * 2 / 3, rb / 2, 1, 2);                  /* runes */
    ui_sprite(fb, ox - size / 2, oy - size / 2, size, size, 0, 0, 64, 64, b, b, b, 0, 0, 2);               /* frame */
    if (active) ui_sprite(fb, kx - 26, ky - 26, 52, 52, 32, 144, 32, 32, 150, 70, 40, 1, 0, 1);
    ui_sprite(fb, kx - 17, ky - 17, 34, 34, 96, 64, 32, 32, b, b, b, 0, 0, 0);                            /* the eye */
}
