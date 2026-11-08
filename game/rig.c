#include "rig.h"
#include "ik.h"
#define DEMON_MESH_NO_DATA
#include "demon_mesh.h"

static void rot_yxz(MATRIX *out, int ry, int rx, int rz)   /* out = Ry * Rx * Rz */
{
    MATRIX a, b;
    mat_rot_y(&a, ry); mat_rot_x(&b, rx); mat_mul(&a, &b, out);
    mat_rot_z(&b, rz); mat_mul(out, &b, out);
}

static void bone_off(const Model *m, int b, VECTOR *v)
{
    v->vx = m->bones[b].ox; v->vy = m->bones[b].oy; v->vz = m->bones[b].oz;
}

/* child = parent * T(offset of bone b) * R */
static void chain(const Model *m, const MATRIX *parent, int b, const MATRIX *R, MATRIX *out)
{
    MATRIX l = *R;
    mat_translate(&l, m->bones[b].ox, m->bones[b].oy, m->bones[b].oz);
    mat_mul(parent, &l, out);
}

/* express a root-space point / direction in a bone's local space */
static void to_local(const MATRIX *M, const VECTOR *p, VECTOR *o)
{
    VECTOR d;
    d.vx = p->vx - M->t[0]; d.vy = p->vy - M->t[1]; d.vz = p->vz - M->t[2];
    mat_rot_tapply(M, &d, o);
}

static void limb(const Model *m, const MATRIX *parent_root, int b1, int b2, int b3,
                 const s16 *target, const VECTOR *pole_root, MATRIX *pose)
{
    VECTOR a, r1, r2, t, tl, pl;
    bone_off(m, b1, &a);
    bone_off(m, b2, &r1);
    bone_off(m, b3, &r2);
    t.vx = target[0]; t.vy = target[1]; t.vz = target[2];
    to_local(parent_root, &t, &tl);
    mat_rot_tapply(parent_root, pole_root, &pl);
    ik_two_bone(&a, &r1, &r2, &tl, &pl, &pose[b1], &pose[b2]);
}

void rig_solve(const Model *m, const AnimPose *ap, MATRIX *pose, VECTOR *root_off)
{
    const s16 *v = ap->v;
    MATRIX root, mp, mt, t1, t2, rot;
    VECTOR pole;
    int i, side;

    for (i = 0; i < m->nbones; i++) mat_identity(&pose[i]);

    root_off->vx = v[AP_PX]; root_off->vy = v[AP_PY]; root_off->vz = v[AP_PZ];
    mat_identity(&root);
    mat_translate(&root, v[AP_PX], v[AP_PY], v[AP_PZ]);

    /* spine: FK */
    rot_yxz(&pose[DEMON_BONE_PELVIS], v[AP_PRY], v[AP_PRX], v[AP_PRZ]);
    rot_yxz(&pose[DEMON_BONE_TORSO], v[AP_TRY], v[AP_TRX], v[AP_TRZ]);
    rot_yxz(&pose[DEMON_BONE_NECK], v[AP_HRY] / 2, v[AP_HRX] / 2, 0);
    rot_yxz(&pose[DEMON_BONE_HEAD], v[AP_HRY] - v[AP_HRY] / 2, v[AP_HRX] - v[AP_HRX] / 2, 0);
    chain(m, &root, DEMON_BONE_PELVIS, &pose[DEMON_BONE_PELVIS], &mp);
    chain(m, &mp, DEMON_BONE_TORSO, &pose[DEMON_BONE_TORSO], &mt);

    for (side = 0; side < 2; side++) {
        int sg = side ? -1 : 1;
        /* arms: elbows hang down, back and slightly out */
        pole.vx = sg * 1400; pole.vy = 3100; pole.vz = -2300;
        limb(m, &mt, side ? DEMON_BONE_UPPER_ARM_R : DEMON_BONE_UPPER_ARM_L,
             side ? DEMON_BONE_FOREARM_R : DEMON_BONE_FOREARM_L,
             side ? DEMON_BONE_FIST_R : DEMON_BONE_FIST_L,
             &v[side ? AP_RHX : AP_LHX], &pole, pose);

        /* legs: knees forward and slightly out */
        pole.vx = sg * 800; pole.vy = 0; pole.vz = 4000;
        limb(m, &mp, side ? DEMON_BONE_THIGH_R : DEMON_BONE_THIGH_L,
             side ? DEMON_BONE_CALF_R : DEMON_BONE_CALF_L,
             side ? DEMON_BONE_HOOF_R : DEMON_BONE_HOOF_L,
             &v[side ? AP_RFX : AP_LFX], &pole, pose);

        /* hooves stay level with the ground: undo the leg chain's rotation */
        {
            int th = side ? DEMON_BONE_THIGH_R : DEMON_BONE_THIGH_L;
            int ca = side ? DEMON_BONE_CALF_R : DEMON_BONE_CALF_L;
            int ho = side ? DEMON_BONE_HOOF_R : DEMON_BONE_HOOF_L;
            mat_mul(&pose[DEMON_BONE_PELVIS], &pose[th], &t1);
            mat_mul(&t1, &pose[ca], &t2);
            mat_transpose(&t2, &rot);
            pose[ho] = rot;
        }
    }
    (void)mt;
}
