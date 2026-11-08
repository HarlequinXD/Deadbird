/*
 * Fixed-point inverse kinematics (PS1-safe: 32-bit integer maths only).
 * Unit vectors are 4.12 (4096 = 1.0); positions are model units.
 */
#ifndef IK_H
#define IK_H

#include "gte.h"

s32  vec_len(const VECTOR *v);
void vec_norm(const VECTOR *v, VECTOR *out);                  /* out = unit 4.12 */
s32  vec_dot12(const VECTOR *a, const VECTOR *b);             /* (a.b) >> 12 */
void vec_cross12(const VECTOR *a, const VECTOR *b, VECTOR *o);/* (a x b) >> 12 */

void mat_transpose(const MATRIX *m, MATRIX *o);               /* rotation part only, t cleared */
void mat_rot_apply(const MATRIX *m, const VECTOR *v, VECTOR *o);   /* R*v >> 12 (no translation) */
void mat_rot_tapply(const MATRIX *m, const VECTOR *v, VECTOR *o);  /* R^T*v >> 12 */

/*
 * Two-bone IK (shoulder-elbow-wrist / hip-knee-ankle) solved in the parent bone's space.
 *   a      : position of joint A (shoulder/hip) in parent space
 *   r1     : rest offset A->B (elbow/knee) in A's local space
 *   r2     : rest offset B->C (wrist/ankle) in B's local space
 *   target : where C should go (parent space); clamped to reach
 *   pole   : unit 4.12 hint the middle joint bends toward (parent space)
 * Outputs local rotations: R1 for bone A (relative to parent), R2 for bone B (relative to A).
 */
void ik_two_bone(const VECTOR *a, const VECTOR *r1, const VECTOR *r2,
                 const VECTOR *target, const VECTOR *pole, MATRIX *R1, MATRIX *R2);

#endif
