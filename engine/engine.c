/*
 * The engine runtime: frame buffers, the fixed tick, and every kind of input turned into one
 * Input the game reads (see engine.h / app.h).
 */
#include "engine.h"
#include "files.h"

#define MAX_FINGERS 10

static FrameBuf frames[2];
static int cur, disp_x, disp_y;
static Input in;
static u32 prev_pad, tap_bits, touch_bits_held;
static int an_lx, an_ly, an_rx, an_ry, rumble;
static u8 keys[KEY_COUNT];

/* touch controls */
static struct { int id, x, y, on; } fing[MAX_FINGERS];
static int stick_id = -1, look_id = -1, look_x;
static int forced_stick;                         /* eng_set_stick (tests) */

static const App *A(void) { return &game_app; }
const App *eng_app(void) { return A(); }
const Input *eng_in(void) { return &in; }
void eng_rumble(int s) { rumble = s < 0 ? 0 : s > 255 ? 255 : s; }
int  eng_rumble_level(void) { return rumble; }
void eng_tap(u32 bits) { tap_bits |= bits; }
void eng_set_touch_ui(int on) { in.touch_ui = on; }
void eng_set_pad_style(int s) { in.pad_style = s; }
void eng_show_fps(int fps) { in.fps = fps; }
void eng_set_analog(int lx, int ly, int rx, int ry) { an_lx = lx; an_ly = ly; an_rx = rx; an_ry = ry; }
void eng_display_area(int *x, int *y) { *x = disp_x; *y = disp_y; }
int  eng_pointer_mode(void) { return A()->pointer_mode && A()->pointer_mode(); }

void eng_init(void)
{
    gpu_init();
    in.touch_ui = 0;
    if (A()->init) A()->init();
    cur = 0;
}

/* ------------------------------------------------------------------ keyboard */
void eng_key(int key, int down) { if (key > 0 && key < KEY_COUNT) keys[key] = (u8)(down != 0); }
void eng_keys_clear(void) { memset(keys, 0, sizeof(keys)); }
int  eng_key_held(int key) { return key > 0 && key < KEY_COUNT && keys[key]; }
static u32 key_pad(int *lx, int *ly)
{
    const KeyBind *k = eng_pointer_mode() ? A()->keys_pointer : A()->keys;
    u32 bits = 0;
    if (!k) return 0;
    for (; k->key; k++) {
        if (!keys[k->key] || (k->no_ctrl && keys[KEY_CTRL])) continue;
        bits |= k->bits;
        if (k->axis == KB_LX_POS) *lx += 4096;
        if (k->axis == KB_LX_NEG) *lx -= 4096;
        if (k->axis == KB_LY_POS) *ly += 4096;
        if (k->axis == KB_LY_NEG) *ly -= 4096;
    }
    return bits;
}

/* ------------------------------------------------------------------ touch controls */
static u32 buttons_at(int x, int y) { return A()->touch_buttons ? A()->touch_buttons(x, y) : 0; }

void eng_touch_cancel(void)
{
    memset(fing, 0, sizeof(fing));
    stick_id = look_id = -1;
}

void eng_touch(int id, int ev, int x, int y)
{
    int i, slot = -1;
    if (eng_pointer_mode()) {
        eng_touch_cancel();
        if (A()->pointer) A()->pointer(id, ev, x, y, PK_TOUCH);
        return;
    }
    if (ev == EV_DOWN) {
        int split = SCREEN_W * A()->stick_split / 100;
        if (A()->touch_tap) A()->touch_tap(x, y);
        if (stick_id < 0 && A()->stick_split && x < split && y > 40 && !buttons_at(x, y)) {
            stick_id = id; in.stick_ox = in.stick_x = x; in.stick_oy = in.stick_y = y;
            return;
        }
        if (look_id < 0 && A()->touch_look && x >= split && y > 40 && !buttons_at(x, y)) {
            look_id = id; look_x = x;
            return;
        }
    }
    if (id == stick_id) {
        if (ev == EV_UP) stick_id = -1; else { in.stick_x = x; in.stick_y = y; }
        return;
    }
    if (id == look_id) {
        if (ev == EV_UP) look_id = -1;
        else { if (A()->touch_look) A()->touch_look(x - look_x); look_x = x; }
        return;
    }
    for (i = 0; i < MAX_FINGERS; i++) {
        if (fing[i].on && fing[i].id == id) break;
        if (!fing[i].on && slot < 0) slot = i;
    }
    if (i == MAX_FINGERS) { if (ev == EV_UP || slot < 0) return; i = slot; }
    fing[i].id = id; fing[i].x = x; fing[i].y = y; fing[i].on = ev != EV_UP;
}

void eng_set_stick(int active, int ox, int oy, int x, int y)
{
    forced_stick = active;
    in.stick_ox = ox; in.stick_oy = oy; in.stick_x = x; in.stick_y = y;
}

/* ------------------------------------------------------------------ pointer passthrough */
void eng_pointer(int id, int ev, int x, int y, int kind) { if (eng_pointer_mode() && A()->pointer) A()->pointer(id, ev, x, y, kind); }
void eng_wheel(int steps) { if (eng_pointer_mode() && A()->wheel) A()->wheel(steps); }
void eng_char(int ch) { if (eng_pointer_mode() && A()->text) A()->text(ch); }

/* ------------------------------------------------------------------ the tick */
static int clamp4k(int v) { return v > 4096 ? 4096 : v < -4096 ? -4096 : v; }

void eng_step(u32 pad)
{
    int i, lx = an_lx, ly = an_ly;
    pad |= key_pad(&lx, &ly);
    touch_bits_held = 0;
    if (!eng_pointer_mode())
        for (i = 0; i < MAX_FINGERS; i++) if (fing[i].on) touch_bits_held |= buttons_at(fing[i].x, fing[i].y);
    pad |= touch_bits_held;
    in.lx = clamp4k(lx); in.ly = clamp4k(ly); in.rx = an_rx; in.ry = an_ry;
    if (A()->stick_as_dpad && A()->stick_as_dpad() && !eng_pointer_mode()) {   /* menus */
        if (in.ly > 2400) pad |= PAD_UP;
        if (in.ly < -2400) pad |= PAD_DOWN;
        if (in.lx > 2400) pad |= PAD_RIGHT;
        if (in.lx < -2400) pad |= PAD_LEFT;
    }
    in.stick_on = forced_stick || stick_id >= 0;
    in.pad = pad;
    in.press = (pad & ~prev_pad) | tap_bits;
    tap_bits = 0;
    prev_pad = pad;
    rumble = rumble * 3 / 4;
    A()->tick();
    in.ticks++;
}

void eng_frame(u32 pad)
{
    FrameBuf *fb = &frames[cur];
    int by = cur * SCREEN_H;
    eng_step(pad);
    ot_clear(fb);
    gpu_set_draw_env(FB_X, by, SCREEN_W, SCREEN_H, FB_X, by, 1);
    A()->draw(fb);
    gpu_draw_ot(fb);
    disp_x = FB_X; disp_y = by;
    cur ^= 1;
}

const char *eng_files_dir(void) { return A()->files_dir ? A()->files_dir : "files"; }
const char *eng_files_ext(void) { return A()->files_ext ? A()->files_ext : ".dat"; }

int eng_ram_used(void) { return (int)sizeof(frames) + (A()->ram_used ? A()->ram_used() : 0); }

PSX_STATIC_ASSERT(frames_fit, sizeof(frames) < RAM_BUDGET);
