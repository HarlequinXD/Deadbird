/*
 * Procedural fire, the PS1 way: a 128x80 8-bit heat buffer is simulated every frame
 * (each cell takes the heat of a random neighbour below, minus a random decay, drifting
 * with a slow wind), streamed into VRAM with LoadImage, and drawn as an 8bpp textured
 * quad through a 37-colour fire CLUT using additive blending. Heat 0 = texel 0 = transparent.
 */
#include "title.h"
#include "hud.h"
#include "fx.h"
#include "gte.h"

#define FW 128
#define FH 80
#define FIRE_VX 832          /* VRAM: 8bpp texture page 13 */
#define FIRE_VY 256
#define FIRE_CLUT_Y 483
#define HEAT_MAX 36

static u8  heat[FH][FW];
static u16 fire_clut[256];
static u32 rng = 0xdeadb1edu;
static int wind;

static int sel, confirm_t = -1, fade_in, choice = -1, tap_item = -1;
static const char *MENU[MENU_COUNT] = { "VERSUS CPU", "TRAINING", "MOVE LIST" };
#define MENU_Y0 116
#define MENU_DY 30

static u32 rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void title_init(void)
{
    RECT r;
    int i;
    /* fire palette: black -> blood -> red -> orange -> yellow -> white, STP set for additive */
    for (i = 0; i < 256; i++) fire_clut[i] = 0;
    for (i = 1; i <= HEAT_MAX; i++) {
        int t = i * 4096 / HEAT_MAX, r, g, b;
        r = t < 1400 ? t * 255 / 1400 : 255;
        g = t < 1300 ? 0 : (t < 3300 ? (t - 1300) * 230 / 2000 : 230 + (t - 3300) * 25 / 796);
        b = t < 3000 ? 0 : (t - 3000) * 220 / 1096;
        if (i < 6) { r = r * i / 6; }                    /* thin smoky edge */
        fire_clut[i] = (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
    }
    r.x = 0; r.y = FIRE_CLUT_Y; r.w = 256; r.h = 1;
    gpu_load_image(&r, fire_clut);
    memset(heat, 0, sizeof(heat));
    title_reset();
}

void title_reset(void) { sel = 0; confirm_t = -1; fade_in = 32; choice = -1; tap_item = -1; }

void fire_step(int intensity)
{
    static u16 up[FH][FW / 2];
    RECT r;
    int x, y;
    /* source row: white-hot with flicker, intensity 0..36 */
    static int phase;
    phase += 37;
    for (x = 0; x < FW; x++) {
        /* slowly travelling hot spots along the base give the fire separate tongues */
        int lump = (isin(x * 190 + phase) + isin(x * 77 - phase * 2) + 8192) >> 9;   /* 0..16 */
        int v = intensity - 12 + lump - (int)(rnd() % 4);
        heat[FH - 1][x] = (u8)(v < 0 ? 0 : (v > HEAT_MAX ? HEAT_MAX : v));
    }
    if ((rnd() & 63) == 0) wind = (int)(rnd() % 3) - 1;
    for (y = 0; y < FH - 1; y++)
        for (x = 0; x < FW; x++) {
            u32 rr = rnd();
            int src = heat[y + 1][x];
            int dx = (int)(rr & 3) - 1 + wind;
            int decay = (int)((rr >> 2) & 1) + ((y < FH / 3) && ((rr >> 3) & 3) == 0);
            int nx = (x + dx) & (FW - 1);
            int v = src - decay;
            heat[y][nx] = (u8)(v < 0 ? 0 : v);
        }
    /* stream to VRAM as an 8bpp image (two heat cells per 16-bit word) */
    for (y = 0; y < FH; y++)
        for (x = 0; x < FW / 2; x++)
            up[y][x] = (u16)(heat[y][x * 2] | (heat[y][x * 2 + 1] << 8));
    r.x = FIRE_VX; r.y = FIRE_VY; r.w = FW / 2; r.h = FH;
    gpu_load_image(&r, &up[0][0]);
}

void fire_draw(FrameBuf *fb, int y_top, int alpha)
{
    /* one stretched 8bpp quad, additive over the scene */
    int pass;
    for (pass = 0; pass < 1; pass++) {
        POLY *p = prim_alloc(fb);
        int k, x0 = pass ? -40 : -8, x1 = pass ? 360 : 328, yt = y_top + pass * 26;
        if (!p) return;
        memset(p, 0, sizeof(*p));
        p->code = PF_TEX | PF_QUAD | PF_SEMI;
        p->tpage = get_tpage(TP_8BIT, SEMI_ADD, FIRE_VX, FIRE_VY);
        p->clut = get_clut(0, FIRE_CLUT_Y);
        p->v[0].x = (s16)x0; p->v[0].y = (s16)yt;  p->v[0].u = (u8)(pass ? 127 : 0); p->v[0].v = 0;
        p->v[1].x = (s16)x1; p->v[1].y = (s16)yt;  p->v[1].u = (u8)(pass ? 0 : 127); p->v[1].v = 0;
        p->v[2].x = (s16)x0; p->v[2].y = 242;      p->v[2].u = (u8)(pass ? 127 : 0); p->v[2].v = FH;
        p->v[3].x = (s16)x1; p->v[3].y = 242;      p->v[3].u = (u8)(pass ? 0 : 127); p->v[3].v = FH;
        { int a = pass ? alpha / 2 : alpha;
          for (k = 0; k < 4; k++) { p->v[k].r = (u8)a; p->v[k].g = (u8)(a * 3 / 4); p->v[k].b = (u8)(a / 2); } }
        ot_add(fb, 12 + pass, p);
    }
}

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
    sky_draw(fb, t, t * 4);
    embers_draw(fb, t, 20);
    fire_step(HEAT_MAX);
    fire_draw(fb, 96, 104);

    /* logo: big fiery DEADBIRD with a pulsing glow plate behind it */
    {
        int glow = 60 + (isin(t * 40) * 30 >> 12);
        quad_g(fb, 20, 14, 300, 34, RGB(0, 0, 0), RGB(glow, glow / 5, 0), SEMI_ADD, 10);
        quad_g(fb, 20, 34, 300, 60, RGB(glow, glow / 5, 0), RGB(0, 0, 0), SEMI_ADD, 10);
        text_hell(fb, "DEADBIRD", 160, 16, 5, t, 4);
        text_center(fb, "- HELL BRAWLER -", 160, 72, 1, 150 + (isin(t * 70) * 40 >> 12), 4);
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
    text(fb, "V0.2", 290, 228, 1, 90, 5);

    /* fade from / to black */
    if (fade_in > 0) quad_g(fb, 0, 0, 320, 240, RGB(fade_in * 8, fade_in * 8, fade_in * 8), RGB(fade_in * 8, fade_in * 8, fade_in * 8), SEMI_SUB, 1);
    if (confirm_t >= 0) quad_g(fb, 0, 0, 320, 240, RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), RGB(confirm_t * 8, confirm_t * 8, confirm_t * 8), SEMI_SUB, 1);
}
