#define _DEFAULT_SOURCE
/* The level editor driven by a scripted gamepad (and a few taps), headless: from the title into
   the editor, through the 2D map, the panels and the 3D view, building a small level, saving it
   and play-testing it. Writes screenshots and fails if something doesn't happen. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deadbird.h"
#include "editor.h"
#include "gpu.h"

static const char *out = ".";
static int fails;

static void shot(const char *name)
{
    static unsigned char row[SCREEN_W * 3];
    unsigned char h[54] = { 'B', 'M' };
    char path[512];
    int x, y, dx, dy, size = 54 + SCREEN_W * SCREEN_H * 3;
    const u16 *v = gpu_vram();
    FILE *f;
    sprintf(path, "%s/%s.bmp", out, name);
    f = fopen(path, "wb");
    if (!f) return;
    game_display_area(&dx, &dy);
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255;
    h[10] = 54; h[14] = 40; h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8; h[22] = SCREEN_H & 255; h[23] = SCREEN_H >> 8; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[(dy + y) * VRAM_W + dx + x];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            row[x * 3 + 0] = (unsigned char)((b << 3) | (b >> 2)); row[x * 3 + 1] = (unsigned char)((g << 3) | (g >> 2)); row[x * 3 + 2] = (unsigned char)((r << 3) | (r >> 2));
        }
        fwrite(row, 1, sizeof(row), f);
    }
    fclose(f);
}
static void run(u32 pad, int n) { int i; for (i = 0; i < n; i++) game_frame(pad); }
static void press(u32 b) { game_frame(b); game_frame(0); game_frame(0); }
static void presses(u32 b, int n) { int i; for (i = 0; i < n; i++) press(b); }
static void expect(int cond, const char *what) { printf("%s %s\n", cond ? "  ok  " : "  FAIL", what); if (!cond) fails++; }
static void tap(int x, int y) { game_pointer(0, 0, x, y, 0); game_frame(0); game_pointer(0, 2, x, y, 0); game_frame(0); game_frame(0); }

int main(int argc, char **argv)
{
    if (argc > 1) out = argv[1];
    setenv("DEADBIRD_LEVELS", "/tmp/deadbird_levels_test", 1);
    if (system("rm -rf /tmp/deadbird_levels_test")) {}
    game_init();
    game_set_touch_ui(0);
    game_set_pad_style(0);
    run(0, 40);
    /* title: EDITOR is the second item */
    press(PAD_DOWN); press(PAD_CROSS); run(0, 40);
    expect(game_phase() == GS_EDITOR, "the title's EDITOR opens the editor");
    shot("ed_01_list");
    /* copy of level 1 */
    press(PAD_DOWN); press(PAD_CROSS); run(0, 4);
    shot("ed_02_level1_map");
    presses(PAD_L2, 4); run(0, 2);
    shot("ed_03_level1_zoomed_out");
    presses(PAD_R2, 4);
    presses(PAD_RIGHT, 8); presses(PAD_UP, 3);
    press(PAD_SQUARE); run(0, 2);
    shot("ed_04_inspect_sector");
    presses(PAD_DOWN, 4); press(PAD_RIGHT); run(0, 2);        /* floor up a step */
    shot("ed_05_inspect_floor_up");
    press(PAD_CIRCLE);
    press(PAD_TRIANGLE); run(0, 2);
    shot("ed_06_tools");
    press(PAD_CIRCLE);
    press(PAD_START); run(0, 2);
    shot("ed_07_menu");
    press(PAD_CIRCLE);
    /* 3D view */
    press(PAD_SELECT); run(0, 3);
    expect(editor_in_3d(), "SELECT opens the 3D view");
    shot("ed_08_3d");
    game_set_analog(0, 0, 0, 2400); run(0, 20); game_set_analog(0, 0, 0, 0);   /* look down a bit */
    game_set_analog(0, 3000, 0, 0); run(0, 25); game_set_analog(0, 0, 0, 0);
    run(0, 2);
    shot("ed_09_3d_looking");
    presses(PAD_R1, 3); press(PAD_CROSS); run(0, 3);
    shot("ed_10_3d_painted");
    game_set_analog(0, 0, 0, -3600); run(0, 30); game_set_analog(0, 0, 0, 0); run(0, 2);   /* look up at the ceiling */
    press(PAD_DOWN); press(PAD_DOWN); run(0, 3);
    shot("ed_10b_3d_ceiling_lowered");
    press(PAD_TRIANGLE); run(0, 2);
    shot("ed_11_tile_picker");
    press(PAD_CIRCLE); press(PAD_CIRCLE); run(0, 2);
    expect(!editor_in_3d(), "O goes back to the map");
    /* exit to the list (saves), then a new level */
    press(PAD_START); presses(PAD_UP, 1); press(PAD_CROSS); run(0, 4);
    shot("ed_12_list_after_save");
    press(PAD_CROSS); run(0, 4);               /* NEW LEVEL */
    shot("ed_13_new_level");
    /* RECTANGLE: a platform inside the starter room, then raise it */
    press(PAD_R1); run(0, 1);
    presses(PAD_LEFT, 2); presses(PAD_UP, 2); press(PAD_CROSS);
    presses(PAD_RIGHT, 4); presses(PAD_UP, 3); press(PAD_CROSS); run(0, 2);
    shot("ed_14_carved_platform");
    press(PAD_SQUARE); presses(PAD_DOWN, 4); presses(PAD_RIGHT, 4); press(PAD_CIRCLE); run(0, 2);
    shot("ed_15_platform_raised");
    /* FLOOR tool: X raises the room's floor two steps, O lowers it one */
    press(PAD_R1); run(0, 1);
    presses(PAD_LEFT, 6); press(PAD_CROSS); press(PAD_CROSS); press(PAD_CIRCLE); run(0, 2);
    shot("ed_15b_floor_tool");
    {
        char buf[1 << 16]; int n;
        press(PAD_START); press(PAD_CROSS); run(0, 2);          /* SAVE */
        n = plat_file_load("level02.map", buf, sizeof(buf) - 1);
        buf[n > 0 ? n : 0] = 0;
        expect(n > 0 && strstr(buf, "floor 0.2 ceil 4") != 0, "the FLOOR tool raised the room a step (2 up, 1 down)");
    }
    presses(PAD_RIGHT, 6);
    /* EXTRUDE the east wall 6 m into a corridor */
    presses(PAD_R1, 3); run(0, 1);
    presses(PAD_DOWN, 5); presses(PAD_RIGHT, 6); press(PAD_CROSS);
    presses(PAD_RIGHT, 12); press(PAD_CROSS); run(0, 2);
    shot("ed_16_extruded");
    /* a trooper in the corridor: PLACE via the tools menu */
    press(PAD_TRIANGLE); presses(PAD_DOWN, 10); press(PAD_CROSS); run(0, 1);   /* from EXTRUDE (4) to TROOPER (13 tools + 1) */
    press(PAD_CROSS); run(0, 2);
    shot("ed_17_trooper");
    /* play test from the menu */
    press(PAD_START); press(PAD_DOWN); press(PAD_CROSS); run(0, 60);
    expect(game_phase() == GS_PLAY, "PLAY TEST plays the level");
    shot("ed_18_playtest");
    press(PAD_START); run(0, 4); press(PAD_CROSS); run(0, 4);
    expect(game_phase() == GS_EDITOR, "the pause menu goes back to the editor");
    shot("ed_19_back_in_editor");
    {
        char buf[1 << 16]; int n = plat_file_load("level01.map", buf, sizeof(buf) - 1);
        expect(n > 0, "level 1's copy was saved as level01.map");
        n = plat_file_load("level02.map", buf, sizeof(buf) - 1);
        expect(n > 0, "the new level was saved as level02.map");
        if (n > 0) { buf[n] = 0; printf("---- level02.map ----\n%s----\n", buf); }
    }
    /* the sector panel's wall rows and the name keyboard */
    presses(PAD_DOWN, 2); press(PAD_SQUARE); run(0, 1);
    { int k; for (k = 0; k < 19; k++) press(PAD_DOWN); }       /* to WALL ROWS */
    press(PAD_CROSS); run(0, 2);
    shot("ed_21_rows_editor");
    press(PAD_CIRCLE); presses(PAD_UP, 19); press(PAD_CROSS); run(0, 2);
    shot("ed_22_keyboard");
    press(PAD_CIRCLE); press(PAD_CIRCLE);
    /* touch: a tap on the map moves the cursor; a tap on a prompt presses its button */
    game_set_touch_ui(1);
    tap(200, 80); run(0, 2);
    shot("ed_20_touch");
    tap(16, 232); run(0, 2);                                  /* the first prompt: X (place) */
    shot("ed_23_touch_prompt");
    /* back to the title restores level 1 */
    press(PAD_START); presses(PAD_UP, 1); press(PAD_CROSS); run(0, 4);
    press(PAD_CIRCLE); run(0, 10);
    expect(game_phase() == GS_TITLE, "O on the level list goes back to the title");
    printf(fails ? "edittest: %d FAILURES\n" : "edittest: all passed\n", fails);
    return fails ? 1 : 0;
}
