#include "ik.h"

s32 vec_len(const VECTOR *v)
{
    /* components are model units (|v| < ~16000) so squares fit 32 bits */
    return (s32)isqrt((u32)(v->vx * v->vx + v->vy * v->vy + v->vz * v->vz));
}

void vec_norm(const VECTOR *v, VECTOR *o)
{
    s32 l = vec_len(v);
    s32 inv;
    if (l == 0) { o->vx = 0; o->vy = 0; o->vz = ONE; return; }
    /* one divide instead of three (the R3000 divides in ~36 cycles): |v| / l <= 1, so v * inv
       stays under 2^24 */
    inv = (1 << 24) / l;
    o->vx = (v->vx * inv) >> 12; o->vy = (v->vy * inv) >> 12; o->vz = (v->vz * inv) >> 12;
}

s32 vec_dot12(const VECTOR *a, const VECTOR *b)
{
    return (a->vx * b->vx + a->vy * b->vy + a->vz * b->vz) >> 12;
}

void vec_cross12(const VECTOR *a, const VECTOR *b, VECTOR *o)
{
    VECTOR r;
    r.vx = (a->vy * b->vz - a->vz * b->vy) >> 12;
    r.vy = (a->vz * b->vx - a->vx * b->vz) >> 12;
    r.vz = (a->vx * b->vy - a->vy * b->vx) >> 12;
    *o = r;
}

void mat_transpose(const MATRIX *m, MATRIX *o)
{
    MATRIX r;
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) r.m[i][j] = m->m[j][i];
    r.t[0] = r.t[1] = r.t[2] = 0;
    *o = r;
}

void mat_rot_apply(const MATRIX *m, const VECTOR *v, VECTOR *o)
{
    VECTOR r;
    r.vx = (m->m[0][0] * v->vx + m->m[0][1] * v->vy + m->m[0][2] * v->vz) >> 12;
    r.vy = (m->m[1][0] * v->vx + m->m[1][1] * v->vy + m->m[1][2] * v->vz) >> 12;
    r.vz = (m->m[2][0] * v->vx + m->m[2][1] * v->vy + m->m[2][2] * v->vz) >> 12;
    *o = r;
}

void mat_rot_tapply(const MATRIX *m, const VECTOR *v, VECTOR *o)
{
    VECTOR r;
    r.vx = (m->m[0][0] * v->vx + m->m[1][0] * v->vy + m->m[2][0] * v->vz) >> 12;
    r.vy = (m->m[0][1] * v->vx + m->m[1][1] * v->vy + m->m[2][1] * v->vz) >> 12;
    r.vz = (m->m[0][2] * v->vx + m->m[1][2] * v->vy + m->m[2][2] * v->vz) >> 12;
    *o = r;
}

/* orthonormal frame (x along dir, y toward pole) */
static void frame(const VECTOR *dir, const VECTOR *pole, VECTOR *x, VECTOR *y, VECTOR *z)
{
    VECTOR p;
    s32 d;
    vec_norm(dir, x);
    d = vec_dot12(pole, x);
    p.vx = pole->vx - (x->vx * d >> 12);
    p.vy = pole->vy - (x->vy * d >> 12);
    p.vz = pole->vz - (x->vz * d >> 12);
    if (vec_len(&p) < 200) {               /* pole parallel to the bone: pick any perpendicular */
        VECTOR alt = { 0, 0, ONE, 0 };
        if (x->vz > 3000 || x->vz < -3000) { alt.vy = ONE; alt.vz = 0; }
        d = vec_dot12(&alt, x);
        p.vx = alt.vx - (x->vx * d >> 12);
        p.vy = alt.vy - (x->vy * d >> 12);
        p.vz = alt.vz - (x->vz * d >> 12);
    }
    vec_norm(&p, y);
    vec_cross12(x, y, z);
    vec_norm(z, z);
    vec_cross12(z, x, y);                  /* re-orthogonalise */
}

/* R = F2 * F1^T : rotates frame 1 onto frame 2 */
static void rot_between(const VECTOR *x1, const VECTOR *y1, const VECTOR *z1,
                        const VECTOR *x2, const VECTOR *y2, const VECTOR *z2, MATRIX *R)
{
    const s32 a1[3][3] = { { x1->vx, x1->vy, x1->vz }, { y1->vx, y1->vy, y1->vz }, { z1->vx, z1->vy, z1->vz } };
    const s32 a2[3][3] = { { x2->vx, x2->vy, x2->vz }, { y2->vx, y2->vy, y2->vz }, { z2->vx, z2->vy, z2->vz } };
    int i, j, k;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            s32 s = 0;
            for (k = 0; k < 3; k++) s += a2[k][i] * a1[k][j];
            R->m[i][j] = (s16)(s >> 12);
        }
    R->t[0] = R->t[1] = R->t[2] = 0;
}

void ik_two_bone(const VECTOR *a, const VECTOR *r1, const VECTOR *r2,
                 const VECTOR *target, const VECTOR *pole, MATRIX *R1, MATRIX *R2)
{
    VECTOR dv, D, pp, dir1, dir2, k, tc, x1, y1, z1, x2, y2, z2, pole_l, dir2_l;
    s32 L1 = vec_len(r1), L2 = vec_len(r2), d, c, s, den, pd;

    dv.vx = target->vx - a->vx; dv.vy = target->vy - a->vy; dv.vz = target->vz - a->vz;
    d = vec_len(&dv);
    vec_norm(&dv, &D);
    if (d > L1 + L2 - 4) d = L1 + L2 - 4;
    if (d < (L1 > L2 ? L1 - L2 : L2 - L1) + 4) d = (L1 > L2 ? L1 - L2 : L2 - L1) + 4;

    /* law of cosines: angle at joint A between A->C and A->B */
    den = (2 * L1 * d) >> 6;
    c = den ? ((L1 * L1 + d * d - L2 * L2) << 6) / den : ONE;
    if (c > ONE) c = ONE;
    if (c < -ONE) c = -ONE;
    s = (s32)isqrt((u32)(ONE * ONE - c * c));

    /* bend direction: pole projected off the A->C line */
    pd = vec_dot12(pole, &D);
    pp.vx = pole->vx - (D.vx * pd >> 12);
    pp.vy = pole->vy - (D.vy * pd >> 12);
    pp.vz = pole->vz - (D.vz * pd >> 12);
    vec_norm(&pp, &pp);

    dir1.vx = (D.vx * c + pp.vx * s) >> 12;
    dir1.vy = (D.vy * c + pp.vy * s) >> 12;
    dir1.vz = (D.vz * c + pp.vz * s) >> 12;
    vec_norm(&dir1, &dir1);
    k.vx = a->vx + (dir1.vx * L1 >> 12);
    k.vy = a->vy + (dir1.vy * L1 >> 12);
    k.vz = a->vz + (dir1.vz * L1 >> 12);
    tc.vx = a->vx + (D.vx * d >> 12);
    tc.vy = a->vy + (D.vy * d >> 12);
    tc.vz = a->vz + (D.vz * d >> 12);
    dir2.vx = tc.vx - k.vx; dir2.vy = tc.vy - k.vy; dir2.vz = tc.vz - k.vz;

    /* bone A: rest direction r1 -> dir1, twist fixed by the pole */
    frame(r1, pole, &x1, &y1, &z1);
    frame(&dir1, pole, &x2, &y2, &z2);
    rot_between(&x1, &y1, &z1, &x2, &y2, &z2, R1);

    /* bone B, in A's rotated frame: rest r2 -> dir2 */
    mat_rot_tapply(R1, &dir2, &dir2_l);
    mat_rot_tapply(R1, pole, &pole_l);
    frame(r2, &pole_l, &x1, &y1, &z1);
    frame(&dir2_l, &pole_l, &x2, &y2, &z2);
    rot_between(&x1, &y1, &z1, &x2, &y2, &z2, R2);
}
