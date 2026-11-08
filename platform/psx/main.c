/*
 * Deadbird - PlayStation platform layer (PSn00bSDK).
 *
 * Same game and engine code as Android / Windows; only this file and gpu_psx.c are PS1
 * specific. NTSC 320x240, double buffered in VRAM at (0,0) / (0,240) exactly as the engine
 * already lays it out, controller in port 1 (digital pad or DualShock - press ANALOG for the
 * sticks: left stick moves, right stick turns the camera).
 */
#include <stdint.h>
#include <psxapi.h>
#include <psxgpu.h>
#include "deadbird.h"

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
            if (game_phase() != GS_PLAY) {                /* menus: left stick as a d-pad */
                if (ly > 2400) pad |= PAD_UP;
                if (ly < -2400) pad |= PAD_DOWN;
                if (lx > 2400) pad |= PAD_RIGHT;
                if (lx < -2400) pad |= PAD_LEFT;
            }
        }
    }
    game_set_analog(lx, ly, rx, ry);
    return pad;
}

int main(int argc, const char **argv)
{
    int dx, dy;
    (void)argc; (void)argv;
    ResetGraph(0);
    SetVideoMode(MODE_NTSC);
    SetDefDispEnv(&disp, 0, 0, SCREEN_W, SCREEN_H);

    InitPAD(pad_buf[0], 34, pad_buf[1], 34);
    StartPAD();
    ChangeClearPAD(0);

    game_init();
    game_set_touch_ui(0);
    SetDispMask(1);

    for (;;) {
        game_frame(read_pad());                         /* simulate + build + submit this frame */
        VSync(0);
        game_display_area(&dx, &dy);                    /* show the buffer that was just drawn */
        disp.disp.x = (int16_t)dx;
        disp.disp.y = (int16_t)dy;
        PutDispEnv(&disp);
    }
    return 0;
}
