/*
 * Keyframed joint-angle (forward kinematics) animation for the upper body and pelvis tilt.
 * A pose is a set of joint angles in DEGREES (converted when the bones are built). The legs
 * are placed by foot IK in player.c (the clips' leg channels are only used as a fallback).
 */
#ifndef FK_H
#define FK_H

#include "model.h"

enum {
    FK_PY,                         /* extra pelvis drop (+ = down), model units */
    FK_PRX, FK_PRY, FK_PRZ,        /* pelvis pitch (- = lean fwd), twist (+ = turn to her left), roll */
    FK_TRX, FK_TRY, FK_TRZ,        /* torso relative to pelvis */
    FK_HRX, FK_HRY,                /* head (neck takes half): pitch (+ = look up), yaw */
    FK_LSD, FK_LSS, FK_LEL,        /* left arm: shoulder down from T-pose, swing (+ = forward), elbow bend */
    FK_RSD, FK_RSS, FK_REL,        /* right arm */
    FK_LHP, FK_LKN, FK_LAN, FK_LAB, /* left leg: hip (+ = forward), knee bend, ankle (+ = toe down), abduct (+ = out) */
    FK_RHP, FK_RKN, FK_RAN, FK_RAB, /* right leg */
    FK_RFR,                        /* right forearm roll about its own axis (turns the chainsaw blade) */
    FK_COUNT
};

typedef struct { s16 v[FK_COUNT]; } FKPose;
typedef struct { u8 frame; s16 v[FK_COUNT]; } FKKey;

typedef struct {
    const char *name;
    const FKKey *keys;
    u8 nkeys, length, loop;
    u8 contact[2], liftoff[2];     /* cycles: frames each foot touches down / lifts off (left, right) */
} FKClip;

void fk_sample(const FKClip *c, s32 t256, FKPose *out);              /* t in 1/256 frames */
void fk_blend(const FKPose *a, const FKPose *b, int w, FKPose *out); /* w 0..4096 */

/* leg measurements taken from the model */
void fk_init(const Model *m);
/* thigh (hip->knee) and calf (knee->ankle) lengths, model units */
s32  fk_thigh_len(void);
s32  fk_calf_len(void);
/* pelvis rotation of a pose */
void fk_pelvis_rot(const FKPose *p, MATRIX *R);

/* build bone rotations; root_off receives the pelvis offset (grounding already applied by caller via FK_PY) */
void fk_bones(const Model *m, const FKPose *p, MATRIX *pose, VECTOR *root_off);

#endif
