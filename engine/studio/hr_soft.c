/*
 * A software rasteriser for HrLists (engine/hires.h): the headless tests' screenshots and the
 * Windows editor, which draws its whole full-resolution UI with it every frame - so it's built for
 * speed: upright rectangles (most of a UI: panels, sprites, glyphs, the game view) are filled as
 * spans with their colour and texture coordinates stepped along, other triangles by scanline
 * spans with their attributes stepped as planes. Per-vertex colour (the AA rims are alpha ramps),
 * the UI atlas sampled nearest, the engine frame sampled nearest, blended over.
 */
#include "hr_soft.h"
#include <math.h>
#include <string.h>

typedef struct {
    const HrList *l;
    u32 *px; int w;
    int cx0, cy0, cx1, cy1;                      /* clip, pixels */
    int tex;
    const HrFrameSrc *fr;
    int white;                                   /* the atlas texel the untextured shapes point at is white */
} Ctx;

#define CH(c, s) ((float)(((c) >> (s)) & 255))

/* one pixel: colour r,g,b (0..255), alpha a (0..1), the texture at (u, v) multiplied in */
static inline void put(const Ctx *k, u32 *o, float r, float g, float b, float a, float u, float v, int textured)
{
    if (textured) {
        if (k->tex == HR_TEX_FRAME) {
            const HrFrameSrc *fr = k->fr;
            if (fr && fr->vram) {
                int tx = (int)(u * fr->w), ty = (int)(v * fr->h);
                u16 p;
                if (tx < 0) tx = 0;
                if (ty < 0) ty = 0;
                if (tx >= fr->w) tx = fr->w - 1;
                if (ty >= fr->h) ty = fr->h - 1;
                p = fr->vram[(fr->y + ty) * fr->stride + fr->x + tx];
                r *= (float)((p & 31) << 3) * (1 / 255.0f); g *= (float)((p >> 5 & 31) << 3) * (1 / 255.0f); b *= (float)((p >> 10 & 31) << 3) * (1 / 255.0f);
            } else r = g = b = 0;
        } else {
            const HrList *l = k->l;
            int tx = (int)(u * l->atlas_w), ty = (int)(v * l->atlas_h);
            u32 t;
            if (tx < 0) tx = 0;
            if (ty < 0) ty = 0;
            if (tx >= l->atlas_w) tx = l->atlas_w - 1;
            if (ty >= l->atlas_h) ty = l->atlas_h - 1;
            t = l->atlas[ty * l->atlas_w + tx];
            if (!(t >> 24)) return;
            r *= (float)(t & 255) * (1 / 255.0f); g *= (float)(t >> 8 & 255) * (1 / 255.0f); b *= (float)(t >> 16 & 255) * (1 / 255.0f); a *= (float)(t >> 24) * (1 / 255.0f);
        }
    }
    if (a <= 0.002f) return;
    if (a >= 0.998f) { *o = 0xff000000u | (u32)(int)(b + 0.5f) << 16 | (u32)(int)(g + 0.5f) << 8 | (u32)(int)(r + 0.5f); return; }
    {
        u32 d = *o;
        float dr = (float)(d & 255), dg = (float)(d >> 8 & 255), db = (float)(d >> 16 & 255);
        *o = 0xff000000u | (u32)(int)(db + (b - db) * a + 0.5f) << 16 | (u32)(int)(dg + (g - dg) * a + 0.5f) << 8 | (u32)(int)(dr + (r - dr) * a + 0.5f);
    }
}

/* sui's quads: (x0,y0)(x1,y0)(x1,y1) + (x0,y0)(x1,y1)(x0,y1), colour varying only down, u only
   across, v only down */
static int rect_pair(const HrVert *p)
{
    const HrVert *a = p, *b = p + 1, *c = p + 2, *d = p + 3, *e = p + 4, *f = p + 5;
    return a->x == d->x && a->y == d->y && c->x == e->x && c->y == e->y &&
           a->y == b->y && b->x == c->x && f->x == a->x && f->y == c->y &&
           a->rgba == b->rgba && c->rgba == f->rgba &&
           a->u == f->u && b->u == c->u && a->v == b->v && c->v == f->v;
}
static void rect(const Ctx *k, const HrVert *p)
{
    const HrVert *a = p, *c = p + 2;
    float x0 = fminf(a->x, c->x), x1 = fmaxf(a->x, c->x), y0 = fminf(a->y, c->y), y1 = fmaxf(a->y, c->y);
    float ua = a->x <= c->x ? a->u : c->u, ub = a->x <= c->x ? c->u : a->u, va = a->y <= c->y ? a->v : c->v, vb = a->y <= c->y ? c->v : a->v;
    u32 ct = a->y <= c->y ? a->rgba : c->rgba, cb = a->y <= c->y ? c->rgba : a->rgba;
    int X0 = (int)ceilf(x0 - 0.5f), X1 = (int)ceilf(x1 - 0.5f), Y0 = (int)ceilf(y0 - 0.5f), Y1 = (int)ceilf(y1 - 0.5f), x, y;
    int textured = k->tex == HR_TEX_FRAME || !(k->white && ua == ub && va == vb && ua < 0.01f && va < 0.01f);
    float du = x1 > x0 ? (ub - ua) / (x1 - x0) : 0, dv = y1 > y0 ? (vb - va) / (y1 - y0) : 0;
    if (X0 < k->cx0) X0 = k->cx0;
    if (Y0 < k->cy0) Y0 = k->cy0;
    if (X1 > k->cx1) X1 = k->cx1;
    if (Y1 > k->cy1) Y1 = k->cy1;
    if (X0 >= X1 || Y0 >= Y1) return;
    for (y = Y0; y < Y1; y++) {
        float t = y1 > y0 ? (y + 0.5f - y0) / (y1 - y0) : 0, it = 1 - t;
        float r = CH(ct, 0) * it + CH(cb, 0) * t, g = CH(ct, 8) * it + CH(cb, 8) * t, b = CH(ct, 16) * it + CH(cb, 16) * t, al = (CH(ct, 24) * it + CH(cb, 24) * t) * (1 / 255.0f);
        float v = va + (y + 0.5f - y0) * dv, u = ua + (X0 + 0.5f - x0) * du;
        u32 *o = k->px + y * k->w + X0;
        if (!textured && al >= 0.998f) {                        /* solid: a plain fill */
            u32 c = 0xff000000u | (u32)(int)(b + 0.5f) << 16 | (u32)(int)(g + 0.5f) << 8 | (u32)(int)(r + 0.5f);
            for (x = X0; x < X1; x++) *o++ = c;
            continue;
        }
        for (x = X0; x < X1; x++, o++, u += du) put(k, o, r, g, b, al, u, v, textured);
    }
}

/* any triangle: scanline spans, the attributes as planes a*x + b*y + c */
static void tri(const Ctx *k, const HrVert *A, const HrVert *B, const HrVert *C)
{
    const HrVert *v[3] = { A, B, C }, *t;
    float area = (B->x - A->x) * (C->y - A->y) - (C->x - A->x) * (B->y - A->y), at[8], ax[8], ay[8];
    int i, y, Y0, Y1, textured;
    if (fabsf(area) < 1e-6f) return;
    /* plane coefficients for r g b a u v */
    {
        float val[3][6];
        for (i = 0; i < 3; i++) {
            val[i][0] = CH(v[i]->rgba, 0); val[i][1] = CH(v[i]->rgba, 8); val[i][2] = CH(v[i]->rgba, 16); val[i][3] = CH(v[i]->rgba, 24) * (1 / 255.0f);
            val[i][4] = v[i]->u; val[i][5] = v[i]->v;
        }
        for (i = 0; i < 6; i++) {
            float d1 = val[1][i] - val[0][i], d2 = val[2][i] - val[0][i];
            ax[i] = (d1 * (C->y - A->y) - d2 * (B->y - A->y)) / area;
            ay[i] = (d2 * (B->x - A->x) - d1 * (C->x - A->x)) / area;
            at[i] = val[0][i] - ax[i] * A->x - ay[i] * A->y;
        }
    }
    textured = k->tex == HR_TEX_FRAME || !(k->white && ax[4] == 0 && ay[4] == 0 && ax[5] == 0 && ay[5] == 0 && at[4] < 0.01f && at[5] < 0.01f);
    /* sort by y */
    if (v[0]->y > v[1]->y) { t = v[0]; v[0] = v[1]; v[1] = t; }
    if (v[1]->y > v[2]->y) { t = v[1]; v[1] = v[2]; v[2] = t; }
    if (v[0]->y > v[1]->y) { t = v[0]; v[0] = v[1]; v[1] = t; }
    Y0 = (int)ceilf(v[0]->y - 0.5f); Y1 = (int)ceilf(v[2]->y - 0.5f);
    if (Y0 < k->cy0) Y0 = k->cy0;
    if (Y1 > k->cy1) Y1 = k->cy1;
    for (y = Y0; y < Y1; y++) {
        float yc = y + 0.5f, xa, xb, xl, xr, r, g, b, al, u, vv;
        int X0, X1, x;
        u32 *o;
        /* the long edge 0-2 and the short one on this side of vertex 1 */
        xa = v[0]->x + (v[2]->x - v[0]->x) * (yc - v[0]->y) / (v[2]->y - v[0]->y);
        if (yc < v[1]->y) xb = v[1]->y > v[0]->y ? v[0]->x + (v[1]->x - v[0]->x) * (yc - v[0]->y) / (v[1]->y - v[0]->y) : v[1]->x;
        else xb = v[2]->y > v[1]->y ? v[1]->x + (v[2]->x - v[1]->x) * (yc - v[1]->y) / (v[2]->y - v[1]->y) : v[1]->x;
        xl = fminf(xa, xb); xr = fmaxf(xa, xb);
        X0 = (int)ceilf(xl - 0.5f); X1 = (int)ceilf(xr - 0.5f);
        if (X0 < k->cx0) X0 = k->cx0;
        if (X1 > k->cx1) X1 = k->cx1;
        if (X0 >= X1) continue;
        {
            float xc = X0 + 0.5f;
            r = at[0] + ax[0] * xc + ay[0] * yc; g = at[1] + ax[1] * xc + ay[1] * yc; b = at[2] + ax[2] * xc + ay[2] * yc;
            al = at[3] + ax[3] * xc + ay[3] * yc; u = at[4] + ax[4] * xc + ay[4] * yc; vv = at[5] + ax[5] * xc + ay[5] * yc;
        }
        o = k->px + y * k->w + X0;
        for (x = X0; x < X1; x++, o++) {
            float a2 = al > 1 ? 1 : al;
            put(k, o, r < 0 ? 0 : r > 255 ? 255 : r, g < 0 ? 0 : g > 255 ? 255 : g, b < 0 ? 0 : b > 255 ? 255 : b, a2, u, vv, textured);
            r += ax[0]; g += ax[1]; b += ax[2]; al += ax[3]; u += ax[4]; vv += ax[5];
        }
    }
}

void hr_soft_draw(const HrList *l, u32 *px, int w, int h, const HrFrameSrc *fr)
{
    int c, i;
    u32 cl = l->clear;
    Ctx k;
    for (i = 0; i < w * h; i++) px[i] = cl;
    k.l = l; k.px = px; k.w = w; k.fr = fr;
    k.white = l->atlas && l->atlas_w > 0 && l->atlas[0] == 0xffffffffu;
    for (c = 0; c < l->ncmd; c++) {
        const HrCmd *cm = &l->cmd[c];
        int t;
        k.cx0 = 0; k.cy0 = 0; k.cx1 = w; k.cy1 = h;
        if (cm->clip[2] > cm->clip[0]) {
            k.cx0 = cm->clip[0] < 0 ? 0 : cm->clip[0]; k.cy0 = cm->clip[1] < 0 ? 0 : cm->clip[1];
            k.cx1 = cm->clip[2] > w ? w : cm->clip[2]; k.cy1 = cm->clip[3] > h ? h : cm->clip[3];
        }
        k.tex = cm->tex;
        for (t = 0; t + 2 < cm->count; ) {
            const HrVert *a = &l->v[cm->first + t];
            if (t + 5 < cm->count && rect_pair(a)) { rect(&k, a); t += 6; continue; }
            tri(&k, a, a + 1, a + 2);
            t += 3;
        }
    }
}
