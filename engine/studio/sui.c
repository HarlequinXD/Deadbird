/* Studio UI: geometry, text and widgets on a pixel-art canvas (see sui.h). Not built for the PS1:
   floats are fine here. */
#include "sui.h"
#include <math.h>
#include <string.h>

#define SUI_PI 3.14159265f

#define MAXV  240000
#define MAXC  4096
#define MAXQ  64

static HrVert vert[MAXV];
static HrCmd  cmd[MAXC];
static int nv, nc;
static HrList list;
static const u32 *atlas; static int AW = 1, AH = 1;
static float S = 1, Wdp = 100, Hdp = 100;      /* the canvas: 1 unit = 1 pixel of it */
static float PS = 1;                           /* screen pixels per canvas pixel (input) */
static int Wpx, Hpx;
static const SuiCell *skin;
static const char *fchars; static int fnbase, fcw = 8, fch = 10, fadv = 7, fx0, fy0, fey, ffire;
static s16 clipst[16][4];
static int nclip;
static int cur_tex = -1;

/* ------------------------------------------------------------------ the app's art */
void sui_set_atlas(const u32 *rgba, int w, int h) { atlas = rgba; AW = w; AH = h; }
void sui_set_font(const char *chars, int nbase, int cw, int ch, int adv, int x0, int y0, int ey, int fire_dx)
{
    fchars = chars; fnbase = nbase; fcw = cw; fch = ch; fadv = adv; fx0 = x0; fy0 = y0; fey = ey; ffire = fire_dx;
}
void sui_set_skin(const SuiCell *cells) { skin = cells; }

static u32 gpu(u32 c)                    /* 0xAARRGGBB -> bytes r,g,b,a */
{
    return (c & 0xff00ff00u) | ((c >> 16) & 0xff) | ((c & 0xff) << 16);
}
static u32 with_alpha(u32 c, float a) { int x = (int)((c >> 24) * a); return (c & 0x00ffffffu) | ((u32)(x < 0 ? 0 : x > 255 ? 255 : x) << 24); }

static void new_cmd(int tex)
{
    HrCmd *c;
    if (nc && cmd[nc - 1].tex == tex && cmd[nc - 1].count == 0) { return; }
    if (nc >= MAXC) return;
    c = &cmd[nc++];
    c->tex = (u16)tex; c->pad = 0;
    if (nclip) memcpy(c->clip, clipst[nclip - 1], sizeof(c->clip));
    else { c->clip[0] = c->clip[1] = 0; c->clip[2] = c->clip[3] = -1; }
    c->first = nv; c->count = 0;
    cur_tex = tex;
}
static void use_tex(int tex) { if (!nc || tex != cur_tex) new_cmd(tex); }

static void v(float x, float y, float u, float vv, u32 col)
{
    if (nv >= MAXV) return;
    vert[nv].x = x; vert[nv].y = y; vert[nv].u = u; vert[nv].v = vv; vert[nv].rgba = col;
    nv++; cmd[nc - 1].count++;
}
#define WU (0.5f / AW)
#define WV (0.5f / AH)
static void tri_px(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2)
{
    use_tex(HR_TEX_ATLAS);
    v(x0, y0, WU, WV, c0); v(x1, y1, WU, WV, c1); v(x2, y2, WU, WV, c2);
}

void sui_begin(int w, int h, int density)
{
    /* the canvas: about one pixel per dp, whole screen pixels per canvas pixel when they come close */
    float d = density > 0 ? density / 100.0f : 1;
    if (d < 1) d = 1;
    PS = d;
    S = 1;
    Wpx = w; Hpx = h;
    Hdp = floorf(h / PS + 0.5f); Wdp = floorf(w * Hdp / h + 0.5f);
    nv = nc = 0; nclip = 0; cur_tex = -1;
    new_cmd(HR_TEX_ATLAS);
}
float sui_w(void) { return Wdp; }
float sui_h(void) { return Hdp; }

const HrList *sui_end(u32 clear)
{
    list.v = vert; list.cmd = cmd; list.ncmd = nc;
    list.clear = gpu(SUI_RGB(clear));
    list.atlas = atlas; list.atlas_w = AW; list.atlas_h = AH; list.atlas_serial = 1;
    list.vw = (int)Wdp; list.vh = (int)Hdp;
    return &list;
}

/* ------------------------------------------------------------------ clipping */
void sui_clip(float x, float y, float w, float h)
{
    s16 c[4];
    c[0] = (s16)floorf(x * S); c[1] = (s16)floorf(y * S); c[2] = (s16)ceilf((x + w) * S); c[3] = (s16)ceilf((y + h) * S);
    if (nclip) {
        const s16 *p = clipst[nclip - 1];
        if (c[0] < p[0]) c[0] = p[0];
        if (c[1] < p[1]) c[1] = p[1];
        if (c[2] > p[2]) c[2] = p[2];
        if (c[3] > p[3]) c[3] = p[3];
    }
    if (nclip < 16) memcpy(clipst[nclip++], c, sizeof(c));
    new_cmd(cur_tex < 0 ? HR_TEX_ATLAS : cur_tex);
}
void sui_unclip(void) { if (nclip) nclip--; new_cmd(cur_tex < 0 ? HR_TEX_ATLAS : cur_tex); }

/* ------------------------------------------------------------------ anti-aliased geometry (px) */
/* a convex polygon, with a 1 px feathered rim */
static void poly(const float *p, int n, u32 col)
{
    static float nx[256], ny[256];
    float area = 0;
    int i;
    u32 c = gpu(col), z = gpu(col & 0x00ffffffu);
    if (n < 3 || n > 256) return;
    for (i = 0; i < n; i++) { int j = (i + 1) % n; area += p[i * 2] * p[j * 2 + 1] - p[j * 2] * p[i * 2 + 1]; }
    for (i = 0; i < n; i++) {               /* outward vertex normals */
        int a = (i + n - 1) % n, b = (i + 1) % n;
        float e0x = p[i * 2] - p[a * 2], e0y = p[i * 2 + 1] - p[a * 2 + 1], e1x = p[b * 2] - p[i * 2], e1y = p[b * 2 + 1] - p[i * 2 + 1];
        float l0 = sqrtf(e0x * e0x + e0y * e0y), l1 = sqrtf(e1x * e1x + e1y * e1y), mx, my, d;
        if (l0 < 1e-4f) l0 = 1;
        if (l1 < 1e-4f) l1 = 1;
        mx = e0y / l0 + e1y / l1; my = -e0x / l0 - e1x / l1;
        if (area > 0) { mx = -mx; my = -my; }
        d = mx * mx + my * my;
        if (d < 1e-6f) { nx[i] = ny[i] = 0; continue; }
        d = 1.0f / d * 2;                    /* miter: scale so the offset edge sits 1 unit out */
        if (d > 8) d = 8;
        nx[i] = mx * d * 0.5f; ny[i] = my * d * 0.5f;
    }
    use_tex(HR_TEX_ATLAS);
    (void)z;
    for (i = 1; i + 1 < n; i++) tri_px(p[0], p[1], c, p[i * 2], p[i * 2 + 1], c, p[(i + 1) * 2], p[(i + 1) * 2 + 1], c);
    for (i = 0; i < n && 0; i++) {           /* (pixel art: hard edges, no feathered rim) */
        int j = (i + 1) % n;
        float ix = p[i * 2] - nx[i] * 0.5f, iy = p[i * 2 + 1] - ny[i] * 0.5f, jx = p[j * 2] - nx[j] * 0.5f, jy = p[j * 2 + 1] - ny[j] * 0.5f;
        float ox = p[i * 2] + nx[i] * 0.5f, oy = p[i * 2 + 1] + ny[i] * 0.5f, qx = p[j * 2] + nx[j] * 0.5f, qy = p[j * 2 + 1] + ny[j] * 0.5f;
        tri_px(ix, iy, c, jx, jy, c, qx, qy, z);
        tri_px(ix, iy, c, qx, qy, z, ox, oy, z);
    }
}

/* an open polyline of width t px with feathered sides */
static void stroke(const float *p, int n, float t, u32 col)
{
    static float nx[512], ny[512];
    float h = t * 0.5f, core = h, out = h;    /* pixel art: hard edges (out == core: the rims vanish) */
    u32 c = gpu(col), z = gpu(col & 0x00ffffffu);
    int i;
    if (n < 2 || n > 512) return;
    if (core < 0.5f) { core = out = 0.5f; }
    (void)with_alpha;
    for (i = 0; i < n; i++) {
        int a = i > 0 ? i - 1 : i, b = i < n - 1 ? i + 1 : i;
        float dx = p[b * 2] - p[a * 2], dy = p[b * 2 + 1] - p[a * 2 + 1], l = sqrtf(dx * dx + dy * dy);
        if (l < 1e-4f) l = 1;
        nx[i] = -dy / l; ny[i] = dx / l;
    }
    for (i = 0; i + 1 < n; i++) {
        int j = i + 1;
        float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[j * 2], by = p[j * 2 + 1];
        /* core */
        tri_px(ax + nx[i] * core, ay + ny[i] * core, c, bx + nx[j] * core, by + ny[j] * core, c, bx - nx[j] * core, by - ny[j] * core, c);
        tri_px(ax + nx[i] * core, ay + ny[i] * core, c, bx - nx[j] * core, by - ny[j] * core, c, ax - nx[i] * core, ay - ny[i] * core, c);
        /* the two feathered sides */
        tri_px(ax + nx[i] * core, ay + ny[i] * core, c, bx + nx[j] * core, by + ny[j] * core, c, bx + nx[j] * out, by + ny[j] * out, z);
        tri_px(ax + nx[i] * core, ay + ny[i] * core, c, bx + nx[j] * out, by + ny[j] * out, z, ax + nx[i] * out, ay + ny[i] * out, z);
        tri_px(ax - nx[i] * core, ay - ny[i] * core, c, bx - nx[j] * core, by - ny[j] * core, c, bx - nx[j] * out, by - ny[j] * out, z);
        tri_px(ax - nx[i] * core, ay - ny[i] * core, c, bx - nx[j] * out, by - ny[j] * out, z, ax - nx[i] * out, ay - ny[i] * out, z);
    }
}

static int rr_points(float *p, float x, float y, float w, float h, float r)
{
    int n = 0, k, seg, q;
    float cx[4], cy[4];
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0.5f) {
        p[0] = x; p[1] = y; p[2] = x + w; p[3] = y; p[4] = x + w; p[5] = y + h; p[6] = x; p[7] = y + h;
        return 4;
    }
    seg = (int)(r / 2.5f) + 2;
    if (seg > 10) seg = 10;
    cx[0] = x + w - r; cy[0] = y + r; cx[1] = x + w - r; cy[1] = y + h - r; cx[2] = x + r; cy[2] = y + h - r; cx[3] = x + r; cy[3] = y + r;
    for (q = 0; q < 4; q++)
        for (k = 0; k <= seg; k++) {
            float a = (float)(-SUI_PI / 2 + q * SUI_PI / 2 + (SUI_PI / 2) * k / seg);
            p[n * 2] = cx[q] + cosf(a) * r; p[n * 2 + 1] = cy[q] + sinf(a) * r; n++;
        }
    return n;
}

/* ------------------------------------------------------------------ shapes (dp) */
void sui_rect(float x, float y, float w, float h, u32 c)
{
    u32 g = gpu(c >> 24 ? c : SUI_RGB(c));
    float x0 = x * S, y0 = y * S, x1 = (x + w) * S, y1 = (y + h) * S;
    tri_px(x0, y0, g, x1, y0, g, x1, y1, g);
    tri_px(x0, y0, g, x1, y1, g, x0, y1, g);
}
void sui_gradient(float x, float y, float w, float h, u32 top, u32 bot)
{
    u32 a = gpu(top >> 24 ? top : SUI_RGB(top)), b = gpu(bot >> 24 ? bot : SUI_RGB(bot));
    float x0 = x * S, y0 = y * S, x1 = (x + w) * S, y1 = (y + h) * S;
    tri_px(x0, y0, a, x1, y0, a, x1, y1, b);
    tri_px(x0, y0, a, x1, y1, b, x0, y1, b);
}
void sui_rrect(float x, float y, float w, float h, float r, u32 c)
{
    static float p[256];
    int n = rr_points(p, x * S, y * S, w * S, h * S, r * S);
    poly(p, n, c >> 24 ? c : SUI_RGB(c));
}
void sui_rrect_line(float x, float y, float w, float h, float r, float t, u32 c)
{
    static float p[264];
    float ht = t * S * 0.5f;
    int n = rr_points(p, x * S + ht, y * S + ht, w * S - 2 * ht, h * S - 2 * ht, r * S - ht);
    p[n * 2] = p[0]; p[n * 2 + 1] = p[1];
    p[n * 2 + 2] = p[2]; p[n * 2 + 3] = p[3];
    stroke(p, n + 2, t * S, c >> 24 ? c : SUI_RGB(c));
}
void sui_circle(float cx, float cy, float r, u32 c)
{
    static float p[256];
    int n = (int)(r * S / 1.5f) + 12, i;
    if (n > 120) n = 120;
    for (i = 0; i < n; i++) { float a = (float)(2 * SUI_PI * i / n); p[i * 2] = (cx + cosf(a) * r) * S; p[i * 2 + 1] = (cy + sinf(a) * r) * S; }
    poly(p, n, c >> 24 ? c : SUI_RGB(c));
}
void sui_tri(float x0, float y0, float x1, float y1, float x2, float y2, u32 c)
{
    float p[6];
    p[0] = x0 * S; p[1] = y0 * S; p[2] = x1 * S; p[3] = y1 * S; p[4] = x2 * S; p[5] = y2 * S;
    poly(p, 3, c >> 24 ? c : SUI_RGB(c));
}
void sui_line(float x0, float y0, float x1, float y1, float t, u32 c)
{
    float p[4];
    p[0] = x0 * S; p[1] = y0 * S; p[2] = x1 * S; p[3] = y1 * S;
    stroke(p, 2, t * S, c >> 24 ? c : SUI_RGB(c));
}
void sui_polyline(const float *xy, int n, float t, u32 c)
{
    static float p[2 * 256];
    int i;
    if (n > 256) n = 256;
    for (i = 0; i < n * 2; i++) p[i] = xy[i] * S;
    stroke(p, n, t * S, c >> 24 ? c : SUI_RGB(c));
}
void sui_bezier_point(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float k, float *x, float *y)
{
    float u = 1 - k;
    *x = u * u * u * x0 + 3 * u * u * k * x1 + 3 * u * k * k * x2 + k * k * k * x3;
    *y = u * u * u * y0 + 3 * u * u * k * y1 + 3 * u * k * k * y2 + k * k * k * y3;
}
void sui_bezier(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float t, u32 c)
{
    static float p[2 * 64];
    float len = (fabsf(x3 - x0) + fabsf(y3 - y0) + fabsf(x1 - x0) + fabsf(y2 - y3)) * S;
    int n = (int)(len / 6) + 8, i;
    if (n > 63) n = 63;
    for (i = 0; i <= n; i++) {
        float x, y;
        sui_bezier_point(x0, y0, x1, y1, x2, y2, x3, y3, (float)i / n, &x, &y);
        p[i * 2] = x * S; p[i * 2 + 1] = y * S;
    }
    stroke(p, n + 1, t * S, c >> 24 ? c : SUI_RGB(c));
}
void sui_frame_image(float x, float y, float w, float h)
{
    u32 c = 0xffffffffu;
    float x0 = x * S, y0 = y * S, x1 = (x + w) * S, y1 = (y + h) * S;
    use_tex(HR_TEX_FRAME);
    v(x0, y0, 0, 0, c); v(x1, y0, 1, 0, c); v(x1, y1, 1, 1, c);
    v(x0, y0, 0, 0, c); v(x1, y1, 1, 1, c); v(x0, y1, 0, 1, c);
}

/* ------------------------------------------------------------------ sprites, 9-slices, text */
void sui_sprite(float x, float y, float w, float h, int u, int vv, int uw, int vh, u32 tint)
{
    u32 c = gpu(tint >> 24 ? tint : SUI_RGB(tint));
    float u0 = (float)u / AW, v0 = (float)vv / AH, u1 = (float)(u + uw) / AW, v1 = (float)(vv + vh) / AH;
    x = floorf(x * S + 0.5f); y = floorf(y * S + 0.5f); w = floorf(w * S + 0.5f); h = floorf(h * S + 0.5f);
    if (w <= 0 || h <= 0) return;
    use_tex(HR_TEX_ATLAS);
    v(x, y, u0, v0, c); v(x + w, y, u1, v0, c); v(x + w, y + h, u1, v1, c);
    v(x, y, u0, v0, c); v(x + w, y + h, u1, v1, c); v(x, y + h, u0, v1, c);
}

void sui_nine(int cell, float x, float y, float w, float h)
{
    const SuiCell *k;
    int c, cy, i, j;
    float xs[4], ys[4];
    int us[4], vs[4];
    if (!skin || cell < 0 || cell >= SK_COUNT) { sui_rect(x, y, w, h, C_PANEL); return; }
    k = &skin[cell];
    x = floorf(x + 0.5f); y = floorf(y + 0.5f); w = floorf(w + 0.5f); h = floorf(h + 0.5f);
    c = k->corner; cy = k->corner;
    if (c * 2 > w) c = (int)w / 2;
    if (cy * 2 > h) cy = (int)h / 2;
    xs[0] = x; xs[1] = x + c; xs[2] = x + w - c; xs[3] = x + w;
    ys[0] = y; ys[1] = y + cy; ys[2] = y + h - cy; ys[3] = y + h;
    us[0] = k->x; us[1] = k->x + c; us[2] = k->x + k->w - c; us[3] = k->x + k->w;
    vs[0] = k->y; vs[1] = k->y + cy; vs[2] = k->y + k->h - cy; vs[3] = k->y + k->h;
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            if (i == 1 && j == 1) {
                if (!k->hollow) sui_gradient(xs[1], ys[1], xs[2] - xs[1], ys[2] - ys[1], k->face_top, k->face_bottom);
                continue;
            }
            sui_sprite(xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j], us[i], vs[j], us[i + 1] - us[i], vs[j + 1] - vs[j], 0xffffff);
        }
}

static int glyph_at(int ch, int *gx, int *gy)
{
    int i;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch == '[') ch = '('; else if (ch == ']') ch = ')'; else if (ch == '&') ch = '+'; else if (ch == ';') ch = ':';
    if (!fchars) return 0;
    for (i = 0; fchars[i]; i++) if (fchars[i] == ch) break;
    if (!fchars[i]) return 0;
    if (i < fnbase) { *gx = fx0 + (i % 16) * fcw; *gy = fy0 + (i / 16) * fch; }
    else { *gx = fx0 + (i - fnbase) * fcw; *gy = fey; }
    return 1;
}
static int tscale(float size) { return size >= 30 ? 3 : size >= 18 ? 2 : 1; }
float sui_text_w(int font, float size, const char *s) { (void)font; return (float)strlen(s) * fadv * tscale(size); }
float sui_text(int font, float size, float x, float y, const char *s, u32 col)
{
    int k = tscale(size), gx, gy;
    float x0 = x;
    x = floorf(x + 0.5f); y = floorf(y + 0.5f);
    for (; *s; s++, x += fadv * k)
        if (*s != ' ' && glyph_at((u8)*s, &gx, &gy))
            sui_sprite(x, y, (float)fcw * k, (float)fch * k, gx + (font == FONT_FIRE ? ffire : 0), gy, fcw, fch, font == FONT_FIRE ? 0xffffff : col);
    return x - x0;
}
void sui_text_c(int font, float size, float cx, float y, const char *s, u32 c) { sui_text(font, size, floorf(cx - sui_text_w(font, size, s) / 2), y, s, c); }

/* ------------------------------------------------------------------ input */
static struct { int ev, id; float x, y; } q[MAXQ];
static int qh, qt;
static SuiPtr P;
static const void *active, *cap;
static float fx[4], fy[4]; static int fon[4];
static float last_spread;

void sui_pointer(int id, int ev, int x, int y)
{
    int n = (qh + 1) % MAXQ;
    if (n == qt) return;
    q[qh].ev = ev; q[qh].id = id; q[qh].x = x / PS; q[qh].y = y / PS;
    qh = n;
}

/* one press or release a frame, every move: a quick tap still shows a pressed frame */
static void take_input(void)
{
    float ox = P.x, oy = P.y;
    P.pressed = P.released = 0;
    while (qt != qh) {
        int ev = q[qt].ev, id = q[qt].id;
        if (id >= 0 && id < 4) {
            if (ev == 0) { fon[id] = 1; fx[id] = q[qt].x; fy[id] = q[qt].y; }
            else if (ev == 2) fon[id] = 0;
            else { fx[id] = q[qt].x; fy[id] = q[qt].y; }
        }
        if (id == 0 || id >= 4) {
            if (ev == 0 && (P.pressed || P.released)) break;    /* next frame */
            if (ev == 2 && P.pressed) { P.x = q[qt].x; P.y = q[qt].y; qt = (qt + 1) % MAXQ; P.down = 0; P.released = 1; break; }
            P.x = q[qt].x; P.y = q[qt].y;
            if (ev == 0) { P.down = 1; P.pressed = 1; P.sx = P.x; P.sy = P.y; P.moved = 0; }
            if (ev == 2) { P.down = 0; P.released = 1; }
        }
        qt = (qt + 1) % MAXQ;
        if (P.released) break;
    }
    P.dx = P.pressed ? 0 : P.x - ox; P.dy = P.pressed ? 0 : P.y - oy;
    if (P.down && (fabsf(P.x - P.sx) > 10 || fabsf(P.y - P.sy) > 10)) P.moved = 1;
    P.fingers = fon[0] + fon[1] + fon[2] + fon[3];
    P.pinch = 1;
    if (fon[0] && fon[1]) {
        float s = sqrtf((fx[0] - fx[1]) * (fx[0] - fx[1]) + (fy[0] - fy[1]) * (fy[0] - fy[1]));
        if (last_spread > 1 && s > 1) P.pinch = s / last_spread;
        last_spread = s;
        P.moved = 1;
    } else last_spread = 0;
}
static int muted;
static SuiPtr none;
void sui_mute(int on) { muted = on; }
const SuiPtr *sui_ptr(void) { return muted ? &none : &P; }
int  sui_in(float x, float y, float w, float h) { return !muted && P.x >= x && P.x < x + w && P.y >= y && P.y < y + h; }
int  sui_pressing(const void *id) { return active == id && P.down; }
void sui_capture(const void *id) { cap = id; }
const void *sui_captured(void) { return cap; }
int  sui_tap(const void *id, float x, float y, float w, float h)
{
    if (muted) return 0;
    if (P.pressed && sui_in(x, y, w, h) && !cap) active = id;
    if (P.released && active == id) { active = 0; return !P.moved && sui_in(x, y, w, h); }
    return 0;
}

void sui_frame_input(void)
{
    if (P.released) { active = 0; cap = 0; }
    take_input();
}

/* ------------------------------------------------------------------ widgets */
int sui_button(const void *id, float x, float y, float w, float h, const char *label, int style)
{
    int hit = sui_tap(id, x, y, w, h), held = sui_pressing(id) && sui_in(x, y, w, h);
    u32 fg = C_TEXT;
    int cell = SK_BUTTON, font = FONT_SEMIBOLD;
    switch (style) {
    case BTN_PRIMARY: cell = SK_BUTTON_HOT; fg = 0xFFF0D2; break;
    case BTN_GHOST:   cell = SK_FIELD; break;
    case BTN_TAB:     cell = -1; fg = C_MUTED; break;
    case BTN_TAB_ON:  cell = SK_BUTTON_HOT; fg = 0xFFF0D2; break;
    case BTN_DANGER:  cell = SK_BUTTON; fg = C_RED; break;
    case BTN_CHIP:    cell = SK_BUTTON; break;
    case BTN_CHIP_ON: cell = SK_BUTTON_HOT; fg = 0xFFF0D2; break;
    }
    if (held) { cell = SK_BUTTON_LIT; fg = 0x3C0A02; }
    if (cell >= 0) sui_nine(cell, x, y, w, h);
    if (label) sui_text_c(font, 12, x + w / 2, floorf(y + h / 2 - 5), label, fg);
    return hit;
}

void sui_label(float x, float y, const char *s)
{
    sui_text(FONT_FIRE, 10.5f, x, y, s, C_MUTED);
}
