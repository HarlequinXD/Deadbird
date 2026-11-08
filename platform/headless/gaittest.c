/* Gait contact sheet: drives the real player locomotion (walk, then run) in open space and
   renders it side-on with a tracking camera. Row 1 = walk, row 2 = run, row 3 = run from behind while curving.
   usage: gaittest out.bmp */
#include <stdio.h>
#include "gpu.h"
#include "gte.h"
#include "tim.h"
#include "model.h"
#include "player.h"
#include "level.h"
#include "demon_tim.h"
#undef DEMON_MESH_NO_DATA
#include "demon_mesh.h"

#define COLS 8
#define CW 150
#define CH 236
static FrameBuf fb;
static unsigned char sheet[CH * 3][CW * COLS][3];
static Player pl;

static void shot(const Model *m, int row, int col, int behind)
{
    MATRIX view, rx, ry, cam[DEMON_NBONES];
    SVECTOR t; VECTOR tv;
    DrawCtx ctx;
    RECT r = { 0, 0, 320, 240 };
    const u16 *v = gpu_vram();
    int b, x, y;
    mat_rot_x(&rx, 40); mat_rot_y(&ry, -(pl.yaw + (behind ? 0 : 1024))); mat_mul(&rx, &ry, &view);
    t.vx = (s16)-pl.x; t.vy = 1100; t.vz = (s16)-pl.z;
    view.t[0] = view.t[1] = view.t[2] = 0;
    mat_apply(&view, &t, &tv);
    view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz + 3500;
    ctx.proj.h = 300; ctx.proj.ofx = 160; ctx.proj.ofy = 120; ctx.proj.near_z = 100;
    ctx.ot_shift = 4; ctx.cull_sign = 1; ctx.r = ctx.g = ctx.b = 128;
    gpu_fill(&r, 40, 12, 14);
    ot_clear(&fb);
    gpu_set_draw_env(0, 0, 320, 240, 0, 0, 1);
    /* (no level geometry: the camera sits outside the arena walls) */
    for (b = 0; b < DEMON_NBONES; b++) mat_mul(&view, &pl.world[b], &cam[b]);
    model_draw_mats(m, cam, &ctx, &fb);
    gpu_draw_ot(&fb);
    for (y = 0; y < CH; y++)
        for (x = 0; x < CW; x++) {
            u16 p = v[(y + 2) * VRAM_W + x + 85];
            unsigned char *d = sheet[row * CH + y][col * CW + x];
            d[0] = (unsigned char)(((p >> 10) & 31) << 3);
            d[1] = (unsigned char)(((p >> 5) & 31) << 3);
            d[2] = (unsigned char)((p & 31) << 3);
        }
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "gait.bmp";
    TimInfo tim;
    Model m;
    int row, i, W = CW * COLS, H = CH * 3, y;
    FILE *f;

    gpu_init();
    tim_upload(demon_tim, DEMON_TIM_SIZE, &tim);
    level_init();
    m.verts = (const DVERT *)demon_verts; m.nverts = DEMON_NVERTS;
    m.prims = (const DPRIM *)demon_prims; m.nprims = DEMON_NPRIMS;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    m.tpage = tim.tpage; m.clut = tim.clut;

    player_init(&m);
    for (row = 0; row < 3; row++) {
        int mag = row ? 4096 : 2600, step = row ? 3 : 4, turn = row == 2 ? 200 : 0;
        player_reset(&pl, -2800, -3000, 0);                     /* open floor, heading +Z */
        for (i = 0; i < 70; i++) { player_update(&pl, 0, 0, mag); player_pose(&pl, &m); }
        for (i = 0; i < COLS; i++) {
            int k;
            for (k = 0; k < step; k++) { player_update(&pl, 0, turn * i / 8, mag); player_pose(&pl, &m); }
            shot(&m, row, i, row == 2);
        }
        printf("%s: speed %d u/f, phase %d, run %d amp %d\n", row == 2 ? "run, from behind, curving right" : row ? "run" : "walk", (int)pl.speed, pl.phase, pl.runw, pl.amp);
    }

    f = fopen(out, "wb");
    if (!f) return 1;
    {
        unsigned char h[54] = { 'B', 'M' };
        int size = 54 + W * H * 3;
        h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255; h[5] = (size >> 24) & 255;
        h[10] = 54; h[14] = 40; h[18] = W & 255; h[19] = W >> 8; h[22] = H & 255; h[23] = H >> 8;
        h[26] = 1; h[28] = 24;
        fwrite(h, 1, 54, f);
        for (y = H - 1; y >= 0; y--) fwrite(sheet[y], 1, (size_t)W * 3, f);
    }
    fclose(f);
    return 0;
}
