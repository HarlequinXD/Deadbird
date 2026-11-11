/*
 * Headless platform layer: runs any game on the engine without a window, for CI and tools.
 *
 *   ./game_headless <out dir> [script]
 *
 * The script is plain text, one command per line ('#' starts a comment):
 *   hold <frames> [BUTTON ...]     run frames holding buttons (UP DOWN LEFT RIGHT CROSS CIRCLE
 *                                  SQUARE TRIANGLE L1 R1 L2 R2 START SELECT)
 *   tap <BUTTON ...>               one frame pressed, one released
 *   stick <lx> <ly>                gamepad left stick, -4096..4096
 *   touch <down|move|up> <id> <x> <y>
 *   shot <name>                    write <out dir>/<name>.bmp
 * Without a script it runs 120 frames and writes final.bmp. WIDE=<320..512> sets the frame
 * width, TOUCH=1 turns the touch UI on. Prints the static RAM use and the time per frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine.h"
#include "snd.h"

static const char *outdir = ".";

static void shot(const char *name)
{
    static unsigned char row[SCREEN_W_MAX * 3 + 4];
    unsigned char h[54] = { 'B', 'M' };
    char path[512];
    int x, y, dx, dy, pitch = (SCREEN_W * 3 + 3) & ~3, size = 54 + pitch * SCREEN_H;
    const u16 *v = gpu_vram();
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.bmp", outdir, name);
    f = fopen(path, "wb");
    if (!f) { printf("can't write %s\n", path); return; }
    eng_display_area(&dx, &dy);
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40;
    h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8;
    h[22] = SCREEN_H & 255; h[23] = SCREEN_H >> 8;
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    memset(row, 0, sizeof(row));
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[(dy + y) * VRAM_W + dx + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            row[x * 3 + 0] = (unsigned char)((b << 3) | (b >> 2));
            row[x * 3 + 1] = (unsigned char)((g << 3) | (g >> 2));
            row[x * 3 + 2] = (unsigned char)((r << 3) | (r >> 2));
        }
        fwrite(row, 1, (size_t)pitch, f);
    }
    fclose(f);
}

static long frames_run;
static double secs;
static void run(u32 pad, int n)
{
    static s16 mix[2 * 368];
    while (n-- > 0) {
        clock_t t0 = clock();
        eng_frame(pad);
        secs += (double)(clock() - t0) / CLOCKS_PER_SEC;
        snd_mix(mix, 368);                       /* drain the sound commands like a platform would */
        frames_run++;
    }
}

static u32 button(const char *s)
{
    static const struct { const char *n; u32 b; } B[] = {
        { "UP", PAD_UP }, { "DOWN", PAD_DOWN }, { "LEFT", PAD_LEFT }, { "RIGHT", PAD_RIGHT },
        { "CROSS", PAD_CROSS }, { "CIRCLE", PAD_CIRCLE }, { "SQUARE", PAD_SQUARE }, { "TRIANGLE", PAD_TRIANGLE },
        { "L1", PAD_L1 }, { "R1", PAD_R1 }, { "L2", PAD_L2 }, { "R2", PAD_R2 }, { "START", PAD_START }, { "SELECT", PAD_SELECT },
    };
    int i;
    for (i = 0; i < (int)(sizeof(B) / sizeof(B[0])); i++) if (!strcmp(s, B[i].n)) return B[i].b;
    printf("unknown button %s\n", s);
    return 0;
}

static u32 buttons(char *rest)
{
    u32 b = 0;
    char *t = strtok(rest, " \t\r\n");
    while (t) { b |= button(t); t = strtok(0, " \t\r\n"); }
    return b;
}

int main(int argc, char **argv)
{
    const char *w = getenv("WIDE");
    if (argc > 1) outdir = argv[1];
#ifndef GEN_BAKED
    if (w) { screen_w = atoi(w) & ~1; if (screen_w < 320) screen_w = 320; if (screen_w > SCREEN_W_MAX) screen_w = SCREEN_W_MAX; }
#endif
    eng_init();
    eng_set_touch_ui(getenv("TOUCH") != 0);
    printf("%s: static RAM %d of %d bytes\n", eng_app()->name, eng_ram_used(), RAM_BUDGET);
    if (argc > 2) {
        char line[256];
        FILE *f = fopen(argv[2], "r");
        if (!f) { printf("can't read %s\n", argv[2]); return 1; }
        while (fgets(line, sizeof(line), f)) {
            char cmd[16] = "", *rest;
            int n;
            if (line[0] == '#' || sscanf(line, "%15s%n", cmd, &n) < 1) continue;
            rest = line + n;
            if (!strcmp(cmd, "hold")) { int k = atoi(strtok(rest, " \t\r\n")); run(buttons(0), k); }
            else if (!strcmp(cmd, "tap")) { run(buttons(rest), 1); run(0, 1); }
            else if (!strcmp(cmd, "stick")) { int lx = 0, ly = 0; sscanf(rest, "%d %d", &lx, &ly); eng_set_analog(lx, ly, 0, 0); }
            else if (!strcmp(cmd, "touch")) {
                char ev[8]; int id, x, y;
                if (sscanf(rest, "%7s %d %d %d", ev, &id, &x, &y) == 4)
                    eng_touch(id, !strcmp(ev, "down") ? EV_DOWN : !strcmp(ev, "up") ? EV_UP : EV_MOVE, x, y);
            }
            else if (!strcmp(cmd, "shot")) { char nm[64]; if (sscanf(rest, "%63s", nm) == 1) shot(nm); }
            else printf("unknown command %s\n", cmd);
        }
        fclose(f);
    } else {
        run(0, 120);
        shot("final");
    }
    printf("ran %ld frames, %.3f ms/frame avg\n", frames_run, frames_run ? secs * 1000.0 / (double)frames_run : 0.0);
    return 0;
}
