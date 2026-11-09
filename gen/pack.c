/* Asset codec: see pack.h. One modelling routine per format runs both ways (encode / decode),
   so the two sides can never disagree. */
#include <stdlib.h>
#include <string.h>
#include "model.h"
#include "pack.h"
#include "gmath.h"

typedef struct {
    int enc;
    const pk_u8 *p, *end; pk_u32 range, code;          /* decoder */
#ifdef PACK_ENCODER
    PkEnc e;
#endif
} Coder;

#define PB 11
#define PINIT (1 << (PB - 1))

#ifdef PACK_ENCODER
static void e_byte(PkEnc *e, pk_u8 b)
{
    if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 4096; e->buf = (pk_u8 *)realloc(e->buf, (size_t)e->cap); }
    e->buf[e->n++] = b;
}
static void e_shift(PkEnc *e)
{
    if ((pk_u32)e->low < 0xff000000u || (e->low >> 32)) {
        pk_u8 carry = (pk_u8)(e->low >> 32), t = e->cache;
        do { e_byte(e, (pk_u8)(t + carry)); t = 0xff; } while (--e->csize != 0);
        e->cache = (pk_u8)((pk_u32)e->low >> 24);
    }
    e->csize++;
    e->low = (pk_u32)e->low << 8;
}
#endif

static pk_u8 rd(Coder *c) { return c->p < c->end ? *c->p++ : 0; }

static int cbit(Coder *c, pk_u16 *pr, int b)
{
    pk_u32 bound = (c->range >> PB) * *pr;
#ifdef PACK_ENCODER
    if (c->enc) {
        if (!b) { c->range = bound; *pr += ((1 << PB) - *pr) >> 5; }
        else { c->e.low += bound; c->range -= bound; *pr -= *pr >> 5; }
        while (c->range < (1u << 24)) { c->range <<= 8; e_shift(&c->e); }
        return b;
    }
#endif
    if (c->code < bound) { c->range = bound; *pr += ((1 << PB) - *pr) >> 5; b = 0; }
    else { c->code -= bound; c->range -= bound; *pr -= *pr >> 5; b = 1; }
    while (c->range < (1u << 24)) { c->range <<= 8; c->code = (c->code << 8) | rd(c); }
    return b;
}

static int cdirect(Coder *c, int nbits, int v)
{
    int out = 0;
    for (int i = nbits - 1; i >= 0; i--) {
        int b;
        c->range >>= 1;
#ifdef PACK_ENCODER
        if (c->enc) { b = (v >> i) & 1; if (b) c->e.low += c->range; }
        else
#endif
        { b = c->code >= c->range; if (b) c->code -= c->range; }
        while (c->range < (1u << 24)) {
            c->range <<= 8;
#ifdef PACK_ENCODER
            if (c->enc) e_shift(&c->e); else
#endif
            c->code = (c->code << 8) | rd(c);
        }
        out |= b << i;
    }
    return out;
}

/* adaptive Elias-gamma integers */
typedef struct { pk_u16 zero, sign, len[24], top[24]; } ICtx;
static void ictx_init(ICtx *x) { pk_u16 *p = (pk_u16 *)x; for (unsigned i = 0; i < sizeof(*x) / 2; i++) p[i] = PINIT; }

static int cuint(Coder *c, ICtx *x, int v)        /* v >= 0 */
{
    unsigned m = (unsigned)v + 1;
    int L = 0;
    if (c->enc) while ((m >> (L + 1)) != 0) L++;            /* bit length - 1 */
    int l = 0;
    while (l < 23 && cbit(c, &x->len[l], l < L)) l++;
    if (!c->enc) L = l;
    if (L == 0) return 0;
    int top = cbit(c, &x->top[L], (int)((m >> (L - 1)) & 1));
    int rest = L > 1 ? cdirect(c, L - 1, (int)(m & ((1u << (L - 1)) - 1))) : 0;
    return (int)(((1u << L) | ((unsigned)top << (L - 1)) | (unsigned)rest) - 1);
}
static int cint(Coder *c, ICtx *x, int v)
{
    if (!cbit(c, &x->zero, v != 0)) return 0;
    int s = cbit(c, &x->sign, v < 0);
    int m = cuint(c, x, (v < 0 ? -v : v) - 1) + 1;
    return s ? -m : m;
}

static void begin_dec(Coder *c, const pk_u8 *d, int n)
{
    memset(c, 0, sizeof(*c));
    c->p = d; c->end = d + n; c->range = 0xffffffffu;
    for (int i = 0; i < 5; i++) c->code = (c->code << 8) | rd(c);
}
#ifdef PACK_ENCODER
static void begin_enc(Coder *c) { memset(c, 0, sizeof(*c)); c->enc = 1; c->range = 0xffffffffu; c->e.csize = 1; }
static pk_u8 *end_enc(Coder *c, int *size) { for (int i = 0; i < 5; i++) e_shift(&c->e); *size = c->e.n; return c->e.buf; }
#endif

/* ------------------------------------------------------------------ images */
static const pk_u8 ZZ[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };
static const pk_u8 QL[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };
static const pk_u8 QC[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };

static int qstep(int chroma, int k, int qs) { int q = ((chroma ? QC : QL)[k] * qs + 50) / 100; return q < 1 ? 1 : q; }

typedef struct { pk_u16 last[2][64], nz[2][64][2]; ICtx dc[2], mag[2][8]; } BlockModel;

static void code_blocks(Coder *c, BlockModel *m, int *coef, int nb, int ch)
{
    int prev_dc = 0;
    for (int b = 0; b < nb; b++) {
        int *q = coef + b * 64, last = 0;
        q[0] = prev_dc + cint(c, &m->dc[ch], q[0] - prev_dc);
        prev_dc = q[0];
        if (c->enc) for (int k = 63; k > 0; k--) if (q[ZZ[k]]) { last = k; break; }
        {   /* last nonzero zig-zag position: 6-bit tree */
            int node = 1;
            for (int i = 5; i >= 0; i--) { int bit = cbit(c, &m->last[ch][node], (last >> i) & 1); node = node * 2 + bit; }
            last = node - 64;
        }
        int pz = 0;
        for (int k = 1; k <= last; k++) {
            int *v = &q[ZZ[k]];
            int nzb = k == last ? 1 : cbit(c, &m->nz[ch][k][pz], *v != 0);
            if (!nzb) { *v = 0; pz = 1; continue; }
            pz = 0;
            int a = c->enc ? (*v < 0 ? -*v : *v) - 1 : 0;
            int s = cbit(c, &m->mag[ch][k < 8 ? 0 : k < 32 ? 1 + (k - 8) / 8 : 4 + (k - 32) / 8].sign, *v < 0);
            a = cuint(c, &m->mag[ch][k < 8 ? 0 : k < 32 ? 1 + (k - 8) / 8 : 4 + (k - 32) / 8], a) + 1;
            *v = s ? -a : a;
        }
        if (!c->enc) for (int k = last + 1; k < 64; k++) q[ZZ[k]] = 0;
    }
}

static void code_mask(Coder *c, pk_u8 *mask, int w, int h)
{
    pk_u16 pr[8];
    for (int i = 0; i < 8; i++) pr[i] = PINIT;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int l = x ? mask[y * w + x - 1] : 0, u = y ? mask[(y - 1) * w + x] : 0, ul = x && y ? mask[(y - 1) * w + x - 1] : 0;
        mask[y * w + x] = (pk_u8)cbit(c, &pr[l | u << 1 | ul << 2], mask[y * w + x]);
    }
}

static float ctab[8][8];
static void init_ctab(void)
{
    if (ctab[0][0] != 0) return;
    for (int x = 0; x < 8; x++) for (int u = 0; u < 8; u++)
        ctab[x][u] = (u ? 0.5f : 0.35355339f) * gcos((2 * x + 1) * u * GPI / 16);
}

/* planes: Y (pw x ph), Cb, Cr (cw x chh), multiples of 8 */
typedef struct { int w, h, pw, ph, cw, chh, nby, nbc, sub; } Geo;
static void geo(Geo *g, int w, int h, int sub)
{
    g->w = w; g->h = h; g->sub = sub;
    g->pw = (w + 7) & ~7; g->ph = (h + 7) & ~7;
    g->cw = sub ? (((w + 1) / 2) + 7) & ~7 : g->pw; g->chh = sub ? (((h + 1) / 2) + 7) & ~7 : g->ph;
    g->nby = (g->pw / 8) * (g->ph / 8); g->nbc = (g->cw / 8) * (g->chh / 8);
}

static void idct_plane(const int *coef, int bw, int qs, int chroma, float *plane, int pw, int ph)
{
    init_ctab();
    for (int by = 0; by < ph / 8; by++) for (int bx = 0; bx < bw; bx++) {
        const int *q = coef + (by * bw + bx) * 64;
        float t[8][8], d[64];
        for (int k = 0; k < 64; k++) d[k] = (float)(q[k] * qstep(chroma, k, qs));
        for (int y = 0; y < 8; y++) for (int u = 0; u < 8; u++) {   /* rows: inverse over v */
            float s = 0;
            for (int v = 0; v < 8; v++) s += ctab[y][v] * d[v * 8 + u];
            t[y][u] = s;
        }
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += ctab[x][u] * t[y][u];
            plane[(by * 8 + y) * pw + bx * 8 + x] = s + 128;
        }
    }
}

int pk_image(const pk_u8 *data, int size, PkImage *out)
{
    Coder c; Geo g; BlockModel *m = (BlockModel *)malloc(sizeof(BlockModel));
    begin_dec(&c, data, size);
    int w = cdirect(&c, 16, 0), h = cdirect(&c, 16, 0), hasmask = cdirect(&c, 1, 0), qs = cdirect(&c, 10, 0), full = cdirect(&c, 1, 0);
    geo(&g, w, h, !full);
    out->w = w; out->h = h;
    out->mask = 0;
    if (hasmask) { out->mask = (pk_u8 *)calloc((size_t)(w * h), 1); code_mask(&c, out->mask, w, h); }
    int *coef = (int *)calloc((size_t)(g.nby + 2 * g.nbc) * 64, sizeof(int));
    float *Y = (float *)malloc(sizeof(float) * (size_t)(g.pw * g.ph)), *Cb = (float *)malloc(sizeof(float) * (size_t)(g.cw * g.chh)),
          *Cr = (float *)malloc(sizeof(float) * (size_t)(g.cw * g.chh));
    { pk_u16 *p = (pk_u16 *)m; for (unsigned i = 0; i < sizeof(*m) / 2; i++) p[i] = PINIT; }
    code_blocks(&c, m, coef, g.nby, 0);
    code_blocks(&c, m, coef + g.nby * 64, g.nbc, 1);
    code_blocks(&c, m, coef + (g.nby + g.nbc) * 64, g.nbc, 1);
    idct_plane(coef, g.pw / 8, qs, 0, Y, g.pw, g.ph);
    idct_plane(coef + g.nby * 64, g.cw / 8, qs, 1, Cb, g.cw, g.chh);
    idct_plane(coef + (g.nby + g.nbc) * 64, g.cw / 8, qs, 1, Cr, g.cw, g.chh);
    out->rgb = (float *)malloc(sizeof(float) * (size_t)(w * h * 3));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        /* bilinear chroma (sample centres at 2x+0.5) */
        float fx = g.sub ? (x - 0.5f) / 2 : x, fy = g.sub ? (y - 0.5f) / 2 : y;
        int cwv = g.sub ? (w + 1) / 2 : w, chv = g.sub ? (h + 1) / 2 : h;
        int x0 = (int)gfloor(fx), y0 = (int)gfloor(fy);
        float ax = fx - x0, ay = fy - y0, cb = 0, cr = 0;
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
            int xx = x0 + i, yy = y0 + j;
            xx = xx < 0 ? 0 : xx >= cwv ? cwv - 1 : xx;
            yy = yy < 0 ? 0 : yy >= chv ? chv - 1 : yy;
            float wt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
            cb += wt * Cb[yy * g.cw + xx]; cr += wt * Cr[yy * g.cw + xx];
        }
        float yv = Y[y * g.pw + x];
        cb -= 128; cr -= 128;
        float *o = out->rgb + (y * w + x) * 3;
        o[0] = gclamp(yv + 1.402f * cr, 0, 255);
        o[1] = gclamp(yv - 0.344136f * cb - 0.714136f * cr, 0, 255);
        o[2] = gclamp(yv + 1.772f * cb, 0, 255);
    }
    free(Y); free(Cb); free(Cr); free(coef); free(m);
    return 0;
}

/* ------------------------------------------------------------------ indexed (4-bit) images */
static void code_indexed(Coder *c, pk_u8 *idx, int w, int h, pk_u16 clut[16])
{
    pk_u16 (*pr)[16] = (pk_u16 (*)[16])malloc(sizeof(pk_u16) * 32 * 16);
    for (int i = 0; i < 32 * 16; i++) pr[i / 16][i % 16] = PINIT;
    for (int i = 0; i < 16; i++) clut[i] = (pk_u16)cdirect(c, 16, clut[i]);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int l = x ? idx[y * w + x - 1] : 0, u = y ? idx[(y - 1) * w + x] : 0, node = 1;
        int ctx = l + 16 * (u == l);
        for (int i = 3; i >= 0; i--) node = node * 2 + cbit(c, &pr[ctx][node], (idx[y * w + x] >> i) & 1);
        idx[y * w + x] = (pk_u8)(node - 16);
    }
    free(pr);
}
int pk_indexed(const pk_u8 *data, int size, int *w, int *h, pk_u8 **idx, pk_u16 clut[16])
{
    Coder c;
    begin_dec(&c, data, size);
    *w = cdirect(&c, 16, 0); *h = cdirect(&c, 16, 0);
    *idx = (pk_u8 *)calloc((size_t)(*w * *h), 1);
    code_indexed(&c, *idx, *w, *h, clut);
    return 0;
}

/* ------------------------------------------------------------------ meshes */
typedef struct { ICtx n, pos[3], vi[4], uv[2][4]; } MeshModel;
static void code_mesh(Coder *c, int *v3, int nv, int *p13, int np)
{
    MeshModel *m = (MeshModel *)malloc(sizeof(MeshModel));
    for (unsigned i = 0; i < sizeof(*m) / sizeof(ICtx); i++) ictx_init((ICtx *)m + i);
    int prev[3] = { 0, 0, 0 };
    for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { v3[i * 3 + k] = prev[k] + cint(c, &m->pos[k], v3[i * 3 + k] - prev[k]); prev[k] = v3[i * 3 + k]; }
    int pv = 0, pu = 0, pt = 0;
    for (int i = 0; i < np; i++) {
        int *p = p13 + i * 13;
        p[0] = 3 + cuint(c, &m->n, p[0] - 3);
        for (int k = 0; k < 4; k++) {
            if (k >= p[0]) { p[1 + k] = p[5 + k] = p[9 + k] = 0; continue; }
            int bv = k ? p[1] : pv, bu = k ? p[5] : pu, bt = k ? p[9] : pt;
            p[1 + k] = bv + cint(c, &m->vi[k], p[1 + k] - bv);
            p[5 + k] = bu + cint(c, &m->uv[0][k], p[5 + k] - bu);
            p[9 + k] = bt + cint(c, &m->uv[1][k], p[9 + k] - bt);
        }
        pv = p[1]; pu = p[5]; pt = p[9];
    }
    free(m);
}
int pk_mesh(const pk_u8 *data, int size, DVERT **verts, int *nv, DPRIM **prims, int *np)
{
    Coder c;
    begin_dec(&c, data, size);
    *nv = cdirect(&c, 16, 0); *np = cdirect(&c, 16, 0);
    int *v3 = (int *)calloc((size_t)*nv * 3, sizeof(int)), *p13 = (int *)calloc((size_t)*np * 13, sizeof(int));
    code_mesh(&c, v3, *nv, p13, *np);
    *verts = (DVERT *)calloc((size_t)*nv, sizeof(DVERT));
    *prims = (DPRIM *)calloc((size_t)*np, sizeof(DPRIM));
    for (int i = 0; i < *nv; i++) { (*verts)[i].x = (short)v3[i * 3]; (*verts)[i].y = (short)v3[i * 3 + 1]; (*verts)[i].z = (short)v3[i * 3 + 2]; }
    for (int i = 0; i < *np; i++) {
        DPRIM *d = &(*prims)[i]; int *p = p13 + i * 13;
        d->n = (unsigned char)p[0];
        for (int k = 0; k < 4; k++) { d->v[k] = (unsigned short)p[1 + k]; d->u[k] = (unsigned char)p[5 + k]; d->t[k] = (unsigned char)p[9 + k]; }
    }
    free(v3); free(p13);
    return 0;
}

/* ------------------------------------------------------------------ encoders */
#ifdef PACK_ENCODER
#include <math.h>
pk_u8 *pk_enc_image(const pk_u8 *rgb, const pk_u8 *mask, int w, int h, float quality, int full, int *size)
{
    Coder c; Geo g; BlockModel *m = (BlockModel *)malloc(sizeof(BlockModel));
    int qs = (int)(quality < 50 ? 5000 / quality : 200 - 2 * quality);           /* JPEG quality scale, percent */
    geo(&g, w, h, !full);
    float *Y = (float *)calloc((size_t)(g.pw * g.ph), sizeof(float)), *Cb = (float *)calloc((size_t)(g.cw * g.chh), sizeof(float)),
          *Cr = (float *)calloc((size_t)(g.cw * g.chh), sizeof(float)), *ful = (float *)malloc(sizeof(float) * (size_t)(w * h * 3));
    /* transparent texels: fill with the average of nearby opaque texels so blocks stay smooth */
    for (int i = 0; i < w * h * 3; i++) ful[i] = rgb[i];
    if (mask)
        for (int pass = 0; pass < 64; pass++) {
            int changed = 0;
            static pk_u8 *done; done = (pk_u8 *)realloc(done, (size_t)(w * h));
            if (pass == 0) memcpy(done, mask, (size_t)(w * h));
            pk_u8 *nd = (pk_u8 *)malloc((size_t)(w * h)); memcpy(nd, done, (size_t)(w * h));
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                if (done[y * w + x]) continue;
                float s[3] = { 0, 0, 0 }; int n = 0;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h || !done[yy * w + xx]) continue;
                    for (int k = 0; k < 3; k++) s[k] += ful[(yy * w + xx) * 3 + k];
                    n++;
                }
                if (n) { for (int k = 0; k < 3; k++) ful[(y * w + x) * 3 + k] = s[k] / n; nd[y * w + x] = 1; changed = 1; }
            }
            memcpy(done, nd, (size_t)(w * h)); free(nd);
            if (!changed) break;
        }
    for (int y = 0; y < g.ph; y++) for (int x = 0; x < g.pw; x++) {
        int sx = x < w ? x : w - 1, sy = y < h ? y : h - 1;
        const float *p = ful + (sy * w + sx) * 3;
        Y[y * g.pw + x] = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
    }
    for (int y = 0; y < g.chh; y++) for (int x = 0; x < g.cw; x++) {
        float cb = 0, cr = 0;
        int sc = g.sub ? 2 : 1;
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
            int sx = sc * x + (g.sub ? i : 0), sy = sc * y + (g.sub ? j : 0);
            sx = sx < w ? sx : w - 1; sy = sy < h ? sy : h - 1;
            const float *p = ful + (sy * w + sx) * 3;
            cb += -0.168736f * p[0] - 0.331264f * p[1] + 0.5f * p[2] + 128;
            cr += 0.5f * p[0] - 0.418688f * p[1] - 0.081312f * p[2] + 128;
        }
        Cb[y * g.cw + x] = cb / 4; Cr[y * g.cw + x] = cr / 4;
    }
    int *coef = (int *)calloc((size_t)(g.nby + 2 * g.nbc) * 64, sizeof(int));
    init_ctab();
    float *pl[3] = { Y, Cb, Cr };
    int pws[3] = { g.pw, g.cw, g.cw }, phs[3] = { g.ph, g.chh, g.chh }, off[3] = { 0, g.nby * 64, (g.nby + g.nbc) * 64 };
    for (int p = 0; p < 3; p++) {
        int bw = pws[p] / 8;
        for (int by = 0; by < phs[p] / 8; by++) for (int bx = 0; bx < bw; bx++)
            for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {
                double s = 0;
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
                    s += ctab[y][v] * ctab[x][u] * (pl[p][(by * 8 + y) * pws[p] + bx * 8 + x] - 128);
                int k = v * 8 + u;
                coef[off[p] + (by * bw + bx) * 64 + k] = (int)lround(s / qstep(p > 0, k, qs));
            }
    }
    begin_enc(&c);
    cdirect(&c, 16, w); cdirect(&c, 16, h); cdirect(&c, 1, mask != 0); cdirect(&c, 10, qs); cdirect(&c, 1, full);
    if (mask) { pk_u8 *mm = (pk_u8 *)malloc((size_t)(w * h)); memcpy(mm, mask, (size_t)(w * h)); code_mask(&c, mm, w, h); free(mm); }
    { pk_u16 *pp = (pk_u16 *)m; for (unsigned i = 0; i < sizeof(*m) / 2; i++) pp[i] = PINIT; }
    code_blocks(&c, m, coef, g.nby, 0);
    code_blocks(&c, m, coef + g.nby * 64, g.nbc, 1);
    code_blocks(&c, m, coef + (g.nby + g.nbc) * 64, g.nbc, 1);
    free(Y); free(Cb); free(Cr); free(ful); free(coef); free(m);
    return end_enc(&c, size);
}
pk_u8 *pk_enc_indexed(const pk_u8 *idx, int w, int h, const pk_u16 clut[16], int *size)
{
    Coder c; pk_u16 cl[16]; pk_u8 *tmp = (pk_u8 *)malloc((size_t)(w * h));
    memcpy(cl, clut, sizeof(cl)); memcpy(tmp, idx, (size_t)(w * h));
    begin_enc(&c);
    cdirect(&c, 16, w); cdirect(&c, 16, h);
    code_indexed(&c, tmp, w, h, cl);
    free(tmp);
    return end_enc(&c, size);
}
pk_u8 *pk_enc_mesh_ints(const int *v3, int nv, const int *p13, int np, int *size)
{
    Coder c;
    int *a = (int *)malloc(sizeof(int) * (size_t)nv * 3), *b = (int *)malloc(sizeof(int) * (size_t)np * 13);
    memcpy(a, v3, sizeof(int) * (size_t)nv * 3); memcpy(b, p13, sizeof(int) * (size_t)np * 13);
    begin_enc(&c);
    cdirect(&c, 16, nv); cdirect(&c, 16, np);
    code_mesh(&c, a, nv, b, np);
    free(a); free(b);
    return end_enc(&c, size);
}
#endif
