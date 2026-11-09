/* Gait contact sheet: drives the real player locomotion (walk, then run) in open space and
   renders it side-on with a tracking camera. Row 1 = walk, row 2 = run, row 3 = run from behind while curving.
   usage: gaittest out.bmp */
#include <stdio.h>
#include "gpu.h"
#include "gte.h"
#include "model.h"
#include "player.h"
#include "level.h"
#include "assets.h"
#undef DEMON_MESH_NO_DATA
#include "demon_mesh.h"
#include "chainsaw_mesh.h"
#include "soldier_mesh.h"
#include <string.h>
#include <stdlib.h>

#define COLS 8
#define CW 150
#define CH 236
static FrameBuf fb;
#define ROWS 8
static unsigned char sheet[CH * ROWS][CW * COLS][3];
static Player pl;
static Model saw_body, saw_chain;
static int fixcam = -1;               /* >= 0: camera yaw fixed (moves sheet) */

static void shot(const Model *m, int row, int col, int behind)
{
    MATRIX view, rx, ry, cam[DEMON_NBONES];
    SVECTOR t; VECTOR tv;
    DrawCtx ctx;
    RECT r = { 0, 0, 320, 240 };
    const u16 *v = gpu_vram();
    int b, x, y;
    mat_rot_x(&rx, 40); mat_rot_y(&ry, -(fixcam >= 0 ? fixcam : pl.yaw + (behind ? 0 : 1024))); mat_mul(&rx, &ry, &view);
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
    {   /* the arm chainsaw, as in the game */
        MATRIX mount, sawm;
        mat_identity(&mount);
        mat_translate(&mount, DEMON_MOUNT_X, DEMON_MOUNT_Y, DEMON_MOUNT_Z);
        mat_mul(&cam[DEMON_MOUNT_BONE], &mount, &sawm);
        model_draw_mats(&saw_body, &sawm, &ctx, &fb);
        model_draw_mats(&saw_chain, &sawm, &ctx, &fb);
    }
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
    Model m;
    int row, i, W = CW * COLS, H = CH * ROWS, y;
    FILE *f;

    gpu_init();
    level_init();
    assets_init();
    m = asset_demon;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    {
        saw_body = asset_saw;
        saw_body.bones = (const DBONE *)saw_bones; saw_body.nbones = 1;
        saw_chain = saw_body; saw_chain.bones = (const DBONE *)&saw_bones[1];
    }
    player_init(&m);
    for (row = 0; row < 3; row++) {
        int mag = row ? 4096 : 2600, step = row ? 3 : 4, turn = row == 2 ? 200 : 0;
        {   /* the long Turbine Hall floor, heading +X */
            int a;
            for (a = 0; a < level_area_count(); a++) if (!strcmp(level_area(a)->name, "Turbine Hall")) break;
            if (a < level_area_count()) {
                const LabArea *ar = level_area(a);
                player_reset(&pl, ar->x0 + 1500, (ar->z0 + ar->z1) / 2, 1024);
            } else player_reset(&pl, -2800, -3000, 0);
        }
        for (i = 0; i < 70; i++) { player_update(&pl, 0, 0, mag); player_pose(&pl, &m); }
        for (i = 0; i < COLS; i++) {
            int k;
            for (k = 0; k < step; k++) { player_update(&pl, 0, turn * i / 8, mag); player_pose(&pl, &m); }
            shot(&m, row, i, row == 2);
        }
        printf("%s: speed %d u/f, phase %d, run %d amp %d\n", row == 2 ? "run, from behind, curving right" : row ? "run" : "walk", (int)pl.speed, pl.phase, pl.runw, pl.amp);
    }

    /* MOVES=1: rows 4-6 show the sprint, the dodge roll and the turn-round instead of the combo */
    if (getenv("MOVES")) {
        int a, k;
        for (a = 0; a < level_area_count(); a++) if (!strcmp(level_area(a)->name, "Turbine Hall")) break;
        fixcam = 2048;
        for (row = 3; row < 6; row++) {
            const LabArea *ar = level_area(a);
            player_reset(&pl, ar->x0 + 1500, (ar->z0 + ar->z1) / 2, 1024);
            for (i = 0; i < (row == 3 ? 85 : 40); i++) { player_update(&pl, 0, 1024, 4096); player_pose(&pl, &m); }
            for (i = 0; i < COLS; i++) {
                for (k = 0; k < 3; k++) {
                    u32 pad = row == 4 && i == 0 && k == 0 ? BTN_DODGE : 0;
                    int yaw = row == 5 ? 3072 : 1024;
                    player_update(&pl, pad, yaw, 4096); player_pose(&pl, &m);
                }
                shot(&m, row, i, 0);
                if (row == 5) printf("pivot %d yaw %d speed %d\n", pl.pivot_t, pl.yaw, (int)pl.speed);
            }
        }
        printf("moves: sprint %d roll %d pivot %d\n", pl.sprint, pl.roll_t, pl.pivot_t);
        {   /* rows 7-8: a trooper on the same controller: strafing while aiming, then running */
            Model sm = asset_soldier;
            const LabArea *ar = level_area(a);
            sm.bones = (const DBONE *)soldier_bones; sm.nbones = SOLDIER_NBONES;
            for (row = 6; row < 8; row++) {
                player_reset(&pl, ar->x0 + 4000, (ar->z0 + ar->z1) / 2, 2048);
                pl.aim = 1; pl.aim_yaw = 2048;
                for (i = 0; i < 40; i++) { player_update(&pl, 0, row == 6 ? 1024 : 2048, row == 6 ? 1500 : 4096); player_pose(&pl, &sm); }
                for (i = 0; i < COLS; i++) {
                    for (k = 0; k < 3; k++) { player_update(&pl, 0, row == 6 ? 1024 : 2048, row == 6 ? 1500 : 4096); player_pose(&pl, &sm); }
                    shot(&sm, row, i, 0);
                }
            }
        }
    } else
    /* rows 4-6: the chainsaw combo, side on: 24 frames over slash, backhand and slam */
    {
        static const int when[24] = { 2, 4, 6, 8, 9, 11, 14, 16,  2, 4, 6, 8, 9, 11, 14, 16,  3, 5, 8, 10, 12, 15, 19, 28 };
        int k, t = 0, stage = 0;
        for (i = 0; i < 2; i++) { player_reset(&pl, 0, 0, 0); }
        {
            int a;
            for (a = 0; a < level_area_count(); a++) if (!strcmp(level_area(a)->name, "Turbine Hall")) break;
            if (a < level_area_count()) { const LabArea *ar = level_area(a); player_reset(&pl, ar->x0 + 1500, (ar->z0 + ar->z1) / 2, 1024); }
        }
        for (i = 0; i < 30; i++) { player_update(&pl, 0, 0, 0); player_pose(&pl, &m); }
        for (k = 0; k < 24; k++) {
            if (k % 8 == 0) { stage++; t = 0; }
            while (t < when[k]) {
                u32 pad = 0;
                if (stage == 1 && t == 0 && k == 0) pad = BTN_ATTACK;
                if ((stage == 2 || stage == 3) && t == 0) pad = BTN_ATTACK;    /* chains at the window */
                player_update(&pl, pad, 0, 0); player_pose(&pl, &m);
                if (pl.atk == stage) t = pl.atk_t; else if (stage > 1 && pl.atk == stage - 1) { player_update(&pl, BTN_ATTACK, 0, 0); player_pose(&pl, &m); }
                else t++;
            }
            shot(&m, 3 + k / 8, k % 8, getenv("BEHIND") != 0);
        }
        printf("combo: stage %d t %d\n", pl.atk, pl.atk_t);
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
