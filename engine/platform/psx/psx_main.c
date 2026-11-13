/*
 * PlayStation platform layer (PSn00bSDK), for any game on the engine.
 *
 * Same game and engine code as Android / Windows; only this file, gpu_psx.c and snd_psx.c are
 * PS1 specific. NTSC 320x240, double buffered in VRAM at (0,0) / (0,240) exactly as the engine
 * lays it out, controller in port 1 (digital pad or DualShock - press ANALOG for the sticks).
 * SELECT toggles a frame-rate readout (Input.fps).
 */
#include <stdint.h>
#include <psxapi.h>
#include <psxgpu.h>
#include <psxgte.h>
/* the engine's RECT has the same layout as libgpu's; keep the names apart */
#define RECT ENGINE_RECT
#include "engine.h"
#include "prof.h"
#undef RECT

void gpu_psx_submit(void);

static DISPENV disp;
static uint8_t pad_buf[2][34];

/* raw BIOS pad buffer: stat, (len|type<<4), buttons (active low), rx, ry, lx, ly */
static int axis(uint8_t v)
{
    int d = (int)v - 128;
    if (d > -28 && d < 28) return 0;                  /* dead zone */
    d = d < 0 ? d + 28 : d - 28;
    d = d * 4096 / 100;
    return d > 4096 ? 4096 : (d < -4096 ? -4096 : d);
}

static u32 read_pad(void)
{
    const uint8_t *b = pad_buf[0];
    int type = b[1] >> 4, lx = 0, ly = 0, rx = 0, ry = 0;
    u32 pad = 0;
    if (b[0] == 0 && (type == 4 || type == 5 || type == 7)) {
        pad = (u32)(~(b[2] | (b[3] << 8)) & 0xffff);    /* same bit layout as the engine's PAD_* */
        if (type == 5 || type == 7) {                     /* analog: sticks */
            rx = axis(b[4]); ry = -axis(b[5]);
            lx = axis(b[6]); ly = -axis(b[7]);
        }
    }
    eng_set_analog(lx, ly, rx, ry);
    return pad;
}

int main(int argc, const char **argv)
{
    int dx, dy, last, show_fps = 0, fps_acc = 0, fps_frames = 0;
    u32 prev = 0;
    (void)argc; (void)argv;
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    SetDefDispEnv(&disp, 0, 0, SCREEN_W, SCREEN_H);

    InitPAD(pad_buf[0], 34, pad_buf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    InitGeom();                                       /* turn on the GTE (vertex transforms) */
    prof_init();                                      /* timer 1 counts scanlines for the profile readout */
    eng_init();
    eng_set_touch_ui(0);
    SetDispMask(1);

    last = VSync(-1);
    for (;;) {
        u32 pad = read_pad();
        int now, ticks;
        /* CPU: simulate and build this frame's display list (the GPU is still drawing the last one) */
        eng_frame(pad);
        DrawSync(0);                                    /* last frame finished drawing ... */
        VSync(0);
        eng_display_area(&dx, &dy);                    /* ... so show it: it's the other buffer */
        disp.disp.x = (int16_t)dx;
        disp.disp.y = (int16_t)(dy ^ SCREEN_H);
        PutDispEnv(&disp);
        gpu_psx_submit();                               /* GPU draws this frame while the loop goes on */
        /* game logic runs at a fixed 60 ticks per second: when a frame took several vblanks,
           run the missed ticks (logic only, no drawing) so the game keeps real-time speed */
        now = VSync(-1);
        ticks = now - last - 1;
        last = now;
        /* SELECT toggles a frame-rate readout (averaged over about a second) */
        if ((pad & PAD_SELECT) && !(prev & PAD_SELECT)) show_fps ^= 1;
        prev = pad;
        fps_acc += ticks + 1; fps_frames++;
        if (fps_acc >= 60) { eng_show_fps(show_fps ? (fps_frames * 60 + fps_acc / 2) / fps_acc : 0); fps_acc = fps_frames = 0; }
        if (!show_fps) eng_show_fps(0);
        /* at most 2 catch-up ticks: real-time speed down to 20 fps; below that the game slows
           down instead - more catch-up work would only make the next frame later still */
        if (ticks > 2) ticks = 2;
        while (ticks-- > 0) eng_step_catchup(pad);
    }
    return 0;
}
