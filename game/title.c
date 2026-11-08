/* Title screen: hell sky, embers, DEADBIRD logo and menu over a live attract scene. */
#include "title.h"
#include "hud.h"
#include "fx.h"
#include "gte.h"
#include "tim.h"
#include "logo_tim.h"

static TimInfo logo;

static int sel, confirm_t = -1, fade_in, choice = -1, tap_item = -1;
static const char *MENU[MENU_COUNT] = { "PLAY", "CONTROLS" };
#define MENU_Y0 128
#define MENU_DY 30

void title_init(void) { tim_upload(logo_tim, LOGO_TIM_SIZE, &logo); title_reset(); }

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

    /* logo: the DEADBIRD artwork as an 8bpp texture, two 160 px halves drawn 1:1, gently pulsing */
    {
        int half, k, y0 = 4 + (isin(t * 24) * 2 >> 12);
        int b = 128 + (isin(t * 37) * 14 >> 12);
        for (half = 0; half < 2; half++) {
            POLY *p = prim_alloc(fb);
            int x0 = half * (LOGO_W / 2), v0 = half * 128;
            if (!p) break;
            memset(p, 0, sizeof(*p));
            p->code = PF_TEX | PF_QUAD;
            p->tpage = logo.tpage; p->clut = logo.clut;
            p->v[0].x = (s16)x0;                p->v[0].y = (s16)y0;            p->v[0].u = 0;               p->v[0].v = (u8)v0;
            p->v[1].x = (s16)(x0 + LOGO_W / 2); p->v[1].y = (s16)y0;            p->v[1].u = (u8)(LOGO_W / 2); p->v[1].v = (u8)v0;
            p->v[2].x = (s16)x0;                p->v[2].y = (s16)(y0 + LOGO_H); p->v[2].u = 0;               p->v[2].v = (u8)(v0 + LOGO_H);
            p->v[3].x = (s16)(x0 + LOGO_W / 2); p->v[3].y = (s16)(y0 + LOGO_H); p->v[3].u = (u8)(LOGO_W / 2); p->v[3].v = (u8)(v0 + LOGO_H);
            for (k = 0; k < 4; k++) { p->v[k].r = (u8)b; p->v[k].g = (u8)(b * 15 / 16); p->v[k].b = (u8)(b * 15 / 16); }
            ot_add(fb, 4, p);
        }
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
