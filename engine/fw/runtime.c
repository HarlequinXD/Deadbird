/*
 * The game runtime: an App that plays a project (fw/fwproj.h) in a world. A game made in the
 * editor is a project file plus this; examples/fps shows how a binary wires it up:
 *
 *   static void init(void) { fw_runtime_init(&my_world, project_text, project_len); }
 *   const App game_app = FW_RUNTIME_APP("MY GAME", init);
 *
 * Touch: the floating stick (left 45%), look by dragging the right side, FIRE and JUMP buttons.
 * After winning or dying, any button starts the level again.
 */
#include "fwrt.h"
#include "fwproj.h"
#include "draw2d.h"
#include "fwsfx.h"

static FwProject proj;
static int ok, end_wait;
static const char *src; static int src_len;

#define FIRE_R 28
#define JUMP_R 19
static int fire_x(void) { return SCREEN_W - 50; }
#define FIRE_Y 180
static int jump_x(void) { return SCREEN_W - 112; }
#define JUMP_Y 206

int fw_runtime_load(const char *text, int len)
{
    src = text; src_len = len;
    ok = fw_project_load(text, len, &proj);
    if (ok) fw_start();
    end_wait = 0;
    return ok;
}

void fw_runtime_init(const FwWorld *w, const char *text, int len)
{
    fw_set_world(w);
    fw_runtime_load(text, len);
}

const char *fw_runtime_title(void) { return proj.title; }

void fw_rt_tick(void)
{
    const Input *in = eng_in();
    if (!ok) return;
    fw_tick();
    if (fw_game.won || fw_game.lost) {
        if (++end_wait > 90 && (in->press & (PAD_CROSS | PAD_START | PAD_SQUARE))) { fw_start(); end_wait = 0; }
    }
}

static void disc(FrameBuf *fb, int cx, int cy, int r, u32 rgb, int semi, int ot)
{
    int a;
    for (a = 0; a < 4096; a += 256)
        tri_f(fb, cx, cy, cx + (icos(a) * r >> 12), cy + (isin(a) * r >> 12), cx + (icos(a + 256) * r >> 12), cy + (isin(a + 256) * r >> 12), rgb, semi, ot);
}

void fw_rt_draw(FrameBuf *fb)
{
    const Input *in = eng_in();
    if (!ok) {
        quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(40, 4, 4), RGB(10, 0, 0), NO_SEMI, OT_LEN - 1);
        fw_text_draw(fb, "THE PROJECT HAS AN ERROR", 12, 20, RGB(255, 200, 120), 2);
        fw_text_draw(fb, fw_project_error(), 12, 40, RGB(255, 255, 255), 2);
        return;
    }
    fw_draw(fb);
    if (in->touch_ui) {
        disc(fb, fire_x(), FIRE_Y, FIRE_R, (in->pad & PAD_SQUARE) ? RGB(150, 70, 30) : RGB(70, 40, 30), SEMI_ADD, 2);
        fw_text_draw(fb, "FIRE", fire_x() - 12, FIRE_Y - 4, RGB(255, 230, 200), 1);
        disc(fb, jump_x(), JUMP_Y, JUMP_R, (in->pad & PAD_CROSS) ? RGB(90, 90, 120) : RGB(40, 40, 60), SEMI_ADD, 2);
        fw_text_draw(fb, "JUMP", jump_x() - 12, JUMP_Y - 4, RGB(220, 220, 255), 1);
        if (in->stick_on) {
            disc(fb, in->stick_ox, in->stick_oy, 30, RGB(40, 40, 40), SEMI_ADD, 3);
            disc(fb, in->stick_x, in->stick_y, 12, RGB(90, 90, 90), SEMI_ADD, 2);
        }
    }
    if ((fw_game.won || fw_game.lost) && end_wait > 90)
        fw_text_draw(fb, in->touch_ui ? "TAP TO PLAY AGAIN" : "PRESS A BUTTON TO PLAY AGAIN", SCREEN_W / 2 - (in->touch_ui ? 51 : 84), SCREEN_H / 2 + 10, RGB(255, 255, 255), 1);
}

u32 fw_rt_touch_buttons(int x, int y)
{
    int dx = x - fire_x(), dy = y - FIRE_Y;
    if (dx * dx + dy * dy <= (FIRE_R + 8) * (FIRE_R + 8)) return PAD_SQUARE;
    dx = x - jump_x(); dy = y - JUMP_Y;
    if (dx * dx + dy * dy <= (JUMP_R + 8) * (JUMP_R + 8)) return PAD_CROSS;
    return 0;
}

void fw_rt_tap(int x, int y) { (void)x; (void)y; if ((fw_game.won || fw_game.lost) && end_wait > 90) eng_tap(PAD_START); }
void fw_rt_look(int dx) { fw_look(dx * 10); }
int  fw_rt_ram(void) { return fw_ram_used() + (int)sizeof(proj); }

const KeyBind fw_runtime_keys[] = {
    { 'W', KB_LY_POS, 0, 0 }, { 'S', KB_LY_NEG, 0, 0 }, { 'A', KB_LX_NEG, 0, 0 }, { 'D', KB_LX_POS, 0, 0 },
    { KEY_UP, 0, 0, PAD_UP }, { KEY_DOWN, 0, 0, PAD_DOWN }, { KEY_LEFT, 0, 0, PAD_L1 }, { KEY_RIGHT, 0, 0, PAD_R1 },
    { 'Q', 0, 0, PAD_L1 }, { 'E', 0, 0, PAD_R1 },
    { ' ', 0, 0, PAD_CROSS }, { KEY_CTRL, 0, 0, PAD_SQUARE }, { 'F', 0, 0, PAD_SQUARE }, { KEY_MOUSE_L, 0, 0, PAD_SQUARE },
    { KEY_SHIFT, 0, 0, PAD_R2 }, { KEY_ENTER, 0, 0, PAD_START }, { KEY_ESC, 0, 0, PAD_START },
    { 0, 0, 0, 0 }
};
