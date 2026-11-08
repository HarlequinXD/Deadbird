/*
 * Rigid hierarchical model (PS1 style): each bone owns a range of vertices (stored
 * local to the bone pivot) and a range of primitives. Every bone is drawn with its own
 * GTE matrix: world = parent_world * T(offset) * R(pose).
 */
#ifndef MODEL_H
#define MODEL_H

#include "gte.h"
#include "gpu.h"

/* layouts produced by tools/build_demon.py (see assets/demon_mesh.h) */
#ifndef DMODEL_TYPES_DEFINED
#define DMODEL_TYPES_DEFINED
typedef struct { short x, y, z, pad; } DVERT;
typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;
typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;
#endif

typedef struct {
    const DVERT *verts; int nverts;
    const DPRIM *prims; int nprims;
    const DBONE *bones; int nbones;
    u16 tpage, clut;
} Model;

#define MODEL_MAX_VERTS 512
#define MODEL_MAX_BONES 32

typedef struct {
    PROJ proj;
    int  ot_shift;       /* OT index = average camera z >> ot_shift */
    int  cull_sign;      /* which NCLIP sign is front facing */
    u8   r, g, b;        /* modulation colour (128 = texture as is) */
} DrawCtx;

/* pose[i] = rotation of bone i; root_world = model -> camera matrix.
   bone_world (optional, nbones entries) receives each bone's final matrix (for attachments). */
void model_draw(const Model *m, const MATRIX *root_world, const MATRIX *pose,
                const DrawCtx *ctx, FrameBuf *fb, MATRIX *bone_world);

#endif
