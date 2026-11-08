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
#include "deadbird.h"
#include "gpu.h"

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
    int i, free_slot = -1;
    for (i = 0; i < MAX_TOUCH; i++) {
        if (touches[i].down && touches[i].id == id) break;
        if (!touches[i].down && free_slot < 0) free_slot = i;
    }
    if (i == MAX_TOUCH) { if (!down || free_slot < 0) return; i = free_slot; }
    touches[i].id = id;
    touches[i].down = down;
    to_frame(px, py, &touches[i].x, &touches[i].y);
}

static int32_t on_input(struct android_app *app, AInputEvent *ev)
{
    (void)app;
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(ev);
        int act = a & AMOTION_EVENT_ACTION_MASK;
        int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        size_t i, n = AMotionEvent_getPointerCount(ev);
        switch (act) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            touch_set(AMotionEvent_getPointerId(ev, idx), AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx), 1);
            break;
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
            break;
        }
        return 1;
    }
    return 0;
}

static u32 read_pad(void)
{
    u32 bits = 0;
    int i;
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
        break;
    }
}

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

    if (!game_started) {
        game_init();
        game_set_touch_ui(1);
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
            while (t >= next && steps < 4) { game_frame(read_pad()); next += 1.0 / 60.0; steps++; }
        }
        present();
    }
}
