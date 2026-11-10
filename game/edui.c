/* The level editor's 2D kit (see edui.h). */
#ifndef GEN_BAKED
#include "edui.h"
#include "gte.h"

ERect eu_clip = { 0, 0, 320, SCREEN_H };                 /* (eu_begin sets it to the frame) */
static FrameBuf *FB;
#define MAXHIT 160
static ERect hits[2][MAXHIT]; static int hit_id[2][MAXHIT], nhit[2], hcur;

void eu_begin(FrameBuf *fb) { FB = fb; hcur ^= 1; nhit[hcur] = 0; eu_clip.x0 = 0; eu_clip.y0 = 0; eu_clip.x1 = SCREEN_W; eu_clip.y1 = SCREEN_H; }
FrameBuf *eu_fb(void) { return FB; }

void eu_hit(int x0, int y0, int x1, int y1, int id)
{
    if (nhit[hcur] >= MAXHIT) return;
    hits[hcur][nhit[hcur]].x0 = x0; hits[hcur][nhit[hcur]].y0 = y0; hits[hcur][nhit[hcur]].x1 = x1; hits[hcur][nhit[hcur]].y1 = y1;
    hit_id[hcur][nhit[hcur]++] = id;
}
int eu_hit_at(int x, int y)
{
    int i, b = hcur;                     /* this frame's areas if any were drawn yet, else last frame's */
    if (!nhit[b]) b ^= 1;
    for (i = nhit[b] - 1; i >= 0; i--) {
        const ERect *r = &hits[b][i];
        if (x >= r->x0 && x < r->x1 && y >= r->y0 && y < r->y1) return hit_id[b][i];
    }
    return -1;
}

static POLY *flat_prim(int semi)
{
    POLY *p = prim_alloc(FB);
    if (!p) return 0;
    memset(p, 0, sizeof(*p));
    p->code = (u8)(semi >= 0 ? PF_SEMI : 0);
    if (semi >= 0) p->tpage = get_tpage(0, semi, 0, 0);
    return p;
}
static void put_tri(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 c, int semi, int ot)
{
    POLY *p = flat_prim(semi);
    if (!p) return;
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0; p->v[1].x = (s16)x1; p->v[1].y = (s16)y1; p->v[2].x = (s16)x2; p->v[2].y = (s16)y2;
    p->v[0].r = (u8)(c >> 16); p->v[0].g = (u8)(c >> 8); p->v[0].b = (u8)c;
    ot_add(FB, ot, p);
}
static void put_quad(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, s32 x3, s32 y3, u32 c, int semi, int ot)
{
    POLY *p = flat_prim(semi);
    if (!p) return;
    p->code |= PF_QUAD;
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0; p->v[1].x = (s16)x1; p->v[1].y = (s16)y1;
    p->v[2].x = (s16)x2; p->v[2].y = (s16)y2; p->v[3].x = (s16)x3; p->v[3].y = (s16)y3;
    p->v[0].r = (u8)(c >> 16); p->v[0].g = (u8)(c >> 8); p->v[0].b = (u8)c;
    ot_add(FB, ot, p);
}

/* Liang-Barsky against the clip rectangle */
static int clip_line(s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
    long long dx = *x1 - *x0, dy = *y1 - *y0;
    long long p[4], q[4];
    long long tn = 0, td = 1, un = 1, ud = 1;              /* enter t = tn/td, leave u = un/ud */
    int i;
    p[0] = -dx; q[0] = *x0 - eu_clip.x0;
    p[1] = dx;  q[1] = eu_clip.x1 - 1 - *x0;
    p[2] = -dy; q[2] = *y0 - eu_clip.y0;
    p[3] = dy;  q[3] = eu_clip.y1 - 1 - *y0;
    for (i = 0; i < 4; i++) {
        if (p[i] == 0) { if (q[i] < 0) return 0; continue; }
        if (p[i] < 0) {          /* entering: t = q/p */
            long long n = q[i], d = p[i];
            if (d < 0) { n = -n; d = -d; }
            if (n * td > tn * d) { tn = n; td = d; }
        } else {
            long long n = q[i], d = p[i];
            if (n * ud < un * d) { un = n; ud = d; }
        }
    }
    if (tn * ud > un * td) return 0;
    {
        s32 ax = *x0, ay = *y0;
        *x0 = ax + (s32)(dx * tn / td); *y0 = ay + (s32)(dy * tn / td);
        *x1 = ax + (s32)(dx * un / ud); *y1 = ay + (s32)(dy * un / ud);
    }
    return 1;
}

void eu_line(int x0, int y0, int x1, int y1, u32 rgb, int w, int ot)
{
    s32 a = x0, b = y0, c = x1, d = y1, adx, ady;
    if (!clip_line(&a, &b, &c, &d)) return;
    adx = c > a ? c - a : a - c; ady = d > b ? d - b : b - d;
    if (w < 1) w = 1;
    if (adx >= ady) put_quad(a, b, c + (c >= a ? 1 : -1), d, a, b + w, c + (c >= a ? 1 : -1), d + w, rgb, -1, ot);
    else put_quad(a, b, a + w, b, c, d + (d >= b ? 1 : -1), c + w, d + (d >= b ? 1 : -1), rgb, -1, ot);
}
void eu_line_semi(int x0, int y0, int x1, int y1, u32 rgb, int ot)
{
    s32 a = x0, b = y0, c = x1, d = y1, adx, ady;
    if (!clip_line(&a, &b, &c, &d)) return;
    adx = c > a ? c - a : a - c; ady = d > b ? d - b : b - d;
    if (adx >= ady) put_quad(a, b, c + (c >= a ? 1 : -1), d, a, b + 1, c + (c >= a ? 1 : -1), d + 1, rgb, SEMI_ADD, ot);
    else put_quad(a, b, a + 1, b, c, d + (d >= b ? 1 : -1), c + 1, d + (d >= b ? 1 : -1), rgb, SEMI_ADD, ot);
}
void eu_rect(int x0, int y0, int x1, int y1, u32 rgb, int ot) { if (x1 > x0 && y1 > y0) put_quad(x0, y0, x1, y0, x0, y1, x1, y1, rgb, -1, ot); }
void eu_rect_g(int x0, int y0, int x1, int y1, u32 top, u32 bot, int ot) { quad_g(FB, x0, y0, x1, y1, top, bot, NO_SEMI, ot); }
void eu_rect_semi(int x0, int y0, int x1, int y1, u32 rgb, int mode, int ot) { if (x1 > x0 && y1 > y0) put_quad(x0, y0, x1, y0, x0, y1, x1, y1, rgb, mode, ot); }
void eu_frame(int x0, int y0, int x1, int y1, u32 rgb, int ot)
{
    eu_rect(x0, y0, x1, y0 + 1, rgb, ot); eu_rect(x0, y1 - 1, x1, y1, rgb, ot);
    eu_rect(x0, y0, x0 + 1, y1, rgb, ot); eu_rect(x1 - 1, y0, x1, y1, rgb, ot);
}

/* Sutherland-Hodgman of a triangle against the clip rectangle, then a fan */
void eu_tri(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2, u32 rgb, int semi, int ot)
{
    s32 bx[2][12], by[2][12];
    int n = 3, e, cur = 0, i;
    s32 mnx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2), mxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    s32 mny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2), mxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (mxx < eu_clip.x0 || mnx >= eu_clip.x1 || mxy < eu_clip.y0 || mny >= eu_clip.y1) return;
    if (mnx >= eu_clip.x0 && mxx < eu_clip.x1 && mny >= eu_clip.y0 && mxy < eu_clip.y1) { put_tri(x0, y0, x1, y1, x2, y2, rgb, semi, ot); return; }
    bx[0][0] = x0; by[0][0] = y0; bx[0][1] = x1; by[0][1] = y1; bx[0][2] = x2; by[0][2] = y2;
    for (e = 0; e < 4 && n >= 3; e++) {
        int k = 0;
        for (i = 0; i < n && k < 11; i++) {
            s32 ax = bx[cur][i], ay = by[cur][i], cx = bx[cur][(i + 1) % n], cy = by[cur][(i + 1) % n];
            s32 da = e == 0 ? ax - eu_clip.x0 : e == 1 ? eu_clip.x1 - ax : e == 2 ? ay - eu_clip.y0 : eu_clip.y1 - ay;
            s32 dc = e == 0 ? cx - eu_clip.x0 : e == 1 ? eu_clip.x1 - cx : e == 2 ? cy - eu_clip.y0 : eu_clip.y1 - cy;
            if (da >= 0) { bx[cur ^ 1][k] = ax; by[cur ^ 1][k] = ay; k++; }
            if ((da >= 0) != (dc >= 0) && k < 11) {
                long long t = da, d = (long long)da - dc;
                bx[cur ^ 1][k] = ax + (s32)((cx - ax) * t / d); by[cur ^ 1][k] = ay + (s32)((cy - ay) * t / d); k++;
            }
        }
        n = k; cur ^= 1;
    }
    for (i = 1; i + 1 < n; i++) put_tri(bx[cur][0], by[cur][0], bx[cur][i], by[cur][i], bx[cur][i + 1], by[cur][i + 1], rgb, semi, ot);
}
void eu_poly(const s32 *x, const s32 *y, int n, u32 rgb, int semi, int ot)
{
    int i;
    for (i = 1; i + 1 < n; i++) eu_tri(x[0], y[0], x[i], y[i], x[i + 1], y[i + 1], rgb, semi, ot);
}
/* circles are drawn as pixel rows (a true pixel circle, round on every side); only very big ones
   as polygons */
static void row(int x0, int x1, int y, u32 c, int semi, int ot)   /* pixels x0..x1 inclusive, clipped */
{
    if (y < eu_clip.y0 || y >= eu_clip.y1) return;
    if (x0 < eu_clip.x0) x0 = eu_clip.x0;
    if (x1 >= eu_clip.x1) x1 = eu_clip.x1 - 1;
    if (x1 < x0) return;
    put_quad(x0, y, x1 + 1, y, x0, y + 1, x1 + 1, y + 1, c, semi, ot);
}
static int half_w(int r, int dy) { int w = r; long long lim = (long long)r * r + r; if (dy < -r || dy > r) return -1; while (w >= 0 && (long long)w * w + (long long)dy * dy > lim) w--; return w; }
void eu_disc(int cx, int cy, int r, u32 rgb, int semi, int ot)
{
    int i, dy;
    if (r <= 40) {
        for (dy = -r; dy <= r; dy++) { int w = half_w(r, dy); if (w >= 0) row(cx - w, cx + w, cy + dy, rgb, semi, ot); }
        return;
    }
    for (i = 0; i < 16; i++) {
        int a0 = i * 256, a1 = (i + 1) * 256;
        eu_tri(cx, cy, cx + (isin(a0) * r >> 12), cy - (icos(a0) * r >> 12), cx + (isin(a1) * r >> 12), cy - (icos(a1) * r >> 12), rgb, semi, ot);
    }
}
void eu_ring(int cx, int cy, int r, u32 rgb, int ot)
{
    int i, n, st, dy;
    if (r <= 40) {
        for (dy = -r; dy <= r; dy++) {
            int wo = half_w(r, dy), wi = half_w(r - 1, dy);
            if (wo < 0) continue;
            if (wi < 0) { row(cx - wo, cx + wo, cy + dy, rgb, -1, ot); continue; }
            row(cx - wo, cx - wi - 1 < cx - wo ? cx - wo : cx - wi - 1, cy + dy, rgb, -1, ot);
            row(cx + wi + 1 > cx + wo ? cx + wo : cx + wi + 1, cx + wo, cy + dy, rgb, -1, ot);
        }
        return;
    }
    n = 32; st = 4096 / n;
    for (i = 0; i < n; i++) {
        int a0 = i * st, a1 = (i + 1) * st;
        eu_line(cx + (isin(a0) * r >> 12), cy - (icos(a0) * r >> 12), cx + (isin(a1) * r >> 12), cy - (icos(a1) * r >> 12), rgb, 1, ot);
    }
}

void eu_text(const char *s, int x, int y, u32 rgb, int ot) { text_rgb(FB, s, x, y, rgb, ot); }
int  eu_text_w(const char *s) { return (int)strlen(s) * 7; }
void eu_text_c(const char *s, int cx, int y, u32 rgb, int ot) { text_rgb(FB, s, cx - eu_text_w(s) / 2, y, rgb, ot); }

void eu_panel(int x0, int y0, int x1, int y1, int ot)
{
    eu_rect_g(x0 + 1, y0 + 1, x1 - 1, y1 - 1, RGB(34, 10, 8), RGB(12, 4, 6), ot);
    eu_frame(x0, y0, x1, y1, RGB(150, 46, 18), ot);
    eu_rect(x0 + 1, y0 + 1, x1 - 1, y0 + 2, RGB(255, 120, 40), ot);         /* hot top edge */
    eu_rect(x0 + 2, y0 + 2, x1 - 2, y0 + 3, RGB(110, 34, 14), ot);
}
void eu_button(int x0, int y0, int x1, int y1, const char *label, int lit, int ot)
{
    if (lit) {
        eu_rect_g(x0, y0, x1, y1, RGB(190, 60, 16), RGB(90, 18, 8), ot + 1);
        eu_frame(x0, y0, x1, y1, RGB(255, 170, 70), ot);
    } else {
        eu_rect_g(x0, y0, x1, y1, RGB(46, 18, 14), RGB(22, 8, 8), ot + 1);
        eu_frame(x0, y0, x1, y1, RGB(96, 40, 26), ot);
    }
    if (label) eu_text_c(label, (x0 + x1) / 2 + 1, (y0 + y1) / 2 - 4, lit ? RGB(255, 236, 200) : RGB(200, 160, 140), ot - 1);
}

int eu_badge_w(u32 b, int style)
{
    if (b & (PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE | PAD_SQUARE)) return 13;
    if (b & (PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2)) return 17;
    if (b & (PAD_START | PAD_SELECT)) return style ? 17 : (b & PAD_START ? 39 : 25);
    return 13;                                   /* d-pad */
}
void eu_badge(u32 b, int x, int y, int style, int ot)
{
    int cx = x + 6, cy = y + 4;
    if (b & (PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE | PAD_SQUARE)) {
        u32 col;
        if (style) {   /* Xbox: A green, B red, X blue, Y yellow (PS cross = A, circle = B, square = X, triangle = Y) */
            const char *l = b & PAD_CROSS ? "A" : b & PAD_CIRCLE ? "B" : b & PAD_SQUARE ? "X" : "Y";
            col = b & PAD_CROSS ? RGB(70, 200, 70) : b & PAD_CIRCLE ? RGB(230, 60, 50) : b & PAD_SQUARE ? RGB(60, 120, 240) : RGB(240, 200, 40);
            eu_disc(cx, cy, 6, RGB(16, 10, 10), -1, ot + 1);
            eu_ring(cx, cy, 6, col, ot);
            eu_text(l, cx - 3, cy - 3, col, ot);
            return;
        }
        eu_disc(cx, cy, 6, RGB(16, 10, 10), -1, ot + 1);
        eu_ring(cx, cy, 6, RGB(120, 110, 110), ot);
        if (b & PAD_CROSS) {   /* pixel-drawn so every glyph is symmetric */
            int k;
            col = RGB(110, 160, 255);
            for (k = -3; k <= 3; k++) { eu_rect(cx + k, cy + k, cx + k + 1, cy + k + 1, col, ot); eu_rect(cx + k, cy - k, cx + k + 1, cy - k + 1, col, ot); }
        }
        else if (b & PAD_CIRCLE) { col = RGB(255, 90, 90); eu_ring(cx, cy, 3, col, ot); }
        else if (b & PAD_TRIANGLE) {
            int k;
            col = RGB(80, 230, 170);
            for (k = 0; k <= 5; k++) { int w = k / 2 + (k & 1 ? 1 : 0); eu_rect(cx - w, cy - 3 + k, cx - w + 1, cy - 2 + k, col, ot); eu_rect(cx + w, cy - 3 + k, cx + w + 1, cy - 2 + k, col, ot); }
            eu_rect(cx - 3, cy + 3, cx + 4, cy + 4, col, ot);
        }
        else { col = RGB(255, 140, 220); eu_frame(cx - 3, cy - 3, cx + 4, cy + 4, col, ot); }
        return;
    }
    if (b & (PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2)) {
        const char *l = style ? (b & PAD_L1 ? "LB" : b & PAD_R1 ? "RB" : b & PAD_L2 ? "LT" : "RT") : (b & PAD_L1 ? "L1" : b & PAD_R1 ? "R1" : b & PAD_L2 ? "L2" : "R2");
        eu_rect(x, y - 1, x + 16, y + 10, RGB(16, 10, 10), ot + 1);
        eu_frame(x, y - 1, x + 16, y + 10, RGB(120, 110, 110), ot);
        eu_text(l, x + 2, y, RGB(220, 210, 200), ot);
        return;
    }
    if (b & (PAD_START | PAD_SELECT)) {
        const char *l = style ? (b & PAD_START ? "ME" : "VW") : (b & PAD_START ? "START" : "SEL");
        int w = eu_badge_w(b, style);
        eu_rect(x, y - 1, x + w - 1, y + 10, RGB(16, 10, 10), ot + 1);
        eu_frame(x, y - 1, x + w - 1, y + 10, RGB(120, 110, 110), ot);
        eu_text(l, x + 2, y, RGB(220, 210, 200), ot);
        if (style) {                                  /* Xbox menu / view glyphs: three bars, two squares */
            eu_rect(x + 1, y, x + w - 2, y + 9, RGB(16, 10, 10), ot - 1);
            if (b & PAD_START) { int k; for (k = 0; k < 3; k++) eu_rect(x + 4, y + 2 + k * 3, x + 13, y + 3 + k * 3, RGB(220, 210, 200), ot - 2); }
            else { eu_frame(x + 3, y + 1, x + 10, y + 6, RGB(220, 210, 200), ot - 2); eu_frame(x + 6, y + 3, x + 13, y + 8, RGB(220, 210, 200), ot - 2); }
        }
        return;
    }
    /* d-pad: a little cross, the arm(s) meant lit */
    {
        u32 dim = RGB(110, 100, 95), hot = RGB(255, 200, 120);
        int all = (b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) == (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
        eu_rect(cx - 1, cy - 1, cx + 2, cy + 2, dim, ot);
        eu_rect(cx - 1, cy - 4, cx + 2, cy - 1, all || (b & PAD_UP) ? hot : dim, ot);
        eu_rect(cx - 1, cy + 2, cx + 2, cy + 5, all || (b & PAD_DOWN) ? hot : dim, ot);
        eu_rect(cx - 4, cy - 1, cx - 1, cy + 2, all || (b & PAD_LEFT) ? hot : dim, ot);
        eu_rect(cx + 2, cy - 1, cx + 5, cy + 2, all || (b & PAD_RIGHT) ? hot : dim, ot);
    }
}

void eu_tex(int x0, int y0, int x1, int y1, u16 tpage, u16 clut, int u0, int v0, int u1, int v1, int ot)
{
    POLY *p = prim_alloc(FB);
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->code = PF_TEX | PF_QUAD | PF_RAW;
    p->tpage = tpage; p->clut = clut;
    p->v[0].x = (s16)x0; p->v[0].y = (s16)y0; p->v[0].u = (u8)u0; p->v[0].v = (u8)v0;
    p->v[1].x = (s16)x1; p->v[1].y = (s16)y0; p->v[1].u = (u8)u1; p->v[1].v = (u8)v0;
    p->v[2].x = (s16)x0; p->v[2].y = (s16)y1; p->v[2].u = (u8)u0; p->v[2].v = (u8)v1;
    p->v[3].x = (s16)x1; p->v[3].y = (s16)y1; p->v[3].u = (u8)u1; p->v[3].v = (u8)v1;
    ot_add(FB, ot, p);
}

char *eu_str(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
char *eu_int(char *o, long v)
{
    char d[20]; int n = 0;
    if (v < 0) { *o++ = '-'; v = -v; }
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 19);
    while (n) *o++ = d[--n];
    *o = 0;
    return o;
}
char *eu_m(char *o, long u)
{
    long c = (u < 0 ? -u : u) * 100, q = (c + 512) / 1024;          /* centimetres */
    if (u < 0 && q) *o++ = '-';
    o = eu_int(o, q / 100);
    if (q % 100) {
        *o++ = '.';
        *o++ = (char)('0' + (q % 100) / 10);
        if (q % 10) *o++ = (char)('0' + q % 10);
    }
    *o = 0;
    return o;
}
#endif
