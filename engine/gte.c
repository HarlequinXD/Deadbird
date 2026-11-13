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

#ifdef PSX_GTE
/* On the PS1 the rotation part goes through the GTE: A in the light matrix (unused by this engine,
   so the rotation/translation registers that the draw code keeps loaded are left alone), then one
   MVMVA per column of B. sf=1 shifts by 12 like the C code; the translation stays in C (it can
   be past the GTE's 16-bit inputs). Same results as the C version for rotation matrices. */
void mat_mul(const MATRIX *a, const MATRIX *b, MATRIX *out)
{
    MATRIX r;
    int i, j;
    __asm__ volatile(
        "lw    $8, 0(%0)\n\t"  "lw    $9, 4(%0)\n\t"
        "ctc2  $8, $8\n\t"     "ctc2  $9, $9\n\t"
        "lw    $8, 8(%0)\n\t"  "lw    $9, 12(%0)\n\t"
        "lhu   $10, 16(%0)\n\t"
        "ctc2  $8, $10\n\t"    "ctc2  $9, $11\n\t"
        "ctc2  $10, $12\n\t"
        : : "r"(a) : "$8", "$9", "$10");
    for (j = 0; j < 3; j++) {
        u32 xy = ((u32)b->m[0][j] & 0xffff) | ((u32)b->m[1][j] << 16);
        s32 z = b->m[2][j], o1, o2, o3;
        __asm__ volatile(
            "mtc2  %3, $0\n\t" "mtc2  %4, $1\n\t"
            "nop\n\t" "nop\n\t"
            "cop2  0x04A6012\n\t"           /* MVMVA sf=1, L * V0, no translation */
            "mfc2  %0, $9\n\t" "mfc2  %1, $10\n\t" "mfc2  %2, $11\n\t"
            "nop\n\t"
            : "=r"(o1), "=r"(o2), "=r"(o3) : "r"(xy), "r"(z));
        r.m[0][j] = (s16)o1; r.m[1][j] = (s16)o2; r.m[2][j] = (s16)o3;
    }
    for (i = 0; i < 3; i++)
        r.t[i] = ((a->m[i][0] * b->t[0] + a->m[i][1] * b->t[1] + a->m[i][2] * b->t[2]) >> 12) + a->t[i];
    *out = r;
}
#else
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
#endif

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

/* angle of (dx, dz): 4096 = 360, 0 = +Z, 1024 = +X */
int ratan2(s32 dx, s32 dz)
{
    s32 ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz, z, a;
    if (ax == 0 && az == 0) return 0;
    while (ax > 0x7ffff || az > 0x7ffff) { ax >>= 1; az >>= 1; }
    if (ax <= az) { z = ax * 4096 / az; a = (512 * z >> 12) + (178 * ((z * (4096 - z)) >> 12) >> 12); }
    else          { z = az * 4096 / ax; a = 1024 - ((512 * z >> 12) + (178 * ((z * (4096 - z)) >> 12) >> 12)); }
    if (dz < 0) a = 2048 - a;
    if (dx < 0) a = -a;
    return a & 4095;
}
