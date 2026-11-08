/*
 * GTE-style fixed point geometry (the PS1 Geometry Transformation Engine model).
 * Matrices: 3x3 rotation in 4.12 + 32-bit translation, exactly like libgte's MATRIX,
 * so on PS1 these calls map 1:1 onto RotMatrix / CompMatrix / RotTransPers / NormalClip.
 */
#ifndef GTE_H
#define GTE_H

#include "psx.h"

typedef struct { s16 vx, vy, vz, pad; } SVECTOR;
typedef struct { s32 vx, vy, vz, pad; } VECTOR;
typedef struct { s16 m[3][3]; s32 t[3]; } MATRIX;

int  isin(int a);                 /* a: 4096 = 360 deg, returns 4.12 */
int  icos(int a);

void mat_identity(MATRIX *m);
void mat_rot_x(MATRIX *m, int a); /* rotation only, translation cleared */
void mat_rot_y(MATRIX *m, int a);
void mat_rot_z(MATRIX *m, int a);
void mat_mul(const MATRIX *a, const MATRIX *b, MATRIX *out);       /* out = a * b (with translations) */
void mat_translate(MATRIX *m, s32 x, s32 y, s32 z);                /* m.t = (x,y,z) */
void mat_apply(const MATRIX *m, const SVECTOR *v, VECTOR *out);    /* out = R*v>>12 + t */

/* projection (GTE "H" register and screen offset) */
typedef struct { s32 h, ofx, ofy, near_z; } PROJ;
/* returns 0 if the point is behind the near plane */
int  rot_trans_pers(const PROJ *p, const VECTOR *cam, s16 *sx, s16 *sy);
/* GTE NCLIP: >0 for one winding, <0 for the other */
static inline s32 normal_clip(s32 x0, s32 y0, s32 x1, s32 y1, s32 x2, s32 y2)
{
    return (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
}

/* integer square root (32-bit) */
u32 isqrt(u32 x);

#endif
