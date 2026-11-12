/*
 * Windows platform layer (Win32 + GDI + XInput + waveOut, no other dependencies), for any game
 * on the engine.
 *
 * The engine renders a 240-line PS1 frame (as wide as the window's shape) into its emulated
 * VRAM; this file converts it to 32-bit pixels and scales it to fill the window (nearest
 * filtering), runs the game at a fixed 60 Hz and feeds the engine input:
 *   - Xbox controllers (XInput, any of the 4 slots, hot-plug): the PS1 pad layout
 *     (A cross, B circle, X square, Y triangle, LB/RB L1/R1, LT/RT L2/R2, START, BACK select),
 *     both sticks, rumble
 *   - the keyboard through the game's KeyBind tables (app.h), typed text in pointer mode
 *   - the mouse as a pointer (pointer mode) and as KEY_MOUSE_L
 *   - F11 or ALT+ENTER fullscreen
 * Apps with a full-resolution UI (App.hires: the Studio) are drawn with the software rasteriser
 * (engine/studio/hr_soft.c) on a pixel-art canvas scaled up to the window by a whole number
 * (CTRL + / CTRL - / CTRL 0 change it); the mouse, wheel, typed text and keys go to the app.
 * User files live in a folder named by the game (App.files_dir) next to the exe.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <math.h>
#include <string.h>
#include "snd.h"
/* the engine names its VRAM rectangle RECT like libgpu does; keep it apart from Win32's RECT */
#define RECT PSX_RECT
#include "engine.h"
#include "files.h"
#ifdef WIN_HIRES
#include "hr_soft.h"
#endif
#undef RECT

/* ------------------------------------------------------------------ XInput (xinput9_1_0.dll, ships with Windows) */
typedef struct { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } XPad;
typedef struct { DWORD packet; XPad pad; } XState;
typedef struct { WORD left, right; } XVibe;
typedef DWORD (WINAPI *XGetStateFn)(DWORD, XState *);
typedef DWORD (WINAPI *XSetStateFn)(DWORD, XVibe *);
static XGetStateFn x_get;
static XSetStateFn x_set;

#define XB_DUP    0x0001
#define XB_DDOWN  0x0002
#define XB_DLEFT  0x0004
#define XB_DRIGHT 0x0008
#define XB_START  0x0010
#define XB_BACK   0x0020
#define XB_LTHUMB 0x0040
#define XB_RTHUMB 0x0080
#define XB_LB     0x0100
#define XB_RB     0x0200
#define XB_A      0x1000
#define XB_B      0x2000
#define XB_X      0x4000
#define XB_Y      0x8000

static void xinput_load(void)
{
    /* linked normally (import table) rather than LoadLibrary + GetProcAddress, which antivirus
       heuristics treat as suspicious; XState/XVibe match XINPUT_STATE/XINPUT_VIBRATION */
    x_get = (XGetStateFn)(void (*)(void))XInputGetState;
    x_set = (XSetStateFn)(void (*)(void))XInputSetState;
}

static int pad_slot = -1, pad_rescan;

/* radial dead zone, rescaled so the stick still reaches full range: -4096..4096 */
static void stick(SHORT sx, SHORT sy, int dead, int *ox, int *oy)
{
    double x = sx / 32767.0, y = sy / 32767.0, m = x * x + y * y, k;
    *ox = *oy = 0;
    if (m <= (double)dead * dead / (32767.0 * 32767.0)) return;
    m = sqrt(m);
    k = (m - dead / 32767.0) / (1.0 - dead / 32767.0);
    if (k > 1.0) k = 1.0;
    *ox = (int)(x / m * k * 4096.0);
    *oy = (int)(y / m * k * 4096.0);
}

/* ------------------------------------------------------------------ window */
static HWND wnd;
static BITMAPINFO bmi;
static unsigned int frame32[SCREEN_W_MAX * SCREEN_H];
static int fullscreen;
static WINDOWPLACEMENT prev_place;
static int running = 1;
static int view_x, view_y, view_w = 320, view_h = SCREEN_H;   /* where the picture is in the window */

/* a window point -> the 320x240 screen */
static void to_screen(LPARAM lp, int *x, int *y)
{
    int wx = (int)(short)LOWORD(lp), wy = (int)(short)HIWORD(lp);
    *x = view_w > 0 ? (wx - view_x) * SCREEN_W / view_w : 0;
    *y = view_h > 0 ? (wy - view_y) * SCREEN_H / view_h : 0;
}

/* ------------------------------------------------------------------ full-resolution UI (App.hires) */
#ifdef WIN_HIRES
static u32 *hr_px; static int hr_cap, hr_w, hr_h, hr_shown;
static BITMAPINFO hbmi;
static int ui_scale;                         /* window pixels per canvas pixel; 0: by the window's size */
static int hires_on(void) { const App *a = eng_app(); return a->hires && a->hires(); }
static int scale_for(int h)
{
    int s = ui_scale ? ui_scale : (h + 310) / 620;      /* about 600 canvas lines: 1080p 2x, 1440p 2x, 2160p 3x */
    if (s < 1) s = 1;
    if (s > 6) s = 6;
    return s;
}
static void scale_load(void) { char b[8]; int n = plat_file_load("studio-ui.cfg", b, 7); if (n > 0) { b[n] = 0; ui_scale = b[0] >= '0' && b[0] <= '6' ? b[0] - '0' : 0; } }
static void scale_set(int s)
{
    char b[2];
    if (s > 6) s = 6;
    ui_scale = s < 0 ? 0 : s;
    b[0] = (char)('0' + ui_scale); b[1] = 0;
    plat_file_save("studio-ui.cfg", b, 1);
    InvalidateRect(wnd, NULL, FALSE);
}
/* the app's list onto its canvas, then the canvas scaled up over the window */
static void present_hires(HDC dc, int redraw)
{
    RECT rc;
    int w, h, i, dx, dy;
    GetClientRect(wnd, &rc);
    w = rc.right; h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    if (redraw || !hr_shown) {
        const App *a = eng_app();
        const HrList *l = a->hires_frame(w, h, scale_for(h) * 100);
        HrFrameSrc fr;
        int cw = l->vw > 0 ? l->vw : w, ch = l->vh > 0 ? l->vh : h;
        if (cw * ch > hr_cap) {
            HeapFree(GetProcessHeap(), 0, hr_px);
            hr_cap = cw * ch;
            hr_px = (u32 *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)hr_cap * 4);
            if (!hr_px) { hr_cap = 0; return; }
        }
        eng_display_area(&dx, &dy);
        fr.vram = gpu_vram(); fr.stride = VRAM_W; fr.x = dx; fr.y = dy; fr.w = SCREEN_W; fr.h = SCREEN_H;
        hr_soft_draw(l, hr_px, cw, ch, &fr);
        for (i = 0; i < cw * ch; i++) {                 /* bytes r,g,b,a -> GDI's b,g,r,x */
            u32 c = hr_px[i];
            hr_px[i] = (c & 0x0000ff00u) | (c & 0xffu) << 16 | (c >> 16 & 0xffu);
        }
        hr_w = cw; hr_h = ch; hr_shown = 1;
        if (GetForegroundWindow() == wnd) {                    /* the pointer's shape follows what's under it */
            POINT pt;
            if (GetCursorPos(&pt) && ScreenToClient(wnd, &pt) && pt.x >= 0 && pt.y >= 0 && pt.x < w && pt.y < h)
                SetCursor(LoadCursor(NULL, a->hires_cursor && a->hires_cursor() ? IDC_HAND : IDC_ARROW));
        }
    }
    memset(&hbmi, 0, sizeof(hbmi));
    hbmi.bmiHeader.biSize = sizeof(hbmi.bmiHeader);
    hbmi.bmiHeader.biWidth = hr_w; hbmi.bmiHeader.biHeight = -hr_h;
    hbmi.bmiHeader.biPlanes = 1; hbmi.bmiHeader.biBitCount = 32; hbmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);                       /* nearest: crisp pixel art */
    StretchDIBits(dc, 0, 0, w, h, 0, 0, hr_w, hr_h, hr_px, &hbmi, DIB_RGB_COLORS, SRCCOPY);
}
#else
static int hires_on(void) { return 0; }
#endif

static void toggle_fullscreen(void)
{
    DWORD style = (DWORD)GetWindowLongA(wnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
        prev_place.length = sizeof(prev_place);
        if (GetWindowPlacement(wnd, &prev_place) && GetMonitorInfoA(MonitorFromWindow(wnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongA(wnd, GWL_STYLE, (LONG)(style & ~WS_OVERLAPPEDWINDOW));
            SetWindowPos(wnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        fullscreen = 1;
        if (!hires_on()) ShowCursor(FALSE);                    /* an editor keeps its mouse pointer */
    } else {
        SetWindowLongA(wnd, GWL_STYLE, (LONG)(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(wnd, &prev_place);
        SetWindowPos(wnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        if (fullscreen && !hires_on()) ShowCursor(TRUE);
        fullscreen = 0;
    }
}

/* the frame is 240 PS1 lines, as wide as the window's shape (320..512): it fills the window */
static void fit_width(void)
{
    RECT rc;
    int w;
    GetClientRect(wnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    w = (int)((long long)SCREEN_H * rc.right / rc.bottom) & ~1;
    if (w < 320) w = 320;
    if (w > SCREEN_W_MAX) w = SCREEN_W_MAX;
    screen_w = w;
}

static void present(HDC dc)
{
    RECT rc;
    int w, h, vw, vh, vx, vy, x, y, dx, dy;
    const u16 *v = gpu_vram();
#ifdef WIN_HIRES
    if (hires_on()) { present_hires(dc, 0); return; }
#endif
    eng_display_area(&dx, &dy);
    for (y = 0; y < SCREEN_H; y++) {
        const u16 *src = v + (dy + y) * VRAM_W + dx;
        unsigned int *dst = frame32 + y * SCREEN_W;
        for (x = 0; x < SCREEN_W; x++) {
            unsigned c = src[x], r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            dst[x] = ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
        }
    }
    GetClientRect(wnd, &rc);
    w = rc.right; h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    /* cover the window (a shape wider than 512:240 loses a pixel or two top and bottom) */
    if ((long long)w * SCREEN_H > (long long)h * SCREEN_W) { vw = w; vh = (int)((long long)w * SCREEN_H / SCREEN_W); }
    else { vh = h; vw = (int)((long long)h * SCREEN_W / SCREEN_H); }
    vx = (w - vw) / 2; vy = (h - vh) / 2;
    bmi.bmiHeader.biWidth = SCREEN_W;
    view_x = vx; view_y = vy; view_w = vw; view_h = vh;
    /* black bars */
    if (vx > 0) { PatBlt(dc, 0, 0, vx, h, BLACKNESS); PatBlt(dc, vx + vw, 0, w - vx - vw, h, BLACKNESS); }
    if (vy > 0) { PatBlt(dc, 0, 0, w, vy, BLACKNESS); PatBlt(dc, 0, vy + vh, w, h - vy - vh, BLACKNESS); }
    SetStretchBltMode(dc, COLORONCOLOR);                       /* nearest: crisp PS1 pixels */
    StretchDIBits(dc, vx, vy, vw, vh, 0, 0, SCREEN_W, SCREEN_H, frame32, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

/* a Windows virtual key -> the engine's key codes (app.h) */
static int key_of(WPARAM vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || vk == VK_SPACE) return (int)vk;
    switch (vk) {
    case VK_RETURN: return KEY_ENTER;
    case VK_ESCAPE: return KEY_ESC;
    case VK_BACK: return KEY_BACKSPACE;
    case VK_TAB: return KEY_TAB;
    case VK_UP: return KEY_UP;
    case VK_DOWN: return KEY_DOWN;
    case VK_LEFT: return KEY_LEFT;
    case VK_RIGHT: return KEY_RIGHT;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return KEY_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return KEY_CTRL;
    case VK_PRIOR: return KEY_PGUP;
    case VK_NEXT: return KEY_PGDN;
    case VK_OEM_MINUS: case VK_SUBTRACT: return '-';
    case VK_OEM_PLUS: case VK_ADD: return '+';
    case VK_F1: return KEY_F1;
    case VK_F2: return KEY_F2;
    case VK_F5: return KEY_F5;
    case VK_F10: return KEY_F10;
    case VK_DELETE: return KEY_DEL;
    case VK_HOME: return KEY_HOME;
    case VK_END: return KEY_END;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return KEY_ALT;
    }
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: running = 0; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (wp == VK_F11 || (wp == VK_RETURN && (lp & (1 << 29)))) { if (!(lp & (1 << 30))) toggle_fullscreen(); return 0; }
#ifdef WIN_HIRES
        if (hires_on() && (GetKeyState(VK_CONTROL) & 0x8000)) {   /* CTRL + / - / 0: the UI's size */
            RECT rc; int cur;
            GetClientRect(h, &rc);
            cur = scale_for(rc.bottom);
            if (wp == VK_OEM_PLUS || wp == VK_ADD) { scale_set(cur + 1); return 0; }
            if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) { scale_set(cur > 1 ? cur - 1 : 1); return 0; }
            if (wp == '0' || wp == VK_NUMPAD0) { scale_set(0); return 0; }
        }
#endif
        eng_key(key_of(wp), 1);
        if (msg == WM_SYSKEYDOWN && wp != VK_F4) return 0;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        eng_key(key_of(wp), 0);
        break;
    case WM_KILLFOCUS:
        eng_keys_clear();
        break;
    /* the mouse: a pointer (pointer mode) and a button */
    case WM_MOUSEMOVE: {
        int x, y;
#ifdef WIN_HIRES
        if (hires_on()) {
            const App *a = eng_app();
            x = (int)(short)LOWORD(lp); y = (int)(short)HIWORD(lp);
            if (a->hires_pointer) a->hires_pointer(0, (wp & (MK_LBUTTON | MK_RBUTTON)) ? EV_MOVE : EV_HOVER, x, y, (wp & MK_RBUTTON) ? PK_MOUSE_R : PK_MOUSE_L);
            return 0;
        }
#endif
        to_screen(lp, &x, &y);
        if (wp & MK_RBUTTON) eng_pointer(0, EV_MOVE, x, y, PK_MOUSE_R);
        else if (wp & MK_LBUTTON) eng_pointer(0, EV_MOVE, x, y, PK_MOUSE_L);
        else eng_pointer(0, EV_HOVER, x, y, PK_MOUSE_L);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
        int x, y;
        SetCapture(h);
#ifdef WIN_HIRES
        if (hires_on()) {
            const App *a = eng_app();
            if (msg == WM_LBUTTONDOWN) eng_key(KEY_MOUSE_L, 1);
            if (a->hires_pointer) a->hires_pointer(0, EV_DOWN, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), msg == WM_LBUTTONDOWN ? PK_MOUSE_L : PK_MOUSE_R);
            return 0;
        }
#endif
        to_screen(lp, &x, &y);
        if (msg == WM_LBUTTONDOWN) eng_key(KEY_MOUSE_L, 1);
        eng_pointer(0, EV_DOWN, x, y, msg == WM_LBUTTONDOWN ? PK_MOUSE_L : PK_MOUSE_R);
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: {
        int x, y;
        ReleaseCapture();
#ifdef WIN_HIRES
        if (hires_on()) {
            const App *a = eng_app();
            if (msg == WM_LBUTTONUP) eng_key(KEY_MOUSE_L, 0);
            if (a->hires_pointer) a->hires_pointer(0, EV_UP, (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), msg == WM_LBUTTONUP ? PK_MOUSE_L : PK_MOUSE_R);
            return 0;
        }
#endif
        to_screen(lp, &x, &y);
        if (msg == WM_LBUTTONUP) eng_key(KEY_MOUSE_L, 0);
        eng_pointer(0, EV_UP, x, y, msg == WM_LBUTTONUP ? PK_MOUSE_L : PK_MOUSE_R);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (hires_on()) { if (eng_app()->wheel) eng_app()->wheel((short)HIWORD(wp) > 0 ? 1 : -1); return 0; }
        eng_wheel((short)HIWORD(wp) > 0 ? 1 : -1);
        return 0;
    case WM_CHAR:
        if (hires_on()) { if (eng_app()->text) eng_app()->text((int)wp); return 0; }
        eng_char((int)wp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        present(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
#ifdef WIN_HIRES
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && hires_on()) {
            const App *a = eng_app();
            SetCursor(LoadCursor(NULL, a->hires_cursor && a->hires_cursor() ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
#endif
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ the controller -> PS1 pad bits
   (the keyboard goes through eng_key and the game's KeyBinds) */
static u32 read_input(void)
{
    u32 pad = 0;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    /* Xbox controller: use the first connected slot; look for one once a second if none */
    if (x_get) {
        XState st;
        if (pad_slot < 0 && --pad_rescan <= 0) {
            DWORD i;
            pad_rescan = 60;
            for (i = 0; i < 4; i++) if (x_get(i, &st) == 0) { pad_slot = (int)i; break; }
        }
        if (pad_slot >= 0) {
            if (x_get((DWORD)pad_slot, &st) != 0) { pad_slot = -1; pad_rescan = 0; }
            else {
                WORD b = st.pad.buttons;
                if (b & XB_DUP) pad |= PAD_UP;
                if (b & XB_DDOWN) pad |= PAD_DOWN;
                if (b & XB_DLEFT) pad |= PAD_LEFT;
                if (b & XB_DRIGHT) pad |= PAD_RIGHT;
                if (b & XB_A) pad |= PAD_CROSS;
                if (b & XB_B) pad |= PAD_CIRCLE;
                if (b & XB_X) pad |= PAD_SQUARE;
                if (b & XB_Y) pad |= PAD_TRIANGLE;
                if (b & XB_LB) pad |= PAD_L1;
                if (b & XB_RB) pad |= PAD_R1;
                if (b & XB_START) pad |= PAD_START;
                if (b & XB_BACK) pad |= PAD_SELECT;
                if (st.pad.rt > 60) pad |= PAD_R2;
                if (st.pad.lt > 60) pad |= PAD_L2;
                stick(st.pad.lx, st.pad.ly, 7849, &lx, &ly);
                stick(st.pad.rx, st.pad.ry, 8689, &rx, &ry);
                if (x_set) {
                    XVibe vb;
                    int r = eng_rumble_level();
                    if (r > 255) r = 255;
                    vb.left = (WORD)(r * 200); vb.right = (WORD)(r * 120);
                    x_set((DWORD)pad_slot, &vb);
                }
            }
        }
    }
    eng_set_analog(lx, ly, rx, ry);
    return pad;
}

/* ------------------------------------------------------------------ user files: the game's folder
   (App.files_dir) next to the exe */
static char lvl_dir[MAX_PATH], lvl_where[MAX_PATH];
static void lvl_init(void)
{
    int n = (int)GetModuleFileNameA(NULL, lvl_dir, MAX_PATH - 16), i;
    for (i = n - 1; i >= 0 && lvl_dir[i] != '\\' && lvl_dir[i] != '/'; i--) {}
    lvl_dir[i + 1] = 0;
    lstrcatA(lvl_dir, eng_files_dir());
    wsprintfA(lvl_where, "%s NEXT TO THE EXE", eng_files_dir());
    CharUpperA(lvl_where);
}
static void lvl_path(char *o, const char *name)
{
    lstrcpynA(o, lvl_dir, MAX_PATH - 40);
    lstrcatA(o, "\\");
    lstrcatA(o, name);
}
const char *plat_file_where(void) { return lvl_where; }
int plat_file_save(const char *name, const char *data, int len)
{
    char p[MAX_PATH]; HANDLE f; DWORD w = 0;
    CreateDirectoryA(lvl_dir, NULL);
    lvl_path(p, name);
    f = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, data, (DWORD)len, &w, NULL);
    CloseHandle(f);
    return (int)w == len;
}
int plat_file_load(const char *name, char *buf, int cap)
{
    char p[MAX_PATH]; HANDLE f; DWORD r = 0;
    lvl_path(p, name);
    f = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(f, buf, (DWORD)cap, &r, NULL)) r = 0;
    CloseHandle(f);
    return (int)r;
}
int plat_file_list(char (*names)[32], int max)
{
    char p[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h; int n = 0, i, j;
    char pat[40];
    lstrcpyA(pat, "*");
    lstrcatA(pat, eng_files_ext());
    lvl_path(p, pat);
    h = FindFirstFileA(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (lstrlenA(fd.cFileName) < 31 && n < max) { lstrcpynA(names[n], fd.cFileName, 32); n++; }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && lstrcmpiA(names[j - 1], names[j]) > 0; j--) { char t[32]; lstrcpynA(t, names[j], 32); lstrcpynA(names[j], names[j - 1], 32); lstrcpynA(names[j - 1], t, 32); }
    return n;
}
int plat_file_delete(const char *name) { char p[MAX_PATH]; lvl_path(p, name); return DeleteFileA(p) != 0; }

/* ------------------------------------------------------------------ sound (waveOut, no extra DLLs) */
#define AUD_BUFS   8
#define AUD_FRAMES 512                       /* 23 ms each at 22 kHz */
static HWAVEOUT wout;
static WAVEHDR whdr[AUD_BUFS];
static s16 wbuf[AUD_BUFS][AUD_FRAMES * 2];

static void audio_open(void)
{
    WAVEFORMATEX wf;
    int i;
    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = SND_RATE; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4; wf.nAvgBytesPerSec = SND_RATE * 4;
    if (waveOutOpen(&wout, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { wout = 0; return; }
    for (i = 0; i < AUD_BUFS; i++) {
        memset(&whdr[i], 0, sizeof(whdr[i]));
        whdr[i].lpData = (LPSTR)wbuf[i]; whdr[i].dwBufferLength = sizeof(wbuf[i]);
        waveOutPrepareHeader(wout, &whdr[i], sizeof(WAVEHDR));
        whdr[i].dwFlags |= WHDR_DONE;
    }
}

/* refill every buffer the device has finished with (called each loop: keeps ~50-180 ms queued) */
static void audio_pump(void)
{
    int i;
    if (!wout) return;
    for (i = 0; i < AUD_BUFS; i++)
        if (whdr[i].dwFlags & WHDR_DONE) {
            snd_mix(wbuf[i], AUD_FRAMES);
            whdr[i].dwFlags &= ~(DWORD)WHDR_DONE;
            waveOutWrite(wout, &whdr[i], sizeof(WAVEHDR));
        }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    RECT r = { 0, 0, 426 * 3, SCREEN_H * 3 };                   /* 16:9 */
#ifdef WIN_HIRES
    RECT wa;
#endif
    LARGE_INTEGER freq, last, now;
    double acc = 0.0, step;
    (void)prev; (void)cmd;

    timeBeginPeriod(1);
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));          /* the game's icon (its .rc, resource 1) */
    wc.lpszClassName = "PS1Engine";
    RegisterClassA(&wc);
#ifdef WIN_HIRES
    /* an editor: most of the screen's work area (it can be resized, F11 fills the screen) */
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0)) {
        int ww = (wa.right - wa.left) * 85 / 100, wh = (wa.bottom - wa.top) * 85 / 100;
        if (ww < 1024) ww = 1024;
        if (wh < 600) wh = 600;
        r.right = ww; r.bottom = wh;
    }
#endif
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wnd = CreateWindowA("PS1Engine", eng_app()->name, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(wnd, show);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = SCREEN_W;
    bmi.bmiHeader.biHeight = -SCREEN_H;                        /* top-down rows */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    xinput_load();
    lvl_init();
#ifdef WIN_HIRES
    scale_load();
#endif
    eng_init();
    eng_set_touch_ui(0);
    eng_set_pad_style(1);
    audio_open();

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    step = 1.0 / 60.0;
    while (running) {
        MSG msg;
        int ticks = 0;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        QueryPerformanceCounter(&now);
        acc += (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        last = now;
        if (acc > 0.25) acc = 0.25;                            /* after a stall, don't fast-forward */
        fit_width();
        while (acc >= step && ticks < 4) {                     /* fixed 60 Hz game logic */
            eng_frame(read_input());
            acc -= step;
            ticks++;
        }
        audio_pump();
        if (ticks) {
            HDC dc = GetDC(wnd);
#ifdef WIN_HIRES
            if (hires_on()) present_hires(dc, 1);
            else
#endif
            present(dc);
            ReleaseDC(wnd, dc);
        } else {
            Sleep(1);
        }
    }
    if (x_set && pad_slot >= 0) { XVibe off = { 0, 0 }; x_set((DWORD)pad_slot, &off); }
    if (wout) { waveOutReset(wout); waveOutClose(wout); }
    timeEndPeriod(1);
    return 0;
}
