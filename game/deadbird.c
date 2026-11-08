/*
 * Deadbird - game flow: title (with a CPU-vs-CPU fight running behind the menu), move list,
 * versus CPU, training. Rounds: best of three, 60 second timer.
 * Pure fixed-point C, no floats, no malloc: PS1-portable.
 */
#include "deadbird.h"
#include "gte.h"
#include "gpu.h"
#include "tim.h"
#include "model.h"
#include "anim.h"
#include "fighter.h"
#include "ai.h"
#include "hud.h"
#include "fx.h"
#include "title.h"
#undef DEMON_MESH_NO_DATA
#include "demon_tim.h"
#include "demon_mesh.h"

enum { GS_TITLE, GS_MOVES, GS_INTRO, GS_FIGHT, GS_KO, GS_MATCH_END, GS_PAUSE };

static FrameBuf frames[2];
static int cur, disp_x, disp_y;
static Model demon;
static TimInfo tim;
static u16 clut_p1, clut_p2;

static Fighter fp[2];
static AI ai[2];
static int human[2];
static int training;
static int gs, gs_t, round_no, timer, winner, frame_count;
static int shake, flash;
static s32 cam_x, cam_dist;
static u32 prev_pad, cur_pad;
static int touch_ui;

#define ROUND_FRAMES (60 * 60)
#define ARENA_X 3300

/* ------------------------------------------------------------------ touch layout */
const TouchButton touch_buttons[] = {
    { 250, 196, 15, BTN_LIGHT },
    { 274, 172, 15, BTN_HEAVY },
    { 298, 196, 15, PAD_CIRCLE },
    { 298, 130, 11, PAD_START },
};
const int touch_button_count = (int)(sizeof(touch_buttons) / sizeof(touch_buttons[0]));
static const char *touch_labels[] = { "L", "H", "G", "II" };

u32 touch_hit(int x, int y)
{
    int i, dx = x - TOUCH_DPAD_X, dy = y - TOUCH_DPAD_Y;
    u32 bits = 0;
    if (dx * dx + dy * dy <= (TOUCH_DPAD_R + 12) * (TOUCH_DPAD_R + 12) && dx * dx + dy * dy > 36) {
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax * 5 > ay * 2) bits |= dx < 0 ? PAD_LEFT : PAD_RIGHT;
        if (ay * 5 > ax * 2) bits |= dy < 0 ? PAD_UP : PAD_DOWN;
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        dx = x - b->x; dy = y - b->y;
        if (dx * dx + dy * dy <= (b->r + 7) * (b->r + 7)) bits |= b->button;
    }
    return bits;
}

void game_touch_tap(int x, int y)
{
    if (gs == GS_TITLE) title_tap(x, y);
}

void game_set_touch_ui(int on) { touch_ui = on; }
void game_display_area(int *x, int *y) { *x = disp_x; *y = disp_y; }
int  game_phase(void) { return gs; }

/* ------------------------------------------------------------------ setup */
static void make_shade_clut(void)
{
    /* palette swap for the mirror twin: same texture, cold void colours (PS1 CLUT trick) */
    static u16 row[256];
    u16 *v = gpu_vram();
    RECT r;
    int i;
    for (i = 0; i < 256; i++) {
        u16 c = v[tim.cy * VRAM_W + tim.cx + i];
        int R = c & 31, G = (c >> 5) & 31, B = (c >> 10) & 31, hot = R > 24 && G > 10 && B < 8;
        if (c == 0) { row[i] = 0; continue; }
        if (hot) { row[i] = (u16)((G / 3) | ((G) << 5) | (31 << 10) | (c & 0x8000)); continue; }   /* eyes: icy blue */
        /* skin/scales: bruised violet-grey, armour stays dark metal */
        {
            int nr = (R + B) / 2 + 2, ng = G * 2 / 3, nb = R * 9 / 10 + 3;
            if (nr > 31) nr = 31;
            if (nb > 31) nb = 31;
            row[i] = (u16)(nr | (ng << 5) | (nb << 10) | (c & 0x8000));
            if (row[i] == 0) row[i] = 0x8000;
        }
    }
    r.x = 0; r.y = 481; r.w = 256; r.h = 1;
    gpu_load_image(&r, row);
    clut_p2 = get_clut(0, 481);
}

static void start_round(void)
{
    fighter_reset(&fp[0], 0, -900, clut_p1, "DEADBIRD");
    fighter_reset(&fp[1], 1, 900, clut_p2, "SHADE");
    fx_reset();
    timer = ROUND_FRAMES;
    gs = GS_INTRO; gs_t = 0;
    cam_x = 0; cam_dist = 4600;
}

static void start_match(int with_human, int is_training)
{
    human[0] = with_human; human[1] = 0;
    training = is_training;
    fp[0].wins = fp[1].wins = 0;
    ai_reset(&ai[0], 55, 0, 777);
    ai_reset(&ai[1], is_training ? 0 : 60, is_training, 4242);
    round_no = 1;
    start_round();
}

static void go_title(void)
{
    gs = GS_TITLE; gs_t = 0;
    title_reset();
    /* attract mode: two CPUs brawl behind the menu */
    human[0] = human[1] = 0;
    training = 0;
    ai_reset(&ai[0], 70, 0, 99);
    ai_reset(&ai[1], 70, 0, 123);
    fighter_reset(&fp[0], 0, -900, clut_p1, "DEADBIRD");
    fighter_reset(&fp[1], 1, 900, clut_p2, "SHADE");
    fp[0].wins = fp[1].wins = 0;
}

void game_init(void)
{
    gpu_init();
    tim_upload(demon_tim, DEMON_TIM_SIZE, &tim);
    demon.verts = (const DVERT *)demon_verts; demon.nverts = DEMON_NVERTS;
    demon.prims = (const DPRIM *)demon_prims; demon.nprims = DEMON_NPRIMS;
    demon.bones = (const DBONE *)demon_bones; demon.nbones = DEMON_NBONES;
    demon.tpage = tim.tpage; demon.clut = clut_p1 = tim.clut;
    make_shade_clut();
    anim_init();
    hud_init();
    title_init();
    go_title();
    cur = 0;
}

/* ------------------------------------------------------------------ simulation */
static void separate(void)
{
    s32 dx = fp[1].x - fp[0].x, ad = dx < 0 ? -dx : dx, minsep = 420;
    if (fp[0].state == ST_LIE || fp[1].state == ST_LIE || fp[0].state == ST_KO || fp[1].state == ST_KO) minsep = 260;
    if (ad < minsep) {
        s32 push = (minsep - ad) / 2 + 1;
        if (dx >= 0) { fp[0].x -= push; fp[1].x += push; }
        else { fp[0].x += push; fp[1].x -= push; }
    }
    if (fp[0].x < -ARENA_X) fp[0].x = -ARENA_X;
    if (fp[0].x > ARENA_X) fp[0].x = ARENA_X;
    if (fp[1].x < -ARENA_X) fp[1].x = -ARENA_X;
    if (fp[1].x > ARENA_X) fp[1].x = ARENA_X;
}

static void simulate(u32 pad, int frozen)
{
    int i;
    HitEvent ev;
    for (i = 0; i < 2; i++) {
        u32 in = human[i] ? pad : ai_update(&ai[i], &fp[i], &fp[1 - i]);
        if (frozen) in = 0;
        fighter_input(&fp[i], in);
    }
    fighter_update(&fp[0], &fp[1], frozen);
    fighter_update(&fp[1], &fp[0], frozen);
    separate();
    fighter_pose(&fp[0], &demon);
    fighter_pose(&fp[1], &demon);
    if (!frozen)
        for (i = 0; i < 2; i++)
            if (fighter_check_hit(&fp[i], &fp[1 - i], &ev)) {
                fx_hit(&ev.at, ev.power, ev.blocked);
                if (!ev.blocked) { shake = ev.power >= 100 ? 7 : 4; if (ev.power >= 100) flash = 4; }
                if (training && fp[1 - i].hp < 1) fp[1 - i].hp = 1;
            }
    if (training) { fp[0].hp = fp[0].hp < 1000 ? fp[0].hp + 2 : 1000; if (fp[1].state == ST_IDLE && fp[1].hp < 1000) fp[1].hp += 4; }
    fx_update();
}

static void flow(u32 pad, u32 press)
{
    gs_t++;
    switch (gs) {
    case GS_TITLE: {
        int c;
        simulate(0, 0);
        if (fp[0].state == ST_KO || fp[1].state == ST_KO) { if (gs_t > 60000) gs_t = 0; }
        if ((fp[0].state == ST_KO || fp[1].state == ST_KO) && (frame_count % 240) == 0) {
            fighter_reset(&fp[0], 0, -900, clut_p1, "DEADBIRD");
            fighter_reset(&fp[1], 1, 900, clut_p2, "SHADE");
        }
        c = title_update(pad, press);
        if (c == MENU_VERSUS) start_match(1, 0);
        else if (c == MENU_TRAINING) start_match(1, 1);
        else if (c == MENU_MOVES) { gs = GS_MOVES; gs_t = 0; }
        break;
    }
    case GS_MOVES:
        simulate(0, 0);
        if (gs_t > 10 && (press & (PAD_CROSS | PAD_START | PAD_CIRCLE | BTN_LIGHT | BTN_HEAVY))) { gs = GS_TITLE; gs_t = 0; title_reset(); }
        break;
    case GS_INTRO:
        if (gs_t == 1) { fighter_set_state(&fp[0], ST_TAUNT); fighter_set_state(&fp[1], ST_TAUNT); }
        simulate(0, 1);
        if (gs_t >= 110) { gs = GS_FIGHT; gs_t = 0; fighter_set_state(&fp[0], ST_IDLE); fighter_set_state(&fp[1], ST_IDLE); }
        break;
    case GS_FIGHT:
        if (press & PAD_START) { gs = GS_PAUSE; gs_t = 0; break; }
        simulate(pad, 0);
        if (!training && timer > 0) timer--;
        if (fp[0].hp <= 0 || fp[1].hp <= 0 || timer == 0) {
            winner = fp[0].hp == fp[1].hp ? -1 : (fp[0].hp > fp[1].hp ? 0 : 1);
            if (winner >= 0) fp[winner].wins++;
            gs = GS_KO; gs_t = 0;
        }
        break;
    case GS_KO:
        simulate(0, 0);
        if (gs_t == 70 && winner >= 0) fighter_set_state(&fp[winner], ST_WIN);
        if (gs_t >= 190) {
            if (fp[0].wins >= 2 || fp[1].wins >= 2) { gs = GS_MATCH_END; gs_t = 0; }
            else { round_no++; start_round(); }
        }
        break;
    case GS_MATCH_END:
        simulate(0, 0);
        if (gs_t > 240 || (gs_t > 60 && (press & (PAD_CROSS | PAD_START | BTN_LIGHT)))) go_title();
        break;
    case GS_PAUSE:
        if (press & PAD_START) { gs = GS_FIGHT; gs_t = 0; }
        if (press & PAD_SELECT) go_title();
        if (press & BTN_HEAVY) go_title();
        break;
    }
}

/* ------------------------------------------------------------------ rendering */
static void draw_touch(FrameBuf *fb)
{
    static const s16 dir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    static const u32 bit[4] = { PAD_UP, PAD_RIGHT, PAD_DOWN, PAD_LEFT };
    static const s16 cs[9] = { 4096, 2896, 0, -2896, -4096, -2896, 0, 2896, 4096 };
    static const s16 sn[9] = { 0, 2896, 4096, 2896, 0, -2896, -4096, -2896, 0 };
    int i, k;
    for (k = 0; k < 8; k++)
        tri_f(fb, TOUCH_DPAD_X, TOUCH_DPAD_Y,
              TOUCH_DPAD_X + (TOUCH_DPAD_R * cs[k] >> 12), TOUCH_DPAD_Y + (TOUCH_DPAD_R * sn[k] >> 12),
              TOUCH_DPAD_X + (TOUCH_DPAD_R * cs[k + 1] >> 12), TOUCH_DPAD_Y + (TOUCH_DPAD_R * sn[k + 1] >> 12),
              RGB(50, 12, 8), SEMI_ADD, 0);
    for (i = 0; i < 4; i++) {
        int dx = dir[i][0], dy = dir[i][1], c = (cur_pad & bit[i]) ? 220 : 110;
        tri_f(fb, TOUCH_DPAD_X + dx * 32, TOUCH_DPAD_Y + dy * 32,
              TOUCH_DPAD_X + dx * 16 - dy * 11, TOUCH_DPAD_Y + dy * 16 + dx * 11,
              TOUCH_DPAD_X + dx * 16 + dy * 11, TOUCH_DPAD_Y + dy * 16 - dx * 11, RGB(c, c / 3, c / 6), SEMI_ADD, 0);
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        int lit = (cur_pad & b->button) != 0, c = lit ? 150 : 60;
        for (k = 0; k < 8; k++)
            tri_f(fb, b->x, b->y, b->x + (b->r * cs[k] >> 12), b->y + (b->r * sn[k] >> 12),
                  b->x + (b->r * cs[k + 1] >> 12), b->y + (b->r * sn[k + 1] >> 12), RGB(c, c / 3, c / 5), SEMI_ADD, 0);
        text_center(fb, touch_labels[i], b->x + 1, b->y - 4, 1, lit ? 255 : 150, 0);
    }
}

static void draw_world(FrameBuf *fb, MATRIX *view, DrawCtx *ctx)
{
    MATRIX rx, ry, cam[DEMON_NBONES];
    SVECTOR tgt; VECTOR tv;
    s32 sep, want;
    int i, b, sx = 0, sy = 0;

    /* fighting-game camera: frames both fighters, pulls back as they separate */
    sep = fp[1].x - fp[0].x; if (sep < 0) sep = -sep;
    cam_x += ((fp[0].x + fp[1].x) / 2 - cam_x) / 6;
    want = 3600 + sep * 7 / 10; if (want > 7200) want = 7200;
    cam_dist += (want - cam_dist) / 8;
    if (shake > 0) { sx = (int)(fx_rand() % 61) - 30; sy = (int)(fx_rand() % 41) - 20; shake--; }

    mat_rot_x(&rx, 130); mat_rot_y(&ry, 0); mat_mul(&rx, &ry, view);
    tgt.vx = (s16)(-cam_x + sx); tgt.vy = (s16)(1050 + sy); tgt.vz = 0;
    view->t[0] = view->t[1] = view->t[2] = 0;
    mat_apply(view, &tgt, &tv);
    view->t[0] = tv.vx; view->t[1] = tv.vy; view->t[2] = tv.vz + cam_dist;

    ctx->proj.h = 300; ctx->proj.ofx = SCREEN_W / 2; ctx->proj.ofy = SCREEN_H / 2 + 6; ctx->proj.near_z = 160;
    ctx->ot_shift = 4; ctx->cull_sign = 1; ctx->r = ctx->g = ctx->b = 128;

    sky_draw(fb, frame_count, cam_x);
    arena_draw(fb, view, &ctx->proj, frame_count);
    for (i = 0; i < 2; i++) {
        blob_shadow(fb, view, &ctx->proj, fp[i].x, fp[i].z);
        for (b = 0; b < DEMON_NBONES; b++) mat_mul(view, &fp[i].world[b], &cam[b]);
        demon.clut = fp[i].clut;
        /* flash white-hot on heavy impact */
        ctx->r = ctx->g = ctx->b = (u8)(fp[i].hitstop > 5 ? 200 : 128);
        model_draw_mats(&demon, cam, ctx, fb);
    }
    ctx->r = ctx->g = ctx->b = 128;
    fx_draw(fb, view, &ctx->proj);
    embers_draw(fb, frame_count, 30);
}

static void draw_moves(FrameBuf *fb)
{
    static const char *lines[][2] = {
        { "L  L  L",   "CLAW JAB - HELL HOOK - RAKE" },
        { "L  L  H",   "... SKULL HAMMER  DOWN" },
        { "H  H",      "HAYMAKER - HOOF STOMP" },
        { "FWD + H",   "HORN GORE  DOWN" },
        { "L  H  H",   "JAB - HAYMAKER - STOMP" },
        { "HOLD G",    "GUARD" },
        { "D-PAD",     "WALK IN / BACK OFF" },
    };
    int i;
    quad_g(fb, 18, 34, 302, 214, RGB(30, 4, 6), RGB(8, 1, 2), NO_SEMI, 8);
    quad_g(fb, 16, 32, 304, 216, RGB(160, 40, 20), RGB(60, 8, 4), NO_SEMI, 9);
    text_hell(fb, "MOVE LIST", 160, 42, 2, frame_count, 6);
    for (i = 0; i < 7; i++) {
        text(fb, lines[i][0], 30, 74 + i * 18, 1, 230, 6);
        text(fb, lines[i][1], 100, 74 + i * 18, 1, 140, 6);
    }
    if ((frame_count >> 4) & 1) text_center(fb, "PRESS ANY BUTTON", 160, 200, 1, 160, 6);
}

void game_frame(u32 pad)
{
    FrameBuf *fb = &frames[cur];
    u32 press = pad & ~prev_pad;
    int by = cur * SCREEN_H;
    MATRIX view;
    DrawCtx ctx;

    cur_pad = pad;
    prev_pad = pad;
    frame_count++;
    flow(pad, press);

    ot_clear(fb);
    gpu_set_draw_env(0, by, SCREEN_W, SCREEN_H, 0, by, 1);

    if (gs == GS_TITLE) {
        /* attract fight in the background, darkened, then the title on top */
        draw_world(fb, &view, &ctx);
        quad_g(fb, 0, 0, 320, 240, RGB(70, 80, 80), RGB(40, 60, 60), SEMI_SUB, 40);
        title_draw(fb, frame_count);
    } else if (gs == GS_MOVES) {
        draw_world(fb, &view, &ctx);
        quad_g(fb, 0, 0, 320, 240, RGB(80, 90, 90), RGB(80, 90, 90), SEMI_SUB, 40);
        draw_moves(fb);
    } else {
        char buf[24];
        draw_world(fb, &view, &ctx);
        hud_health(fb, 0, fp[0].hp, fp[0].trail, fp[0].name, fp[0].wins, frame_count);
        hud_health(fb, 1, fp[1].hp, fp[1].trail, fp[1].name, fp[1].wins, frame_count);
        hud_timer(fb, training ? 99 : (timer + 59) / 60, frame_count);
        hud_combo(fb, 0, fp[0].combo, fp[0].combo_show);
        hud_combo(fb, 1, fp[1].combo, fp[1].combo_show);
        if (training) text_center(fb, "TRAINING", 160, 40, 1, 140, 2);

        if (gs == GS_INTRO) {
            if (gs_t < 70) {
                memcpy(buf, "ROUND ", 6); buf[6] = (char)('0' + round_no); buf[7] = 0;
                text_hell(fb, buf, 160, 88, 4, frame_count, 2);
            } else {
                int s = gs_t < 78 ? 7 - (gs_t - 70) / 2 : 4;
                text_hell(fb, "FIGHT!", 160, 100 - s * 5, s, frame_count, 2);
            }
        } else if (gs == GS_KO) {
            if (gs_t < 100) {
                int s = gs_t < 8 ? 9 - gs_t / 2 : 5;
                text_hell(fb, timer == 0 ? "TIME" : "K.O.", 160, 92 - s * 5, s, frame_count, 2);
                if (gs_t < 6) quad_g(fb, 0, 0, 320, 240, RGB(160, 40, 10), RGB(160, 40, 10), SEMI_ADD, 1);
            } else if (winner >= 0) {
                memcpy(buf, fp[winner].name, strlen(fp[winner].name) + 1);
                text_hell(fb, buf, 160, 90, 3, frame_count, 2);
                text_center(fb, "TAKES THE ROUND", 160, 124, 1, 200, 2);
            } else {
                text_hell(fb, "DRAW", 160, 90, 4, frame_count, 2);
            }
        } else if (gs == GS_MATCH_END) {
            int w = fp[0].wins >= 2 ? 0 : 1;
            fire_step(30);
            fire_draw(fb, 140, 80);
            text_hell(fb, fp[w].name, 160, 70, 4, frame_count, 2);
            text_hell(fb, "WINS", 160, 116, 4, frame_count, 2);
            if (human[0]) text_center(fb, w == 0 ? "THE PIT IS YOURS" : "YOUR SOUL IS FORFEIT", 160, 168, 1, 220, 2);
        } else if (gs == GS_PAUSE) {
            quad_g(fb, 0, 0, 320, 240, RGB(90, 100, 100), RGB(90, 100, 100), SEMI_SUB, 4);
            text_hell(fb, "PAUSED", 160, 80, 3, frame_count, 2);
            text_center(fb, "II  RESUME", 160, 130, 1, 200, 2);
            text_center(fb, "H  QUIT TO TITLE", 160, 146, 1, 200, 2);
        }
        if (flash > 0) { quad_g(fb, 0, 0, 320, 240, RGB(flash * 20, flash * 10, flash * 4), RGB(flash * 20, flash * 10, flash * 4), SEMI_ADD, 1); flash--; }
    }
    if (touch_ui && gs != GS_TITLE) draw_touch(fb);

    gpu_draw_ot(fb);
    disp_x = 0; disp_y = by;
    cur ^= 1;
}

int game_ram_used(void)
{
    return (int)(sizeof(frames) + sizeof(fp) + sizeof(demon_verts) + sizeof(demon_prims) + sizeof(demon_bones)
                 + DEMON_TIM_SIZE);
}

PSX_STATIC_ASSERT(ram_budget, sizeof(frames) + sizeof(fp) + sizeof(demon_verts) + sizeof(demon_prims) + DEMON_TIM_SIZE < RAM_BUDGET);
