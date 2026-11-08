#include "fk.h"
#define DEMON_MESH_NO_DATA
#include "demon_mesh.h"

static s32 L1, L2;                 /* thigh and calf lengths */


#define DEG(d) ((int)(d) * 4096 / 360)

void fk_init(const Model *m)
{
    const DBONE *c = &m->bones[DEMON_BONE_CALF_L], *h = &m->bones[DEMON_BONE_HOOF_L];
    L1 = (s32)isqrt((u32)(c->ox * c->ox + c->oy * c->oy + c->oz * c->oz));
    L2 = (s32)isqrt((u32)(h->ox * h->ox + h->oy * h->oy + h->oz * h->oz));
}

s32 fk_leg_reach(int hip, int knee)
{
    /* thigh swings by hip, calf by (hip - knee): vertical drop from hip to ankle */
    return (L1 * icos(DEG(hip)) >> 12) + (L2 * icos(DEG(hip - knee)) >> 12);
}

s32 fk_rest_reach(void) { return L1 + L2; }

static s32 leg_forward(int hip, int knee)
{
    return (L1 * isin(DEG(hip)) >> 12) + (L2 * isin(DEG(hip - knee)) >> 12);
}

s32 fk_cycle_distance(const FKClip *c)
{
    FKPose a, b;
    s32 travel, stance = c->liftoff[0] - c->contact[0];
    if (stance <= 0) stance += c->length;
    fk_sample(c, (s32)c->contact[0] << 8, &a);
    fk_sample(c, (s32)c->liftoff[0] << 8, &b);
    travel = leg_forward(a.v[FK_LHP] + a.v[FK_PRX], a.v[FK_LKN]) - leg_forward(b.v[FK_LHP] + b.v[FK_PRX], b.v[FK_LKN]);
    if (travel < 50) travel = 50;
    /* the body covers `travel` while the foot is planted, i.e. during stance/length of the cycle */
    return travel * c->length / stance;
}

static int ease(int t) { s32 tt = (s32)t * t >> 12; return (int)(3 * tt - (2 * (tt * t >> 12))); }

void fk_sample(const FKClip *c, s32 t, FKPose *out)
{
    s32 len = (s32)c->length << 8;
    int k, i, w;
    const FKKey *a, *b;
    if (c->loop) { t %= len; if (t < 0) t += len; }
    else { if (t < 0) t = 0; if (t > len) t = len; }
    for (k = 0; k + 1 < c->nkeys && ((s32)c->keys[k + 1].frame << 8) <= t; k++) ;
    a = &c->keys[k];
    if (k + 1 >= c->nkeys) { for (i = 0; i < FK_COUNT; i++) out->v[i] = a->v[i]; return; }
    b = &c->keys[k + 1];
    w = (int)(((t - ((s32)a->frame << 8)) << 12) / (((s32)(b->frame - a->frame)) << 8));
    if (!c->loop) w = ease(w);              /* one-shots ease, cycles stay linear (even foot speed) */
    for (i = 0; i < FK_COUNT; i++)
        out->v[i] = (s16)(a->v[i] + (((s32)(b->v[i] - a->v[i]) * w) >> 12));
}

void fk_blend(const FKPose *a, const FKPose *b, int w, FKPose *out)
{
    int i;
    for (i = 0; i < FK_COUNT; i++) out->v[i] = (s16)(a->v[i] + (((s32)(b->v[i] - a->v[i]) * w) >> 12));
}

static void rot_yxz(MATRIX *out, int ry, int rx, int rz)    /* out = Ry * Rx * Rz (angles 4096 = 360) */
{
    MATRIX a, b;
    mat_rot_y(&a, ry); mat_rot_x(&b, rx); mat_mul(&a, &b, out);
    mat_rot_z(&b, rz); mat_mul(out, &b, out);
}

void fk_bones(const Model *m, const FKPose *p, MATRIX *pose, VECTOR *off)
{
    const s16 *v = p->v;
    int i;
    for (i = 0; i < m->nbones; i++) mat_identity(&pose[i]);
    off->vx = 0; off->vy = v[FK_PY]; off->vz = 0;

    /* Y-down space: rx < 0 tips forward, rz > 0 swings +X downward */
    rot_yxz(&pose[DEMON_BONE_PELVIS], DEG(v[FK_PRY]), DEG(v[FK_PRX]), DEG(v[FK_PRZ]));
    rot_yxz(&pose[DEMON_BONE_TORSO], DEG(v[FK_TRY]), DEG(v[FK_TRX]), DEG(v[FK_TRZ]));
    rot_yxz(&pose[DEMON_BONE_NECK], DEG(v[FK_HRY]) / 2, DEG(v[FK_HRX]) / 2, 0);
    rot_yxz(&pose[DEMON_BONE_HEAD], DEG(v[FK_HRY]) - DEG(v[FK_HRY]) / 2, DEG(v[FK_HRX]) - DEG(v[FK_HRX]) / 2, 0);

    /* arms from the T-pose: drop (about Z), then swing forward (about X); elbows bend forward */
    rot_yxz(&pose[DEMON_BONE_UPPER_ARM_L], 0, DEG(v[FK_LSS]), DEG(v[FK_LSD]));
    rot_yxz(&pose[DEMON_BONE_UPPER_ARM_R], 0, DEG(v[FK_RSS]), -DEG(v[FK_RSD]));
    mat_rot_y(&pose[DEMON_BONE_FOREARM_L], -DEG(v[FK_LEL]));
    mat_rot_y(&pose[DEMON_BONE_FOREARM_R], DEG(v[FK_REL]));

    /* legs: hip pitch + abduction, knee bends the calf back, hoof stays level plus the ankle angle */
    rot_yxz(&pose[DEMON_BONE_THIGH_L], 0, DEG(v[FK_LHP]), -DEG(v[FK_LAB]));
    rot_yxz(&pose[DEMON_BONE_THIGH_R], 0, DEG(v[FK_RHP]), DEG(v[FK_RAB]));
    mat_rot_x(&pose[DEMON_BONE_CALF_L], -DEG(v[FK_LKN]));
    mat_rot_x(&pose[DEMON_BONE_CALF_R], -DEG(v[FK_RKN]));
    mat_rot_x(&pose[DEMON_BONE_HOOF_L], DEG(-(v[FK_LHP] - v[FK_LKN]) - v[FK_PRX] - v[FK_LAN]));
    mat_rot_x(&pose[DEMON_BONE_HOOF_R], DEG(-(v[FK_RHP] - v[FK_RKN]) - v[FK_PRX] - v[FK_RAN]));
}
