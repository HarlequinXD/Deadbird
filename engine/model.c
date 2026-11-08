#include "model.h"

/* PS1 "scratchpad" style working set: projected vertices for the current model */
static s16 sx[MODEL_MAX_VERTS], sy[MODEL_MAX_VERTS];
static s32 sz[MODEL_MAX_VERTS];
static u8  ok[MODEL_MAX_VERTS];

void model_draw(const Model *m, const MATRIX *root, const MATRIX *pose,
                const DrawCtx *c, FrameBuf *fb, MATRIX *bone_world)
{
    static MATRIX world[MODEL_MAX_BONES];
    int b, i, k;

    for (b = 0; b < m->nbones && b < MODEL_MAX_BONES; b++) {
        const DBONE *bn = &m->bones[b];
        MATRIX local = pose[b];
        mat_translate(&local, bn->ox, bn->oy, bn->oz);
        mat_mul(bn->parent >= 0 ? &world[bn->parent] : root, &local, &world[b]);
        if (bone_world) bone_world[b] = world[b];

        /* transform + project this bone's vertices (RotTransPers) */
        for (i = bn->v0; i < bn->v0 + bn->vn && i < MODEL_MAX_VERTS; i++) {
            SVECTOR sv; VECTOR cv;
            sv.vx = m->verts[i].x; sv.vy = m->verts[i].y; sv.vz = m->verts[i].z;
            mat_apply(&world[b], &sv, &cv);
            ok[i] = (u8)rot_trans_pers(&c->proj, &cv, &sx[i], &sy[i]);
            sz[i] = cv.vz;
        }

        /* emit this bone's primitives */
        for (i = bn->p0; i < bn->p0 + bn->pn; i++) {
            const DPRIM *dp = &m->prims[i];
            const unsigned short *vi = dp->v;
            s32 nc, zsum;
            POLY *p;
            int n = dp->n;
            if (!ok[vi[0]] || !ok[vi[1]] || !ok[vi[2]] || (n == 4 && !ok[vi[3]])) continue;   /* near clip */
            nc = normal_clip(sx[vi[0]], sy[vi[0]], sx[vi[1]], sy[vi[1]], sx[vi[2]], sy[vi[2]]);
            if (nc * c->cull_sign <= 0) continue;                                             /* back face */
            zsum = sz[vi[0]] + sz[vi[1]] + sz[vi[2]];
            if (n == 4) { zsum += sz[vi[3]]; zsum >>= 2; } else zsum /= 3;
            p = prim_alloc(fb);
            if (!p) return;
            p->code = (u8)(PF_TEX | (n == 4 ? PF_QUAD : 0));
            p->tpage = m->tpage; p->clut = m->clut;
            for (k = 0; k < n; k++) {
                p->v[k].x = sx[vi[k]]; p->v[k].y = sy[vi[k]];
                p->v[k].u = dp->u[k]; p->v[k].v = dp->t[k];
                p->v[k].r = c->r; p->v[k].g = c->g; p->v[k].b = c->b;
            }
            ot_add(fb, zsum >> c->ot_shift, p);
        }
    }
}
