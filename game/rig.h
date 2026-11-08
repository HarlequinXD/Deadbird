/* Full-body IK rig: AnimPose (targets) -> per-bone local rotations for model_skeleton(). */
#ifndef RIG_H
#define RIG_H

#include "model.h"
#include "anim.h"

/* pose[] receives one local rotation per bone; root_off receives the pelvis offset
   that must be added to the fighter's root translation */
void rig_solve(const Model *m, const AnimPose *ap, MATRIX *pose, VECTOR *root_off);

#endif
