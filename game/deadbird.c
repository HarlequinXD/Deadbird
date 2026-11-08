/*
 * Deadbird - third-person shooter test level.
 * Title (Deadbird roaming the level on autopilot behind the menu) -> controls -> play.
 * Pure fixed-point C, no floats, no malloc: PS1-portable.
 */
#include "deadbird.h"
#include "gte.h"
#include "gpu.h"
#include "tim.h"
#include "model.h"
#include "player.h"
#include "level.h"
#include "ik.h"
#include "fk.h"
#include "hud.h"
#include "fx.h"
#include "title.h"
#include "gte.h"
#undef DEMON_MESH_NO_DATA
#include "demon_tim.h"
#include "demon_mesh.h"


static FrameBuf frames[2];
static int cur, disp_x, disp_y;
static Model demon;
static TimInfo tim;
static Player pl;
static int gs, gs_t, frame_count, shake, rumble;
static s32 cam_yaw, cam_dist, cam_h, cam_side, cam_clip = 2900;
static u32 prev_pad, cur_pad;
static int stick_on, stick_ox, stick_oy, stick_x, stick_y;   /* floating touch stick (screen px) */
static int an_lx, an_ly, an_rx;                               /* gamepad sticks, -4096..4096 */
#define STICK_R 34
static int debug_side_cam;
static int touch_ui;

/* ------------------------------------------------------------------ touch layout */
const TouchButton touch_buttons[] = {
    { 296, 200, 17, BTN_JUMP },
    { 258, 210, 15, PAD_CIRCLE },
    { 240, 160, 12, BTN_CAM_L },
    { 300, 160, 12, BTN_CAM_R },
    { 300, 122, 11, PAD_START },
};
const int touch_button_count = (int)(sizeof(touch_buttons) / sizeof(touch_buttons[0]));
static const char *touch_labels[] = { "J", "R", "<", ">", "II" };

u32 touch_hit(int x, int y)
{
    int i;
    u32 bits = 0;
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        int dx = x - b->x, dy = y - b->y;
        if (dx * dx + dy * dy <= (b->r + 7) * (b->r + 7)) bits |= b->button;
    }
    return bits;
}

void game_set_stick(int active, int ox, int oy, int x, int y)
{
    stick_on = active; stick_ox = ox; stick_oy = oy; stick_x = x; stick_y = y;
}

void game_cam_drag(int dx) { if (gs == GS_PLAY) { cam_yaw = (cam_yaw + dx * 12) & 4095; } }
void game_touch_tap(int x, int y) { if (gs == GS_TITLE) title_tap(x, y); }
void game_set_touch_ui(int on) { touch_ui = on; }
int  game_rumble(void) { return rumble; }
void game_set_analog(int lx, int ly, int rx, int ry) { an_lx = lx; an_ly = ly; an_rx = rx; (void)ry; }
void game_display_area(int *x, int *y) { *x = disp_x; *y = disp_y; }
int  game_phase(void) { return gs; }
void game_debug_side_cam(int on) { debug_side_cam = on; }
int  game_debug_player_yaw(void) { return pl.yaw; }

/* ------------------------------------------------------------------ attract autopilot */
static u32 autopilot(int t)
{
    /* wander: walk, run, curve left and right, jump now and then, stop to idle */
    int ph = t % 900;
    u32 b = PAD_UP;
    if (ph > 200 && ph < 420) b |= BTN_RUN;
    if ((t / 50) % 6 == 1) b |= PAD_RIGHT;
    if ((t / 70) % 7 == 4) b |= PAD_LEFT;
    if (ph == 300 || ph == 700) b |= BTN_JUMP;
    if (ph > 840) b = 0;                        /* stop to show the idle stance */
    return b;
}

static void start_play(void)
{
    s32 sx, sz;
    int syaw;
    level_init();
    fx_reset();
    level_spawn(&sx, &sz, &syaw);
    player_reset(&pl, sx, sz, syaw);
    cam_yaw = syaw;
    cam_clip = 2900;
    gs = GS_PLAY; gs_t = 0;
}

static void go_title(void)
{
    s32 sx, sz;
    int syaw;
    level_init();
    fx_reset();
    level_spawn(&sx, &sz, &syaw);
    player_reset(&pl, sx - 600, sz + 600, syaw + 300);
    cam_yaw = syaw + 300;
    cam_clip = 2900;
    gs = GS_TITLE; gs_t = 0;
    title_reset();
}

void game_init(void)
{
    gpu_init();
    tim_upload(demon_tim, DEMON_TIM_SIZE, &tim);
    demon.verts = (const DVERT *)demon_verts; demon.nverts = DEMON_NVERTS;
    demon.prims = (const DPRIM *)demon_prims; demon.nprims = DEMON_NPRIMS;
    demon.bones = (const DBONE *)demon_bones; demon.nbones = DEMON_NBONES;
    demon.tpage = tim.tpage; demon.clut = tim.clut;
    player_init(&demon);
    hud_init();
    title_init();
    cam_dist = 2700; cam_h = 1650;
    go_title();
    cur = 0;
}

/* ------------------------------------------------------------------ simulation */
static void camera_update(u32 pad)
{
    /* MediEvil-style follow camera: L1/R1 (or the < > touch buttons) swing it round; while you
       move away from it, it slowly drifts back behind your heading */
    s32 dyaw = ((pl.yaw - cam_yaw + 2048) & 4095) - 2048;
    if (pad & BTN_CAM_L) cam_yaw -= 40;
    if (pad & BTN_CAM_R) cam_yaw += 40;
    cam_yaw += an_rx * 56 >> 12;                       /* right stick: full tilt ~ 300 degrees a second */
    if (!(pad & (BTN_CAM_L | BTN_CAM_R)) && !an_rx && pl.speed > 4 && dyaw > -1300 && dyaw < 1300)
        cam_yaw += dyaw / (pl.speed > 40 ? 28 : 48);
    cam_yaw &= 4095;
}

/* stick or d-pad -> world heading + push amount, relative to where the camera looks right now
   (Slayer / MediEvil): hold the stick to one side and she keeps turning as the camera follows. */
static void read_move(u32 pad, int *yaw, int *mag)
{
    s32 dx = 0, dz = 0, len;
    *mag = 0;
    if (stick_on) {
        dx = stick_x - stick_ox; dz = stick_oy - stick_y;
        len = (s32)isqrt((u32)(dx * dx + dz * dz));
        if (len > 5) *mag = (int)(len >= STICK_R ? ONE : len * ONE / STICK_R);
    }
    if (!*mag && (an_lx || an_ly)) {
        dx = an_lx; dz = an_ly;
        len = (s32)isqrt((u32)(dx * dx + dz * dz));
        *mag = (int)(len > ONE ? ONE : len);
    }
    if (!*mag) {
        dx = ((pad & PAD_RIGHT) != 0) - ((pad & PAD_LEFT) != 0);
        dz = ((pad & PAD_UP) != 0) - ((pad & PAD_DOWN) != 0);
        if (dx || dz) *mag = 2600;                        /* d-pad walks; hold run to sprint */
    }
    if (!*mag) { *yaw = pl.yaw; return; }
    *yaw = (cam_yaw + player_input_angle(dx, dz)) & 4095;
}

static void simulate(u32 pad)
{
    int myaw, mmag;
    camera_update(pad);
    read_move(pad, &myaw, &mmag);
    player_update(&pl, pad, myaw, mmag);
    if (pl.land_t == 1 && pl.land_amp > 1500) rumble = pl.land_amp >> 4;      /* thump on a hard landing */
    player_pose(&pl, &demon);
    level_update();
    fx_update();
}

static void flow(u32 pad, u32 press)
{
    gs_t++;
    switch (gs) {
    case GS_TITLE: {
        int c;
        simulate(autopilot(gs_t));
        c = title_update(pad, press);
        if (c == MENU_PLAY) start_play();
        else if (c == MENU_CONTROLS) { gs = GS_CONTROLS; gs_t = 0; }
        break;
    }
    case GS_CONTROLS:
        simulate(autopilot(gs_t + 300));
        if (gs_t > 10 && (press & (PAD_CROSS | PAD_START | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE))) { gs = GS_TITLE; gs_t = 0; title_reset(); }
        break;
    case GS_PLAY:
        if (press & PAD_START) { gs = GS_PAUSE; gs_t = 0; break; }
        simulate(pad);
        break;
    case GS_PAUSE:
        if (press & PAD_START) { gs = GS_PLAY; gs_t = 0; }
        if (press & (PAD_SELECT | BTN_JUMP)) go_title();
        break;
    }
}

/* ------------------------------------------------------------------ rendering */
static void ring(FrameBuf *fb, int cx, int cy, int r, u32 col)
{
    static const s16 cs[9] = { 4096, 2896, 0, -2896, -4096, -2896, 0, 2896, 4096 };
    static const s16 sn[9] = { 0, 2896, 4096, 2896, 0, -2896, -4096, -2896, 0 };
    int k;
    for (k = 0; k < 8; k++)
        tri_f(fb, cx, cy, cx + (r * cs[k] >> 12), cy + (r * sn[k] >> 12),
              cx + (r * cs[k + 1] >> 12), cy + (r * sn[k + 1] >> 12), col, SEMI_ADD, 0);
}

static void draw_touch(FrameBuf *fb)
{
    int i;
    /* floating stick: base where the thumb landed, knob follows (hint shown when idle) */
    if (stick_on) {
        s32 dx = stick_x - stick_ox, dy = stick_y - stick_oy, len = (s32)isqrt((u32)(dx * dx + dy * dy));
        if (len > STICK_R) { dx = dx * STICK_R / len; dy = dy * STICK_R / len; }
        ring(fb, stick_ox, stick_oy, STICK_R + 4, RGB(46, 10, 6));
        ring(fb, stick_ox + dx, stick_oy + dy, 15, RGB(150, 46, 18));
    } else {
        ring(fb, 52, 184, STICK_R + 4, RGB(26, 6, 4));
        ring(fb, 52, 184, 13, RGB(60, 18, 8));
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        int lit = (cur_pad & b->button) != 0, c = lit ? 150 : 60;
        ring(fb, b->x, b->y, b->r, RGB(c, c / 3, c / 5));
        text_center(fb, touch_labels[i], b->x + 1, b->y - 4, 1, lit ? 255 : 150, 0);
    }
}

/* the shinigami held in the great tank: hangs arms out, head bowed, slowly turning */
static void draw_captive(FrameBuf *fb, const MATRIX *view, const DrawCtx *ctx0)
{
    static MATRIX pose[DEMON_NBONES], world[DEMON_NBONES];
    MATRIX root, ry, cam[DEMON_NBONES];
    VECTOR off, c;
    FKPose p;
    DrawCtx ctx = *ctx0;
    int i, b;
    for (i = 0; i < FK_COUNT; i++) p.v[i] = 0;
    p.v[FK_LSD] = 18; p.v[FK_RSD] = 18; p.v[FK_LSS] = 12; p.v[FK_RSS] = 12; p.v[FK_LEL] = 24; p.v[FK_REL] = 24;
    p.v[FK_HRX] = -38 + (isin(frame_count * 9) * 4 >> 12); p.v[FK_TRX] = -6;
    p.v[FK_LHP] = 12; p.v[FK_LKN] = 40; p.v[FK_LAN] = 30; p.v[FK_RHP] = -6; p.v[FK_RKN] = 26; p.v[FK_RAN] = 34;
    p.v[FK_LAB] = 4; p.v[FK_RAB] = 4;
    fk_bones(&demon, &p, pose, &off);
    level_tank(&c);
    mat_rot_y(&ry, frame_count * 3);
    mat_translate(&ry, c.vx, c.vy + 900 + (isin(frame_count * 14) * 60 >> 12), c.vz);
    mat_identity(&root);
    mat_mul(&ry, &root, &root);
    model_skeleton(&demon, &root, pose, world);
    for (b = 0; b < DEMON_NBONES; b++) mat_mul(view, &world[b], &cam[b]);
    ctx.r = 60; ctx.g = 120; ctx.b = 150;
    model_draw_mats(&demon, cam, &ctx, fb);
}

static void draw_world(FrameBuf *fb, PROJ *pr)
{
    MATRIX view, rx, ry, cam[DEMON_NBONES];
    SVECTOR tgt; VECTOR tv;
    DrawCtx ctx;
    s32 want_d = 2900, want_h = 1750, want_s = 0;
    s32 rxv, rzv;
    int b, sx = 0, sy = 0;

    /* third-person follow camera (heading set in camera_update) */
    cam_dist += (want_d - cam_dist) / 8;
    cam_h += (want_h - cam_h) / 8;
    cam_side += (want_s - cam_side) / 8;
    if (shake > 0) { sx = (int)(fx_rand() % 41) - 20; sy = (int)(fx_rand() % 31) - 15; shake--; }

    mat_rot_x(&rx, 150); mat_rot_y(&ry, -cam_yaw - (debug_side_cam ? 1024 : 0)); mat_mul(&rx, &ry, &view);
    /* look-at point: chest height */
    rxv = (icos(cam_yaw) * cam_side) >> 12; rzv = (-isin(cam_yaw) * cam_side) >> 12;
    tgt.vx = (s16)(-(pl.x + rxv) + sx); tgt.vy = (s16)(-(pl.y - cam_h + 300) + sy); tgt.vz = (s16)(-(pl.z + rzv));
    view.t[0] = view.t[1] = view.t[2] = 0;
    mat_apply(&view, &tgt, &tv);
    {
        /* indoor camera: pull in in front of walls, props and the ceiling */
        VECTOR off, back, head, campos;
        s32 d;
        int f;
        off.vx = 0; off.vy = 300; off.vz = cam_dist;
        mat_rot_tapply(&view, &off, &back);
        campos.vx = (pl.x + rxv) - back.vx; campos.vy = (pl.y - cam_h + 300) - back.vy; campos.vz = (pl.z + rzv) - back.vz;
        head.vx = pl.x; head.vy = pl.y - 1500; head.vz = pl.z;
        f = level_clip_segment(&head, &campos, 160);
        d = cam_dist * f / 4096 - 120;
        if (d < 450) d = 450;
        if (d < cam_clip) cam_clip = d;
        else cam_clip += (d - cam_clip) / 10;
    }
    view.t[0] = tv.vx; view.t[1] = tv.vy + 300; view.t[2] = tv.vz + cam_clip;

    pr->h = 250; pr->ofx = SCREEN_W / 2; pr->ofy = SCREEN_H / 2; pr->near_z = 120;
    ctx.proj = *pr; ctx.ot_shift = 4; ctx.cull_sign = 1; ctx.r = ctx.g = ctx.b = 128;

    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(3, 4, 7), RGB(1, 1, 3), NO_SEMI, OT_LEN - 1);
    level_draw(fb, &view, pr, frame_count);
    draw_captive(fb, &view, &ctx);
    blob_shadow(fb, &view, pr, pl.x, level_ground(pl.x, pl.z, pl.y), pl.z);
    for (b = 0; b < DEMON_NBONES; b++) mat_mul(&view, &pl.world[b], &cam[b]);
    model_draw_mats(&demon, cam, &ctx, fb);
    fx_draw(fb, &view, pr);
}

static void draw_hud(FrameBuf *fb)
{
    /* bone-framed name plate */
    quad_g(fb, 8, 8, 124, 26, RGB(30, 6, 8), RGB(10, 2, 3), NO_SEMI, 5);
    quad_g(fb, 6, 6, 126, 28, RGB(150, 120, 96), RGB(70, 44, 34), NO_SEMI, 6);
    tri_f(fb, 6, 6, 18, 6, 6, -2, RGB(140, 110, 86), NO_SEMI, 6);
    tri_f(fb, 126, 6, 114, 6, 126, -2, RGB(140, 110, 86), NO_SEMI, 6);
    text(fb, "DEADBIRD", 14, 12, 1, 200, 4);
    if (pl.speed > 40) text(fb, "RUN", 96, 12, 1, 120, 4);
}

static void draw_controls(FrameBuf *fb)
{
    static const char *lines[][2] = {
        { "LEFT THUMB",   "STICK - PUSH FAR TO RUN" },
        { "HOLD R",       "SPRINT" },
        { "J",            "JUMP" },
        { "<  >",         "SWING THE CAMERA" },
        { "RIGHT THUMB",  "DRAG TO TURN THE CAMERA" },
        { "",             "CAMERA FOLLOWS YOU" },
        { "II",           "PAUSE" },
    };
    static const char *pad_lines[][2] = {               /* Xbox controller / keyboard (PC) */
        { "LEFT STICK",   "MOVE - PUSH FAR TO RUN" },
        { "B  X  RT",     "HOLD TO SPRINT" },
        { "A",            "JUMP" },
        { "RIGHT STICK",  "TURN THE CAMERA" },
        { "LB  RB",       "SWING THE CAMERA" },
        { "KEYS",         "WASD SPACE SHIFT Q E" },
        { "START",        "PAUSE" },
    };
    const char *(*L)[2] = touch_ui ? lines : pad_lines;
    int i;
    quad_g(fb, 18, 34, 302, 214, RGB(30, 4, 6), RGB(8, 1, 2), NO_SEMI, 8);
    quad_g(fb, 16, 32, 304, 216, RGB(160, 40, 20), RGB(60, 8, 4), NO_SEMI, 9);
    text_hell(fb, "CONTROLS", 160, 42, 2, frame_count, 6);
    for (i = 0; i < 7; i++) {
        text(fb, L[i][0], 28, 74 + i * 18, 1, 230, 6);
        text(fb, L[i][1], 128, 74 + i * 18, 1, 140, 6);
    }
    if ((frame_count >> 4) & 1) text_center(fb, "PRESS ANY BUTTON", 160, 200, 1, 160, 6);
}

void game_frame(u32 pad)
{
    FrameBuf *fb = &frames[cur];
    u32 press = pad & ~prev_pad;
    int by = cur * SCREEN_H;
    PROJ pr;

    cur_pad = pad;
    prev_pad = pad;
    frame_count++;
    rumble = rumble * 3 / 4;
    flow(pad, press);

    ot_clear(fb);
    gpu_set_draw_env(0, by, SCREEN_W, SCREEN_H, 0, by, 1);
    draw_world(fb, &pr);

    if (gs == GS_TITLE) {
        quad_g(fb, 0, 0, 320, 240, RGB(70, 80, 80), RGB(30, 50, 50), SEMI_SUB, 40);
        title_draw(fb, frame_count);
    } else if (gs == GS_CONTROLS) {
        quad_g(fb, 0, 0, 320, 240, RGB(80, 90, 90), RGB(80, 90, 90), SEMI_SUB, 40);
        draw_controls(fb);
    } else {
        draw_hud(fb);
        if (gs == GS_PAUSE) {
            quad_g(fb, 0, 0, 320, 240, RGB(90, 100, 100), RGB(90, 100, 100), SEMI_SUB, 4);
            text_hell(fb, "PAUSED", 160, 80, 3, frame_count, 2);
            text_center(fb, touch_ui ? "II  RESUME" : "START  RESUME", 160, 130, 1, 200, 2);
            text_center(fb, touch_ui ? "J  QUIT TO TITLE" : "A  QUIT TO TITLE", 160, 146, 1, 200, 2);
        }
        if (touch_ui) draw_touch(fb);
    }

    gpu_draw_ot(fb);
    disp_x = 0; disp_y = by;
    cur ^= 1;
}

int game_ram_used(void)
{
    return (int)(sizeof(frames) + sizeof(pl) + sizeof(demon_verts) + sizeof(demon_prims) + sizeof(demon_bones)
                 + DEMON_TIM_SIZE);
}

PSX_STATIC_ASSERT(ram_budget, sizeof(frames) + sizeof(pl) + sizeof(demon_verts) + sizeof(demon_prims) + DEMON_TIM_SIZE < RAM_BUDGET);
