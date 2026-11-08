/* Title screen: hell sky, embers, DEADBIRD logo and menu over a live attract scene. */
#include "title.h"
#include "hud.h"
#include "fx.h"
#include "gte.h"

static int sel, confirm_t = -1, fade_in, choice = -1, tap_item = -1;
static const char *MENU[MENU_COUNT] = { "PLAY", "CONTROLS" };
#define MENU_Y0 128
#define MENU_DY 30

void title_init(void) { title_reset(); }

void title_reset(void) { sel = 0; confirm_t = -1; fade_in = 32; choice = -1; tap_item = -1; }

void title_tap(int x, int y)
{
    int i;
    (void)x;
    for (i = 0; i < MENU_COUNT; i++)
        if (y >= MENU_Y0 + i * MENU_DY - 6 && y < MENU_Y0 + i * MENU_DY + 24) tap_item = i;
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
    if (press & PAD_UP)   sel = (sel + MENU_COUNT - 1) % MENU_COUNT;
    if (press & PAD_DOWN) sel = (sel + 1) % MENU_COUNT;
    if (press & (PAD_CROSS | PAD_START | PAD_SQUARE)) { choice = sel; confirm_t = 0; }
    return -1;
}

void title_draw(FrameBuf *fb, int t)
{
    int i;
    embers_draw(fb, t, 20);

    /* logo: big fiery DEADBIRD with a pulsing glow plate behind it */
    {
        int glow = 60 + (isin(t * 40) * 30 >> 12);
        quad_g(fb, 20, 14, 300, 34, RGB(0, 0, 0), RGB(glow, glow / 5, 0), SEMI_ADD, 10);
        quad_g(fb, 20, 34, 300, 60, RGB(glow, glow / 5, 0), RGB(0, 0, 0), SEMI_ADD, 10);
        text_hell(fb, "DEADBIRD", 160, 16, 5, t, 4);
        text_center(fb, "- HELLFIRE -", 160, 72, 1, 150 + (isin(t * 70) * 40 >> 12), 4);
    }

    /* menu: stacked down from the centre; selected row burns */
    for (i = 0; i < MENU_COUNT; i++) {
        int y = MENU_Y0 + i * MENU_DY, w = text_width(MENU[i], 2);
        if (i == sel) {
            int flick = 150 + (isin(t * 180) * 60 >> 12);
            int blink = confirm_t >= 0 && ((confirm_t >> 2) & 1);
            int ax = 160 - w / 2 - 18 - (isin(t * 120) * 4 >> 12), bx = 160 + w / 2 + 18 + (isin(t * 120) * 4 >> 12);
            quad_g(fb, 160 - w / 2 - 30, y - 5, 160 + w / 2 + 30, y + 23, RGB(flick, flick / 4, 0), RGB(40, 4, 0), SEMI_ADD, 8);
            quad_g(fb, 160 - w / 2 - 30, y - 6, 160 + w / 2 + 30, y - 5, RGB(255, 200, 90), RGB(255, 200, 90), NO_SEMI, 7);
            quad_g(fb, 160 - w / 2 - 30, y + 23, 160 + w / 2 + 30, y + 24, RGB(200, 60, 20), RGB(200, 60, 20), NO_SEMI, 7);
            tri_f(fb, ax, y + 9, ax - 10, y + 2, ax - 10, y + 16, RGB(255, 180, 60), NO_SEMI, 6);
            tri_f(fb, bx, y + 9, bx + 10, y + 2, bx + 10, y + 16, RGB(255, 180, 60), NO_SEMI, 6);
            if (!blink) text_center(fb, MENU[i], 160, y, 2, 255, 5);
        } else {
            quad_g(fb, 160 - w / 2 - 22, y - 3, 160 + w / 2 + 22, y + 21, RGB(20, 2, 4), RGB(8, 1, 2), NO_SEMI, 9);
            text_center(fb, MENU[i], 160, y, 2, 95, 5);
        }
    }
    text(fb, "V0.3", 290, 228, 1, 90, 5);

    /* fade from / to black */
    if (fade_in > 0) quad_g(fb, 0, 0, 320, 240, RGB(fade_in * 8, fade_in * 8, fade_in * 8), RGB(fade_in * 8, fade_in * 8, fade_in * 8), SEMI_SUB, 1);
    if (confirm_t >= 0) quad_g(fb, 0, 0, 320, 240, RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), SEMI_SUB, 1);
}
