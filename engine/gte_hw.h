/*
 * The real PS1 GTE (coprocessor 2) for the hot vertex loops. Only the PS1 build defines
 * PSX_GTE; every other platform uses the C fixed point in gte.c, which computes the same thing.
 *
 * gte_load_matrix: rotation + translation (our MATRIX has libgte's layout)
 * gte_load_proj:   screen offset and H (projection plane distance)
 * gte_xform:       RTPS on one vertex -> 32-bit view-space x,y,z (MAC1-3) and the projected
 *                  screen x,y (SXY2, saturated to -1024..1023 like the GPU wants)
 */
#ifndef GTE_HW_H
#define GTE_HW_H

#include "gte.h"

#ifdef PSX_GTE

static inline void gte_load_matrix(const MATRIX *m)
{
    __asm__ volatile(
        "lw    $8, 0(%0)\n\t"  "lw    $9, 4(%0)\n\t"
        "ctc2  $8, $0\n\t"     "ctc2  $9, $1\n\t"
        "lw    $8, 8(%0)\n\t"  "lw    $9, 12(%0)\n\t"
        "lhu   $10, 16(%0)\n\t"
        "ctc2  $8, $2\n\t"     "ctc2  $9, $3\n\t"
        "ctc2  $10, $4\n\t"
        "lw    $8, 20(%0)\n\t" "lw    $9, 24(%0)\n\t"
        "lw    $10, 28(%0)\n\t"
        "ctc2  $8, $5\n\t"     "ctc2  $9, $6\n\t"
        "ctc2  $10, $7\n\t"
        : : "r"(m) : "$8", "$9", "$10");
}

static inline void gte_load_proj(const PROJ *p)
{
    s32 ox = p->ofx << 16, oy = p->ofy << 16, h = p->h;
    __asm__ volatile(
        "ctc2  %0, $24\n\t" "ctc2  %1, $25\n\t" "ctc2  %2, $26\n\t"
        : : "r"(ox), "r"(oy), "r"(h));
}

/* v: view-space result, sxy: screen x in the low 16 bits, y in the high 16 */
static inline void gte_xform(s32 x, s32 y, s32 z, VECTOR *v, u32 *sxy)
{
    u32 xy = ((u32)x & 0xffff) | ((u32)y << 16);
    __asm__ volatile(
        "mtc2  %2, $0\n\t"
        "mtc2  %3, $1\n\t"
        "nop\n\t" "nop\n\t"
        "cop2  0x0180001\n\t"            /* RTPS, sf=1 */
        "swc2  $14, 0(%1)\n\t"           /* SXY2 (interlocks until RTPS is done) */
        "swc2  $25, 0(%0)\n\t"           /* MAC1..3 = view x,y,z */
        "swc2  $26, 4(%0)\n\t"
        "swc2  $27, 8(%0)\n\t"
        : : "r"(v), "r"(sxy), "r"(xy), "r"(z) : "memory");
}

#endif
#endif
