/*
 * Deadbird - test scene: the skull demon on a dithered floor, orbit camera,
 * T-pose / walk cycle. Pure fixed-point C, no floats, no malloc: PS1-portable.
 */
#include "deadbird.h"
#include "gte.h"
#include "gpu.h"
#include "tim.h"
#include "model.h"
#include "demon_tim.h"
#include "demon_mesh.h"

/* ------------------------------------------------------------------ state */
static FrameBuf frames[2];          /* double-buffered OT + packet buffers */
static int      cur;               /* buffer being drawn */
static int      disp_x, disp_y;

static Model    demon;
static MATRIX   pose[DEMON_NBONES];

static int cam_yaw = 2048 + 420, cam_pitch = 150, cam_dist = 5200;
static int auto_spin = 1;
static int walking = 0, walk_w = 0;     /* walk blend 0..4096 */
static int phase = 0;
static u32 prev_pad = 0;
static int touch_ui = 0;
static u32 cur_pad = 0;

/* ------------------------------------------------------------------ touch layout */
const TouchButton touch_buttons[] = {
    { 274, 206, 17, PAD_CROSS },
    { 300, 178, 17, PAD_CIRCLE },
    { 274, 150, 17, PAD_TRIANGLE },
    { 300, 122, 15, PAD_START },
};
const int touch_button_count = (int)(sizeof(touch_buttons) / sizeof(touch_buttons[0]));

u32 touch_hit(int x, int y)
{
    int i, dx = x - TOUCH_DPAD_X, dy = y - TOUCH_DPAD_Y;
    u32 bits = 0;
    if (dx * dx + dy * dy <= (TOUCH_DPAD_R + 10) * (TOUCH_DPAD_R + 10) && dx * dx + dy * dy > 36) {
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax * 5 > ay * 2) bits |= dx < 0 ? PAD_LEFT : PAD_RIGHT;   /* 8-way with diagonals */
        if (ay * 5 > ax * 2) bits |= dy < 0 ? PAD_UP : PAD_DOWN;
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        dx = x - b->x; dy = y - b->y;
        if (dx * dx + dy * dy <= (b->r + 6) * (b->r + 6)) bits |= b->button;
    }
    return bits;
}

/* ------------------------------------------------------------------ helpers */
static POLY *poly(FrameBuf *fb, int code)
{
    POLY *p = prim_alloc(fb);
    if (p) { memset(p, 0, sizeof(*p)); p->code = (u8)code; }
    return p;
}

static void set_rgb(POLY *p, int i, int r, int g, int b)
{
    p->v[i].r = (u8)r; p->v[i].g = (u8)g; p->v[i].b = (u8)b;
}

/* background: dithered gouraud gradient, drawn first (far end of the OT) */
static void draw_background(FrameBuf *fb)
{
    POLY *p = poly(fb, PF_QUAD | PF_GOURAUD);
    if (!p) return;
    p->v[0].x = 0;        p->v[0].y = 0;
    p->v[1].x = SCREEN_W; p->v[1].y = 0;
    p->v[2].x = 0;        p->v[2].y = SCREEN_H;
    p->v[3].x = SCREEN_W; p->v[3].y = SCREEN_H;
    set_rgb(p, 0, 70, 12, 14); set_rgb(p, 1, 70, 12, 14);
    set_rgb(p, 2, 6, 2, 4);    set_rgb(p, 3, 6, 2, 4);
    ot_add(fb, OT_LEN - 1, p);
}

/* floor: grid of gouraud quads with distance fade (PS1 fog trick) */
#define FLOOR_N    12
#define FLOOR_TILE 512
static void draw_floor(FrameBuf *fb, const MATRIX *view, const PROJ *pr)
{
    static s16 gx[FLOOR_N + 1][FLOOR_N + 1], gy[FLOOR_N + 1][FLOOR_N + 1];
    static s32 gz[FLOOR_N + 1][FLOOR_N + 1];
    static u8  gok[FLOOR_N + 1][FLOOR_N + 1];
    int i, j;
    for (j = 0; j <= FLOOR_N; j++)
        for (i = 0; i <= FLOOR_N; i++) {
            SVECTOR w; VECTOR c;
            w.vx = (s16)((i - FLOOR_N / 2) * FLOOR_TILE);
            w.vy = 0;
            w.vz = (s16)((j - FLOOR_N / 2) * FLOOR_TILE);
            mat_apply(view, &w, &c);
            gok[j][i] = (u8)rot_trans_pers(pr, &c, &gx[j][i], &gy[j][i]);
            gz[j][i] = c.vz;
        }
    for (j = 0; j < FLOOR_N; j++)
        for (i = 0; i < FLOOR_N; i++) {
            static const int ci[4] = { 0, 1, 0, 1 }, cj[4] = { 0, 0, 1, 1 };
            POLY *p;
            int k, dark = (i + j) & 1;
            if (!gok[j][i] || !gok[j][i + 1] || !gok[j + 1][i] || !gok[j + 1][i + 1]) continue;
            p = poly(fb, PF_QUAD | PF_GOURAUD);
            if (!p) return;
            for (k = 0; k < 4; k++) {
                int x = i + ci[k], y = j + cj[k];
                /* distance fade + radial falloff from the centre */
                int dx = x - FLOOR_N / 2, dy = y - FLOOR_N / 2;
                int fall = 4096 - (dx * dx + dy * dy) * 4096 / 50;
                int fog = 4096 - gz[y][x] * 4096 / 14000;
                int lvl;
                if (fall < 0) fall = 0;
                if (fog < 0) fog = 0;
                lvl = (fall >> 1) * (fog >> 1) >> 12;          /* 0..1024 */
                p->v[k].x = gx[y][x]; p->v[k].y = gy[y][x];
                set_rgb(p, k, (dark ? 70 : 104) * lvl >> 10, (dark ? 24 : 38) * lvl >> 10, (dark ? 20 : 28) * lvl >> 10);
            }
            ot_add(fb, OT_LEN - 2, p);
        }
}

/* blob shadow: subtractive semi-transparent octagon under the feet */
static void draw_shadow(FrameBuf *fb, const MATRIX *view, const PROJ *pr)
{
    static const s16 ox[8] = { 600, 424, 0, -424, -600, -424, 0, 424 };
    static const s16 oz[8] = { 0, 300, 420, 300, 0, -300, -420, -300 };
    s16 cx, cy, px[8], py[8];
    SVECTOR w; VECTOR c;
    int k;
    w.vx = 0; w.vy = -4; w.vz = 40;
    mat_apply(view, &w, &c);
    if (!rot_trans_pers(pr, &c, &cx, &cy)) return;
    for (k = 0; k < 8; k++) {
        w.vx = ox[k]; w.vz = (s16)(oz[k] + 40);
        mat_apply(view, &w, &c);
        if (!rot_trans_pers(pr, &c, &px[k], &py[k])) return;
    }
    for (k = 0; k < 8; k++) {
        POLY *p = poly(fb, PF_SEMI | PF_GOURAUD);
        int n = (k + 1) & 7;
        if (!p) return;
        p->tpage = get_tpage(0, SEMI_SUB, 0, 0);
        p->v[0].x = cx; p->v[0].y = cy; set_rgb(p, 0, 60, 60, 60);
        p->v[1].x = px[k]; p->v[1].y = py[k]; set_rgb(p, 1, 0, 0, 0);
        p->v[2].x = px[n]; p->v[2].y = py[n]; set_rgb(p, 2, 0, 0, 0);
        ot_add(fb, OT_LEN - 3, p);
    }
}

/* on-screen touch pad, semi-transparent PS1 prims drawn on top (OT index 0) */
static void disc(FrameBuf *fb, int x, int y, int r, int lit)
{
    static const s16 cs[9] = { 4096, 2896, 0, -2896, -4096, -2896, 0, 2896, 4096 };
    static const s16 sn[9] = { 0, 2896, 4096, 2896, 0, -2896, -4096, -2896, 0 };
    int k, c = lit ? 120 : 60;
    for (k = 0; k < 8; k++) {
        POLY *p = poly(fb, PF_SEMI);
        if (!p) return;
        p->tpage = get_tpage(0, SEMI_ADD, 0, 0);
        p->v[0].x = (s16)x; p->v[0].y = (s16)y;
        p->v[1].x = (s16)(x + (r * cs[k] >> 12)); p->v[1].y = (s16)(y + (r * sn[k] >> 12));
        p->v[2].x = (s16)(x + (r * cs[k + 1] >> 12)); p->v[2].y = (s16)(y + (r * sn[k + 1] >> 12));
        set_rgb(p, 0, c, c / 3, c / 4);
        ot_add(fb, 0, p);
    }
}

static void draw_touch_ui(FrameBuf *fb)
{
    static const s16 dir[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
    static const u32 bit[4] = { PAD_UP, PAD_RIGHT, PAD_DOWN, PAD_LEFT };
    int i;
    disc(fb, TOUCH_DPAD_X, TOUCH_DPAD_Y, TOUCH_DPAD_R, 0);
    for (i = 0; i < 4; i++) {
        POLY *p = poly(fb, PF_SEMI);
        int dx = dir[i][0], dy = dir[i][1], c = (cur_pad & bit[i]) ? 200 : 110;
        if (!p) return;
        p->tpage = get_tpage(0, SEMI_ADD, 0, 0);
        p->v[0].x = (s16)(TOUCH_DPAD_X + dx * 32);          p->v[0].y = (s16)(TOUCH_DPAD_Y + dy * 32);
        p->v[1].x = (s16)(TOUCH_DPAD_X + dx * 16 - dy * 11); p->v[1].y = (s16)(TOUCH_DPAD_Y + dy * 16 + dx * 11);
        p->v[2].x = (s16)(TOUCH_DPAD_X + dx * 16 + dy * 11); p->v[2].y = (s16)(TOUCH_DPAD_Y + dy * 16 - dx * 11);
        set_rgb(p, 0, c, c / 3, c / 4);
        ot_add(fb, 0, p);
    }
    for (i = 0; i < touch_button_count; i++)
        disc(fb, touch_buttons[i].x, touch_buttons[i].y, touch_buttons[i].r, (cur_pad & touch_buttons[i].button) != 0);
}

/* ------------------------------------------------------------------ animation */
static void rot_xyz(MATRIX *out, int ax, int ay, int az)    /* out = Rx * Ry * Rz */
{
    MATRIX a, b;
    mat_rot_x(&a, ax); mat_rot_y(&b, ay); mat_mul(&a, &b, out);
    mat_rot_z(&b, az); mat_mul(out, &b, out);
}

/* angles in 4096 = 360 deg; Y-down space (X and Z rotations are mirrored vs Y-up tools) */
static void animate(void)
{
    int s = isin(phase), c = icos(phase), w = walk_w, i;
    #define A(x) ((x) * w >> 12)                          /* scale by walk blend */
    for (i = 0; i < DEMON_NBONES; i++) mat_identity(&pose[i]);
    rot_xyz(&pose[DEMON_BONE_THIGH_L], A(293 * s >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_THIGH_R], A(-293 * s >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_CALF_L], A(-358 * (c < 0 ? -c : 0) >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_CALF_R], A(-358 * (c > 0 ? c : 0) >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_HOOF_L], A(130 * s >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_HOOF_R], A(-130 * s >> 12), 0, 0);
    rot_xyz(&pose[DEMON_BONE_UPPER_ARM_L], A(-228 * s >> 12), 0, A(782));
    rot_xyz(&pose[DEMON_BONE_UPPER_ARM_R], A(228 * s >> 12), 0, A(-782));
    rot_xyz(&pose[DEMON_BONE_FOREARM_L], 0, A(-228), 0);
    rot_xyz(&pose[DEMON_BONE_FOREARM_R], 0, A(228), 0);
    rot_xyz(&pose[DEMON_BONE_TORSO], 0, A(78 * s >> 12), 0);
    rot_xyz(&pose[DEMON_BONE_PELVIS], 0, A(-52 * s >> 12), 0);
    rot_xyz(&pose[DEMON_BONE_NECK], 0, A(-39 * s >> 12), 0);
    rot_xyz(&pose[DEMON_BONE_HEAD], A(-39 * isin(phase * 2) >> 12), 0, 0);
    #undef A
}

/* ------------------------------------------------------------------ public */
static TimInfo tim;

void game_init(void)
{
    gpu_init();
    tim_upload(demon_tim, DEMON_TIM_SIZE, &tim);
    demon.verts = (const DVERT *)demon_verts; demon.nverts = DEMON_NVERTS;
    demon.prims = (const DPRIM *)demon_prims; demon.nprims = DEMON_NPRIMS;
    demon.bones = (const DBONE *)demon_bones; demon.nbones = DEMON_NBONES;
    demon.tpage = tim.tpage; demon.clut = tim.clut;
    cur = 0;
}

void game_set_touch_ui(int on) { touch_ui = on; }

void game_display_area(int *x, int *y) { *x = disp_x; *y = disp_y; }

void game_frame(u32 pad)
{
    FrameBuf *fb = &frames[cur];
    u32 hit = pad & ~prev_pad;
    int by = cur * SCREEN_H;                 /* draw buffer: (0,0) or (0,240) in VRAM */
    MATRIX view, rx, ry, model, root;
    SVECTOR tgt; VECTOR tv;
    DrawCtx ctx;
    int bob;

    cur_pad = pad;
    /* ---- input */
    if (pad & PAD_LEFT)  { cam_yaw -= 40; auto_spin = 0; }
    if (pad & PAD_RIGHT) { cam_yaw += 40; auto_spin = 0; }
    if (pad & PAD_UP)    cam_dist -= 60;
    if (pad & PAD_DOWN)  cam_dist += 60;
    if (pad & PAD_L1)    cam_pitch -= 16;
    if (pad & PAD_R1)    cam_pitch += 16;
    if (hit & PAD_CROSS)    walking = !walking;
    if (hit & PAD_CIRCLE)   auto_spin = !auto_spin;
    if (hit & PAD_TRIANGLE) { cam_yaw = 2048 + 420; cam_pitch = 150; cam_dist = 5200; }
    if (hit & PAD_START)    touch_ui = !touch_ui;
    prev_pad = pad;
    if (cam_dist < 2200) cam_dist = 2200;
    if (cam_dist > 9000) cam_dist = 9000;
    if (cam_pitch < -200) cam_pitch = -200;
    if (cam_pitch > 700) cam_pitch = 700;
    if (auto_spin) cam_yaw += 8;

    /* ---- animation */
    walk_w += walking ? 200 : -200;
    if (walk_w < 0) walk_w = 0;
    if (walk_w > 4096) walk_w = 4096;
    if (walk_w) phase += 72;
    animate();

    /* ---- camera: orbit around the chest (Y down: up is negative) */
    mat_rot_x(&rx, cam_pitch); mat_rot_y(&ry, cam_yaw);
    mat_mul(&rx, &ry, &view);
    /* look-at target is the chest at y = -1100; view.t = R * (-target) + (0, 0, dist) */
    tgt.vx = 0; tgt.vy = 1100; tgt.vz = 0;
    view.t[0] = view.t[1] = view.t[2] = 0;
    mat_apply(&view, &tgt, &tv);
    view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz + cam_dist;

    ctx.proj.h = 300; ctx.proj.ofx = SCREEN_W / 2; ctx.proj.ofy = SCREEN_H / 2; ctx.proj.near_z = 160;
    ctx.ot_shift = 4; ctx.cull_sign = 1; ctx.r = ctx.g = ctx.b = 128;

    /* ---- build the frame */
    ot_clear(fb);
    gpu_set_draw_env(0, by, SCREEN_W, SCREEN_H, 0, by, 1);
    draw_background(fb);
    draw_floor(fb, &view, &ctx.proj);
    draw_shadow(fb, &view, &ctx.proj);

    bob = walk_w ? (((icos(phase) < 0 ? -icos(phase) : icos(phase)) * 31 >> 12) - 20) * walk_w >> 12 : 0;
    mat_identity(&model);
    model.t[1] = -bob * 1024 / 1000;
    mat_mul(&view, &model, &root);
    model_draw(&demon, &root, pose, &ctx, fb, 0);

    if (touch_ui) draw_touch_ui(fb);

    /* ---- rasterise (software GPU on Android/PC, hardware GPU on PS1) */
    gpu_draw_ot(fb);
    disp_x = 0; disp_y = by;
    cur ^= 1;
}

int game_ram_used(void)
{
    return (int)(sizeof(frames) + sizeof(demon_verts) + sizeof(demon_prims) + sizeof(demon_bones)
                 + DEMON_TIM_SIZE + sizeof(pose));
}

/* the whole static footprint of the game data must fit the PS1's 2 MB of main RAM */
PSX_STATIC_ASSERT(ram_budget, sizeof(frames) + sizeof(demon_verts) + sizeof(demon_prims) + DEMON_TIM_SIZE < RAM_BUDGET);
