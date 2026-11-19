/* Ragdolls (see ragdoll.h): Verlet points, sticks, the level pushing back. */
#include <string.h>
#include "ragdoll.h"
#include "level.h"

/* the shared skeleton (demon_mesh.h / soldier_mesh.h): 0 pelvis, 1 torso, 2 neck, 3 head,
   4-6 left upper arm / forearm / fist, 7-9 right, 10-12 left thigh / calf / hoof, 13-15 right;
   then the ends: 16 head top, 17 left fist, 18 right fist, 19 left hoof, 20 right hoof */
enum { P_PELVIS = 0, P_TORSO = 1, P_NECK = 2, P_HEAD = 3, P_UARM_L = 4, P_FORE_L = 5, P_FIST_L = 6,
       P_UARM_R = 7, P_FORE_R = 8, P_FIST_R = 9, P_THIGH_L = 10, P_CALF_L = 11, P_HOOF_L = 12,
       P_THIGH_R = 13, P_CALF_R = 14, P_HOOF_R = 15,
       P_HEAD_END = 16, P_FIST_L_END = 17, P_FIST_R_END = 18, P_HOOF_L_END = 19, P_HOOF_R_END = 20 };

static const u8 LEAF[5] = { P_HEAD, P_FIST_L, P_FIST_R, P_HOOF_L, P_HOOF_R };

/* each bone's frame: the point it points at, and a second point off to one side */
static const u8 REF[16][2] = {
    { P_TORSO, P_THIGH_L }, { P_NECK, P_UARM_L }, { P_HEAD, P_UARM_L }, { P_HEAD_END, P_UARM_L },
    { P_FORE_L, P_UARM_R }, { P_FIST_L, P_UARM_R }, { P_FIST_L_END, P_UARM_R },
    { P_FORE_R, P_UARM_L }, { P_FIST_R, P_UARM_L }, { P_FIST_R_END, P_UARM_L },
    { P_CALF_L, P_THIGH_R }, { P_HOOF_L, P_THIGH_R }, { P_HOOF_L_END, P_THIGH_R },
    { P_CALF_R, P_THIGH_L }, { P_HOOF_R, P_THIGH_L }, { P_HOOF_R_END, P_THIGH_L },
};

static u8 stick_a[RAG_NS], stick_b[RAG_NS];     /* the same sticks for every body */

#define GRAVITY 45                 /* 2.8 units a tick squared (9.8 m/s^2 at 1024 units a metre), x16 */
#define FLOOR_GAP 24               /* how far a point stays off the floor: a limb's half thickness */
#define WALL_R 40
#ifdef PSX_GTE
#define ITER 2                     /* the console: fewer passes over the sticks */
#else
#define ITER 4
#endif

static s32 iabs(s32 v) { return v < 0 ? -v : v; }

static s32 dist(const s32 *a, const s32 *b)       /* x16 units in, world units out */
{
    s32 dx = (a[0] - b[0]) >> 4, dy = (a[1] - b[1]) >> 4, dz = (a[2] - b[2]) >> 4;
    return (s32)isqrt((u32)(dx * dx + dy * dy + dz * dz));
}

static int add(Ragdoll *r, int a, int b, int kind, s32 len)
{
    if (r->ns >= RAG_NS) return 0;
    stick_a[r->ns] = (u8)a; stick_b[r->ns] = (u8)b;
    r->kind[r->ns] = (u8)kind;
    r->len[r->ns] = (u16)(len > 65535 ? 65535 : len);
    r->ns++;
    return 1;
}

/* unit vector (4096 = 1) of v (world units); 0 if too short */
static int unit(const s32 *v, s32 *o)
{
    s32 l = (s32)isqrt((u32)(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    if (l < 4) return 0;
    o[0] = v[0] * 4096 / l; o[1] = v[1] * 4096 / l; o[2] = v[2] * 4096 / l;
    return 1;
}

/* an orthonormal frame (rows: the 3 axes, 4096 = 1) from a main direction and a side one */
static int frame(const s32 *a, const s32 *s, s32 B[3][3])
{
    s32 c[3];
    if (!unit(a, B[0])) return 0;
    c[0] = (B[0][1] * s[2] - B[0][2] * s[1]) >> 12;
    c[1] = (B[0][2] * s[0] - B[0][0] * s[2]) >> 12;
    c[2] = (B[0][0] * s[1] - B[0][1] * s[0]) >> 12;
    if (!unit(c, B[2])) return 0;
    B[1][0] = (B[2][1] * B[0][2] - B[2][2] * B[0][1]) >> 12;
    B[1][1] = (B[2][2] * B[0][0] - B[2][0] * B[0][2]) >> 12;
    B[1][2] = (B[2][0] * B[0][1] - B[2][1] * B[0][0]) >> 12;
    return 1;
}

void rag_start(Ragdoll *r, const Model *m, const MATRIX *world, s32 vx, s32 vy, s32 vz, s32 tx, s32 tz)
{
    int b, i, j;
    s32 py;
    memset(r, 0, sizeof(*r));
    for (b = 0; b < 16; b++) { r->p[b][0] = world[b].t[0] << 4; r->p[b][1] = world[b].t[1] << 4; r->p[b][2] = world[b].t[2] << 4; }
    for (i = 0; i < 5; i++) {   /* the ends: the furthest vertex of the head / fist / hoof from its joint */
        const DBONE *bn = &m->bones[LEAF[i]];
        s32 best = -1, lx = 0, ly = 0, lz = 0;
        SVECTOR s; VECTOR w;
        for (j = bn->v0; j < bn->v0 + bn->vn; j++) {
            const DVERT *v = &m->verts[j];
            s32 d = v->x * v->x + v->y * v->y + v->z * v->z;
            if (d > best) { best = d; lx = v->x; ly = v->y; lz = v->z; }
        }
        s.vx = (s16)lx; s.vy = (s16)ly; s.vz = (s16)lz;
        mat_apply(&world[LEAF[i]], &s, &w);
        r->p[16 + i][0] = w.vx << 4; r->p[16 + i][1] = w.vy << 4; r->p[16 + i][2] = w.vz << 4;
    }
    /* moving: everything at (vx, vy, vz), the upper body thrown along (tx, tz) the higher it is */
    py = r->p[P_PELVIS][1];
    for (i = 0; i < RAG_NP; i++) {
        s32 up = (py - r->p[i][1]) >> 4, k = up > 0 ? up : 0;   /* world units above the pelvis (y is down) */
        r->o[i][0] = r->p[i][0] - ((vx + (tx * k >> 12) / 60) << 4);
        r->o[i][1] = r->p[i][1] - (vy << 4);
        r->o[i][2] = r->p[i][2] - ((vz + (tz * k >> 12) / 60) << 4);
    }
    /* the sticks, as long as they are right now */
    r->ns = 0;
    {
        static const u8 T[7] = { P_PELVIS, P_TORSO, P_NECK, P_UARM_L, P_UARM_R, P_THIGH_L, P_THIGH_R };
        static const u8 H[6][2] = { { P_HEAD, P_UARM_L }, { P_HEAD, P_UARM_R }, { P_HEAD_END, P_UARM_L },
                                    { P_HEAD_END, P_UARM_R }, { P_HEAD_END, P_TORSO }, { P_HEAD, P_TORSO } };
        static const u8 E[5][2] = { { P_HEAD_END, P_HEAD }, { P_FIST_L_END, P_FIST_L }, { P_FIST_R_END, P_FIST_R },
                                    { P_HOOF_L_END, P_HOOF_L }, { P_HOOF_R_END, P_HOOF_R } };
        static const u8 W[4][2] = { { P_FIST_L_END, P_FORE_L }, { P_FIST_R_END, P_FORE_R },     /* stiff wrists, ankles */
                                    { P_HOOF_L_END, P_CALF_L }, { P_HOOF_R_END, P_CALF_R } };
        /* limbs can't fold shorter than this share of their length (no elbow / knee through itself) */
        static const u8 L[4][4] = { { P_UARM_L, P_FORE_L, P_FIST_L, 50 }, { P_UARM_R, P_FORE_R, P_FIST_R, 50 },
                                    { P_THIGH_L, P_CALF_L, P_HOOF_L, 60 }, { P_THIGH_R, P_CALF_R, P_HOOF_R, 60 } };
        for (i = 0; i < 7; i++) for (j = i + 1; j < 7; j++) add(r, T[i], T[j], 0, dist(r->p[T[i]], r->p[T[j]]));
        for (i = 0; i < 6; i++) add(r, H[i][0], H[i][1], 0, dist(r->p[H[i][0]], r->p[H[i][1]]));
        for (b = 1; b < 16; b++) {
            int par = m->bones[b].parent;
            if (par >= 0 && b != P_NECK && b != P_UARM_L && b != P_UARM_R && b != P_THIGH_L && b != P_THIGH_R)
                add(r, b, par, 0, dist(r->p[b], r->p[par]));           /* (the braced ones are in already) */
        }
        for (i = 0; i < 5; i++) add(r, E[i][0], E[i][1], 0, dist(r->p[E[i][0]], r->p[E[i][1]]));
        for (i = 0; i < 4; i++) add(r, W[i][0], W[i][1], 0, dist(r->p[W[i][0]], r->p[W[i][1]]));
        for (i = 0; i < 4; i++) {
            s32 full = dist(r->p[L[i][0]], r->p[L[i][1]]) + dist(r->p[L[i][1]], r->p[L[i][2]]);
            add(r, L[i][0], L[i][2], 1, full * L[i][3] / 100);
        }
    }
    /* each bone's frame from its two reference points, in the bone's own space */
    for (b = 0; b < 16; b++) {
        s32 d[2][3], loc[2][3], B[3][3];
        int k, q;
        for (q = 0; q < 2; q++) {
            const s32 *pp = r->p[REF[b][q]];
            d[q][0] = (pp[0] >> 4) - world[b].t[0]; d[q][1] = (pp[1] >> 4) - world[b].t[1]; d[q][2] = (pp[2] >> 4) - world[b].t[2];
            for (k = 0; k < 3; k++)    /* R^T d */
                loc[q][k] = (world[b].m[0][k] * d[q][0] + world[b].m[1][k] * d[q][1] + world[b].m[2][k] * d[q][2]) >> 12;
        }
        if (!frame(loc[0], loc[1], B)) { B[0][0] = 4096; B[0][1] = B[0][2] = 0; B[1][1] = 4096; B[1][0] = B[1][2] = 0; B[2][2] = 4096; B[2][0] = B[2][1] = 0; }
        for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) r->lb[b][i][k] = (s16)B[i][k];
        r->last[b] = world[b];
    }
}

int rag_step(Ragdoll *r)
{
    int i, it, k;
    s32 most = 0;
    if (r->frozen) return 0;
    r->impact = 0;
    for (i = 0; i < RAG_NP; i++) {                  /* Verlet: carry on, a touch of air drag, gravity */
        for (k = 0; k < 3; k++) {
            s32 v = r->p[i][k] - r->o[i][k];
            v -= v / 160;
            r->o[i][k] = r->p[i][k];
            r->p[i][k] += v + (k == 1 ? GRAVITY : 0);
        }
    }
    for (it = 0; it < ITER; it++) {                 /* pull the sticks back to length */
        for (i = 0; i < r->ns; i++) {
            s32 *a = r->p[stick_a[i]], *b = r->p[stick_b[i]];
            s32 dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
            s32 l = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4) + (dz >> 4) * (dz >> 4))) << 4;   /* x16 units */
            s32 want = (s32)r->len[i] << 4, e;
            if (l < 16) continue;
            if (r->kind[i] == 1 && l >= want) continue;  /* "at least": long enough */
            e = l - want;
            /* each end moves half the error along the stick (f: that share of the stick, 4096 = all) */
            {
                s32 f = (e > 0 ? (e > l ? l : e) : (e < -l ? -l : e)) * 2048 / l, mx = dx * f >> 12, my = dy * f >> 12, mz = dz * f >> 12;
                a[0] += mx; a[1] += my; a[2] += mz;
                b[0] -= mx; b[1] -= my; b[2] -= mz;
            }
        }
    }
    for (i = 0; i < RAG_NP; i++) {                  /* the level pushes back */
        s32 x = r->p[i][0] >> 4, y = r->p[i][1] >> 4, z = r->p[i][2] >> 4, nx, nz, fl;
        if (level_collide(&x, &z, y, WALL_R, &nx, &nz)) {
            r->p[i][0] = x << 4; r->p[i][2] = z << 4;
        }
        fl = level_ground(x, z, y) - FLOOR_GAP;
        if (y > fl) {                               /* on (or through) the floor: up out of it, with friction */
            s32 vy = (r->p[i][1] - r->o[i][1]) >> 4;
            if (vy > r->impact) r->impact = vy;
            r->p[i][1] = fl << 4;
            r->o[i][1] = r->p[i][1];
            r->o[i][0] = r->p[i][0] - (r->p[i][0] - r->o[i][0]) * 9 / 16;
            r->o[i][2] = r->p[i][2] - (r->p[i][2] - r->o[i][2]) * 9 / 16;
        }
        for (k = 0; k < 3; k++) { s32 v = iabs(r->p[i][k] - r->o[i][k]) >> 4; if (v > most) most = v; }
    }
    /* settled: nothing has moved more than a unit or two for a while (or it's been going for ages) */
    if (most <= 2) r->still++; else r->still = 0;
    if (r->still > 40) r->frozen = 1;
    return !r->frozen;
}

void rag_bones(Ragdoll *r, MATRIX *world)
{
    int b, i, j, k;
    for (b = 0; b < 16; b++) {
        s32 d[2][3], W[3][3];
        MATRIX *M = &world[b];
        int q;
        for (q = 0; q < 2; q++)
            for (k = 0; k < 3; k++) d[q][k] = (r->p[REF[b][q]][k] - r->p[b][k]) >> 4;
        if (frame(d[0], d[1], W)) {
            /* R = W L^T: what turns the bone's own frame onto where its points are now */
            for (i = 0; i < 3; i++)
                for (j = 0; j < 3; j++) {
                    s32 s = 0;
                    for (k = 0; k < 3; k++) s += W[k][i] * r->lb[b][k][j];
                    M->m[i][j] = (s16)(s >> 12);
                }
            r->last[b] = *M;
        } else *M = r->last[b];
        M->t[0] = r->p[b][0] >> 4; M->t[1] = r->p[b][1] >> 4; M->t[2] = r->p[b][2] >> 4;
    }
}

void rag_pelvis(const Ragdoll *r, s32 *x, s32 *y, s32 *z)
{
    *x = r->p[P_PELVIS][0] >> 4; *y = r->p[P_PELVIS][1] >> 4; *z = r->p[P_PELVIS][2] >> 4;
}
