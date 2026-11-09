/* Headless test harness: plays the game without a window (title -> play with a
   scripted player), writes frames as BMP and reports timing + memory. Used by CI. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "deadbird.h"
#include "snd.h"
#include "player.h"
#include "enemy.h"
#include "gpu.h"


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

static int frames_run;
/* the game's sound, mixed exactly as the Android/Windows builds do, saved as a WAV of the session */
#define AUDIO_MAX (SND_RATE * 90)
static s16 audio[AUDIO_MAX * 2];
static int audio_n, audio_acc;
static void frame(u32 pad)
{
    int n;
    game_frame(pad);
    frames_run++;
    audio_acc += SND_RATE;
    n = audio_acc / 60; audio_acc -= n * 60;
    if (audio_n + n <= AUDIO_MAX) { snd_mix(&audio[audio_n * 2], n); audio_n += n; }
}
static void run(u32 pad, int n) { int i; for (i = 0; i < n; i++) frame(pad); }

static void write_wav(const char *dir)
{
    char path[512];
    FILE *f;
    unsigned char h[44] = { 'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                            0,0,0,0,0,0,0,0,4,0,16,0,'d','a','t','a' };
    unsigned data = (unsigned)audio_n * 4, rate = SND_RATE, bps = SND_RATE * 4, riff = data + 36;
    int k;
    for (k = 0; k < 4; k++) { h[4 + k] = (unsigned char)(riff >> (8 * k)); h[24 + k] = (unsigned char)(rate >> (8 * k));
                              h[28 + k] = (unsigned char)(bps >> (8 * k)); }
    sprintf(path, "%s/session_audio.wav", dir);
    f = fopen(path, "wb");
    if (!f) return;
    fwrite(h, 1, 40, f);
    fwrite(&data, 4, 1, f);
    fwrite(audio, 4, (size_t)audio_n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    clock_t t0;
    char name[32];
    int i;

    game_init();
    printf("static game RAM: %d bytes of %d (PS1 main RAM)\n", game_ram_used(), RAM_BUDGET);
    printf("VRAM: %d bytes\n", VRAM_W * VRAM_H * 2);

    t0 = clock();
    run(0, 240);
    write_bmp(out, "01_title");
    run(PAD_DOWN, 1); run(0, 1); run(PAD_CROSS, 1); run(0, 40);
    write_bmp(out, "02_controls");
    run(PAD_CROSS, 1); run(0, 10);
    run(PAD_CROSS, 1); run(0, 40);                                      /* PLAY (menu resets to the top) */
    if (game_phase() != GS_PLAY) { printf("FAIL: not in play mode\n"); return 1; }
    game_set_touch_ui(1);
    run(0, 20);
    write_bmp(out, "03_idle");
    /* walk cycle: one frame every 4 for a gait strip */
    game_set_stick(1, 60, 180, 60, 162);                 /* half push forward: walk */
    for (i = 0; i < 6; i++) { run(0, 4); sprintf(name, "04_walk_%d", i); write_bmp(out, name); }
    game_set_stick(1, 60, 180, 60, 140);                 /* full push: run */
    run(0, 40);
    for (i = 0; i < 4; i++) { run(0, 3); sprintf(name, "05_run_%d", i); write_bmp(out, name); }
    game_set_stick(0, 0, 0, 0, 0);
    run(PAD_UP | PAD_CIRCLE | PAD_RIGHT, 14);
    write_bmp(out, "06_run_turn_lean");
    run(PAD_RIGHT | PAD_CIRCLE, 30);
    run(PAD_UP | PAD_CIRCLE, 1); run(PAD_UP | PAD_CIRCLE | PAD_CROSS, 1);
    run(PAD_UP | PAD_CIRCLE, 12);
    write_bmp(out, "07_jump");
    run(PAD_UP, 40);
    run(0, 30);
    write_bmp(out, "08_stop");
    run(PAD_DOWN, 20);
    write_bmp(out, "09_turn_back_toward_camera");
    run(PAD_R1, 30);
    write_bmp(out, "10_camera_swing");
    for (i = 0; i < 400; i++) frame(PAD_LEFT);
    write_bmp(out, "11_after_walk");
    /* back off, then run into the wall again: she should just stop and lean on it */
    run(0, 20); run(PAD_RIGHT, 40); run(0, 10);
    run(PAD_LEFT | PAD_CIRCLE, 26);
    for (i = 0; i < 8; i++) { run(PAD_LEFT | PAD_CIRCLE, 3); sprintf(name, "14_wall_%d", i); write_bmp(out, name); }
    /* hard turn while running: banking strip */
    run(PAD_UP | PAD_CIRCLE, 40);
    for (i = 0; i < 4; i++) { run(PAD_UP | PAD_RIGHT | PAD_CIRCLE, 3); sprintf(name, "15_bank_%d", i); write_bmp(out, name); }
    /* chainsaw combo: slash, backhand, overhead slam (one press during each swing chains the next) */
    run(0, 40);
    {
        int x0 = 0;
        static const int at[] = { 2, 5, 9, 14, 3, 6, 9, 14, 5, 10, 15, 22 };
        game_debug_side_cam(1);
        for (i = 0; i < 12; i++) {
            if (i % 4 == 0 && i < 8) { run(BTN_ATTACK, 1); run(0, 1); x0 = 2; }
            if (i == 8) { run(BTN_ATTACK, 1); run(0, 1); x0 = 2; }
            run(i == 2 || i == 6 ? BTN_ATTACK : 0, at[i] - x0); x0 = at[i];
            sprintf(name, "16_combo_%02d", i); write_bmp(out, name);
        }
        run(0, 40);
        game_debug_side_cam(0);
    }
    /* side-on gait strips (walk, then run) for checking stride vs speed */
    game_set_touch_ui(0);
    game_debug_side_cam(1);
    run(0, 40);
    game_set_stick(1, 60, 180, 60, 162);
    run(0, 30);
    for (i = 0; i < 8; i++) { run(0, 3); sprintf(name, "12_side_walk_%d", i); write_bmp(out, name); }
    game_set_stick(1, 60, 180, 60, 140);
    run(0, 40);
    for (i = 0; i < 8; i++) { run(0, 2); sprintf(name, "13_side_run_%d", i); write_bmp(out, name); }
    game_set_stick(0, 0, 0, 0, 0);
    game_debug_side_cam(0);
    {   /* hold the stick fully right: the camera follows, so she should keep curving round */
        int total = 0, last = game_debug_player_yaw(), y;
        game_set_stick(1, 60, 180, 94, 180);
        for (i = 0; i < 150; i++) {
            run(0, 1);
            y = game_debug_player_yaw();
            total += ((y - last + 2048) & 4095) - 2048; last = y;
        }
        game_set_stick(0, 0, 0, 0, 0);
        printf("stick held right for 150 frames: turned %d degrees\n", total * 360 / 4096);
        if (total < 1024) { printf("FAIL: holding the stick sideways doesn't keep turning\n"); return 1; }
    }
    {   /* enemy + FATALITY: walk up to the first trooper, chainsaw him until he reels, finish him */
        int k, st = 0, shots = 0;
        game_set_touch_ui(1);
        game_debug_near_enemy(0, 3800);
        run(0, 70);
        write_bmp(out, "20_trooper_fight");
        printf("trooper: state %d hp %d, Deadbird hp %d\n", game_debug_enemy_state(0) / 1000, game_debug_enemy_state(0) % 1000, game_debug_player_hp());
        game_debug_near_enemy(0, 950);
        for (k = 0; k < 12 && game_debug_enemy_state(0) / 1000 != E_DAZED; k++) {
            run(BTN_ATTACK, 1); run(0, 5); run(BTN_ATTACK, 1); run(0, 8); run(BTN_ATTACK, 1); run(0, 8); run(BTN_ATTACK, 1); run(0, 30);
            game_debug_near_enemy(0, 950);
            if (k == 0) write_bmp(out, "21_trooper_hit");
        }
        st = game_debug_enemy_state(0);
        printf("after %d combos: trooper state %d hp %d, fatality %d\n", k, st / 1000, st % 1000, game_debug_fatality());
        run(0, 20);
        write_bmp(out, "22_fatality_prompt");
        if (game_debug_fatality() != -2) { printf("FAIL: no fatality prompt\n"); return 1; }
        run(PAD_TRIANGLE | PAD_SQUARE, 1);
        {
            static const int at[] = { 10, 30, 36, 50, 70, 84, 90, 100, 120, 140, 170, 200 };
            int j = 0, f;
            for (k = 0; k < 2000 && game_debug_fatality() >= 0; k++) {
                run(0, 1);
                f = game_debug_fatality();
                if (j < 12 && f >= at[j]) { char nm[32]; sprintf(nm, "23_fatality_%02d", j); write_bmp(out, nm); j++; shots++; }
            }
        }
        st = game_debug_enemy_state(0);
        printf("fatality: %d frames captured, trooper state %d (6 = cut in half)\n", shots, st / 1000);
        if (st / 1000 != E_CUT) { printf("FAIL: the fatality didn't cut him in half\n"); return 1; }
        run(0, 60);
        write_bmp(out, "24_after_fatality");
    }
    write_wav(out);
    printf("audio: %.1f s mixed\n", audio_n / (double)SND_RATE);
    printf("ran %d frames, %.3f ms/frame avg\n", frames_run,
           (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / frames_run);
    return 0;
}
