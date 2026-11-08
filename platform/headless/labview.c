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

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    unsigned i;
    gpu_init();
    level_init();
    for (i = 0; i < (unsigned)level_view_count(); i++) {
        const LabView *sp = level_view((int)i);
        char nm[64];
        MATRIX view, rx, ry;
        SVECTOR t; VECTOR tv;
        PROJ pr;
        RECT r = { 0, 0, SCREEN_W, SCREEN_H };
        mat_rot_x(&rx, sp->pitch); mat_rot_y(&ry, -sp->yaw); mat_mul(&rx, &ry, &view);
        t.vx = (s16)-sp->x; t.vy = (s16)-sp->y; t.vz = (s16)-sp->z;
        view.t[0] = view.t[1] = view.t[2] = 0;
        mat_apply(&view, &t, &tv);
        view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
        pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
        gpu_fill(&r, 3, 4, 7);
        ot_clear(&fb);
        gpu_set_draw_env(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1);
        level_draw(&fb, &view, &pr, 100);
        gpu_draw_ot(&fb);
        {   /* file name: lab_NN_room_name */
            int k, n = sprintf(nm, "lab_%02u_", i + 1);
            for (k = 0; sp->name[k] && n < 60; k++) nm[n++] = sp->name[k] == ' ' ? '_' : (char)(sp->name[k] | 0x20);
            nm[n] = 0;
        }
        save(out, nm);
        printf("%-24s %4d polygons\n", sp->name, level_polys_drawn());
    }
    printf("level data: %d bytes (+ 2 texture pages)\n", level_data_bytes());
    return 0;
}
