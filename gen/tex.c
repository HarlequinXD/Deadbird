#include <stdlib.h>
#include <string.h>
#include "tex.h"
#include "gmath.h"

static inline int in_(int x, int y) { return x >= 0 && x < T && y >= 0 && y < T; }
static inline float c255(float v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void unpack(Col c, float o[3]) { o[0] = (float)(c >> 16); o[1] = (float)((c >> 8) & 255); o[2] = (float)(c & 255); }
void t_setf(Img im, int x, int y, const float *c) { if (in_(x, y)) { im[y][x][0] = c[0]; im[y][x][1] = c[1]; im[y][x][2] = c[2]; } }
void t_set(Img im, int x, int y, Col c) { float f[3]; unpack(c, f); t_setf(im, x, y, f); }
void rectf(Img im, int x0, int y0, int x1, int y1, const float *c)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < T; y++) for (int x = x0 < 0 ? 0 : x0; x < x1 && x < T; x++) t_setf(im, x, y, c);
}
void t_new(Img im, Col c) { rect(im, 0, 0, T, T, c); }
void t_mul(Img im, int x, int y, float k) { if (in_(x, y)) { im[y][x][0] *= k; im[y][x][1] *= k; im[y][x][2] *= k; } }
void t_blend(Img im, int x, int y, Col c, float a)
{
    float f[3]; unpack(c, f);
    if (in_(x, y)) for (int k = 0; k < 3; k++) im[y][x][k] = im[y][x][k] * (1 - a) + f[k] * a;
}
void rect(Img im, int x0, int y0, int x1, int y1, Col c) { float f[3]; unpack(c, f); rectf(im, x0, y0, x1, y1, f); }
void rect_mul(Img im, int x0, int y0, int x1, int y1, float k)
{
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < T; y++) for (int x = x0 < 0 ? 0 : x0; x < x1 && x < T; x++) t_mul(im, x, y, k);
}
void bevel(Img im, int x0, int y0, int x1, int y1, Col col, float hi, float lo, int w)
{
    float c[3], h3[3], l3[3];
    unpack(col, c);
    for (int k = 0; k < 3; k++) { h3[k] = c[k] * hi; l3[k] = c[k] * lo; }
    rectf(im, x0, y0, x1, y1, c);
    rectf(im, x0, y0, x1, y0 + w, h3);
    rectf(im, x0, y0, x0 + w, y1, h3);
    rectf(im, x0, y1 - w, x1, y1, l3);
    rectf(im, x1 - w, y0, x1, y1, l3);
}
void inset(Img im, int x0, int y0, int x1, int y1, Col c) { bevel(im, x0, y0, x1, y1, c, 0.5f, 1.3f, 1); }
void rivet(Img im, int x, int y, Col col)
{
    float c[3];
    unpack(col, c);
    if (!(x >= 0 && x < T - 1 && y >= 0 && y < T - 2)) return;
    t_setf(im, x, y, F3(c[0] * 1.4f, c[1] * 1.4f, c[2] * 1.4f));
    t_setf(im, x + 1, y, c); t_setf(im, x, y + 1, c);
    t_setf(im, x + 1, y + 1, F3(c[0] * .5f, c[1] * .5f, c[2] * .5f));
    t_mul(im, x + 1, y + 2, 0.6f);
}

void noise(Map out, int scale, unsigned seed, int octaves)
{
    static float g[130][130];
    GRng r; grng_seed(&r, seed);
    float amp = 1, tot = 0;
    int s = scale;
    memset(out, 0, sizeof(Map));
    for (int o = 0; o < octaves; o++) {
        for (int j = 0; j < s; j++) for (int i = 0; i < s; i++) g[j][i] = grng_f(&r);
        for (int i = 0; i <= s; i++) { g[s][i] = g[0][i % s]; g[i][s] = g[i % s][0]; }
        for (int y = 0; y < T; y++) {
            float py = (float)y * s / T; int y0 = (int)py; float fy = py - y0; fy = fy * fy * (3 - 2 * fy);
            for (int x = 0; x < T; x++) {
                float px = (float)x * s / T; int x0 = (int)px; float fx = px - x0; fx = fx * fx * (3 - 2 * fx);
                float a = g[y0][x0] * (1 - fx) + g[y0][x0 + 1] * fx, b = g[y0 + 1][x0] * (1 - fx) + g[y0 + 1][x0 + 1] * fx;
                out[y][x] += amp * (a * (1 - fy) + b * fy);
            }
        }
        tot += amp; amp *= 0.5f; s *= 2;
    }
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) out[y][x] /= tot;
}

void grime(Img im, float amount, unsigned seed, int streaks)
{
    static Map n;
    noise(n, 4, seed, 3);
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) n[y][x] = 1 - amount * (1 - n[y][x]);
    if (streaks) {
        GRng r; grng_seed(&r, seed + 50);
        for (int k = 0; k < 7; k++) {
            int x = grng_int(&r, 0, T), y0 = grng_int(&r, 0, T / 2), ln = grng_int(&r, 8, 30);
            for (int y = y0; y < y0 + ln && y < T; y++) n[y][x] *= 0.8f;
        }
    }
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) t_mul(im, x, y, n[y][x]);
}
void floor_ao(Img im, float strength, int px)
{
    for (int y = 0; y < T; y++) rect_mul(im, 0, y, T, y + 1, 1 - strength * gclamp((float)(y - (T - px)) / px, 0, 1));
}
void ao_edges(Img im, float strength, float width)
{
    for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
        int d = x; if (T - 1 - x < d) d = T - 1 - x; if (y < d) d = y; if (T - 1 - y < d) d = T - 1 - y;
        t_mul(im, x, y, 1 - strength * gclamp(1 - d / width, 0, 1));
    }
}
void scratches(Img im, int n, unsigned seed, Col c)
{
    GRng r; grng_seed(&r, seed);
    for (int k = 0; k < n; k++) {
        int x = grng_int(&r, 0, T), y = grng_int(&r, 0, T), dx = grng_int(&r, -1, 2), dy = grng_int(&r, -1, 2), len = grng_int(&r, 3, 9);
        for (int i = 0; i < len; i++) t_blend(im, ((x + dx * i) % T + T) % T, ((y + dy * i) % T + T) % T, c, 0.6f);
    }
}

static const char *glyph(char ch)
{
    static const char *const F[][2] = {
        { "A", "010101111101101" }, { "B", "110101110101110" }, { "C", "011100100100011" }, { "D", "110101101101110" },
        { "E", "111100110100111" }, { "F", "111100110100100" }, { "G", "011100101101011" }, { "H", "101101111101101" },
        { "I", "111010010010111" }, { "J", "001001001101010" }, { "K", "101101110101101" }, { "L", "100100100100111" },
        { "M", "101111111101101" }, { "N", "110101101101101" }, { "O", "010101101101010" }, { "P", "110101110100100" },
        { "Q", "010101101110011" }, { "R", "110101110101101" }, { "S", "011100010001110" }, { "T", "111010010010010" },
        { "U", "101101101101111" }, { "V", "101101101101010" }, { "W", "101101111111101" }, { "X", "101101010101101" },
        { "Y", "101101010010010" }, { "Z", "111001010100111" }, { "0", "111101101101111" }, { "1", "010110010010111" },
        { "2", "110001010100111" }, { "3", "110001010001110" }, { "4", "101101111001001" }, { "5", "111100110001110" },
        { "6", "011100111101111" }, { "7", "111001010010010" }, { "8", "111101111101111" }, { "9", "111101111001110" },
        { "-", "000000111000000" }, { "%", "101001010100101" }, { "!", "010010010000010" }, { ":", "000010000010000" },
        { ".", "000000000000010" }, { ">", "100010001010100" }, { "<", "001010100010001" },
    };
    for (unsigned i = 0; i < sizeof(F) / sizeof(F[0]); i++) if (F[i][0][0] == ch) return F[i][1];
    return "000000000000000";
}
void ttext(Img im, const char *s, int x, int y, Col c, int scale)
{
    for (; *s; s++, x += 4 * scale) {
        const char *g = glyph(*s);
        for (int i = 0; i < 15; i++)
            if (g[i] == '1') {
                int yy = y + (i / 3) * scale, xx = x + (i % 3) * scale;
                rect(im, xx, yy, xx + scale, yy + scale, c);
            }
    }
}
int  ttext_w(const char *s, int scale) { return (int)strlen(s) * 4 * scale - scale; }
static int fdiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }
void tctext(Img im, const char *s, int y, Col c, int scale) { ttext(im, s, fdiv(T - ttext_w(s, scale), 2), y, c, scale); }

static float pipe_k(int k, int r)
{
    float t = (float)(k + r) / (2 * r);
    return 0.45f + 0.95f * gpow(gmax(gsin(GPI * t), 0), 1.2f) + (gabs(t - 0.3f) < 0.12f ? 0.35f : 0);
}
void hpipe(Img im, int y, int r, Col col, int brackets)
{
    float c[3];
    unpack(col, c);
    for (int k = -r; k <= r; k++) { float m = pipe_k(k, r); rectf(im, 0, y + k, T, y + k + 1, F3(c[0] * m, c[1] * m, c[2] * m)); }
    if (y + r + 1 < T) rect_mul(im, 0, y + r + 1, T, y + r + 2, 0.55f);
    if (brackets)
        for (int x = 6; x < T; x += 21)
            for (int k = -r - 1; k <= r + 1; k++) {
                float m = k < 0 ? 0.7f : 0.45f;
                rectf(im, x, y + k, x + 2, y + k + 1, F3(c[0] * m, c[1] * m, c[2] * m));
            }
}
void vpipe(Img im, int x, int r, Col col)
{
    float c[3];
    unpack(col, c);
    for (int k = -r; k <= r; k++) { float m = pipe_k(k, r); rectf(im, x + k, 0, x + k + 1, T, F3(c[0] * m, c[1] * m, c[2] * m)); }
    if (x + r + 1 < T) rect_mul(im, x + r + 1, 0, x + r + 2, T, 0.55f);
    for (int y = 4; y < T; y += 19) rect_mul(im, x - r - 1, y, x + r + 2, y + 2, 0.6f);
}
void valve(Img im, int x, int y, Col col)
{
    float c[3];
    unpack(col, c);
    for (int yy = 0; yy < T; yy++) for (int xx = 0; xx < T; xx++) {
        float d = gsqrt((float)((xx - x) * (xx - x) + (yy - y) * (yy - y)));
        if (d > 3.2f && d < 5) t_setf(im, xx, yy, F3(c[0] * .9f, c[1] * .9f, c[2] * .9f));
        if ((xx == x && d < 4) || (yy == y && d < 4)) t_setf(im, xx, yy, F3(c[0] * .7f, c[1] * .7f, c[2] * .7f));
        if (d < 1.5f) t_set(im, xx, yy, C3(40, 40, 40));
    }
}
void hazard_band(Img im, int y0, int y1, int flip)
{
    for (int y = y0; y < y1 && y < T; y++) for (int x = 0; x < T; x++)
        t_set(im, x, y, (fdiv(x + (flip ? -y : y), 5) & 1) == 0 ? C3(205, 165, 30) : C3(32, 30, 28));
}
void kickplate(Img im, int h)
{
    bevel(im, 0, T - h, T, T, C3(62, 64, 70), 1.35f, 0.55f, 1);
    for (int x = 4; x < T; x += 12) RIVET(im, x, T - h + 3);
}
void panel_base(Img im, Col c, int seam)
{
    t_new(im, c);
    if (seam)
        for (int px = 0; px <= 32; px += 32) {
            BEVEL(im, px + 1, 1, px + 31, 63, c);
            RIVET(im, px + 3, 3); RIVET(im, px + 27, 3); RIVET(im, px + 3, 58); RIVET(im, px + 27, 58);
        }
}
void poster(Img im, int x0, int y0, int x1, int y1, Col bg, Col border, int tear)
{
    rect(im, x0 + 1, y0 + 1, x1 + 1, y1 + 1, C3(20, 20, 22));
    rect(im, x0, y0, x1, y1, border);
    rect(im, x0 + 1, y0 + 1, x1 - 1, y1 - 1, bg);
    if (tear) for (int k = 0; k < 4; k++) rect(im, x1 - 4 + k, y1 - 1 - k, x1, y1 - k, C3(70, 74, 80));
}
void skull_icon(Img im, int x, int y, Col c, Col eye, int s)
{
    static const char *sk[7] = { "0011100", "0111110", "1101011", "1111111", "0111110", "0011100", "0001000" };
    for (int j = 0; j < 7; j++) for (int i = 0; i < 7; i++)
        if (sk[j][i] == '1') rect(im, x + i * s, y + j * s, x + (i + 1) * s, y + (j + 1) * s, c);
    rect(im, x + 2 * s, y + 2 * s, x + 3 * s, y + 3 * s, eye);
    rect(im, x + 4 * s, y + 2 * s, x + 5 * s, y + 3 * s, eye);
}
void cyl_shade(Img im, int axis, float lo, float hi)
{
    for (int i = 0; i < T; i++) {
        float t = GPI * i / (T - 1), u = (float)i / (T - 1), q = (u - 0.32f) / 0.06f;
        float k = lo + (hi - lo) * gpow(gmax(gsin(t), 0), 1.5f) + 0.25f * gexp(-q * q);
        if (axis == 0) rect_mul(im, 0, i, T, i + 1, k); else rect_mul(im, i, 0, i + 1, T, k);
    }
}
void t_flip_x(Img im)
{
    for (int y = 0; y < T; y++) for (int x = 0; x < T / 2; x++) for (int k = 0; k < 3; k++) {
        float v = im[y][x][k]; im[y][x][k] = im[y][T - 1 - x][k]; im[y][T - 1 - x][k] = v;
    }
}

/* ---------------------------------------------------------------- 16-colour palettes */
typedef struct { unsigned char c[3]; float w; } Px;
static int sort_ch;
static int cmp_px(const void *a, const void *b) { return ((const Px *)a)->c[sort_ch] - ((const Px *)b)->c[sort_ch]; }

static const int DM[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };

void quant16(const float *rgb, const unsigned char *opaque, int w, int h, int stride,
             unsigned short clut[16], unsigned char *idx, int idx_stride, float spread, int stp)
{
    int n = 0, nb = 1, x, y, k;
    Px *px = (Px *)malloc(sizeof(Px) * (size_t)(w * h + 1));
    int bs[15], bn[15];
    float pal[15][3], p8[15][3];
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        const float *p = rgb + ((size_t)y * stride + x) * 3;
        if (opaque && !opaque[(size_t)y * stride + x]) continue;
        for (k = 0; k < 3; k++) px[n].c[k] = (unsigned char)(c255(p[k]) + 0.5f > 255 ? 255 : c255(p[k]) + 0.5f);
        n++;
    }
    {   /* weight = 1/sqrt(how common the colour is): rare accents (trims, glows) still get palette entries */
        unsigned short *hist = (unsigned short *)calloc(32768, sizeof(unsigned short));
        for (int i = 0; i < n; i++) { int b = (px[i].c[0] >> 3) | (px[i].c[1] >> 3) << 5 | (px[i].c[2] >> 3) << 10; if (hist[b] < 65535) hist[b]++; }
        for (int i = 0; i < n; i++) { int b = (px[i].c[0] >> 3) | (px[i].c[1] >> 3) << 5 | (px[i].c[2] >> 3) << 10; px[i].w = 1 / gsqrt((float)hist[b]); }
        free(hist);
    }
    /* median cut: split the box with the largest (channel range x pixel count) at its median */
    bs[0] = 0; bn[0] = n;
    while (nb < 15) {
        int best = -1, bch = 0; long bscore = 0;
        for (int b = 0; b < nb; b++) {
            if (bn[b] < 2) continue;
            int lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
            for (int i = bs[b]; i < bs[b] + bn[b]; i++) for (k = 0; k < 3; k++) {
                if (px[i].c[k] < lo[k]) lo[k] = px[i].c[k];
                if (px[i].c[k] > hi[k]) hi[k] = px[i].c[k];
            }
            float wsum = 0;
            for (int i = bs[b]; i < bs[b] + bn[b]; i++) wsum += px[i].w;
            for (k = 0; k < 3; k++) {
                long sc = (long)((hi[k] - lo[k]) * wsum * 16);
                if (sc > bscore) { bscore = sc; best = b; bch = k; }
            }
        }
        if (best < 0) break;
        sort_ch = bch;
        qsort(px + bs[best], (size_t)bn[best], sizeof(Px), cmp_px);
        int half = 0;
        {   /* split at the weighted median */
            float tot = 0, acc = 0;
            for (int i = bs[best]; i < bs[best] + bn[best]; i++) tot += px[i].w;
            while (half < bn[best] - 1 && acc + px[bs[best] + half].w <= tot / 2) acc += px[bs[best] + half++].w;
            if (half < 1) half = 1;
        }
        bs[nb] = bs[best] + half; bn[nb] = bn[best] - half; bn[best] = half; nb++;
    }
    for (int b = 0; b < 15; b++) {
        float s[3] = { 0, 0, 0 }, ws = 0;
        int bb = b < nb ? b : 0;
        for (int i = bs[bb]; i < bs[bb] + bn[bb]; i++) { for (k = 0; k < 3; k++) s[k] += px[i].c[k] * px[i].w; ws += px[i].w; }
        for (k = 0; k < 3; k++) pal[b][k] = ws > 0 ? s[k] / ws : 0;
    }
    /* k-means refinement */
    {
        int step = n > 20000 ? n / 20000 : 1;
        for (int it = 0; it < 8 && n; it++) {
            float acc[15][4];
            memset(acc, 0, sizeof(acc));
            for (int i = 0; i < n; i += step) {
                int bi = 0; float bd = 1e30f;
                for (int b = 0; b < nb; b++) {
                    float d = 0;
                    for (k = 0; k < 3; k++) { float e = px[i].c[k] - pal[b][k]; d += e * e; }
                    if (d < bd) { bd = d; bi = b; }
                }
                for (k = 0; k < 3; k++) acc[bi][k] += px[i].c[k] * px[i].w;
                acc[bi][3] += px[i].w;
            }
            for (int b = 0; b < nb; b++) if (acc[b][3] > 0) for (k = 0; k < 3; k++) pal[b][k] = acc[b][k] / acc[b][3];
        }
    }
    for (int b = 0; b < 15; b++) {
        int q[3];
        for (k = 0; k < 3; k++) { q[k] = (int)(pal[b < nb ? b : 0][k] / 8 + 0.5f); if (q[k] > 31) q[k] = 31; p8[b][k] = (float)(q[k] * 8 + q[k] / 4); }
        unsigned short wd = (unsigned short)(q[0] | (q[1] << 5) | (q[2] << 10) | (stp ? 0x8000 : 0));
        if (!(wd & 0x7fff)) wd = 0x8000;                       /* black must not read as transparent */
        clut[b + 1] = wd;
    }
    clut[0] = 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        unsigned char *o = idx + (size_t)y * idx_stride + x;
        if (opaque && !opaque[(size_t)y * stride + x]) { *o = 0; continue; }
        const float *p = rgb + ((size_t)y * stride + x) * 3;
        float d0 = DM[y & 3][x & 3] * spread, bd = 1e30f; int bi = 0;
        for (int b = 0; b < nb; b++) {
            float d = 0;
            for (k = 0; k < 3; k++) { float e = c255(p[k]) + d0 - p8[b][k]; d += e * e; }
            if (d < bd) { bd = d; bi = b; }
        }
        *o = (unsigned char)(bi + 1);
    }
    free(px);
}

unsigned char *tim4(const unsigned char *idx, int w, int h, const unsigned short *clut, int ncl,
                    int vx, int vy, int cx, int cy, int *size)
{
    int nc = ncl * 16, sz = 8 + 12 + nc * 2 + 12 + w * h / 2;
    unsigned char *o = (unsigned char *)malloc((size_t)sz), *p = o;
#define W32(v) do { unsigned _v = (unsigned)(v); p[0] = (unsigned char)_v; p[1] = (unsigned char)(_v >> 8); p[2] = (unsigned char)(_v >> 16); p[3] = (unsigned char)(_v >> 24); p += 4; } while (0)
#define W16(v) do { unsigned _v = (unsigned)(v); p[0] = (unsigned char)_v; p[1] = (unsigned char)(_v >> 8); p += 2; } while (0)
    W32(0x10); W32(0x08);
    W32(12 + nc * 2); W16(cx); W16(cy); W16(nc); W16(1);
    for (int i = 0; i < nc; i++) W16(clut[i]);
    W32(12 + w * h / 2); W16(vx); W16(vy); W16(w / 4); W16(h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x += 2) *p++ = (unsigned char)((idx[y * w + x] & 15) | ((idx[y * w + x + 1] & 15) << 4));
#undef W32
#undef W16
    *size = sz;
    return o;
}
