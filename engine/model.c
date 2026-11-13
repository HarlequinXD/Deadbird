#include "model.h"
#include "gte_hw.h"

/* PS1 "scratchpad" style working set: projected vertices for the current model */
#ifndef PSX_GTE
static s32 sz[MODEL_MAX_VERTS];
static s16 sx[MODEL_MAX_VERTS], sy[MODEL_MAX_VERTS];
static u8  ok[MODEL_MAX_VERTS];
#endif

void model_skeleton(const Model *m, const MATRIX *root, const MATRIX *pose, MATRIX *out)
{
    int b;
    for (b = 0; b < m->nbones && b < MODEL_MAX_BONES; b++) {
        const DBONE *bn = &m->bones[b];
        MATRIX local = pose[b];
        mat_translate(&local, bn->ox, bn->oy, bn->oz);
        mat_mul(bn->parent >= 0 ? &out[bn->parent] : root, &local, &out[b]);
    }
}

#ifdef PSX_GTE
/*
 * The PS1 path. The console has no data cache, so every load and store to main RAM stalls the
 * CPU; this loop keeps them to a minimum: the GTE reads each vertex straight from memory, a vertex
 * keeps its screen x,y packed in one word plus one byte of clip flags (behind the camera, off each
 * screen edge), so a primitive's near-clip and off-screen tests are one OR and one AND; the GTE
 * does the back-face test (NCLIP); and a flat-coloured primitive only gets its colour written once.
 */
typedef u32 __attribute__((may_alias)) u32a;
static u32 vxy[MODEL_MAX_VERTS];
static u8  vfl[MODEL_MAX_VERTS];
static s16 vzz[MODEL_MAX_VERTS];
enum { CL_L = 1, CL_R = 2, CL_T = 4, CL_B = 8, CL_NEAR = 16 };

/* the PS1's 1 KB scratchpad (on-chip, no wait states): a bone whose primitives only use its own
   vertices, at most SP_N of them, keeps its working arrays there */
#define SP_N 144
#define SP_XY ((u32 *)0x1F800000)
#define SP_Z  ((s16 *)(0x1F800000 + SP_N * 4))
#define SP_FL ((u8 *)(0x1F800000 + SP_N * 6))
static struct { const Model *m; u32 ok; } sp_cache[8];     /* per model: bit b = bone b fits */
static u32 sp_bones(const Model *m)
{
    int k, b, i, j;
    u32 ok = 0;
    for (k = 0; k < 8; k++) if (sp_cache[k].m == m) return sp_cache[k].ok;
    for (b = 0; b < m->nbones && b < 32; b++) {
        const DBONE *bn = &m->bones[b];
        int fits = bn->vn <= SP_N;
        for (i = bn->p0; fits && i < bn->p0 + bn->pn; i++)
            for (j = 0; j < m->prims[i].n; j++)
                if ((unsigned)(m->prims[i].v[j] - bn->v0) >= bn->vn) fits = 0;
        if (fits) ok |= 1u << b;
        else { ok = 0; break; }          /* all or none: another bone may use this one's vertices */
    }
    for (k = 0; k < 8; k++) if (!sp_cache[k].m) { sp_cache[k].m = m; sp_cache[k].ok = ok; break; }
    return ok;
}

static void draw_mats_gte(const Model *m, const MATRIX *cam, const DrawCtx *c, FrameBuf *fb)
{
    int b, i;
    s32 near = c->proj.near_z, half = c->proj.h >> 1, sw = SCREEN_W;
    int shift = c->ot_shift, cs = c->cull_sign;
    u8 code3 = PF_TEX, code4 = PF_TEX | PF_QUAD;
    u16 tpage = m->tpage, clut = m->clut;
    u8 cr = c->r, cg = c->g, cb = c->b;
    u32 spok = sp_bones(m);
    gte_load_proj(&c->proj);
    for (b = 0; b < m->nbones && b < MODEL_MAX_BONES; b++) {
        const DBONE *bn = &m->bones[b];
        int v1 = bn->v0 + bn->vn;
        /* this bone's arrays, indexed by the model's vertex numbers */
        u32 *XY = vxy; s16 *Z = vzz; u8 *FL = vfl;
        if (b < 32 && ((spok >> b) & 1)) { XY = SP_XY - bn->v0; Z = SP_Z - bn->v0; FL = SP_FL - bn->v0; }
        if (v1 > MODEL_MAX_VERTS) v1 = MODEL_MAX_VERTS;
        gte_load_matrix(&cam[b]);
        for (i = bn->v0; i < v1; i++) {
            const DVERT *v = &m->verts[i];
            u32 xy; s32 z, x, y; u32 f;
            __asm__ volatile(
                "lwc2  $0, 0(%2)\n\t"      /* VXY0 */
                "lwc2  $1, 4(%2)\n\t"      /* VZ0 (the pad half is ignored) */
                "nop\n\t" "nop\n\t"
                "cop2  0x0180001\n\t"      /* RTPS */
                "mfc2  %0, $14\n\t"        /* SXY2 */
                "mfc2  %1, $27\n\t"        /* MAC3 = view z */
                "nop\n\t"
                : "=r"(xy), "=r"(z) : "r"(v) : "memory");
            x = (s16)(xy & 0xffff); y = (s32)xy >> 16;
            f = (z < near || z <= half) ? CL_NEAR : 0;
            if (x < 0) f |= CL_L; else if (x >= sw) f |= CL_R;
            if (y < 0) f |= CL_T; else if (y >= SCREEN_H) f |= CL_B;
            XY[i] = xy; FL[i] = (u8)f; Z[i] = (s16)(z > 32767 ? 32767 : z);
        }
        for (i = bn->p0; i < bn->p0 + bn->pn; i++) {
            const DPRIM *dp = &m->prims[i];
            int a = dp->v[0], bb = dp->v[1], cc = dp->v[2], d = 0, quad = dp->n == 4;
            u32 fa = FL[a] & FL[bb] & FL[cc], fo = FL[a] | FL[bb] | FL[cc];
            u32 p0 = XY[a], p1 = XY[bb], p2 = XY[cc];
            s32 nc, zs;
            POLY *p;
            if (quad) { d = dp->v[3]; fa &= FL[d]; fo |= FL[d]; }
            if ((fo & CL_NEAR) || (fa & (CL_L | CL_R | CL_T | CL_B))) continue;
            __asm__ volatile(
                "mtc2  %1, $12\n\t" "mtc2  %2, $13\n\t" "mtc2  %3, $14\n\t"
                "nop\n\t" "nop\n\t"
                "cop2  0x1400006\n\t"      /* NCLIP */
                "mfc2  %0, $24\n\t"        /* MAC0 */
                "nop\n\t"
                : "=r"(nc) : "r"(p0), "r"(p1), "r"(p2));
            if (nc * cs <= 0) continue;                                        /* back face */
            if (quad) zs = (Z[a] + Z[bb] + Z[cc] + Z[d]) >> 2;
            else      zs = (((Z[a] + Z[bb] + Z[cc]) >> 2) * 21845) >> 14;   /* / 3 without a divide */
            if (fb->nprims >= PRIM_MAX) return;
            p = &fb->prims[fb->nprims++];
            p->tpage = tpage; p->clut = clut;
            *(u32a *)&p->v[0].x = p0; p->v[0].u = dp->u[0]; p->v[0].v = dp->t[0];
            *(u32a *)&p->v[1].x = p1; p->v[1].u = dp->u[1]; p->v[1].v = dp->t[1];
            *(u32a *)&p->v[2].x = p2; p->v[2].u = dp->u[2]; p->v[2].v = dp->t[2];
            p->v[0].r = cr; p->v[0].g = cg; p->v[0].b = cb;     /* flat: only vertex 0's colour is used */
            if (quad) {
                p->code = code4;
                *(u32a *)&p->v[3].x = XY[d]; p->v[3].u = dp->u[3]; p->v[3].v = dp->t[3];
            } else p->code = code3;
            zs >>= shift;
            if (zs < 0) zs = 0; else if (zs >= OT_LEN) zs = OT_LEN - 1;
            p->next = fb->ot[zs]; fb->ot[zs] = p;
        }
    }
}
#endif

void model_draw_mats(const Model *m, const MATRIX *cam, const DrawCtx *c, FrameBuf *fb)
{
    int b, i, k;
#ifdef PSX_GTE
    (void)b; (void)i; (void)k;
    draw_mats_gte(m, cam, c, fb);
#else
    for (b = 0; b < m->nbones && b < MODEL_MAX_BONES; b++) {
        const DBONE *bn = &m->bones[b];
        /* transform + project this bone's vertices (RotTransPers) */
        for (i = bn->v0; i < bn->v0 + bn->vn && i < MODEL_MAX_VERTS; i++) {
            SVECTOR sv; VECTOR cv;
            sv.vx = m->verts[i].x; sv.vy = m->verts[i].y; sv.vz = m->verts[i].z;
            mat_apply(&cam[b], &sv, &cv);
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
            {   /* wholly off screen: skipped */
                s16 qx[4], qy[4];
                for (k = 0; k < n; k++) { qx[k] = sx[vi[k]]; qy[k] = sy[vi[k]]; }
                if (poly_off_screen(qx, qy, n)) continue;
            }
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
#endif
}

void model_draw(const Model *m, const MATRIX *root, const MATRIX *pose,
                const DrawCtx *c, FrameBuf *fb, MATRIX *bone_world)
{
    static MATRIX world[MODEL_MAX_BONES];
    model_skeleton(m, root, pose, world);
    if (bone_world) memcpy(bone_world, world, sizeof(MATRIX) * (size_t)m->nbones);
    model_draw_mats(m, world, c, fb);
}
