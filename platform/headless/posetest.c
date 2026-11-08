/* Animation contact sheet: renders every clip at several times into one BMP (rows = clips).
   Lets you check the IK rig and the move animations without running the game.
   usage: posetest out.bmp [camera_yaw]   (yaw 4096 = 360 deg, default 1024 = side view) */
#include <stdio.h>
#include <stdlib.h>
#include "gpu.h"
#include "gte.h"
#include "tim.h"
#include "model.h"
#include "anim.h"
#include "rig.h"
#include "demon_tim.h"
#include "demon_mesh.h"

#define COLS 6
#define CW 140
#define CH 190

static FrameBuf fb;
static unsigned char sheet[CH * CLIP_COUNT][CW * COLS][3];

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "poses.bmp";
    int yaw = argc > 2 ? atoi(argv[2]) : 1024;
    TimInfo tim;
    Model m;
    MATRIX pose[DEMON_NBONES], mats[DEMON_NBONES], view, rx, ry, root, cam[DEMON_NBONES];
    DrawCtx ctx;
    int c, col, b, x, y, W = CW * COLS, H = CH * CLIP_COUNT;
    FILE *f;

    gpu_init();
    tim_upload(demon_tim, DEMON_TIM_SIZE, &tim);
    anim_init();
    m.verts = (const DVERT *)demon_verts; m.nverts = DEMON_NVERTS;
    m.prims = (const DPRIM *)demon_prims; m.nprims = DEMON_NPRIMS;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    m.tpage = tim.tpage; m.clut = tim.clut;

    mat_rot_x(&rx, 60); mat_rot_y(&ry, yaw); mat_mul(&rx, &ry, &view);
    { SVECTOR t = { 0, 1050, 0, 0 }; VECTOR tv; mat_apply(&view, &t, &tv);
      view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz + 3400; }
    ctx.proj.h = 300; ctx.proj.ofx = 160; ctx.proj.ofy = 120; ctx.proj.near_z = 100;
    ctx.ot_shift = 4; ctx.cull_sign = 1; ctx.r = ctx.g = ctx.b = 128;

    for (c = 0; c < CLIP_COUNT; c++)
        for (col = 0; col < COLS; col++) {
            AnimPose ap;
            VECTOR off;
            RECT r = { 0, 0, 320, 240 };
            const u16 *v = gpu_vram();
            anim_sample(&clips[c], ((s32)clips[c].length * col / (COLS - 1)) << 8, &ap);
            rig_solve(&m, &ap, pose, &off);
            mat_identity(&root);
            mat_translate(&root, off.vx, off.vy, off.vz);
            model_skeleton(&m, &root, pose, mats);
            for (b = 0; b < DEMON_NBONES; b++) mat_mul(&view, &mats[b], &cam[b]);
            gpu_fill(&r, 40, 12, 14);
            ot_clear(&fb);
            gpu_set_draw_env(0, 0, 320, 240, 0, 0, 1);
            model_draw_mats(&m, cam, &ctx, &fb);
            gpu_draw_ot(&fb);
            for (y = 0; y < CH; y++)
                for (x = 0; x < CW; x++) {
                    u16 p = v[(y + 20) * VRAM_W + x + 90];
                    unsigned char *d = sheet[c * CH + y][col * CW + x];
                    d[0] = (unsigned char)(((p >> 10) & 31) << 3);
                    d[1] = (unsigned char)(((p >> 5) & 31) << 3);
                    d[2] = (unsigned char)((p & 31) << 3);
                }
        }

    f = fopen(out, "wb");
    if (!f) return 1;
    {
        unsigned char h[54] = { 'B', 'M' };
        int size = 54 + W * H * 3;
        h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255; h[5] = (size >> 24) & 255;
        h[10] = 54; h[14] = 40;
        h[18] = W & 255; h[19] = W >> 8; h[22] = H & 255; h[23] = H >> 8;
        h[26] = 1; h[28] = 24;
        fwrite(h, 1, 54, f);
        for (y = H - 1; y >= 0; y--) fwrite(sheet[y], 1, (size_t)W * 3, f);
    }
    fclose(f);
    printf("wrote %s (%d clips x %d frames)\n", out, CLIP_COUNT, COLS);
    return 0;
}
