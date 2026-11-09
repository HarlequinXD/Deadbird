/*
 * Deadbird - Windows platform layer (Win32 + GDI + XInput, no other dependencies).
 *
 * The engine renders a 320x240 PS1 frame into its emulated VRAM exactly as on Android; this
 * file converts that frame to 32-bit pixels and scales it to the window (nearest filtering,
 * 4:3 letterbox), runs the game at a fixed 60 Hz, and feeds it input:
 *
 *   Xbox controller (XInput, any of the 4 slots, hot-plug):
 *     left stick  move (push far to run)      right stick  turn the camera
 *     A           jump / confirm              B, X, RT     sprint
 *     LB / RB     swing the camera            D-pad        move / menus
 *     START       pause                       BACK         quit to title (from pause)
 *     Y           confirm (menus)             rumble on hard landings
 *   Keyboard:
 *     WASD / arrows move, SHIFT sprint, SPACE jump, Q / E camera, ENTER / ESC pause,
 *     F11 or ALT+ENTER fullscreen
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <math.h>
#include <string.h>
#include "deadbird.h"
#include "snd.h"
/* the engine names its VRAM rectangle RECT like libgpu does; keep it apart from Win32's RECT */
#define RECT PSX_RECT
#include "gpu.h"
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
static unsigned int frame32[SCREEN_W * SCREEN_H];
static int fullscreen;
static WINDOWPLACEMENT prev_place;
static BYTE keys[256];
static int running = 1;

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
        ShowCursor(FALSE);
    } else {
        SetWindowLongA(wnd, GWL_STYLE, (LONG)(style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(wnd, &prev_place);
        SetWindowPos(wnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        fullscreen = 0;
        ShowCursor(TRUE);
    }
}

static void present(HDC dc)
{
    RECT rc;
    int w, h, vw, vh, vx, vy, x, y, dx, dy;
    const u16 *v = gpu_vram();
    game_display_area(&dx, &dy);
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
    if (w * 3 > h * 4) { vh = h; vw = h * 4 / 3; } else { vw = w; vh = w * 3 / 4; }
    vx = (w - vw) / 2; vy = (h - vh) / 2;
    /* black bars */
    if (vx > 0) { PatBlt(dc, 0, 0, vx, h, BLACKNESS); PatBlt(dc, vx + vw, 0, w - vx - vw, h, BLACKNESS); }
    if (vy > 0) { PatBlt(dc, 0, 0, w, vy, BLACKNESS); PatBlt(dc, 0, vy + vh, w, h - vy - vh, BLACKNESS); }
    SetStretchBltMode(dc, COLORONCOLOR);                       /* nearest: crisp PS1 pixels */
    StretchDIBits(dc, vx, vy, vw, vh, 0, 0, SCREEN_W, SCREEN_H, frame32, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE: running = 0; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (wp == VK_F11 || (wp == VK_RETURN && (lp & (1 << 29)))) { if (!(lp & (1 << 30))) toggle_fullscreen(); return 0; }
        keys[wp & 255] = 1;
        if (msg == WM_SYSKEYDOWN && wp != VK_F4) return 0;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        keys[wp & 255] = 0;
        break;
    case WM_KILLFOCUS:
        memset(keys, 0, sizeof(keys));
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        present(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ------------------------------------------------------------------ input -> PS1 pad bits */
static u32 read_input(void)
{
    u32 pad = 0;
    int lx = 0, ly = 0, rx = 0, ry = 0;
    /* keyboard */
    if (keys['W'] || keys[VK_UP]) pad |= PAD_UP;
    if (keys['S'] || keys[VK_DOWN]) pad |= PAD_DOWN;
    if (keys['A'] || keys[VK_LEFT]) pad |= PAD_LEFT;
    if (keys['D'] || keys[VK_RIGHT]) pad |= PAD_RIGHT;
    if (keys[VK_SPACE]) pad |= PAD_CROSS;
    if (keys[VK_SHIFT] || keys[VK_LSHIFT] || keys[VK_RSHIFT]) pad |= PAD_CIRCLE;
    if (keys['Q']) pad |= PAD_L1;
    if (keys['E']) pad |= PAD_R1;
    if (keys[VK_RETURN] || keys[VK_ESCAPE]) pad |= PAD_START;
    if (keys[VK_BACK]) pad |= PAD_SELECT;

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
                if (st.pad.rt > 60) pad |= PAD_CIRCLE;           /* right trigger sprints too */
                if (st.pad.lt > 60) pad |= PAD_L2;
                stick(st.pad.lx, st.pad.ly, 7849, &lx, &ly);
                stick(st.pad.rx, st.pad.ry, 8689, &rx, &ry);
                /* menus: the left stick also works as a d-pad */
                if (game_phase() != GS_PLAY) {
                    if (ly > 2400) pad |= PAD_UP;
                    if (ly < -2400) pad |= PAD_DOWN;
                    if (lx > 2400) pad |= PAD_RIGHT;
                    if (lx < -2400) pad |= PAD_LEFT;
                }
                if (x_set) {
                    XVibe vb;
                    int r = game_rumble();
                    if (r > 255) r = 255;
                    vb.left = (WORD)(r * 200); vb.right = (WORD)(r * 120);
                    x_set((DWORD)pad_slot, &vb);
                }
            }
        }
    }
    game_set_analog(lx, ly, rx, ry);
    return pad;
}

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
    RECT r = { 0, 0, SCREEN_W * 3, SCREEN_H * 3 };
    LARGE_INTEGER freq, last, now;
    double acc = 0.0, step;
    (void)prev; (void)cmd;

    timeBeginPeriod(1);
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));          /* the Deadbird skull (deadbird.rc) */
    wc.lpszClassName = "Deadbird";
    RegisterClassA(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wnd = CreateWindowA("Deadbird", "DEADBIRD", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
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
    game_init();
    game_set_touch_ui(0);
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
        while (acc >= step && ticks < 4) {                     /* fixed 60 Hz game logic */
            game_frame(read_input());
            acc -= step;
            ticks++;
        }
        audio_pump();
        if (ticks) {
            HDC dc = GetDC(wnd);
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
