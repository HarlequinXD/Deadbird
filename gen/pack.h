/*
 * Tiny asset codec for the things code can't paint: an LZMA-style binary range coder with
 * adaptive models, used for
 *   - images: JPEG-like (YCbCr 4:2:0, 8x8 DCT) with context-modelled coefficients and an optional
 *     1-bit transparency mask; the game re-quantises them to 16-colour CLUTs at start-up
 *   - indexed images (the font): 4-bit indices with a left-neighbour context + a 16-colour CLUT
 *   - meshes: DVERT / DPRIM arrays as context-coded deltas
 * The encoder half (PACK_ENCODER) is only built into tools/packer.
 */
#ifndef GEN_PACK_H
#define GEN_PACK_H

typedef unsigned char  pk_u8;
typedef unsigned short pk_u16;
typedef unsigned int   pk_u32;

/* ---- decoding (game) */
typedef struct { int w, h; float *rgb; unsigned char *mask; } PkImage;          /* rgb 0..255, w*h*3; mask w*h or 0 */
int  pk_image(const pk_u8 *data, int size, PkImage *out);                        /* malloc'd buffers */
int  pk_indexed(const pk_u8 *data, int size, int *w, int *h, pk_u8 **idx, pk_u16 clut[16]);
#ifdef DMODEL_TYPES_DEFINED
int  pk_mesh(const pk_u8 *data, int size, DVERT **verts, int *nv, DPRIM **prims, int *np);
#endif
/* the image's 16-colour re-quantisation: see gen/tex.h quant16() */

/* ---- encoding (tools/packer only) */
#ifdef PACK_ENCODER
typedef struct { pk_u8 *buf; int n, cap; pk_u32 range; unsigned long long low; pk_u8 cache; long long csize; } PkEnc;
pk_u8 *pk_enc_image(const pk_u8 *rgb, const pk_u8 *mask, int w, int h, float quality, int full_chroma, int *size);
pk_u8 *pk_enc_indexed(const pk_u8 *idx, int w, int h, const pk_u16 clut[16], int *size);
pk_u8 *pk_enc_mesh_ints(const int *v3, int nv, const int *p13, int np, int *size);   /* x,y,z; n,v[4],u[4],t[4] */
#endif

#endif
