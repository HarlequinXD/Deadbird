/* Title screen: the painted backdrop (logo included), rising embers and sci-fi hell menu plates. */
#include "title.h"
#include "hud.h"
#include "fx.h"
#include "gte.h"
#include "tim.h"
#include "ui.h"
#include "snd.h"

static int sel, confirm_t = -1, fade_in, choice = -1, tap_item = -1;
static const char *MENU[MENU_COUNT] = { "PLAY", "CONTROLS" };
#define MENU_Y0 150
#define MENU_DY 38
#define PLATE_W 168

void title_init(void) { title_reset(); }

void title_reset(void) { sel = 0; confirm_t = -1; fade_in = 32; choice = -1; tap_item = -1; }

void title_tap(int x, int y)
{
    int i;
    for (i = 0; i < MENU_COUNT; i++)
        if (y >= MENU_Y0 + i * MENU_DY && y < MENU_Y0 + i * MENU_DY + 32 && x > 160 - PLATE_W / 2 - 30 && x < 160 + PLATE_W / 2 + 30) tap_item = i;
}

int title_update(u32 pad, u32 press)
{
    (void)pad;
    if (fade_in > 0) fade_in--;
    if (confirm_t >= 0) {
        if (++confirm_t >= 30) { int c = choice; title_reset(); return c; }
        return -1;
    }
    if (tap_item >= 0) {
        if (tap_item == sel) press |= PAD_CROSS;
        sel = tap_item;
        tap_item = -1;
    }
    if (press & PAD_UP)   { sel = (sel + MENU_COUNT - 1) % MENU_COUNT; snd_play(SFX_UI_MOVE, 3000, 0, 4096); }
    if (press & PAD_DOWN) { sel = (sel + 1) % MENU_COUNT; snd_play(SFX_UI_MOVE, 3000, 0, 4096); }
    if (press & (PAD_CROSS | PAD_START | PAD_SQUARE)) { choice = sel; confirm_t = 0; snd_play(SFX_UI_OK, 4096, 0, 4096); }
    return -1;
}

void title_draw(FrameBuf *fb, int t)
{
    int i;
    ui_title_bg(fb, 128, OT_LEN - 1);
    embers_draw(fb, t, 20);

    /* menu: forged plates; the selected one burns, talons close in on it */
    for (i = 0; i < MENU_COUNT; i++) {
        int y = MENU_Y0 + i * MENU_DY, lit = i == sel;
        int blink = lit && confirm_t >= 0 && ((confirm_t >> 2) & 1);
        ui_plate(fb, 160, y, PLATE_W, lit, t, 10);
        if (!blink) text_center(fb, MENU[i], 161, y + 7, 2, lit ? 255 : 120, 6);
    }
    text(fb, "V0.4", 290, 228, 1, 90, 5);

    /* fade from / to black */
    if (fade_in > 0) quad_g(fb, 0, 0, 320, 240, RGB(fade_in * 8, fade_in * 8, fade_in * 8), RGB(fade_in * 8, fade_in * 8, fade_in * 8), SEMI_SUB, 1);
    if (confirm_t >= 0) quad_g(fb, 0, 0, 320, 240, RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), SEMI_SUB, 1);
}
