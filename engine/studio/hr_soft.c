/*
 * A software rasteriser for HrLists (engine/hires.h): the headless tests' screenshots and, later,
 * the Windows editor. Triangles with per-vertex colour (the AA rims are alpha ramps), the UI
 * atlas sampled bilinearly as coverage, the engine frame sampled nearest, blended over.
 */
#include "hr_soft.h"
#include <math.h>
#include <string.h>


/* the atlas, nearest (pixel art) */
static u32 atlas_px(const HrList *l, float u, float v)
{
    int x = (int)(u * l->atlas_w), y = (int)(v * l->atlas_h);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= l->atlas_w) x = l->atlas_w - 1;
    if (y >= l->atlas_h) y = l->atlas_h - 1;
    return l->atlas[y * l->atlas_w + x];
}

void hr_soft_draw(const HrList *l, u32 *px, int w, int h, const HrFrameSrc *fr)
{
    int c, i;
    u32 cl = l->clear;
    for (i = 0; i < w * h; i++) px[i] = cl;
    for (c = 0; c < l->ncmd; c++) {
        const HrCmd *cm = &l->cmd[c];
        int cx0 = 0, cy0 = 0, cx1 = w, cy1 = h, t;
        if (cm->clip[2] > cm->clip[0]) {
            cx0 = cm->clip[0] < 0 ? 0 : cm->clip[0]; cy0 = cm->clip[1] < 0 ? 0 : cm->clip[1];
            cx1 = cm->clip[2] > w ? w : cm->clip[2]; cy1 = cm->clip[3] > h ? h : cm->clip[3];
        }
        for (t = 0; t + 2 < cm->count; t += 3) {
            const HrVert *a = &l->v[cm->first + t], *b = a + 1, *d = a + 2;
            float area = (b->x - a->x) * (d->y - a->y) - (d->x - a->x) * (b->y - a->y);
            int x0, y0, x1, y1, x, y;
            if (fabsf(area) < 1e-6f) continue;
            x0 = (int)floorf(fminf(a->x, fminf(b->x, d->x))); x1 = (int)ceilf(fmaxf(a->x, fmaxf(b->x, d->x)));
            y0 = (int)floorf(fminf(a->y, fminf(b->y, d->y))); y1 = (int)ceilf(fmaxf(a->y, fmaxf(b->y, d->y)));
            if (x0 < cx0) x0 = cx0;
            if (y0 < cy0) y0 = cy0;
            if (x1 > cx1) x1 = cx1;
            if (y1 > cy1) y1 = cy1;
            for (y = y0; y < y1; y++)
                for (x = x0; x < x1; x++) {
                    float sx = x + 0.5f, sy = y + 0.5f;
                    float w0 = ((b->x - sx) * (d->y - sy) - (d->x - sx) * (b->y - sy)) / area;
                    float w1 = ((d->x - sx) * (a->y - sy) - (a->x - sx) * (d->y - sy)) / area;
                    float w2 = 1 - w0 - w1, r, g, bb, al, u, vv;
                    u32 *o;
                    if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
                    #define CH(vx, s) ((float)(((vx)->rgba >> (s)) & 255))
                    r = CH(a, 0) * w0 + CH(b, 0) * w1 + CH(d, 0) * w2;
                    g = CH(a, 8) * w0 + CH(b, 8) * w1 + CH(d, 8) * w2;
                    bb = CH(a, 16) * w0 + CH(b, 16) * w1 + CH(d, 16) * w2;
                    al = (CH(a, 24) * w0 + CH(b, 24) * w1 + CH(d, 24) * w2) / 255.0f;
                    u = a->u * w0 + b->u * w1 + d->u * w2; vv = a->v * w0 + b->v * w1 + d->v * w2;
                    if (cm->tex == HR_TEX_FRAME) {
                        if (fr && fr->vram) {
                            int tx = (int)(u * fr->w), ty = (int)(vv * fr->h);
                            u16 p;
                            if (tx >= fr->w) tx = fr->w - 1;
                            if (ty >= fr->h) ty = fr->h - 1;
                            p = fr->vram[(fr->y + ty) * fr->stride + fr->x + tx];
                            r *= ((p & 31) << 3) / 255.0f; g *= ((p >> 5 & 31) << 3) / 255.0f; bb *= ((p >> 10 & 31) << 3) / 255.0f;
                        } else r = g = bb = 0;
                    } else {
                        u32 t = atlas_px(l, u, vv);
                        r *= (t & 255) / 255.0f; g *= (t >> 8 & 255) / 255.0f; bb *= (t >> 16 & 255) / 255.0f; al *= (t >> 24) / 255.0f;
                    }
                    if (al <= 0.002f) continue;
                    if (al > 1) al = 1;
                    o = &px[y * w + x];
                    {
                        u32 dst = *o;
                        float dr = (float)(dst & 255), dg = (float)(dst >> 8 & 255), db = (float)(dst >> 16 & 255);
                        int nr = (int)(dr + (r - dr) * al + 0.5f), ng = (int)(dg + (g - dg) * al + 0.5f), nb = (int)(db + (bb - db) * al + 0.5f);
                        *o = 0xff000000u | (u32)nb << 16 | (u32)ng << 8 | (u32)nr;
                    }
                }
        }
    }
}
