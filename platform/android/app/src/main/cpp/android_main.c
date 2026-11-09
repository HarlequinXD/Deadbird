#define _POSIX_C_SOURCE 199309L
/*
 * Deadbird - Android platform layer (NativeActivity, no Java).
 * The engine renders a 320x240 PS1 frame into its emulated VRAM; this file only
 * converts that frame to RGB565, uploads it as a GL texture and scales it to the
 * screen with nearest filtering (crisp pixels, 4:3 letterbox). Touch -> PS1 pad bits.
 */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <android/keycodes.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "deadbird.h"
#include "gpu.h"
#include "snd.h"
#include "editor.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Deadbird", __VA_ARGS__)
#define TEX_W 512
#define TEX_H 256

static struct {
    EGLDisplay dpy; EGLSurface surf; EGLContext ctx;
    int w, h;
    int vx, vy, vw, vh;          /* 4:3 viewport */
    GLuint prog, tex, vbo;
    int ready;
} G;

static u16 pixels[SCREEN_W * SCREEN_H];
static int game_started;

/* active touches */
#define MAX_TOUCH 10
static struct { int id, x, y, down; } touches[MAX_TOUCH];
/* floating stick: the first finger that lands on the left side of the screen away from buttons */
static int stick_id = -1, stick_ox, stick_oy, stick_x, stick_y;
/* look finger: lands on the right side away from buttons and drags the camera round (Slayer style) */
static int look_id = -1, look_x;

/* ------------------------------------------------------------------ sound: OpenSL ES buffer queue */
#define AUD_FRAMES 512
static SLObjectItf sl_engine_obj, sl_mix_obj, sl_player_obj;
static SLPlayItf sl_play;
static SLAndroidSimpleBufferQueueItf sl_queue;
static s16 aud_buf[2][AUD_FRAMES * 2];
static int aud_cur;

static void aud_callback(SLAndroidSimpleBufferQueueItf q, void *ctx)
{
    (void)ctx;
    snd_mix(aud_buf[aud_cur], AUD_FRAMES);           /* the mixer applies the game's queued commands */
    (*q)->Enqueue(q, aud_buf[aud_cur], sizeof(aud_buf[0]));
    aud_cur ^= 1;
}

static void audio_open(void)
{
    SLEngineItf eng;
    SLDataLocator_AndroidSimpleBufferQueue loc_q = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
    SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, 2, SND_RATE * 1000, SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                             SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, SL_BYTEORDER_LITTLEENDIAN };
    SLDataSource src = { &loc_q, &fmt };
    SLDataLocator_OutputMix loc_out;
    SLDataSink sink;
    const SLInterfaceID ids[1] = { SL_IID_BUFFERQUEUE };
    const SLboolean req[1] = { SL_BOOLEAN_TRUE };
    if (slCreateEngine(&sl_engine_obj, 0, 0, 0, 0, 0) != SL_RESULT_SUCCESS) return;
    (*sl_engine_obj)->Realize(sl_engine_obj, SL_BOOLEAN_FALSE);
    (*sl_engine_obj)->GetInterface(sl_engine_obj, SL_IID_ENGINE, &eng);
    if ((*eng)->CreateOutputMix(eng, &sl_mix_obj, 0, 0, 0) != SL_RESULT_SUCCESS) return;
    (*sl_mix_obj)->Realize(sl_mix_obj, SL_BOOLEAN_FALSE);
    loc_out.locatorType = SL_DATALOCATOR_OUTPUTMIX; loc_out.outputMix = sl_mix_obj;
    sink.pLocator = &loc_out; sink.pFormat = 0;
    if ((*eng)->CreateAudioPlayer(eng, &sl_player_obj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS) return;
    (*sl_player_obj)->Realize(sl_player_obj, SL_BOOLEAN_FALSE);
    (*sl_player_obj)->GetInterface(sl_player_obj, SL_IID_PLAY, &sl_play);
    (*sl_player_obj)->GetInterface(sl_player_obj, SL_IID_BUFFERQUEUE, &sl_queue);
    (*sl_queue)->RegisterCallback(sl_queue, aud_callback, 0);
    memset(aud_buf, 0, sizeof(aud_buf));
    (*sl_queue)->Enqueue(sl_queue, aud_buf[0], sizeof(aud_buf[0]));
    (*sl_queue)->Enqueue(sl_queue, aud_buf[1], sizeof(aud_buf[1]));
    (*sl_play)->SetPlayState(sl_play, SL_PLAYSTATE_PLAYING);
}

static void audio_pause(int paused)
{
    if (sl_play) (*sl_play)->SetPlayState(sl_play, paused ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING);
}

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    return s;
}

static void gl_setup(void)
{
    static const char *vs =
        "attribute vec2 p; attribute vec2 t; varying vec2 v;"
        "void main(){ v = t; gl_Position = vec4(p, 0.0, 1.0); }";
    static const char *fs =
        "precision mediump float; varying vec2 v; uniform sampler2D s;"
        "void main(){ gl_FragColor = texture2D(s, v); }";
    static const float quad[] = {
        /* x, y, u, v  (v = 0 is the top row of the frame) */
        -1.f,  1.f, 0.f, 0.f,
         1.f,  1.f, (float)SCREEN_W / TEX_W, 0.f,
        -1.f, -1.f, 0.f, (float)SCREEN_H / TEX_H,
         1.f, -1.f, (float)SCREEN_W / TEX_W, (float)SCREEN_H / TEX_H,
    };
    G.prog = glCreateProgram();
    glAttachShader(G.prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(G.prog, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(G.prog, 0, "p");
    glBindAttribLocation(G.prog, 1, "t");
    glLinkProgram(G.prog);

    glGenTextures(1, &G.tex);
    glBindTexture(GL_TEXTURE_2D, G.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TEX_W, TEX_H, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0);

    glGenBuffers(1, &G.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
}

static void viewport_43(void)
{
    /* largest 4:3 rectangle that fits, centred */
    if (G.w * 3 > G.h * 4) { G.vh = G.h; G.vw = G.h * 4 / 3; }
    else { G.vw = G.w; G.vh = G.w * 3 / 4; }
    G.vx = (G.w - G.vw) / 2;
    G.vy = (G.h - G.vh) / 2;
}

static int display_init(ANativeWindow *win)
{
    static const EGLint cfg_attr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_NONE
    };
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg; EGLint n;

    G.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(G.dpy, 0, 0);
    eglChooseConfig(G.dpy, cfg_attr, &cfg, 1, &n);
    if (n < 1) { LOG("no EGL config"); return -1; }
    G.surf = eglCreateWindowSurface(G.dpy, cfg, win, 0);
    G.ctx = eglCreateContext(G.dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(G.dpy, G.surf, G.surf, G.ctx)) { LOG("eglMakeCurrent failed"); return -1; }
    eglQuerySurface(G.dpy, G.surf, EGL_WIDTH, &G.w);
    eglQuerySurface(G.dpy, G.surf, EGL_HEIGHT, &G.h);
    eglSwapInterval(G.dpy, 1);
    viewport_43();
    gl_setup();
    G.ready = 1;
    LOG("display %dx%d, frame %dx%d", G.w, G.h, SCREEN_W, SCREEN_H);
    return 0;
}

static void display_term(void)
{
    if (G.dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (G.ctx != EGL_NO_CONTEXT) eglDestroyContext(G.dpy, G.ctx);
        if (G.surf != EGL_NO_SURFACE) eglDestroySurface(G.dpy, G.surf);
        eglTerminate(G.dpy);
    }
    G.dpy = EGL_NO_DISPLAY; G.ctx = EGL_NO_CONTEXT; G.surf = EGL_NO_SURFACE;
    G.ready = 0;
}

/* present the PS1 display area: BGR555 -> RGB565, upload, scale */
static void present(void)
{
    const u16 *v = gpu_vram();
    int dx, dy, x, y;
    game_display_area(&dx, &dy);
    for (y = 0; y < SCREEN_H; y++) {
        const u16 *src = &v[(dy + y) * VRAM_W + dx];
        u16 *dst = &pixels[y * SCREEN_W];
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = src[x];
            u16 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            dst[x] = (u16)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
        }
    }
    glViewport(0, 0, G.w, G.h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(G.vx, G.vy, G.vw, G.vh);
    glUseProgram(G.prog);
    glBindTexture(GL_TEXTURE_2D, G.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SCREEN_W, SCREEN_H, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels);
    glBindBuffer(GL_ARRAY_BUFFER, G.vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(G.dpy, G.surf);
}

/* ---------------------------------------------------------------- input */
static void to_frame(float px, float py, int *x, int *y)
{
    /* window pixels (top-left origin) -> 320x240 frame coordinates */
    *x = (int)((px - G.vx) * SCREEN_W / (G.vw ? G.vw : 1));
    *y = (int)((py - (G.h - G.vy - G.vh)) * SCREEN_H / (G.vh ? G.vh : 1));
}

static void touch_set(int id, float px, float py, int down)
{
    int i, free_slot = -1, fx, fy;
    to_frame(px, py, &fx, &fy);
    if (id == stick_id) {
        if (down) { stick_x = fx; stick_y = fy; }
        else stick_id = -1;
        return;
    }
    if (id == look_id) {
        if (down) { game_cam_drag(fx - look_x); look_x = fx; }
        else look_id = -1;
        return;
    }
    for (i = 0; i < MAX_TOUCH; i++) {
        if (touches[i].down && touches[i].id == id) break;
        if (!touches[i].down && free_slot < 0) free_slot = i;
    }
    if (i == MAX_TOUCH) { if (!down || free_slot < 0) return; i = free_slot; }
    touches[i].id = id;
    touches[i].down = down;
    to_frame(px, py, &touches[i].x, &touches[i].y);
}

/* a Bluetooth / USB gamepad: buttons as PS1 pad bits, sticks as analog */
static u32 gp_bits;
static int gp_lx, gp_ly, gp_rx, gp_ry, gp_on;
static u32 key_bit(int32_t k)
{
    switch (k) {
    case AKEYCODE_DPAD_UP: return PAD_UP;
    case AKEYCODE_DPAD_DOWN: return PAD_DOWN;
    case AKEYCODE_DPAD_LEFT: return PAD_LEFT;
    case AKEYCODE_DPAD_RIGHT: return PAD_RIGHT;
    case AKEYCODE_BUTTON_A: return PAD_CROSS;
    case AKEYCODE_BUTTON_B: return PAD_CIRCLE;
    case AKEYCODE_BUTTON_X: return PAD_SQUARE;
    case AKEYCODE_BUTTON_Y: return PAD_TRIANGLE;
    case AKEYCODE_BUTTON_L1: return PAD_L1;
    case AKEYCODE_BUTTON_R1: return PAD_R1;
    case AKEYCODE_BUTTON_L2: return PAD_L2;
    case AKEYCODE_BUTTON_R2: return PAD_R2;
    case AKEYCODE_BUTTON_START: return PAD_START;
    case AKEYCODE_BUTTON_SELECT: return PAD_SELECT;
    }
    return 0;
}
static int axis(AInputEvent *ev, int a, float dead)
{
    float v = AMotionEvent_getAxisValue(ev, a, 0);
    if (v > -dead && v < dead) return 0;
    v = v > 0 ? (v - dead) / (1 - dead) : (v + dead) / (1 - dead);
    if (v > 1) v = 1;
    if (v < -1) v = -1;
    return (int)(v * 4096);
}

/* touches straight to the level editor (it draws its own buttons) */
static void pointer_all(AInputEvent *ev, int ev_kind, int only)
{
    size_t i, n = AMotionEvent_getPointerCount(ev);
    for (i = 0; i < n; i++) {
        int x, y;
        if (only >= 0 && (int)i != only) continue;
        to_frame(AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i), &x, &y);
        game_pointer(AMotionEvent_getPointerId(ev, i), ev_kind, x, y, 0);
    }
}

static int32_t on_input(struct android_app *app, AInputEvent *ev)
{
    (void)app;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        int32_t k = AKeyEvent_getKeyCode(ev), act = AKeyEvent_getAction(ev);
        u32 b = key_bit(k);
        if (k == AKEYCODE_BACK && game_wants_pointer()) b = PAD_CIRCLE;      /* the phone's back: cancel / back */
        if (!b) return 0;
        if (act == AKEY_EVENT_ACTION_DOWN) gp_bits |= b; else if (act == AKEY_EVENT_ACTION_UP) gp_bits &= ~b;
        return 1;
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION &&
        (AInputEvent_getSource(ev) & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
        float hx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0), hy = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
        gp_lx = axis(ev, AMOTION_EVENT_AXIS_X, 0.24f); gp_ly = -axis(ev, AMOTION_EVENT_AXIS_Y, 0.24f);
        gp_rx = axis(ev, AMOTION_EVENT_AXIS_Z, 0.26f); gp_ry = -axis(ev, AMOTION_EVENT_AXIS_RZ, 0.26f);
        gp_bits &= ~(PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_L2 | PAD_R2);
        if (hx < -0.5f) gp_bits |= PAD_LEFT;
        if (hx > 0.5f) gp_bits |= PAD_RIGHT;
        if (hy < -0.5f) gp_bits |= PAD_UP;
        if (hy > 0.5f) gp_bits |= PAD_DOWN;
        if (AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_LTRIGGER, 0) > 0.3f || AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_BRAKE, 0) > 0.3f) gp_bits |= PAD_L2;
        if (AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_RTRIGGER, 0) > 0.3f || AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_GAS, 0) > 0.3f) gp_bits |= PAD_R2;
        gp_on = 1;
        return 1;
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION && game_wants_pointer()) {
        int32_t a = AMotionEvent_getAction(ev);
        int act = a & AMOTION_EVENT_ACTION_MASK;
        int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        memset(touches, 0, sizeof(touches));
        stick_id = -1; look_id = -1;
        switch (act) {
        case AMOTION_EVENT_ACTION_DOWN: case AMOTION_EVENT_ACTION_POINTER_DOWN: pointer_all(ev, EP_DOWN, idx); break;
        case AMOTION_EVENT_ACTION_MOVE: pointer_all(ev, EP_MOVE, -1); break;
        case AMOTION_EVENT_ACTION_UP: case AMOTION_EVENT_ACTION_POINTER_UP: case AMOTION_EVENT_ACTION_CANCEL: pointer_all(ev, EP_UP, act == AMOTION_EVENT_ACTION_CANCEL ? -1 : idx); break;
        }
        return 1;
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(ev);
        int act = a & AMOTION_EVENT_ACTION_MASK;
        int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        size_t i, n = AMotionEvent_getPointerCount(ev);
        switch (act) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN: {
            int tx, ty, pid = AMotionEvent_getPointerId(ev, idx);
            to_frame(AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx), &tx, &ty);
            if (stick_id < 0 && tx < SCREEN_W * 45 / 100 && ty > 40 && !touch_hit(tx, ty)) {
                stick_id = pid; stick_ox = stick_x = tx; stick_oy = stick_y = ty;
                game_touch_tap(tx, ty);
                break;
            }
            if (look_id < 0 && tx >= SCREEN_W * 45 / 100 && ty > 40 && !touch_hit(tx, ty)) {
                look_id = pid; look_x = tx;
                game_touch_tap(tx, ty);
                break;
            }
            touch_set(pid, AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx), 1);
            game_touch_tap(tx, ty);
            break;
        }
        case AMOTION_EVENT_ACTION_MOVE:
            for (i = 0; i < n; i++)
                touch_set(AMotionEvent_getPointerId(ev, i), AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i), 1);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            touch_set(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx), 0);
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            memset(touches, 0, sizeof(touches));
            stick_id = -1; look_id = -1;
            break;
        }
        return 1;
    }
    return 0;
}

static u32 read_pad(void)
{
    u32 bits = gp_bits;
    int i;
    if (gp_on) game_set_analog(gp_lx, gp_ly, gp_rx, gp_ry);
    for (i = 0; i < MAX_TOUCH; i++)
        if (touches[i].down) bits |= touch_hit(touches[i].x, touches[i].y);
    return bits;
}

static void on_cmd(struct android_app *app, int32_t cmd)
{
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window) display_init(app->window);
        break;
    case APP_CMD_TERM_WINDOW:
        display_term();
        break;
    case APP_CMD_LOST_FOCUS:
        memset(touches, 0, sizeof(touches));
        stick_id = -1; look_id = -1;
        audio_pause(1);                          /* no hum or chainsaw while she's in the background */
        break;
    case APP_CMD_GAINED_FOCUS:
        audio_pause(0);
        break;
    case APP_CMD_PAUSE:
        audio_pause(1);
        break;
    }
}

/* ---------------------------------------------------------------- level files (the editor): the app's
   external files folder, reachable over USB at Android/data/<package>/files/levels */
static char lvl_dir[512], lvl_where[64];
static void lvl_init(struct android_app *app)
{
    const char *base = app->activity->externalDataPath ? app->activity->externalDataPath : app->activity->internalDataPath;
    const char *a;
    int i;
    snprintf(lvl_dir, sizeof(lvl_dir), "%s/levels", base ? base : ".");
    a = strstr(lvl_dir, "Android/");
    snprintf(lvl_where, sizeof(lvl_where), "%s", a ? a : "THE APP'S FILES");
    for (i = 0; lvl_where[i]; i++) if (lvl_where[i] >= 'a' && lvl_where[i] <= 'z') lvl_where[i] -= 32;
}
const char *plat_file_where(void) { return lvl_where; }
int plat_file_save(const char *name, const char *data, int len)
{
    char p[600]; FILE *f;
    mkdir(lvl_dir, 0775);
    snprintf(p, sizeof(p), "%s/%s", lvl_dir, name);
    f = fopen(p, "wb");
    if (!f) return 0;
    fwrite(data, 1, (size_t)len, f);
    fclose(f);
    return 1;
}
int plat_file_load(const char *name, char *buf, int cap)
{
    char p[600]; FILE *f; size_t n;
    snprintf(p, sizeof(p), "%s/%s", lvl_dir, name);
    f = fopen(p, "rb");
    if (!f) return -1;
    n = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)n;
}
int plat_file_list(char (*names)[32], int max)
{
    DIR *d = opendir(lvl_dir);
    struct dirent *e;
    int n = 0, i, j;
    if (!d) return 0;
    while ((e = readdir(d)) && n < max) {
        size_t l = strlen(e->d_name);
        if (l > 4 && l < 31 && !strcmp(e->d_name + l - 4, ".map")) { strcpy(names[n], e->d_name); n++; }
    }
    closedir(d);
    for (i = 1; i < n; i++) for (j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) { char t[32]; strcpy(t, names[j]); strcpy(names[j], names[j - 1]); strcpy(names[j - 1], t); }
    return n;
}
int plat_file_delete(const char *name) { char p[600]; snprintf(p, sizeof(p), "%s/%s", lvl_dir, name); return remove(p) == 0; }

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void android_main(struct android_app *app)
{
    double next = 0;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    G.dpy = EGL_NO_DISPLAY; G.ctx = EGL_NO_CONTEXT; G.surf = EGL_NO_SURFACE;

    lvl_init(app);
    if (!game_started) {
        game_init();
        game_set_touch_ui(1);
        audio_open();
        game_started = 1;
        LOG("static game RAM %d / %d bytes", game_ram_used(), RAM_BUDGET);
    }

    for (;;) {
        int events;
        struct android_poll_source *src;
        while (ALooper_pollOnce(G.ready ? 0 : -1, 0, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) { display_term(); return; }
        }
        if (!G.ready) continue;
        /* fixed 60 Hz game tick (PS1 NTSC field rate), render every display frame */
        {
            double t = now_s();
            int steps = 0;
            if (next == 0 || t - next > 0.25) next = t;
            while (t >= next && steps < 4) {
                game_set_stick(stick_id >= 0, stick_ox, stick_oy, stick_x, stick_y);
                game_frame(read_pad()); next += 1.0 / 60.0; steps++;
            }
        }
        present();
    }
}
