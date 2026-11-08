#include "gte.h"
#include "sintab.h"

int isin(int a)
{
    a &= 4095;
    if (a < 1024) return SINTAB[a];
    if (a < 2048) return SINTAB[2048 - a];
    if (a < 3072) return -SINTAB[a - 2048];
    return -SINTAB[4096 - a];
}

int icos(int a) { return isin(a + 1024); }

void mat_identity(MATRIX *m)
{
    memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = ONE;
}

void mat_rot_x(MATRIX *m, int a)
{
    int s = isin(a), c = icos(a);
    mat_identity(m);
    m->m[1][1] = (s16)c; m->m[1][2] = (s16)-s;
    m->m[2][1] = (s16)s; m->m[2][2] = (s16)c;
}

void mat_rot_y(MATRIX *m, int a)
{
    int s = isin(a), c = icos(a);
    mat_identity(m);
    m->m[0][0] = (s16)c;  m->m[0][2] = (s16)s;
    m->m[2][0] = (s16)-s; m->m[2][2] = (s16)c;
}

void mat_rot_z(MATRIX *m, int a)
{
    int s = isin(a), c = icos(a);
    mat_identity(m);
    m->m[0][0] = (s16)c; m->m[0][1] = (s16)-s;
    m->m[1][0] = (s16)s; m->m[1][1] = (s16)c;
}

void mat_mul(const MATRIX *a, const MATRIX *b, MATRIX *out)
{
    MATRIX r;
    int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            r.m[i][j] = (s16)((a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j]) >> 12);
        r.t[i] = ((a->m[i][0] * b->t[0] + a->m[i][1] * b->t[1] + a->m[i][2] * b->t[2]) >> 12) + a->t[i];
    }
    *out = r;
}

void mat_translate(MATRIX *m, s32 x, s32 y, s32 z)
{
    m->t[0] = x; m->t[1] = y; m->t[2] = z;
}

void mat_apply(const MATRIX *m, const SVECTOR *v, VECTOR *o)
{
    o->vx = ((m->m[0][0] * v->vx + m->m[0][1] * v->vy + m->m[0][2] * v->vz) >> 12) + m->t[0];
    o->vy = ((m->m[1][0] * v->vx + m->m[1][1] * v->vy + m->m[1][2] * v->vz) >> 12) + m->t[1];
    o->vz = ((m->m[2][0] * v->vx + m->m[2][1] * v->vy + m->m[2][2] * v->vz) >> 12) + m->t[2];
}

int rot_trans_pers(const PROJ *p, const VECTOR *c, s16 *sx, s16 *sy)
{
    s32 x, y;
    if (c->vz < p->near_z) return 0;
    x = p->ofx + (c->vx * p->h) / c->vz;
    y = p->ofy + (c->vy * p->h) / c->vz;
    /* GPU takes 11-bit signed coords */
    if (x < -1024) x = -1024;
    if (x > 1023) x = 1023;
    if (y < -1024) y = -1024;
    if (y > 1023) y = 1023;
    *sx = (s16)x; *sy = (s16)y;
    return 1;
}

u32 isqrt(u32 x)
{
    u32 r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}
