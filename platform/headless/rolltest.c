/* Finds, for every keyframe of the chainsaw combo clips, the right-forearm roll that turns the
   blade so its cutting edge (the bar's +/-sd axis) points along the way the blade is moving.
   Prints the values for tools/make_fkclips.py. */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gte.h"
#include "model.h"
#include "fk.h"
#include "fkclips.h"
#include "demon_mesh.h"

static Model m;

/* blade tip + edge direction (world) for clip c at time t (frames) with roll r (degrees) */
static void blade(const FKClip *c, double t, int r, double tip[3], double edge[3])
{
    FKPose p; MATRIX pose[DEMON_NBONES], world[DEMON_NBONES], root; VECTOR off, o;
    SVECTOR s;
    fk_sample(c, (s32)(t * 256), &p);
    p.v[FK_RFR] = (s16)r;
    fk_bones(&m, &p, pose, &off);
    mat_identity(&root);
    model_skeleton(&m, &root, pose, world);
    s.vx = (s16)(DEMON_MOUNT_X + DEMON_MOUNT_DX * 450 / 4096); s.vy = (s16)(DEMON_MOUNT_Y + DEMON_MOUNT_DY * 450 / 4096);
    s.vz = (s16)(DEMON_MOUNT_Z + DEMON_MOUNT_DZ * 450 / 4096);
    mat_apply(&world[DEMON_MOUNT_BONE], &s, &o);
    tip[0] = o.vx; tip[1] = o.vy; tip[2] = o.vz;
    /* edge normal: the saw's sd axis = cross(Y, D) in model space (Y up) -> engine y down: (Dz, 0, -Dx)?? */
    {
        const MATRIX *W = &world[DEMON_MOUNT_BONE];
        double sx = 141.0 / 4096 * 0 + 0.0344, sz = 0.9997;    /* S ~ +Z (her front) in the T-pose */
        int i;
        for (i = 0; i < 3; i++) edge[i] = (W->m[i][0] * sx + W->m[i][2] * sz) / 4096.0;
    }
}

int main(void)
{
    static const FKClip *const C[3] = { &clip_atk1, &clip_atk2, &clip_atk3 };
    int ci, k;
    m.verts = (const DVERT *)demon_verts; m.nverts = DEMON_NVERTS;
    m.prims = (const DPRIM *)demon_prims; m.nprims = DEMON_NPRIMS;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    fk_init(&m);
    int prev = 0;                    /* chained across the combo so the wrist never spins round */
    for (ci = 0; ci < 3; ci++) {
        const FKClip *c = C[ci];
        printf("atk%d:", ci + 1);
        for (k = 0; k < c->nkeys; k++) {
            double t = c->keys[k].frame, a[3], b[3], e[3], v[3], vl, best = -1;
            int r, br = prev;
            double t1 = t + 3 > c->length ? c->length : t + 3, t0 = t1 - 3;
            blade(c, t0, prev, a, e);
            blade(c, t1, prev, b, e);
            v[0] = b[0] - a[0]; v[1] = b[1] - a[1]; v[2] = b[2] - a[2];
            vl = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (vl > 25) {
                for (r = prev - 180; r <= prev + 180; r += 2) {
                    double sc;
                    blade(c, t, r, a, e);
                    sc = fabs(e[0] * v[0] + e[1] * v[1] + e[2] * v[2]) / vl - fabs((double)(r - prev)) / 3000.0;  /* prefer small turns */
                    if (sc > best) { best = sc; br = r; }
                }
                /* r and r+180 give the same blade plane: take the one nearest the last key that a
                   wrist can reach (+-120) */
                while (br > 120) br -= 180;
                while (br < -120) br += 180;
                if (br + 180 <= 120 && abs(br + 180 - prev) < abs(br - prev)) br += 180;
                if (br - 180 >= -120 && abs(br - 180 - prev) < abs(br - prev)) br -= 180;
            }
            printf(" (%d, %d, %.2f)", c->keys[k].frame, br, best);
            prev = br;
        }
        printf("\n");
    }
    /* refine: coordinate descent on every key's roll, maximising the speed-weighted edge lead over
       the whole clip (the first key of the combo and the last of the slam stay 0: the walking hold) */
    for (ci = 0; ci < 3; ci++) {
        static FKKey keys[16];
        FKClip c = *C[ci];
        int it, kk;
        memcpy(keys, c.keys, sizeof(FKKey) * c.nkeys);
        c.keys = keys;
        for (it = 0; it < 6; it++)
            for (kk = 0; kk < c.nkeys; kk++) {
                int d, best_v = keys[kk].v[FK_RFR];
                double best = -1;
                if ((ci == 0 && kk == 0) || (ci == 2 && kk == c.nkeys - 1)) continue;
                for (d = -40; d <= 40; d += 4) {
                    int f, v0 = keys[kk].v[FK_RFR];
                    double sum = 0, wsum = 0;
                    keys[kk].v[FK_RFR] = (s16)(best_v + d);
                    if (keys[kk].v[FK_RFR] > 125 || keys[kk].v[FK_RFR] < -125) { keys[kk].v[FK_RFR] = (s16)v0; continue; }
                    for (f = 0; f + 1 <= c.length; f++) {
                        double a[3], b[3], e[3], v[3], vl; FKPose p;
                        fk_sample(&c, (s32)f << 8, &p);
                        blade(&c, f, p.v[FK_RFR], a, e); blade(&c, f + 1, p.v[FK_RFR], b, e);
                        v[0] = b[0] - a[0]; v[1] = b[1] - a[1]; v[2] = b[2] - a[2];
                        vl = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                        blade(&c, f + 0.5, p.v[FK_RFR], a, e);
                        if (vl > 1) { sum += fabs(e[0] * v[0] + e[1] * v[1] + e[2] * v[2]); wsum += vl; }
                    }
                    if (sum / wsum > best) { best = sum / wsum; best_v = keys[kk].v[FK_RFR]; }
                    keys[kk].v[FK_RFR] = (s16)v0;
                }
                keys[kk].v[FK_RFR] = (s16)best_v;
            }
        printf("atk%d refined:", ci + 1);
        for (kk = 0; kk < c.nkeys; kk++) printf(" %d", keys[kk].v[FK_RFR]);
        {   /* report */
            int f; double sum = 0, wsum = 0, worst = 1;
            for (f = 0; f + 1 <= c.length; f++) {
                double a[3], b[3], e[3], v[3], vl, sc; FKPose p;
                fk_sample(&c, (s32)f << 8, &p);
                blade(&c, f, p.v[FK_RFR], a, e); blade(&c, f + 1, p.v[FK_RFR], b, e);
                v[0] = b[0] - a[0]; v[1] = b[1] - a[1]; v[2] = b[2] - a[2];
                vl = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                blade(&c, f + 0.5, p.v[FK_RFR], a, e);
                sc = fabs(e[0] * v[0] + e[1] * v[1] + e[2] * v[2]) / (vl + 1e-9);
                if (vl > 1) { sum += sc * vl; wsum += vl; }
                if (vl > 60 && sc < worst) worst = sc;
            }
            printf("   (speed-weighted edge lead %.2f, worst fast frame %.2f)\n", sum / wsum, worst);
        }
    }
    return 0;
}
