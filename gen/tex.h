/*
 * Texture helpers for the generators: 64x64 float RGB tiles with painting operations (a port of
 * the old numpy painters), 16-colour palettes (median cut refined by k-means) with the PS1 4x4
 * ordered dither, and 4bpp TIM output (index 0 = transparent).
 */
#ifndef GEN_TEX_H
#define GEN_TEX_H

#define T 64
typedef float Img[T][T][3];
typedef float Map[T][T];
typedef unsigned Col;                                   /* 0xRRGGBB: small to pass around */
#define C3(r, g, b) ((Col)(((r) << 16) | ((g) << 8) | (b)))
#define F3(r, g, b) ((const float[3]){ (float)(r), (float)(g), (float)(b) })   /* computed colours */

void   t_new(Img im, Col c);
void   t_set(Img im, int x, int y, Col c);                       /* clipped */
void   t_setf(Img im, int x, int y, const float *c);
void   rectf(Img im, int x0, int y0, int x1, int y1, const float *c);
void   t_mul(Img im, int x, int y, float k);
void   t_blend(Img im, int x, int y, Col c, float a);            /* im = im*(1-a) + c*a */
void   rect(Img im, int x0, int y0, int x1, int y1, Col c);      /* [x0,x1) x [y0,y1), clipped */
void   rect_mul(Img im, int x0, int y0, int x1, int y1, float k);
void   bevel(Img im, int x0, int y0, int x1, int y1, Col c, float hi, float lo, int w);
#define BEVEL(im, x0, y0, x1, y1, c) bevel(im, x0, y0, x1, y1, c, 1.35f, 0.55f, 1)
void   inset(Img im, int x0, int y0, int x1, int y1, Col c);
void   rivet(Img im, int x, int y, Col c);
#define RIVET(im, x, y) rivet(im, x, y, C3(150, 150, 155))
void   noise(Map out, int scale, unsigned seed, int octaves);
void   grime(Img im, float amount, unsigned seed, int streaks);
void   floor_ao(Img im, float strength, int px);
void   ao_edges(Img im, float strength, float width);
void   scratches(Img im, int n, unsigned seed, Col c);
void   ttext(Img im, const char *s, int x, int y, Col c, int scale);
int    ttext_w(const char *s, int scale);
void   tctext(Img im, const char *s, int y, Col c, int scale);
void   hpipe(Img im, int y, int r, Col c, int brackets);
void   vpipe(Img im, int x, int r, Col c);
void   valve(Img im, int x, int y, Col c);
void   hazard_band(Img im, int y0, int y1, int flip);
void   kickplate(Img im, int h);
void   panel_base(Img im, Col c, int seam);
void   poster(Img im, int x0, int y0, int x1, int y1, Col bg, Col border, int tear);
void   skull_icon(Img im, int x, int y, Col c, Col eye, int s);
void   cyl_shade(Img im, int axis, float lo, float hi);
void   t_flip_x(Img im);

/* ---- palettes and TIMs */
typedef unsigned short u16_;
/* quantise a w x h region of an RGB float image (row stride `stride` pixels) to 15 colours + transparent.
   opaque: per-pixel flags (same stride) or 0 = all opaque. Writes 16 CLUT words and 4-bit indices
   into idx (row stride idx_stride bytes, one index per byte). stp: set the semi-transparency bit. */
void   quant16(const float *rgb, const unsigned char *opaque, int w, int h, int stride,
               unsigned short clut[16], unsigned char *idx, int idx_stride, float spread, int stp);
/* 4bpp TIM in a malloc'd buffer: indices (w x h, one per byte), ncl CLUTs of 16 in one row at (cx, cy) */
unsigned char *tim4(const unsigned char *idx, int w, int h, const unsigned short *clut, int ncl,
                    int vx, int vy, int cx, int cy, int *size);

#endif
