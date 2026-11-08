/*
 * Full-body IK pose description.
 * A pose is a handful of targets in character root space (Y down, facing +Z, +X = her left):
 *   pelvis offset + rotation, torso rotation, head rotation,
 *   wrist targets for both hands, ankle targets for both feet.
 * The rig (rig.c) solves arms and legs with two-bone IK, so feet stay planted and fists
 * land where the animation says.
 */
#ifndef ANIM_H
#define ANIM_H

#include "psx.h"

enum {
    AP_PX, AP_PY, AP_PZ,          /* pelvis offset from rest (model units) */
    AP_PRX, AP_PRY, AP_PRZ,       /* pelvis rotation (4096 = 360 deg) */
    AP_TRX, AP_TRY, AP_TRZ,       /* torso rotation relative to pelvis */
    AP_HRX, AP_HRY,               /* head rotation (neck takes half) */
    AP_LHX, AP_LHY, AP_LHZ,       /* left wrist target */
    AP_RHX, AP_RHY, AP_RHZ,       /* right wrist target */
    AP_LFX, AP_LFY, AP_LFZ,       /* left ankle target */
    AP_RFX, AP_RFY, AP_RFZ,       /* right ankle target */
    AP_COUNT
};

typedef struct { s16 v[AP_COUNT]; } AnimPose;

typedef struct { u8 frame; s16 v[AP_COUNT]; } AnimKey;

typedef struct {
    const char *name;
    const AnimKey *keys;
    u8 nkeys;
    u8 length;                    /* frames */
    u8 loop;
} Clip;

/* optional keyframed overlays (t in 1/256 frames); locomotion itself is procedural (player.c) */
void anim_sample(const Clip *c, s32 t, AnimPose *out);
/* out = a + (b - a) * w / 4096 */
void anim_blend(const AnimPose *a, const AnimPose *b, int w, AnimPose *out);

#endif
