/*
 * Linux platform layer (SDL2: X11 or Wayland, any desktop), for any game on the engine. The same
 * job as the Windows layer (engine/platform/windows/win_main.c):
 *
 * The engine renders a 240-line PS1 frame (as wide as the window's shape) into its emulated VRAM;
 * this file converts it to 32-bit pixels and scales it to fill the window (nearest filtering,
 * cropping a line or two rather than letterboxing), runs the game at a fixed 60 Hz and feeds the
 * engine input:
 *   - game controllers through SDL's GameController API (Xbox, PlayStation, Switch Pro, Steam
 *     Deck and most others, hot-plug): the PS1 pad layout (A/cross, B/circle, X/square,
 *     Y/triangle, LB/RB L1/R1, LT/RT L2/R2, START, BACK/SELECT), both sticks, rumble
 *   - the keyboard through the game's KeyBind tables (app.h), typed text in pointer mode
 *   - the mouse as a pointer (pointer mode) and as KEY_MOUSE_L
 *   - F11 or ALT+ENTER fullscreen
 * Apps with a full-resolution UI (App.hires: the Studio, built with LINUX_HIRES) are drawn with
 * the software rasteriser (engine/studio/hr_soft.c) at the window's own pixel size; CTRL + / - / 0
 * change the UI's size in 10% steps.
 * User files live in ~/.local/share/deadbird/<files_dir> ($XDG_DATA_HOME), or in a <files_dir>
 * folder next to the executable if one is there (a portable copy).
 */
#define _DEFAULT_SOURCE
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "snd.h"
#include "engine.h"
#include "files.h"
#ifdef LINUX_HIRES
#include "hr_soft.h"
#endif

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;                 /* the PS1 frame, SCREEN_W_MAX x SCREEN_H */
static int running = 1, fullscreen;
static int view_x, view_y, view_w = 1, view_h = 1;   /* where the frame is, in window coordinates */
static u32 frame32[SCREEN_W_MAX * SCREEN_H];
static const char *shot_path;            /* ENGINE_SHOT: save the next frame drawn as a BMP (smoke tests) */

static void save_shot(void)
{
    int pw, ph;
    SDL_Surface *sf;
    if (!shot_path || !*shot_path) return;
    SDL_GetRendererOutputSize(ren, &pw, &ph);
    sf = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_ARGB8888);
    if (sf && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) SDL_SaveBMP(sf, shot_path);
    if (sf) SDL_FreeSurface(sf);
    shot_path = NULL;
}

/* ------------------------------------------------------------------ controllers */
static SDL_GameController *pad;
static SDL_JoystickID pad_id = -1;

static void pad_open_first(void)
{
    int i;
    if (pad) return;
    for (i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i)) != NULL) {
            pad_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
            return;
        }
}

/* radial dead zone, rescaled so the stick still reaches full range: -4096..4096 (y up, as XInput) */
static void stick(int sx, int sy, int dead, int *ox, int *oy)
{
    double x = sx / 32767.0, y = sy / 32767.0, m = x * x + y * y, k;
    *ox = *oy = 0;
    if (m <= (double)dead * dead / (32767.0 * 32767.0)) return;
    m = SDL_sqrt(m);
    k = (m - dead / 32767.0) / (1.0 - dead / 32767.0);
    if (k > 1.0) k = 1.0;
    *ox = (int)(x / m * k * 4096.0);
    *oy = (int)(y / m * k * 4096.0);
}

static u32 read_input(void)
{
    u32 p = 0;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    if (pad) {
#define B(b, bit) if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)) p |= bit
        B(DPAD_UP, PAD_UP); B(DPAD_DOWN, PAD_DOWN); B(DPAD_LEFT, PAD_LEFT); B(DPAD_RIGHT, PAD_RIGHT);
        B(A, PAD_CROSS); B(B, PAD_CIRCLE); B(X, PAD_SQUARE); B(Y, PAD_TRIANGLE);
        B(LEFTSHOULDER, PAD_L1); B(RIGHTSHOULDER, PAD_R1); B(START, PAD_START); B(BACK, PAD_SELECT);
        B(LEFTSTICK, PAD_L3); B(RIGHTSTICK, PAD_R3);
#undef B
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 7700) p |= PAD_R2;
        if (SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 7700) p |= PAD_L2;
        /* SDL's y axes point down; the engine takes XInput's (up) */
        stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY), 7849, &lx, &ly);
        stick(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX), -SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY), 8689, &rx, &ry);
        {
            int r = eng_rumble_level();
            if (r > 255) r = 255;
            SDL_GameControllerRumble(pad, (Uint16)(r * 200), (Uint16)(r * 120), 100);
        }
    }
    eng_set_analog(lx, ly, rx, ry);
    eng_set_pad_device(p);
    return p;
}

/* ------------------------------------------------------------------ keyboard */
static int key_of(SDL_Keycode k)
{
    if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
    if ((k >= '0' && k <= '9') || k == ' ') return (int)k;
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return KEY_ENTER;
    case SDLK_ESCAPE: return KEY_ESC;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return KEY_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return KEY_CTRL;
    case SDLK_LALT: case SDLK_RALT: return KEY_ALT;
    case SDLK_PAGEUP: return KEY_PGUP;
    case SDLK_PAGEDOWN: return KEY_PGDN;
    case SDLK_MINUS: case SDLK_KP_MINUS: return '-';
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: return '+';
    case SDLK_F1: return KEY_F1;
    case SDLK_F2: return KEY_F2;
    case SDLK_F5: return KEY_F5;
    case SDLK_F10: return KEY_F10;
    case SDLK_DELETE: return KEY_DEL;
    case SDLK_HOME: return KEY_HOME;
    case SDLK_END: return KEY_END;
    }
    return 0;
}

/* ------------------------------------------------------------------ user files */
static char files_dir[2048], files_where[2100];

static void files_init(void)
{
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 64);
    struct stat st;
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    const char *env = getenv("ENGINE_FILES");
    int i;
    if (env && *env) { snprintf(files_dir, sizeof(files_dir), "%s", env); goto done; }
    if (n > 0) {                                   /* a portable copy: the folder sits next to the binary */
        exe[n] = 0;
        for (i = (int)n - 1; i >= 0 && exe[i] != '/'; i--) {}
        exe[i + 1] = 0;
        snprintf(files_dir, sizeof(files_dir), "%s%s", exe, eng_files_dir());
        if (stat(files_dir, &st) == 0 && S_ISDIR(st.st_mode)) goto done;
    }
    {   /* ~/.local/share/deadbird/<files_dir> (the game's levels, the Studio's projects) */
        char base[900];
        if (xdg && *xdg) snprintf(base, sizeof(base), "%s", xdg);
        else if (home && *home) {
            snprintf(base, sizeof(base), "%s/.local", home); mkdir(base, 0755);
            snprintf(base, sizeof(base), "%s/.local/share", home);
        } else snprintf(base, sizeof(base), ".");
        mkdir(base, 0755);
        snprintf(files_dir, sizeof(files_dir), "%s/deadbird", base); mkdir(files_dir, 0755);
        snprintf(files_dir, sizeof(files_dir), "%s/deadbird/%s", base, eng_files_dir());
    }
done:
    {   /* what the game shows for "saved to": upper case, the home folder as ~ */
        const char *d = files_dir;
        size_t hl = home ? strlen(home) : 0;
        if (hl && !strncmp(d, home, hl)) snprintf(files_where, sizeof(files_where), "~%s", d + hl);
        else snprintf(files_where, sizeof(files_where), "%s", d);
        for (i = 0; files_where[i]; i++) files_where[i] = (char)toupper((unsigned char)files_where[i]);
    }
}
static void fpath(char *o, size_t cap, const char *name) { snprintf(o, cap, "%s/%s", files_dir, name); }
const char *plat_file_where(void) { return files_where; }
int plat_file_save(const char *name, const char *data, int len)
{
    char p[2400]; FILE *f; size_t w;
    mkdir(files_dir, 0755);
    fpath(p, sizeof(p), name);
    f = fopen(p, "wb");
    if (!f) return 0;
    w = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return (int)w == len;
}
int plat_file_load(const char *name, char *buf, int cap)
{
    char p[2400]; FILE *f; size_t n;
    fpath(p, sizeof(p), name);
    f = fopen(p, "rb");
    if (!f) return -1;
    n = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)n;
}
int plat_file_list(char (*names)[32], int max)
{
    DIR *d = opendir(files_dir);
    struct dirent *e;
    int n = 0, i, j;
    size_t el = strlen(eng_files_ext());
    if (!d) return 0;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > el && l < 31 && !strcasecmp(e->d_name + l - el, eng_files_ext())) { strcpy(names[n], e->d_name); n++; }
    }
    closedir(d);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) { char t[32]; strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t); }
    return n;
}
int plat_file_delete(const char *name) { char p[2400]; fpath(p, sizeof(p), name); return remove(p) == 0; }

/* ------------------------------------------------------------------ sound: SDL pulls from the engine's mixer */
static SDL_AudioDeviceID aud;
static void audio_cb(void *u, Uint8 *out, int bytes)
{
    (void)u;
    snd_mix((s16 *)out, bytes / 4);
}
static void audio_open(void)
{
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SND_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
    want.callback = audio_cb;
    aud = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (aud) SDL_PauseAudioDevice(aud, 0);
}

/* ------------------------------------------------------------------ full-resolution UI (App.hires) */
#ifdef LINUX_HIRES
static SDL_Texture *htex; static int htw, hth;
static u32 *hr_px; static int hr_cap;
static int ui_pct = 100;
static int hires_on(void) { const App *a = eng_app(); return a->hires && a->hires(); }
/* density: the screen's scale (HiDPI: drawable pixels per window point) times the user's size */
static int density_now(void)
{
    int ww, wh, pw, ph, d;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &pw, &ph);
    d = (ww > 0 ? 100 * pw / ww : 100) * ui_pct / 100;
    return d < 50 ? 50 : d > 400 ? 400 : d;
}
static void scale_load(void)
{
    char b[8]; int n = plat_file_load("studio-ui.cfg", b, 7), v = 0, i;
    if (n <= 0) return;
    for (i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) v = v * 10 + b[i] - '0';
    if (v >= 50 && v <= 300) ui_pct = v;
}
static void scale_set(int pct)
{
    char b[8];
    ui_pct = pct < 50 ? 50 : pct > 300 ? 300 : pct;
    snprintf(b, sizeof(b), "%d", ui_pct);
    plat_file_save("studio-ui.cfg", b, (int)strlen(b));
}
/* window points -> drawable pixels (the hires UI works in the latter) */
static void to_pixels(int x, int y, int *ox, int *oy)
{
    int ww, wh, pw, ph;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &pw, &ph);
    *ox = ww > 0 ? x * pw / ww : x; *oy = wh > 0 ? y * ph / wh : y;
}
static void present_hires(void)
{
    const App *a = eng_app();
    int w, h, cw, ch, dx, dy;
    const HrList *l;
    HrFrameSrc fr;
    SDL_GetRendererOutputSize(ren, &w, &h);
    if (w <= 0 || h <= 0) return;
    l = a->hires_frame(w, h, density_now());
    cw = l->vw > 0 ? l->vw : w; ch = l->vh > 0 ? l->vh : h;
    if (cw * ch > hr_cap) {
        free(hr_px);
        hr_cap = cw * ch;
        hr_px = (u32 *)malloc((size_t)hr_cap * 4);
        if (!hr_px) { hr_cap = 0; return; }
    }
    eng_display_area(&dx, &dy);
    fr.vram = gpu_vram(); fr.stride = VRAM_W; fr.x = dx; fr.y = dy; fr.w = SCREEN_W; fr.h = SCREEN_H;
    hr_soft_draw(l, hr_px, cw, ch, &fr);
    if (!htex || htw != cw || hth != ch) {
        if (htex) SDL_DestroyTexture(htex);
        htex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);  /* bytes r,g,b,a */
        htw = cw; hth = ch;
    }
    if (!htex) return;
    SDL_UpdateTexture(htex, NULL, hr_px, cw * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, htex, NULL, NULL);
    save_shot();
    SDL_RenderPresent(ren);
    {
        static SDL_Cursor *hand, *arrow;
        if (!hand) { hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND); arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW); }
        SDL_SetCursor(a->hires_cursor && a->hires_cursor() ? hand : arrow);
    }
}
#else
static int hires_on(void) { return 0; }
#endif

/* ------------------------------------------------------------------ the picture */
/* the frame is 240 PS1 lines, as wide as the window's shape (320..512) */
static void fit_width(void)
{
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    if (w <= 0 || h <= 0) return;
    w = (int)((long long)SCREEN_H * w / h) & ~1;
    if (w < 320) w = 320;
    if (w > SCREEN_W_MAX) w = SCREEN_W_MAX;
    screen_w = w;
}

static void present(void)
{
    const u16 *v = gpu_vram();
    int x, y, dx, dy, w, h, pw, ph, ww, wh;
    SDL_Rect src, dst;
    eng_display_area(&dx, &dy);
    for (y = 0; y < SCREEN_H; y++) {
        const u16 *s = v + (dy + y) * VRAM_W + dx;
        u32 *d = frame32 + y * SCREEN_W_MAX;
        for (x = 0; x < SCREEN_W; x++) {
            unsigned c = s[x], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            d[x] = 0xff000000u | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
    }
    SDL_UpdateTexture(tex, NULL, frame32, SCREEN_W_MAX * 4);
    SDL_GetRendererOutputSize(ren, &pw, &ph);
    w = pw; h = ph;
    /* cover the window (a shape wider than 512:240 loses a line or two top and bottom) */
    if ((long long)w * SCREEN_H > (long long)h * SCREEN_W) { dst.w = w; dst.h = (int)((long long)w * SCREEN_H / SCREEN_W); }
    else { dst.h = h; dst.w = (int)((long long)h * SCREEN_W / SCREEN_H); }
    dst.x = (w - dst.w) / 2; dst.y = (h - dst.h) / 2;
    src.x = 0; src.y = 0; src.w = SCREEN_W; src.h = SCREEN_H;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, &src, &dst);
    save_shot();
    SDL_RenderPresent(ren);
    /* the mouse comes in window points: keep the view in them too (HiDPI: points != pixels) */
    SDL_GetWindowSize(win, &ww, &wh);
    view_x = pw ? dst.x * ww / pw : 0; view_y = ph ? dst.y * wh / ph : 0;
    view_w = pw ? dst.w * ww / pw : 1; view_h = ph ? dst.h * wh / ph : 1;
    if (view_w < 1) view_w = 1;
    if (view_h < 1) view_h = 1;
}

static void to_screen(int wx, int wy, int *x, int *y)
{
    *x = (wx - view_x) * SCREEN_W / view_w;
    *y = (wy - view_y) * SCREEN_H / view_h;
}

static void toggle_fullscreen(void)
{
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!hires_on()) SDL_ShowCursor(fullscreen ? SDL_DISABLE : SDL_ENABLE);   /* an editor keeps its pointer */
}

/* ------------------------------------------------------------------ events */
static void handle(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT: running = 0; break;
    case SDL_CONTROLLERDEVICEADDED: pad_open_first(); break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (pad && e->cdevice.which == pad_id) { SDL_GameControllerClose(pad); pad = NULL; pad_id = -1; pad_open_first(); }
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) eng_keys_clear();
        break;
    case SDL_KEYDOWN: {
        SDL_Keycode k = e->key.keysym.sym;
        SDL_Keymod m = (SDL_Keymod)e->key.keysym.mod;
        if (k == SDLK_F11 || (k == SDLK_RETURN && (m & KMOD_ALT))) { if (!e->key.repeat) toggle_fullscreen(); break; }
#ifdef LINUX_HIRES
        if (hires_on() && (m & KMOD_CTRL)) {
            if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) { scale_set(ui_pct + 10); break; }
            if (k == SDLK_MINUS || k == SDLK_KP_MINUS) { scale_set(ui_pct - 10); break; }
            if (k == SDLK_0 || k == SDLK_KP_0) { scale_set(100); break; }
        }
#endif
        if (!e->key.repeat || hires_on()) eng_key(key_of(k), 1);
        /* typed text arrives as SDL_TEXTINPUT; the keys that type nothing printable still edit */
        if (k == SDLK_BACKSPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_TAB || k == SDLK_ESCAPE) {
            int c = k == SDLK_BACKSPACE ? 8 : k == SDLK_TAB ? 9 : k == SDLK_ESCAPE ? 27 : 13;
            if (hires_on()) { if (eng_app()->text) eng_app()->text(c); }
            else eng_char(c);
        }
        break;
    }
    case SDL_KEYUP: eng_key(key_of(e->key.keysym.sym), 0); break;
    case SDL_TEXTINPUT: {
        const unsigned char *t = (const unsigned char *)e->text.text;
        for (; *t; t++) {
            if (*t >= 0x80) continue;                     /* the engine's fonts are ASCII */
            if (hires_on()) { if (eng_app()->text) eng_app()->text(*t); }
            else eng_char(*t);
        }
        break;
    }
    case SDL_MOUSEMOTION: {
        int x, y, kind = (e->motion.state & SDL_BUTTON_RMASK) ? PK_MOUSE_R : PK_MOUSE_L;
        int ev = (e->motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK)) ? EV_MOVE : EV_HOVER;
#ifdef LINUX_HIRES
        if (hires_on()) {
            const App *a = eng_app();
            to_pixels(e->motion.x, e->motion.y, &x, &y);
            if (a->hires_pointer) a->hires_pointer(0, ev, x, y, kind);
            break;
        }
#endif
        to_screen(e->motion.x, e->motion.y, &x, &y);
        eng_pointer(0, ev, x, y, kind);
        break;
    }
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
        int x, y, down = e->type == SDL_MOUSEBUTTONDOWN, kind;
        if (e->button.button != SDL_BUTTON_LEFT && e->button.button != SDL_BUTTON_RIGHT) break;
        kind = e->button.button == SDL_BUTTON_LEFT ? PK_MOUSE_L : PK_MOUSE_R;
        if (kind == PK_MOUSE_L) eng_key(KEY_MOUSE_L, down);
#ifdef LINUX_HIRES
        if (hires_on()) {
            const App *a = eng_app();
            to_pixels(e->button.x, e->button.y, &x, &y);
            if (a->hires_pointer) a->hires_pointer(0, down ? EV_DOWN : EV_UP, x, y, kind);
            break;
        }
#endif
        to_screen(e->button.x, e->button.y, &x, &y);
        eng_pointer(0, down ? EV_DOWN : EV_UP, x, y, kind);
        break;
    }
    case SDL_MOUSEWHEEL:
        if (!e->wheel.y) break;
        if (hires_on()) { if (eng_app()->wheel) eng_app()->wheel(e->wheel.y > 0 ? 1 : -1); }
        else eng_wheel(e->wheel.y > 0 ? 1 : -1);
        break;
    }
}

int main(int argc, char **argv)
{
    Uint64 freq, last, now;
    double acc = 0.0, step = 1.0 / 60.0;
    int ww = 426 * 3, wh = SCREEN_H * 3;                       /* 16:9 */
    (void)argc; (void)argv;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");          /* nearest: crisp PS1 pixels */
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        /* no sound device or no controller support is fine; no video is not */
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
        SDL_InitSubSystem(SDL_INIT_AUDIO);
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
#ifdef LINUX_HIRES
    {   /* an editor: most of the screen */
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(0, &r) == 0) { ww = r.w * 85 / 100; wh = r.h * 85 / 100; }
        if (ww < 1024) ww = 1024;
        if (wh < 600) wh = 600;
    }
#endif
    win = SDL_CreateWindow(eng_app()->name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL window: %s\n", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL renderer: %s\n", SDL_GetError()); return 1; }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W_MAX, SCREEN_H);
    if (!tex) { fprintf(stderr, "SDL texture: %s\n", SDL_GetError()); return 1; }
    SDL_StartTextInput();

    files_init();
#ifdef LINUX_HIRES
    scale_load();
#endif
    pad_open_first();
    eng_init();
    eng_set_touch_ui(0);
    eng_set_pad_style(1);
    audio_open();

    freq = SDL_GetPerformanceFrequency();
    last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        int ticks = 0;
        while (SDL_PollEvent(&e)) handle(&e);
        now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / (double)freq;
        last = now;
        if (acc > 0.25) acc = 0.25;                            /* after a stall, don't fast-forward */
        fit_width();
        if (aud) SDL_LockAudioDevice(aud);                     /* the mixer runs on SDL's audio thread */
        while (acc >= step && ticks < 4) {                     /* fixed 60 Hz game logic */
            eng_frame(read_input());
            acc -= step;
            ticks++;
        }
        if (aud) SDL_UnlockAudioDevice(aud);
        if (ticks) {
#ifdef LINUX_HIRES
            if (hires_on()) present_hires();
            else
#endif
            present();
        } else SDL_Delay(1);
        if (ticks && getenv("ENGINE_FRAMES")) {              /* smoke test: draw N frames and quit */
            static int n;
            if (++n >= atoi(getenv("ENGINE_FRAMES"))) running = 0;
            else if (n == atoi(getenv("ENGINE_FRAMES")) - 1) shot_path = getenv("ENGINE_SHOT");   /* the last frame: saved */
        }
    }
    if (pad) { SDL_GameControllerRumble(pad, 0, 0, 0); SDL_GameControllerClose(pad); }
    if (aud) SDL_CloseAudioDevice(aud);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
