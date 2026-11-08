/* Headless test harness: runs the game without a window, writes frames as BMP and
   reports timing + memory. Used by CI and for checking the renderer on a PC. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "deadbird.h"
#include "gpu.h"

static void write_bmp(const char *path)
{
    static unsigned char row[SCREEN_W * 3];
    unsigned char h[54] = { 'B', 'M' };
    int x, y, dx, dy, size = 54 + SCREEN_W * SCREEN_H * 3;
    const u16 *v = gpu_vram();
    FILE *f = fopen(path, "wb");
    if (!f) return;
    game_display_area(&dx, &dy);
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40;
    h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8;
    h[22] = SCREEN_H & 255; h[23] = SCREEN_H >> 8;
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[(dy + y) * VRAM_W + dx + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            row[x * 3 + 0] = (unsigned char)((b << 3) | (b >> 2));
            row[x * 3 + 1] = (unsigned char)((g << 3) | (g >> 2));
            row[x * 3 + 2] = (unsigned char)((r << 3) | (r >> 2));
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    clock_t t0;
    int i, frames = 600;

    game_init();
    printf("static game RAM: %d bytes of %d (PS1 main RAM)\n", game_ram_used(), RAM_BUDGET);
    printf("VRAM: %d bytes\n", VRAM_W * VRAM_H * 2);

    /* T-pose shot */
    game_frame(PAD_CIRCLE);               /* stop auto spin */
    game_frame(0);
    sprintf(path, "%s/frame_tpose.bmp", out); write_bmp(path);
    /* touch UI shot */
    game_set_touch_ui(1);
    game_frame(PAD_CROSS);                /* start walking */
    for (i = 0; i < 40; i++) game_frame(0);
    sprintf(path, "%s/frame_walk.bmp", out); write_bmp(path);
    game_set_touch_ui(0);
    for (i = 0; i < 25; i++) game_frame(PAD_LEFT);
    game_frame(0);
    sprintf(path, "%s/frame_orbit.bmp", out); write_bmp(path);

    /* timing */
    t0 = clock();
    for (i = 0; i < frames; i++) game_frame(i & 64 ? PAD_RIGHT : 0);
    printf("software render: %.3f ms/frame (%d frames)\n",
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / frames, frames);
    return 0;
}
