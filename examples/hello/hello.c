/*
 * Hello cube - the smallest complete game on the engine (see engine/README.md).
 *
 * A gouraud-shaded cube you spin with the d-pad or the left stick; CROSS (or the round touch
 * button) flashes it and plays a sound. Everything here is engine API: App callbacks, Input,
 * the GTE maths, 2D primitives and the sound bank, so it builds for Android, Windows, the PS1
 * and the headless runner exactly like Deadbird does.
 */
#include "engine.h"
#include "gte.h"
#include "draw2d.h"
#include "snd.h"

/* ---- a sound bank: one blip, synthesised at start-up (8-bit PCM for the mixer) */
#define BLIP_LEN 2200
static signed char blip_pcm[BLIP_LEN];
static SndSample bank[1];

/* ---- the cube */
static const SVECTOR CUBE_V[8] = {
    { -400, -400, -400, 0 }, { 400, -400, -400, 0 }, { -400, 400, -400, 0 }, { 400, 400, -400, 0 },
    { -400, -400, 400, 0 },  { 400, -400, 400, 0 },  { -400, 400, 400, 0 },  { 400, 400, 400, 0 },
};
/* faces as PS1 quads (vertex order 0,1,2,3 = top-left, top-right, bottom-left, bottom-right) */
static const u8 CUBE_F[6][4] = {
    { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 6, 2 }, { 1, 5, 3, 7 }, { 4, 5, 0, 1 }, { 2, 3, 6, 7 },
};
static const u8 FACE_RGB[6][3] = {
    { 230, 60, 30 }, { 40, 160, 230 }, { 240, 200, 40 }, { 60, 200, 90 }, { 200, 80, 220 }, { 240, 240, 240 },
};

static int yaw, pitch = 300, spin = 12, flash;

/* the touch controls: one round button bottom-right that acts as CROSS */
#define BTN_R 26
static int btn_x(void) { return SCREEN_W - 46; }
#define BTN_Y 190
static u32 touch_buttons(int x, int y)
{
    int dx = x - btn_x(), dy = y - BTN_Y;
    return dx * dx + dy * dy <= (BTN_R + 6) * (BTN_R + 6) ? PAD_CROSS : 0;
}

static void init(void)
{
    int i;
    for (i = 0; i < BLIP_LEN; i++) {            /* a falling square-ish tone with a fade */
        int ph = (i * (90 - i / 40)) >> 6 & 63, env = 127 * (BLIP_LEN - i) / BLIP_LEN;
        blip_pcm[i] = (signed char)(ph < 32 ? env : -env);
    }
    bank[0].data = blip_pcm; bank[0].len = BLIP_LEN; bank[0].rate = 22050; bank[0].loop = 0;
    snd_init(bank, 1);
}

static void tick(void)
{
    const Input *in = eng_in();
    int sx = in->lx, sy = in->ly;
    if (in->stick_on) { sx = (in->stick_x - in->stick_ox) * 120; sy = (in->stick_oy - in->stick_y) * 120; }
    if (in->pad & PAD_LEFT) sx = -4096;
    if (in->pad & PAD_RIGHT) sx = 4096;
    if (in->pad & PAD_UP) sy = 4096;
    if (in->pad & PAD_DOWN) sy = -4096;
    if (sx || sy) { yaw += sx * 40 >> 12; pitch += sy * 40 >> 12; spin = 0; }
    else if (spin < 12) spin++;
    yaw += spin;
    if (in->press & PAD_CROSS) { flash = 20; snd_play(0, 3500, 0, 4096); eng_rumble(120); }
    if (flash) flash--;
}

static void draw(FrameBuf *fb)
{
    MATRIX ry, rx, m;
    PROJ pr;
    s16 sx[8], sy[8];
    s32 sz[8];
    int i;
    /* background */
    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(20, 24, 60), RGB(4, 4, 10), NO_SEMI, OT_LEN - 1);
    /* the cube: rotate, push 2400 units in front of the camera, project */
    pr.h = 280; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 100;
    mat_rot_y(&ry, yaw & 4095);
    mat_rot_x(&rx, pitch & 4095);
    mat_mul(&rx, &ry, &m);
    mat_translate(&m, 0, 0, 2400);
    for (i = 0; i < 8; i++) {
        VECTOR c;
        mat_apply(&m, &CUBE_V[i], &c);
        sz[i] = c.vz;
        rot_trans_pers(&pr, &c, &sx[i], &sy[i]);
    }
    for (i = 0; i < 6; i++) {
        const u8 *f = CUBE_F[i];
        POLY *p;
        int k, z = (int)((sz[f[0]] + sz[f[1]] + sz[f[2]] + sz[f[3]]) >> 4);
        if (normal_clip(sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]], sx[f[2]], sy[f[2]]) <= 0) continue;   /* back face */
        p = prim_alloc(fb);
        if (!p) break;
        memset(p, 0, sizeof(*p));
        p->code = PF_QUAD | PF_GOURAUD;
        for (k = 0; k < 4; k++) {
            int lit = k < 2 ? 255 : 150, fl = flash * 6;    /* lighter top edge, a white flash */
            p->v[k].x = sx[f[k]]; p->v[k].y = sy[f[k]];
            p->v[k].r = (u8)((FACE_RGB[i][0] * lit >> 8) + fl > 255 ? 255 : (FACE_RGB[i][0] * lit >> 8) + fl);
            p->v[k].g = (u8)((FACE_RGB[i][1] * lit >> 8) + fl > 255 ? 255 : (FACE_RGB[i][1] * lit >> 8) + fl);
            p->v[k].b = (u8)((FACE_RGB[i][2] * lit >> 8) + fl > 255 ? 255 : (FACE_RGB[i][2] * lit >> 8) + fl);
        }
        ot_add(fb, 10 + (z >> 4), p);
    }
    /* touch controls: the button as a fan of triangles */
    if (eng_in()->touch_ui) {
        int cx = btn_x(), a, held = (eng_in()->pad & PAD_CROSS) != 0;
        for (a = 0; a < 4096; a += 256) {
            tri_f(fb, cx, BTN_Y, cx + (icos(a) * BTN_R >> 12), BTN_Y + (isin(a) * BTN_R >> 12),
                  cx + (icos(a + 256) * BTN_R >> 12), BTN_Y + (isin(a + 256) * BTN_R >> 12),
                  held ? RGB(255, 200, 80) : RGB(120, 90, 50), SEMI_ADD, 2);
        }
    }
}

static int ram_used(void) { return (int)sizeof(blip_pcm); }

/* the keyboard (Windows): arrows / WASD spin it, SPACE or ENTER is CROSS */
static const KeyBind KEYS[] = {
    { KEY_UP, 0, 0, PAD_UP }, { KEY_DOWN, 0, 0, PAD_DOWN }, { KEY_LEFT, 0, 0, PAD_LEFT }, { KEY_RIGHT, 0, 0, PAD_RIGHT },
    { 'W', KB_LY_POS, 0, 0 }, { 'S', KB_LY_NEG, 0, 0 }, { 'A', KB_LX_NEG, 0, 0 }, { 'D', KB_LX_POS, 0, 0 },
    { ' ', 0, 0, PAD_CROSS }, { KEY_ENTER, 0, 0, PAD_CROSS },
    { 0, 0, 0, 0 }
};

const App game_app = {
    "HELLO CUBE",
    init, tick, draw,
    touch_buttons, 0, 0, 50,          /* touch: the button, no tap / look, stick on the left half */
    0, 0, 0, 0,                       /* no raw pointer mode */
    0,                                /* the stick is never a d-pad here */
    KEYS, 0,                          /* keyboard */
    0, 0,                             /* no user files */
    ram_used,
};
