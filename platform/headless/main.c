/* Headless test harness: plays the game without a window (title -> versus CPU with a
   scripted player), writes frames as BMP and reports timing + memory. Used by CI. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "deadbird.h"
#include "gpu.h"

enum { GS_TITLE, GS_MOVES, GS_INTRO, GS_FIGHT, GS_KO, GS_MATCH_END, GS_PAUSE };

static void write_bmp(const char *dir, const char *name)
{
    static unsigned char row[SCREEN_W * 3];
    unsigned char h[54] = { 'B', 'M' };
    char path[512];
    int x, y, dx, dy, size = 54 + SCREEN_W * SCREEN_H * 3;
    const u16 *v = gpu_vram();
    FILE *f;
    sprintf(path, "%s/%s.bmp", dir, name);
    f = fopen(path, "wb");
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

/* scripted player: walk in, then cycle through the combo strings */
static u32 script(int t)
{
    static const u32 seq[] = { PAD_SQUARE, PAD_SQUARE, PAD_SQUARE, 0, 0, PAD_SQUARE, PAD_SQUARE, PAD_TRIANGLE, 0, 0,
                               PAD_TRIANGLE, PAD_TRIANGLE, 0, 0, PAD_RIGHT | PAD_TRIANGLE, 0, 0, PAD_CIRCLE, PAD_CIRCLE, 0 };
    int phase = t % 400;
    if (phase < 40) return PAD_RIGHT;
    if ((t & 7) < 2) return seq[(t / 8) % (sizeof(seq) / sizeof(seq[0]))] & ~(phase & 1 ? 0u : 0u);
    return (seq[(t / 8) % (sizeof(seq) / sizeof(seq[0]))] & PAD_CIRCLE);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    clock_t t0;
    int i, frames = 0, shot_fight = 0, shot_ko = 0, shot_end = 0;

    game_init();
    printf("static game RAM: %d bytes of %d (PS1 main RAM)\n", game_ram_used(), RAM_BUDGET);
    printf("VRAM: %d bytes\n", VRAM_W * VRAM_H * 2);

    t0 = clock();
    for (i = 0; i < 90; i++) { game_frame(0); frames++; }
    write_bmp(out, "1_title");
    game_frame(PAD_DOWN); game_frame(0);
    for (i = 0; i < 20; i++) game_frame(0);
    write_bmp(out, "2_title_menu");
    game_frame(PAD_UP); game_frame(0);
    game_frame(PAD_CROSS);                       /* VERSUS CPU */
    for (i = 0; i < 40; i++) game_frame(0);
    game_set_touch_ui(1);
    for (i = 0; i < 50; i++) game_frame(0);
    write_bmp(out, "3_round_intro");
    for (i = 0; i < 20000 && game_phase() != GS_TITLE; i++) {
        game_frame(game_phase() == GS_FIGHT ? script(i) : 0);
        frames++;
        if (game_phase() == GS_FIGHT && i > 220 && !shot_fight) { write_bmp(out, "4_fight"); shot_fight = 1; }
        if (game_phase() == GS_FIGHT && i > 520 && shot_fight == 1) { write_bmp(out, "5_fight"); shot_fight = 2; }
        if (game_phase() == GS_KO && !shot_ko) { shot_ko = 1; }
        else if (game_phase() == GS_KO && shot_ko && shot_ko++ == 6) write_bmp(out, "6_ko");
        if (game_phase() == GS_MATCH_END && !shot_end++) { int k; for (k = 0; k < 40; k++) game_frame(0); write_bmp(out, "7_winner"); }
    }
    printf("match finished: %s after %d frames\n", game_phase() == GS_TITLE ? "back on title" : "still running", i);
    printf("software render: %.3f ms/frame avg (%d frames)\n",
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / (frames + 200), frames + 200);
    return 0;
}
