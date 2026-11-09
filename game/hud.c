#include "hud.h"
#include "gte.h"
#include "assets.h"

static const char FONT[] = FONT_CHARS;

void hud_init(void) { assets_init(); }

static int glyph(char c)
{
    int i;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (i = 0; FONT[i]; i++) if (FONT[i] == c) return i;
    return -1;
}

int text_width(const char *s, int scale) { return (int)strlen(s) * 7 * scale; }

static void put_char(FrameBuf *fb, int g, int x, int y, int scale, int r, int gg, int b, int ot)
{
    POLY *p = prim_alloc(fb);
    int u = (g % 16) * 8, v = FONT_V0 + (g / 16) * 10, k;
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->code = PF_TEX | PF_QUAD;
    p->tpage = asset_font_tpage; p->clut = asset_font_clut;
    p->v[0].x = (s16)x;             p->v[0].y = (s16)y;             p->v[0].u = (u8)u;       p->v[0].v = (u8)v;
    p->v[1].x = (s16)(x + 8 * scale); p->v[1].y = (s16)y;             p->v[1].u = (u8)(u + 8); p->v[1].v = (u8)v;
    p->v[2].x = (s16)x;             p->v[2].y = (s16)(y + 10 * scale); p->v[2].u = (u8)u;       p->v[2].v = (u8)(v + 10);
    p->v[3].x = (s16)(x + 8 * scale); p->v[3].y = (s16)(y + 10 * scale); p->v[3].u = (u8)(u + 8); p->v[3].v = (u8)(v + 10);
    for (k = 0; k < 4; k++) { p->v[k].r = (u8)r; p->v[k].g = (u8)gg; p->v[k].b = (u8)b; }
    ot_add(fb, ot, p);
}

void text(FrameBuf *fb, const char *s, int x, int y, int scale, int bright, int ot)
{
    for (; *s; s++, x += 7 * scale) {
        int g = glyph(*s);
        if (g >= 0 && *s != ' ') put_char(fb, g, x, y, scale, bright, bright, bright, ot);
    }
}

void text_center(FrameBuf *fb, const char *s, int cx, int y, int scale, int bright, int ot)
{
    text(fb, s, cx - text_width(s, scale) / 2, y, scale, bright, ot);
}

void text_hell(FrameBuf *fb, const char *s, int cx, int y, int scale, int t, int ot)
{
    int x = cx - text_width(s, scale) / 2, i = 0;
    for (; *s; s++, x += 7 * scale, i++) {
        int g = glyph(*s), dy, heat;
        if (g < 0 || *s == ' ') continue;
        dy = isin(t * 40 + i * 600) * scale >> 13;               /* heat shimmer */
        heat = 175 + (isin(t * 97 + i * 1300) * 28 >> 12) + (isin(t * 23 + i * 400) * 16 >> 12);
        put_char(fb, g, x + scale, y + dy + scale * 2, scale, 20, 0, 0, ot);   /* shadow (drawn behind) */
        put_char(fb, g, x, y + dy, scale, heat, heat * 7 / 8, heat * 3 / 4, ot > 0 ? ot - 1 : 0);
    }
}

POLY *quad_g(FrameBuf *fb, int x0, int y0, int x1, int y1, u32 top, u32 bottom, int semi, int ot)
{
    POLY *p = prim_alloc(fb);
    int k;
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = PF_QUAD | PF_GOURAUD | (semi >= 0 ? PF_SEMI : 0);
    if (semi >= 0) p->tpage = get_tpage(0, semi, 0, 0);
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y0;
    p->v[2].x = (s16)x0; p->v[2].y = (s16)y1;
    p->v[3].x = (s16)x1; p->v[3].y = (s16)y1;
    for (k = 0; k < 4; k++) {
        u32 c = k < 2 ? top : bottom;
        p->v[k].r = (u8)(c >> 16); p->v[k].g = (u8)(c >> 8); p->v[k].b = (u8)c;
    }
    ot_add(fb, ot, p);
    return p;
}

POLY *tri_f(FrameBuf *fb, int x0, int y0, int x1, int y1, int x2, int y2, u32 c, int semi, int ot)
{
    POLY *p = prim_alloc(fb);
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = (u8)(semi >= 0 ? PF_SEMI : 0);
    if (semi >= 0) p->tpage = get_tpage(0, semi, 0, 0);
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y1;
    p->v[2].x = (s16)x2; p->v[2].y = (s16)y2;
    p->v[0].r = (u8)(c >> 16); p->v[0].g = (u8)(c >> 8); p->v[0].b = (u8)c;
    ot_add(fb, ot, p);
    return p;
}

/* ------------------------------------------------------------------ fight HUD */
#define BAR_W 128
#define BAR_H 9
#define BAR_Y 14

void hud_health(FrameBuf *fb, int side, int hp, int trail, const char *name, int wins, int t)
{
    /* player 1 bar drains toward the left edge, player 2 toward the right edge */
    int x0 = side ? 320 - 14 - BAR_W : 14, x1 = x0 + BAR_W, i;
    int fill = hp * BAR_W / 1000, tr = trail * BAR_W / 1000;
    int flick = 200 + (isin(t * 150) * 40 >> 12);
    int low = hp < 250 && ((t >> 3) & 1);

    /* fill: molten gradient, white-hot near the centre end */
    if (fill > 0) {
        if (!side) quad_g(fb, x1 - fill, BAR_Y, x1, BAR_Y + BAR_H, RGB(255, flick, 60), RGB(170, 24, 8), NO_SEMI, 3);
        else       quad_g(fb, x0, BAR_Y, x0 + fill, BAR_Y + BAR_H, RGB(255, flick, 60), RGB(170, 24, 8), NO_SEMI, 3);
        if (low) {
            if (!side) quad_g(fb, x1 - fill, BAR_Y, x1, BAR_Y + BAR_H, RGB(90, 0, 0), RGB(90, 0, 0), SEMI_ADD, 2);
            else       quad_g(fb, x0, BAR_Y, x0 + fill, BAR_Y + BAR_H, RGB(90, 0, 0), RGB(90, 0, 0), SEMI_ADD, 2);
        }
    }
    /* damage trail: dark blood */
    if (tr > fill) {
        if (!side) quad_g(fb, x1 - tr, BAR_Y, x1 - fill, BAR_Y + BAR_H, RGB(120, 10, 10), RGB(60, 0, 0), NO_SEMI, 3);
        else       quad_g(fb, x0 + fill, BAR_Y, x0 + tr, BAR_Y + BAR_H, RGB(120, 10, 10), RGB(60, 0, 0), NO_SEMI, 3);
    }
    /* empty socket + bone frame */
    quad_g(fb, x0, BAR_Y, x1, BAR_Y + BAR_H, RGB(18, 4, 6), RGB(8, 2, 3), NO_SEMI, 4);
    quad_g(fb, x0 - 2, BAR_Y - 2, x1 + 2, BAR_Y + BAR_H + 2, RGB(150, 120, 96), RGB(70, 44, 34), NO_SEMI, 5);
    quad_g(fb, x0 - 3, BAR_Y - 3, x1 + 3, BAR_Y + BAR_H + 3, RGB(10, 2, 2), RGB(10, 2, 2), NO_SEMI, 6);
    /* teeth/spikes along the top of the frame */
    for (i = 0; i < 9; i++) {
        int sx = x0 + 4 + i * (BAR_W - 8) / 8;
        tri_f(fb, sx - 4, BAR_Y - 2, sx + 4, BAR_Y - 2, sx, BAR_Y - 8 - (i & 1) * 2, RGB(120, 92, 74), NO_SEMI, 5);
    }
    /* horn at the outer end */
    if (!side) tri_f(fb, x0 - 3, BAR_Y - 3, x0 - 3, BAR_Y + BAR_H + 3, x0 - 12, BAR_Y - 9, RGB(140, 110, 86), NO_SEMI, 5);
    else       tri_f(fb, x1 + 3, BAR_Y - 3, x1 + 3, BAR_Y + BAR_H + 3, x1 + 12, BAR_Y - 9, RGB(140, 110, 86), NO_SEMI, 5);

    /* name and round-win flames */
    if (!side) text(fb, name, x0, BAR_Y + BAR_H + 5, 1, 150, 2);
    else       text(fb, name, x1 - text_width(name, 1), BAR_Y + BAR_H + 5, 1, 150, 2);
    for (i = 0; i < wins; i++) {
        int fx = side ? x0 + 6 + i * 12 : x1 - 6 - i * 12, fy = BAR_Y + BAR_H + 6;
        int h = 9 + (isin(t * 200 + i * 900) * 2 >> 12);
        tri_f(fb, fx - 4, fy + 9, fx + 4, fy + 9, fx, fy + 9 - h, RGB(255, 150, 30), NO_SEMI, 2);
        tri_f(fb, fx - 2, fy + 9, fx + 2, fy + 9, fx, fy + 4, RGB(255, 240, 160), NO_SEMI, 1);
    }
}

void hud_timer(FrameBuf *fb, int seconds, int t)
{
    char buf[4];
    int hot = seconds <= 10 && ((t >> 4) & 1);
    buf[0] = (char)('0' + (seconds / 10) % 10); buf[1] = (char)('0' + seconds % 10); buf[2] = 0;
    /* obsidian hexagon socket with a red rim */
    quad_g(fb, 146, 7, 174, 33, RGB(30, 6, 8), RGB(8, 2, 3), NO_SEMI, 4);
    tri_f(fb, 146, 7, 146, 33, 139, 20, RGB(30, 6, 8), NO_SEMI, 4);
    tri_f(fb, 174, 7, 174, 33, 181, 20, RGB(30, 6, 8), NO_SEMI, 4);
    quad_g(fb, 145, 5, 175, 35, RGB(170, 30, 16), RGB(80, 8, 6), NO_SEMI, 5);
    tri_f(fb, 145, 5, 145, 35, 136, 20, RGB(170, 30, 16), NO_SEMI, 5);
    tri_f(fb, 175, 5, 175, 35, 184, 20, RGB(170, 30, 16), NO_SEMI, 5);
    text_center(fb, buf, 160, 10, 2, hot ? 255 : 160, 2);
}

void hud_combo(FrameBuf *fb, int side, int hits, int life)
{
    char buf[12];
    int x = side ? 316 : 4, w;
    if (hits < 2 || life <= 0) return;
    buf[0] = (char)('0' + hits % 10);
    memcpy(buf + 1, " HITS", 6);
    w = text_width(buf, 2);
    text(fb, buf, side ? x - w : x, 64, 2, life > 60 ? 255 : 160, 1);
}
