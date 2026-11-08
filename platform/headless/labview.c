/* Renders the level from fixed camera spots (no player): a tour of the plant, for docs and
   for checking the lighting bake. Also prints the per-view polygon count. */
#include <stdio.h>
#include <string.h>
#include "gte.h"
#include "gpu.h"
#include "level.h"

static FrameBuf fb;

static void save(const char *dir, const char *name)
{
    unsigned char h[54] = { 'B', 'M' }, row[SCREEN_W * 3];
    char path[512];
    int x, y, size = 54 + SCREEN_W * SCREEN_H * 3;
    const u16 *v = gpu_vram();
    FILE *f;
    sprintf(path, "%s/%s.bmp", dir, name);
    if (!(f = fopen(path, "wb"))) return;
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40; h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8; h[22] = SCREEN_H; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[y * VRAM_W + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            row[x * 3] = (unsigned char)(b << 3 | b >> 2); row[x * 3 + 1] = (unsigned char)(g << 3 | g >> 2); row[x * 3 + 2] = (unsigned char)(r << 3 | r >> 2);
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
}

static const struct { const char *name; s32 x, y, z; int yaw, pitch; } spots[] = {
    { "lab_01_hall",        0, -2300, -4100,    0, 120 },
    { "lab_02_hall_pods", 2600, -2600, -1500, 2700, 160 },
    { "lab_03_catwalk",   -1500, -2800, 1200,  900, 200 },
    { "lab_04_corridor",   4700, -1400,  -600, 1024,  60 },
    { "lab_05_reactor",    8500, -2400, -3200,  700, 100 },
    { "lab_06_lab",           0, -2000,  8300,    0, 160 },
    { "lab_07_lab_racks",  2600, -2000, 11800, 2600, 140 },
    { "lab_08_containment", -8600, -2200, 2000, 3072, 100 },
};

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    unsigned i;
    gpu_init();
    level_init();
    for (i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
        MATRIX view, rx, ry;
        SVECTOR t; VECTOR tv;
        PROJ pr;
        RECT r = { 0, 0, SCREEN_W, SCREEN_H };
        mat_rot_x(&rx, spots[i].pitch); mat_rot_y(&ry, -spots[i].yaw); mat_mul(&rx, &ry, &view);
        t.vx = (s16)-spots[i].x; t.vy = (s16)-spots[i].y; t.vz = (s16)-spots[i].z;
        view.t[0] = view.t[1] = view.t[2] = 0;
        mat_apply(&view, &t, &tv);
        view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
        pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
        gpu_fill(&r, 3, 4, 7);
        ot_clear(&fb);
        gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
        level_draw(&fb, &view, &pr, 100);
        gpu_draw_ot(&fb);
        save(out, spots[i].name);
        printf("%-20s %4d polygons\n", spots[i].name, level_polys_drawn());
    }
    printf("level data: %d bytes (+ 2 texture pages)\n", level_data_bytes());
    return 0;
}
