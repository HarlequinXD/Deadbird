/*
 * Deadbird - third-person shooter test level.
 * Title (painted backdrop + menu) -> controls -> play. Sound effects and ambience via snd.h.
 * Pure fixed-point C, no floats, no malloc: PS1-portable.
 */
#include "deadbird.h"
#include "gte.h"
#include "gpu.h"
#include "model.h"
#include "player.h"
#include "level.h"
#include "ui.h"
#include "snd.h"
#include "combat.h"
#include "enemy.h"
#include "ik.h"
#include "fk.h"
#include "hud.h"
#include "fx.h"
#include "title.h"
#include "gte.h"
#undef DEMON_MESH_NO_DATA
#include "demon_mesh.h"
#include "chainsaw_mesh.h"
#include "assets.h"


static FrameBuf frames[2];
static int cur, disp_x, disp_y;
static Model demon, saw_body, saw_chain[SAW_CHAIN_FRAMES];
static int chain_ph;                   /* chainsaw chain: strip frame = chain_ph >> 4 */
static int land_shake_t = 99, land_shake_amp;   /* camera jolt: landings and blade hits */
static Attack saw_attack;                 /* the chainsaw's live hitbox (combat.h) */
static CombatTarget player_tgt;           /* Deadbird as a target for the troopers' lasers */
static int php = 100, hurt_flash, dead_t;  /* her health, red flash when hit, death timer */
static int pad_style;                     /* button names in prompts: 0 PlayStation, 1 Xbox / keyboard */
static int fatal_ready = -1;              /* a reeling trooper close enough to finish */
static u32 keys;                          /* keycards she carries: bit 1 cyan, 2 amber, 3 crimson */
static const char *msg; static int msg_t, msg_col;   /* a line of text on the HUD (pickups, locked doors) */
static int key_flash_t, key_flash_col;    /* the HUD's key slot flashes when a door wants a key she lacks */
/* FATALITY cutscene (vt = cutscene frames in 1/16, slowed in the grind) */
static struct { int active, vt16, ei, base_yaw, title_t, side; s32 cx, cz, dir_x, dir_z, ex, ey, ez; } cine;
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
/* the stick runs / walks by how far it's pushed and swiping the right half turns the camera,
   so the pad only needs: chainsaw attack (big, under the thumb), jump, pause */
const TouchButton touch_buttons[] = {
    { 286, 196, 21, BTN_ATTACK },
    { 238, 214, 15, BTN_JUMP },
    { 300, 22, 11, PAD_START },
    { 252, 140, 24, PAD_TRIANGLE | PAD_SQUARE },          /* FATALITY: only while one is possible */
    { 296, 150, 13, BTN_DODGE },                          /* dodge roll */
};
const int touch_button_count = (int)(sizeof(touch_buttons) / sizeof(touch_buttons[0]));

u32 touch_hit(int x, int y)
{
    int i;
    u32 bits = 0;
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        int dx = x - b->x, dy = y - b->y;
        if (b->button == (PAD_TRIANGLE | PAD_SQUARE) && fatal_ready < 0) continue;
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
void game_set_pad_style(int style) { pad_style = style; }
int  game_rumble(void) { return rumble; }
void game_set_analog(int lx, int ly, int rx, int ry) { an_lx = lx; an_ly = ly; an_rx = rx; (void)ry; }
static int fps_shown;
void game_show_fps(int fps) { fps_shown = fps; }
void game_display_area(int *x, int *y) { *x = disp_x; *y = disp_y; }
int  game_phase(void) { return gs; }
void game_debug_side_cam(int on) { debug_side_cam = on; }
int  game_debug_player_yaw(void) { return pl.yaw; }
/* tests: put Deadbird next to trooper i (facing him), report state */
void game_debug_near_enemy(int i, s32 dist)
{
    Enemy *e = enemy_get(i);
    if (!e) return;
    pl.x = e->x + (isin(e->yaw) * dist >> 12); pl.z = e->z + (icos(e->yaw) * dist >> 12);
    pl.x16 = pl.x * 16; pl.z16 = pl.z * 16; pl.y = e->y; pl.y16 = pl.y * 16;
    pl.yaw = (e->yaw + 2048) & 4095; cam_yaw = pl.yaw;
}
int game_debug_enemy_state(int i) { Enemy *e = enemy_get(i); return e ? e->state * 1000 + e->tgt.hp : -1; }
int game_debug_fatality(void) { return cine.active ? (cine.vt16 >> 4) : (fatal_ready >= 0 ? -2 : -1); }
int game_debug_player_hp(void) { return php; }

static void player_hit(CombatTarget *t, const Hit *h);
static void enemy_sound(int id, int vol, s32 x, s32 z, int pitch);
static void door_locked(int colour);

static void start_play(void)
{
    s32 sx, sz;
    int syaw;
    level_reload_textures();                 /* the title backdrop borrowed the level's pages */
    fx_reset();
    combat_reset();
    attack_end(&saw_attack);
    level_spawn(&sx, &sz, &syaw);
    player_reset(&pl, sx, sz, syaw);
    memset(&player_tgt, 0, sizeof(player_tgt));
    player_tgt.radius = 210; player_tgt.height = 1800; player_tgt.hp = player_tgt.max_hp = 100;
    player_tgt.team = TEAM_PLAYER; player_tgt.on_hit = player_hit;
    player_tgt.x = pl.x; player_tgt.y = pl.y; player_tgt.z = pl.z;
    combat_add(&player_tgt);
    enemy_reset();
    php = 100; hurt_flash = 0; dead_t = 0; fatal_ready = -1;
    keys = 0; msg = 0; msg_t = 0; key_flash_t = 0;
    {
        LevelHooks lh;
        memset(&lh, 0, sizeof(lh));
        lh.sound = enemy_sound; lh.locked = door_locked;
        level_set_hooks(&lh);
    }
    memset(&cine, 0, sizeof(cine));
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
    ui_title_bg_upload();
}

static int pan_of(s32 x, s32 z);
static const char *const KEY_NAME[4] = { "", "CYAN", "AMBER", "CRIMSON" };
static void show_msg(const char *m, int col) { msg = m; msg_t = 150; msg_col = col; }
static void door_locked(int colour)
{
    static char b[32];
    int n = 0;
    const char *k = KEY_NAME[colour & 3], *tail = " KEYCARD NEEDED";
    while (*k && n < 16) b[n++] = *k++;
    while (*tail && n < 31) b[n++] = *tail++;
    b[n] = 0;
    show_msg(b, colour);
    key_flash_t = 60; key_flash_col = colour;
}
static void enemy_sound(int id, int vol, s32 x, s32 z, int pitch)
{
    s32 dx = (x - pl.x) >> 4, dz = (z - pl.z) >> 4, d = (s32)isqrt((u32)(dx * dx + dz * dz)) << 4;
    vol = vol * (d < 2000 ? 4096 : d > 16000 ? 300 : 4096 - (d - 2000) * 3796 / 14000) >> 12;    /* distance */
    snd_play(id, vol, pan_of(x, z), pitch);
}

void game_init(void)
{
    int b;
    gpu_init();
    assets_init();
    demon = asset_demon;
    demon.bones = (const DBONE *)demon_bones; demon.nbones = DEMON_NBONES;
    /* arm chainsaw: body + the chain as 4 one-bone models (one per strip frame), sharing vertices */
    saw_body = asset_saw;
    saw_body.bones = (const DBONE *)saw_bones; saw_body.nbones = 1;
    for (b = 0; b < SAW_CHAIN_FRAMES; b++) {
        saw_chain[b] = saw_body;
        saw_chain[b].bones = (const DBONE *)&saw_bones[1 + b];
    }
    player_init(&demon);
    hud_init();
    ui_init();
    snd_init();
    {
        EnemyHooks hk;
        memset(&hk, 0, sizeof(hk));
        hk.sound = enemy_sound;
        enemy_init(&hk);
    }
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

/* ------------------------------------------------------------------ sound */
static int amb_timer = 120, was_grounded = 1;
/* per combo swing: damage, knockback (1/16 u/frame), stagger frames */
static const int SAW_DMG[3] = { 22, 26, 50 }, SAW_KNOCK[3] = { 28 * 16, 30 * 16, 70 * 16 }, SAW_STAGGER[3] = { 14, 16, 34 };
static s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int rnd(int n) { return (int)(fx_rand() % (u32)n); }

/* pan of a world point as heard from the camera (-4096 left .. 4096 right) */
static int pan_of(s32 x, s32 z)
{
    s32 dx = x - pl.x, dz = z - pl.z, d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    if (d < 64) return 0;
    return (int)((dx * -icos(cam_yaw) + dz * isin(cam_yaw)) / d) * 3 / 4;
}

/* loops and ambience, once per 60 Hz tick */
static void audio_update(void)
{
    int play = gs == GS_PLAY, menu = gs == GS_TITLE || gs == GS_CONTROLS;
    VECTOR tank;
    s32 dx, dz, d;
    int rv;
    /* power-plant hum everywhere; the reactor drone swells near the great tank */
    level_tank(&tank);
    dx = (tank.vx - pl.x) >> 4; dz = (tank.vz - pl.z) >> 4;
    d = (s32)isqrt((u32)(dx * dx + dz * dz)) << 4;
    rv = (int)clamp(3400 - d * 3400 / 15000, 500, 3400);
    snd_loop(0, SFX_AMB_HUM, play ? 1500 : menu ? 1100 : 700, 0, 4096);
    snd_loop(1, SFX_AMB_REACTOR, play ? rv : menu ? 1700 : 600, play ? pan_of(tank.vx, tank.vz) : 0, menu ? 3500 : 4096);
    /* her chainsaw: one quiet idle loop. Running opens the throttle a little (louder, higher);
       an attack builds the revs up while she swings, then they settle back down to the idle */
    {
        static int throttle;                                   /* 0..4096 */
        int want = play ? (pl.runw * 1400 >> 12) + (pl.speed * 3) : 0;
        if (play && pl.atk) want = 2600 + (pl.atk_w * 1400 >> 12);
        if (want > 4096) want = 4096;
        throttle += want > throttle ? (want - throttle) / 10 + 1 : (want - throttle) / 24 - (want < throttle);
        if (!play) throttle = 0;
        snd_loop(2, SFX_SAW_IDLE, play ? 1200 + (throttle * 1500 >> 12) : 0, 400, 4096 + (throttle * 2600 >> 12));
        snd_loop(3, SFX_SAW_REV, 0, 400, 4096);                /* the loud rev loop is no longer used */
    }
    /* distant plant noises and the moans of captive shinigami */
    if (gs != GS_PAUSE && --amb_timer <= 0) {
        int r = rnd(100), id = r < 36 ? SFX_AMB_STEAM : r < 68 ? SFX_AMB_CLANK : r < 90 ? SFX_AMB_WAIL : SFX_AMB_ALARM;
        if (menu && id == SFX_AMB_STEAM) id = SFX_AMB_WAIL;
        snd_play(id, 1300 + rnd(1500), rnd(6001) - 3000, 3500 + rnd(1200));
        amb_timer = 150 + rnd(360);
    }
}

/* blade from the clamp to the nose, in world space (from the fist bone the saw is strapped to) */
static void saw_blade(VECTOR *base, VECTOR *tip)
{
    SVECTOR s;
    s.vx = (s16)(DEMON_MOUNT_X + DEMON_MOUNT_DX * 90 / 4096); s.vy = (s16)DEMON_MOUNT_Y;
    s.vz = (s16)(DEMON_MOUNT_Z + DEMON_MOUNT_DZ * 90 / 4096);
    mat_apply(&pl.world[DEMON_MOUNT_BONE], &s, base);
    s.vx = (s16)(DEMON_MOUNT_X + DEMON_MOUNT_DX * 480 / 4096); s.vz = (s16)(DEMON_MOUNT_Z + DEMON_MOUNT_DZ * 480 / 4096);
    mat_apply(&pl.world[DEMON_MOUNT_BONE], &s, tip);
}

/* the chainsaw against the world and anything that can be hurt */
static void saw_combat(void)
{
    CombatEvent ev[4];
    VECTOR base, tip;
    int n, i;
    if (pl.atk_event >= 1 && pl.atk_event <= 3) {
        int m = pl.atk_event - 1;
        attack_begin(&saw_attack, TEAM_PLAYER, pl.atk_event, SAW_DMG[m], SAW_KNOCK[m], SAW_STAGGER[m], 70);
    }
    if (!pl.atk) { attack_end(&saw_attack); return; }
    if (!pl.atk_active) return;
    saw_blade(&base, &tip);
    saw_attack.ox = pl.x; saw_attack.oz = pl.z;
    n = attack_update(&saw_attack, &base, &tip, ev, 4);
    for (i = 0; i < n; i++) {
        const CombatEvent *e = &ev[i];
        int pan = pan_of(e->at.vx, e->at.vz);
        if (e->kind == COMBAT_HIT_TARGET) {                       /* flesh: blood, the engine bogs down */
            fx_hit(&e->at, saw_attack.damage, 0);
            snd_play(SFX_RIP, 4096, pan, 3900 + rnd(400));
            player_hit_react(&pl, 0, 0, 0, 5);
            land_shake_t = 0; land_shake_amp = 70 + saw_attack.move * 10;    /* jolt, like a landing */
            rumble = 200;
        } else if (e->kind == COMBAT_HIT_FLOOR && saw_attack.move == 3) {   /* the slam bites the floor */
            fx_hit(&e->at, 70, 1); fx_hit(&e->at, 70, 1);
            snd_play(SFX_SLAM, 4096, pan, 4096);
            snd_play(SFX_GRIND, 3000, pan, 3300);
            player_hit_react(&pl, 0, 0, 0, 4);
            land_shake_t = 0; land_shake_amp = 130;
            rumble = 240;
        } else {                                                  /* steel: sparks, screech, the blade kicks back */
            fx_hit(&e->at, 50, 1);
            snd_play(SFX_GRIND, 3800, pan, 3800 + rnd(600));
            player_hit_react(&pl, e->nrm.vx, e->nrm.vz, e->nrm.vy ? 0 : 18 * 16, 4);
            land_shake_t = 0; land_shake_amp = 75;                          /* jolt, like a landing */
            rumble = 150;
        }
    }
}

/* a laser bolt reached her */
static void player_hit(CombatTarget *t, const Hit *h)
{
    (void)t;
    if (cine.active || php <= 0 || player_invulnerable(&pl)) return;   /* rolled under it */
    php -= h->damage;
    hurt_flash = 12;
    fx_spray(&h->at, 4, 6, h->dir_x * 10 >> 12, -10, h->dir_z * 10 >> 12, 16);
    if (land_shake_t >= 12) { land_shake_t = 6; land_shake_amp = 45; }
    rumble = 180;
    if (php <= 0) { php = 0; dead_t = 1; snd_play(SFX_GORE, 4096, 0, 3000); }
}

/* ------------------------------------------------------------------ FATALITY */
static void cine_start(int ei)
{
    Enemy *e = enemy_get(ei);
    s32 dx = e->x - pl.x, dz = e->z - pl.z, d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    if (d < 64) { dx = isin(pl.yaw); dz = icos(pl.yaw); d = 4096; }
    memset(&cine, 0, sizeof(cine));
    cine.active = 1; cine.ei = ei;
    cine.dir_x = dx * 4096 / d; cine.dir_z = dz * 4096 / d;
    /* she squares up 0.95 m in front of him */
    pl.x = e->x - (cine.dir_x * 950 >> 12); pl.z = e->z - (cine.dir_z * 950 >> 12);
    pl.x16 = pl.x * 16; pl.z16 = pl.z * 16;
    pl.yaw = player_input_angle(cine.dir_x, cine.dir_z) & 4095;
    cine.base_yaw = pl.yaw;
    cine.cx = (pl.x + e->x) / 2; cine.cz = (pl.z + e->z) / 2;
    enemy_fatal_begin(e, pl.yaw + 2048);
    {   /* film from whichever side of the pair has more room */
        VECTOR c, a;
        int sd, best = -1;
        c.vx = cine.cx; c.vy = pl.y - 1100; c.vz = cine.cz;
        cine.side = 1;
        for (sd = -1; sd <= 1; sd += 2) {
            int look = (cine.base_yaw + sd * 1024) & 4095, t;
            a.vx = cine.cx - (isin(look) * 3000 >> 12); a.vy = c.vy - 900; a.vz = cine.cz - (icos(look) * 3000 >> 12);
            t = level_clip_segment(&c, &a, 120);
            if (t > best) { best = t; cine.side = sd; }
        }
    }
    attack_end(&saw_attack);
    fatal_ready = -1;
    snd_play(SFX_SWING, 4096, 0, 3000);
}

/* her swing frame, his reaction and the effects for cutscene frame f (called once per frame) */
static void cine_frame(int f)
{
    Enemy *e = enemy_get(cine.ei);
    VECTOR w, at;
    int wgt = 4096;
    if (f > 146) { wgt = 4096 - (f - 146) * 64; if (wgt < 0) wgt = 0; }
    /* her FATALITY clip (clip_fatal: step in, grab, coil, drive, grind, rip, follow through) */
    player_script(&pl, wgt ? 4 : 0, f < 160 ? f : 160, wgt);
    pl.shake = f >= 33 && f < 80 ? 2600 + (isin(f * 300) * 900 >> 12) : (f >= 80 && f < 86 ? 3600 : 0);   /* straining */
    player_pose(&pl, &demon);
    if (f < 84) enemy_fatal_step(e, f >= 32 ? 4096 : 0);
    else if (f == 84) {
        enemy_cut(e, cine.dir_x, cine.dir_z);
        snd_play(SFX_GORE, 4096, 0, 3600); snd_play(SFX_SLAM, 3400, 0, 4600);
        land_shake_t = 0; land_shake_amp = 150; rumble = 255;
        enemy_waist(e, &w);
        fx_spray(&w, 4, 40, cine.dir_x * 20 >> 12, -26, cine.dir_z * 20 >> 12, 34);
        fx_spray(&w, 0, 20, 0, -12, 0, 30);
    } else enemy_cut_step(e);
    if (f >= 32 && f < 84) {                           /* blood and sparks off the chain */
        enemy_waist(e, &w);
        at.vx = w.vx - (cine.dir_x * 140 >> 12); at.vy = w.vy; at.vz = w.vz - (cine.dir_z * 140 >> 12);
        fx_spray(&at, 4, 5, (-cine.dir_z * 18 >> 12) + (rnd(17) - 8), -14 - rnd(10), (cine.dir_x * 18 >> 12) + (rnd(17) - 8), 16);
        fx_spray(&at, 4, 3, (cine.dir_z * 18 >> 12) + (rnd(17) - 8), -10 - rnd(10), (-cine.dir_x * 18 >> 12) + (rnd(17) - 8), 16);
        fx_spray(&at, 1, 3, 0, -6, 0, 22);
        if ((f & 7) == 0) fx_spray(&at, 2, 4, 0, -10, 0, 30);
        if (f == 32) {
            snd_play(SFX_GORE, 4096, 0, 4096); snd_play(SFX_RIP, 4096, 0, 3600);
            land_shake_t = 0; land_shake_amp = 90; rumble = 220;
            fx_spray(&at, 4, 24, -cine.dir_x * 16 >> 12, -20, -cine.dir_z * 16 >> 12, 30);
            fx_blood_pool(e->x, e->y, e->z, 700);
        } else if (f % 12 == 0) snd_play(SFX_RIP, 3400, 0, 3300 + rnd(500));
        rumble = 120;
    }
    if (f == 120) { snd_play(SFX_STINGER, 4096, 0, 4096); cine.title_t = 1; }
    fx_update();
}

static void cine_step(void)
{
    int f0 = cine.vt16 >> 4, f;
    int slow = (f0 >= 32 && f0 < 80) ? 5 : 16;        /* slow motion while the saw grinds */
    cine.vt16 += slow;
    for (f = f0 + 1; f <= (cine.vt16 >> 4); f++) cine_frame(f);
    if (cine.title_t) cine.title_t++;
    chain_ph += 26;
    if (land_shake_t < 99) land_shake_t++;
    level_update();
    if ((cine.vt16 >> 4) >= 236) {                     /* back to the game */
        cine.active = 0;
        player_script(&pl, 0, 0, 0);
        pl.atk = 0; pl.atk_w = 0; pl.shake = 0;
        cam_yaw = pl.yaw;
    }
}

static s32 iabs(s32 v) { return v < 0 ? -v : v; }

static void simulate(u32 pad)
{
    int myaw, mmag;
    if (dead_t) { pad = 0; if (++dead_t > 150) { start_play(); return; } }
    camera_update(pad);
    read_move(pad, &myaw, &mmag);
    {   /* auto-targeting: the nearest trooper within reach, roughly where she's facing or the stick
           points, is locked; a chainsaw press turns her onto him and each swing's lunge is sized to
           land the chain on him */
        int i, ref = mmag > 300 ? myaw : pl.yaw, best_a = 0;
        s32 best = 0x7fffffff, best_d = 0;
        for (i = 0; i < enemy_count(); i++) {
            const Enemy *e = enemy_get(i);
            s32 dx = e->x - pl.x, dz = e->z - pl.z, d;
            int a, diff;
            if (e->state == E_DEAD || e->state == E_CUT || e->state == E_FATAL) continue;
            if (iabs(e->y - pl.y) > 900 || iabs(dx) > 4200 || iabs(dz) > 4200) continue;
            d = (s32)isqrt((u32)((dx >> 2) * (dx >> 2) + (dz >> 2) * (dz >> 2))) << 2;
            if (d > 4200) continue;
            a = player_input_angle(dx, dz) & 4095;
            diff = ((a - ref + 2048) & 4095) - 2048;
            if (diff < 0) diff = -diff;
            if (diff > 1400) continue;
            if (d + diff * 2 < best) { best = d + diff * 2; best_a = a; best_d = d; }
        }
        if (best_d) {
            pl.lock_yaw = best_a; pl.lock_dist = best_d;
            if (pad & ~pl.prev & BTN_ATTACK) { myaw = best_a; mmag = 4096; }
        } else pl.lock_dist = 0;
    }
    level_think(pl.x, pl.y, pl.z, keys);               /* doors and lifts move first: she rides them */
    player_update(&pl, pad, myaw, mmag);
    if (was_grounded && !pl.grounded && pl.vy < -600) snd_play(SFX_JUMP, 3000, 0, 3800 + rnd(600));
    was_grounded = pl.grounded;
    if (pl.atk_event >= 1 && pl.atk_event <= 3)                               /* swing: whoosh + the saw screams */
        snd_play(SFX_SWING, 3800, 300, pl.atk_event == 3 ? 3400 : 3900 + rnd(500));
    if (pl.roll_t == 5) snd_play(SFX_STEP_RUN, 3000, -600, 3600);             /* dodge roll: the plant ... */
    if (pl.roll_t == 10 || pl.roll_t == 17 || pl.roll_t == 31) {              /* ... shoulder, back, hooves hit the floor */
        snd_play(SFX_LAND, pl.roll_t == 10 ? 2800 : pl.roll_t == 17 ? 1900 : 2400, 0, pl.roll_t == 31 ? 4600 : 5400 + rnd(400));
        if (land_shake_t >= 12) { land_shake_t = 6; land_shake_amp = pl.roll_t == 17 ? 22 : 38; }
    }
    if (pl.pivot_t == PIVOT_FRAMES - 1) snd_play(SFX_STEP_RUN, 3600, 0, 3000);   /* hooves dig in to turn round */
    if (pl.land_t == 1)                                                       /* boom, scaled by the fall */
        snd_play(SFX_LAND, (int)clamp(pl.land_amp, 1300, 4096), 0, 4500 - pl.land_amp / 6);
    if (pl.land_t == 1 && pl.land_amp > 1500) rumble = pl.land_amp >> 4;      /* thump on a hard landing */
    if (pl.land_t == 1 && pl.land_amp > 1000) {                               /* screen shake on landing */
        land_shake_t = 0;
        land_shake_amp = pl.land_amp * 110 >> 12;                             /* up to ~11 cm of camera jolt */
    } else if (land_shake_t < 99) land_shake_t++;
    chain_ph += pl.atk ? 22 : 5 + pl.speed / 6;
    {   /* heavy footfalls when she runs: a small camera thud each time a foot hits the floor */
        static int prev_half;
        int half = (pl.phase >> 15) & 1;
        if (half != prev_half && pl.grounded && pl.runw > 1500 && land_shake_t >= 22) {
            land_shake_t = 9;
            land_shake_amp = 22 + (pl.runw * 26 >> 12);
        }
        if (half != prev_half && pl.grounded && pl.amp > 1200 && pl.land_t > 4)  /* hoof on the grating */
            snd_play(pl.runw > 2048 ? SFX_STEP_RUN : SFX_STEP, 1700 + (pl.amp >> 3) + (pl.runw >> 2),
                     half ? 900 : -900, 3700 + rnd(700));
        prev_half = half;
    }                                             /* chain idles, revs when she runs */
    player_pose(&pl, &demon);
    {   /* pickups: keycards and soul shards */
        int arg, k = level_pickup(pl.x, pl.y, pl.z, &arg);
        if (k == 2) {
            static char b[24];
            int n = 0;
            const char *nm = KEY_NAME[arg & 3], *tail = " KEYCARD";
            while (*nm) b[n++] = *nm++;
            while (*tail) b[n++] = *tail++;
            b[n] = 0;
            keys |= 1u << (arg & 3);
            show_msg(b, arg & 3);
            snd_play(SFX_UI_OK, 4096, 0, 3200);
        } else if (k == 3) {
            php = php + 25 > 100 ? 100 : php + 25;
            show_msg("SOUL SHARD", 0);
            snd_play(SFX_UI_OK, 3600, 0, 5200);
        }
        if (msg_t > 0) msg_t--;
        if (key_flash_t > 0) key_flash_t--;
    }
    player_tgt.x = pl.x; player_tgt.y = pl.y; player_tgt.z = pl.z;
    enemy_update(&pl, 0);
    saw_combat();
    fatal_ready = php > 0 ? enemy_fatality_target(&pl) : -1;
    if (fatal_ready >= 0 && (pad & PAD_TRIANGLE) && (pad & PAD_SQUARE) && pl.grounded) cine_start(fatal_ready);
    if (hurt_flash > 0) hurt_flash--;
    level_update();
    fx_update();
    combat_update();
}

static void flow(u32 pad, u32 press)
{
    gs_t++;
    switch (gs) {
    case GS_TITLE: {
        int c = title_update(pad, press);
        if (c == MENU_PLAY) start_play();
        else if (c == MENU_CONTROLS) { gs = GS_CONTROLS; gs_t = 0; }
        break;
    }
    case GS_CONTROLS:
        if (gs_t > 10 && (press & (PAD_CROSS | PAD_START | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE))) {
            gs = GS_TITLE; gs_t = 0; title_reset(); snd_play(SFX_UI_MOVE, 3000, 0, 3600);
        }
        break;
    case GS_PLAY:
        if ((press & PAD_START) && !cine.active) { gs = GS_PAUSE; gs_t = 0; snd_play(SFX_PAUSE, 3600, 0, 4096); break; }
        if (cine.active) cine_step();
        else simulate(pad);
        break;
    case GS_PAUSE:
        if (press & PAD_START) { gs = GS_PLAY; gs_t = 0; snd_play(SFX_PAUSE, 2600, 0, 5200); }
        if (press & (PAD_SELECT | BTN_JUMP)) { snd_play(SFX_UI_OK, 3600, 0, 3400); go_title(); }
        break;
    }
}

/* ------------------------------------------------------------------ rendering */
static void draw_touch(FrameBuf *fb)
{
    static const int icon_of[] = { UI_ICON_SAW, UI_ICON_JUMP, UI_ICON_PAUSE, 0, UI_ICON_SPRINT };
    int i;
    /* floating stick: a runic iron ring where the thumb landed, the demon eye follows the thumb */
    if (stick_on) {
        s32 dx = stick_x - stick_ox, dy = stick_y - stick_oy, len = (s32)isqrt((u32)(dx * dx + dy * dy));
        if (len > STICK_R) { dx = dx * STICK_R / len; dy = dy * STICK_R / len; }
        ui_stick(fb, stick_ox, stick_oy, stick_ox + dx, stick_oy + dy, STICK_R, 1, frame_count);
    } else {
        ui_stick(fb, 52, 184, 52, 184, STICK_R, 0, frame_count);
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        if (b->button == (PAD_TRIANGLE | PAD_SQUARE)) {              /* FATALITY sigil, pulsing */
            int g;
            if (fatal_ready < 0) continue;
            g = 110 + (isin(frame_count * 140) * 70 >> 12);
            ui_sprite_rot(fb, b->x, b->y, b->r * 3, 64, 144, 64, 64, frame_count * 20, g, g / 5, g / 8, 1, 1);
            ui_sprite(fb, b->x - b->r * 2, b->y - b->r * 2, b->r * 4, b->r * 4, 32, 144, 32, 32, g, g / 3, g / 5, 1, 0, 2);
            text_center(fb, "FATALITY", b->x + 1, b->y - 4, 1, 255, 0);
            continue;
        }
        ui_button(fb, b->x, b->y, b->r, icon_of[i], (cur_pad & b->button) != 0, frame_count);
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

/* cutscene camera: three shots with hard cuts (over her shoulder, low close-up of the grind,
   wide as the halves fall) that drift slowly; returns the view matrix */
static void cine_view(MATRIX *view, int sx, int sy)
{
    int f = cine.vt16 >> 4, look, k;
    s32 R_, H, ty, ex, ey, ez;
    MATRIX rx, ry;
    SVECTOR e; VECTOR tv;
    s32 cy = pl.y - 1050;
    int sd = cine.side;
    if (f < 32)      { look = cine.base_yaw + sd * (700 + f * 4); R_ = 2700 - f * 10; H = 1000; ty = cy; }
    else if (f < 84) { look = cine.base_yaw + sd * (1024 + (f - 32) * 5); R_ = 1500 - (f - 32) * 4; H = 380; ty = cy - 80; }
    else             { look = cine.base_yaw + sd * (1350 + (f - 84) * 3); R_ = 3300 + (f - 84) * 4; H = 1800; ty = cy + 450; }
    look &= 4095;
    {   /* never through a wall: pull in to whatever is in the way */
        VECTOR c, a;
        int t;
        c.vx = cine.cx; c.vy = ty; c.vz = cine.cz;
        a.vx = cine.cx - (isin(look) * R_ >> 12); a.vy = ty - H; a.vz = cine.cz - (icos(look) * R_ >> 12);
        t = level_clip_segment(&c, &a, 140);
        if (t < 4096) { R_ = R_ * t / 4096 - 120; H = H * t / 4096; if (R_ < 500) R_ = 500; }
    }
    ex = cine.cx - (isin(look) * R_ >> 12); ez = cine.cz - (icos(look) * R_ >> 12); ey = ty - H;
    /* ease within a shot */
    if (f == 0 || f == 32 || f == 84 || !cine.ex) { cine.ex = ex; cine.ey = ey; cine.ez = ez; }
    else { cine.ex += (ex - cine.ex) / 5; cine.ey += (ey - cine.ey) / 5; cine.ez += (ez - cine.ez) / 5; }
    k = player_input_angle(ty - cine.ey, R_);
    mat_rot_x(&rx, k); mat_rot_y(&ry, -look); mat_mul(&rx, &ry, view);
    view->t[0] = view->t[1] = view->t[2] = 0;
    e.vx = (s16)-cine.ex; e.vy = (s16)-cine.ey; e.vz = (s16)-cine.ez;
    mat_apply(view, &e, &tv);
    view->t[0] = tv.vx + sx; view->t[1] = tv.vy + sy; view->t[2] = tv.vz;
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
    if (land_shake_t < 22) {
        /* landing jolt: camera kicks down then rings out, with a little sideways rattle */
        int k = 22 - land_shake_t;                                            /* 22 .. 1 */
        s32 a = land_shake_amp * k * k / (22 * 22);
        sy += (int)(a * icos(land_shake_t * 600) >> 12);
        sx += (int)((a / 3) * isin(land_shake_t * 1500) >> 12);
    }

    if (cine.active) { cine_view(&view, sx, sy); goto have_view; }
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
have_view:

    pr->h = 250; pr->ofx = SCREEN_W / 2; pr->ofy = SCREEN_H / 2; pr->near_z = 120;
    ctx.proj = *pr; ctx.ot_shift = 4; ctx.cull_sign = 1; ctx.r = ctx.g = ctx.b = 128;

    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(3, 4, 7), RGB(1, 1, 3), NO_SEMI, OT_LEN - 1);
    level_draw(fb, &view, pr, frame_count);
    draw_captive(fb, &view, &ctx);
    blob_shadow(fb, &view, pr, pl.x, level_ground(pl.x, pl.z, pl.y), pl.z);
    for (b = 0; b < DEMON_NBONES; b++) mat_mul(&view, &pl.world[b], &cam[b]);
    model_draw_mats(&demon, cam, &ctx, fb);
    {   /* chainsaw strapped to her right fist */
        MATRIX mount, sawm;
        mat_identity(&mount);
        mat_translate(&mount, DEMON_MOUNT_X, DEMON_MOUNT_Y, DEMON_MOUNT_Z);
        mat_mul(&cam[DEMON_MOUNT_BONE], &mount, &sawm);
        model_draw_mats(&saw_body, &sawm, &ctx, fb);
        model_draw_mats(&saw_chain[(chain_ph >> 4) & (SAW_CHAIN_FRAMES - 1)], &sawm, &ctx, fb);
    }
    enemy_draw(fb, &view, &ctx);
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
    {   /* health: a blood bar under the name plate */
        int w = php * 112 / 100;
        quad_g(fb, 8, 30, 124, 37, RGB(14, 4, 6), RGB(14, 4, 6), NO_SEMI, 6);
        if (w > 0) quad_g(fb, 10, 31, 10 + w, 36, RGB(230, 40, 26), RGB(110, 8, 8), NO_SEMI, 5);
    }
    if (pl.speed > 40) text(fb, "RUN", 96, 12, 1, 120, 4);
    {   /* keycard slots under the health bar: filled when carried, flashing when a door wants one */
        static const u32 KC[4] = { 0, RGB(40, 200, 240), RGB(250, 160, 30), RGB(230, 30, 50) };
        int c;
        for (c = 1; c <= 3; c++) {
            int x = 10 + (c - 1) * 16, have = (keys >> c) & 1, flash = key_flash_t && key_flash_col == c && ((key_flash_t >> 3) & 1);
            quad_g(fb, x, 41, x + 12, 49, RGB(20, 6, 8), RGB(20, 6, 8), NO_SEMI, 6);
            if (have || flash) quad_g(fb, x + 1, 42, x + 11, 48, KC[c], flash && !have ? RGB(60, 10, 10) : KC[c] >> 1 & 0x7f7f7f, NO_SEMI, 5);
        }
    }
    if (msg && msg_t > 0) {
        if (msg_t > 20 || (msg_t >> 2) & 1) text_center(fb, msg, 160, 172, 1, msg_col ? 230 : 200, 4);
    }
    if (fps_shown > 0) {
        char b[12];
        b[0] = 'F'; b[1] = 'P'; b[2] = 'S'; b[3] = ' ';
        b[4] = (char)('0' + fps_shown / 10 % 10); b[5] = (char)('0' + fps_shown % 10); b[6] = 0;
        text(fb, b, 262, 12, 1, 200, 4);
    }
}

static void draw_controls(FrameBuf *fb)
{
    static const char *lines[][2] = {
        { "LEFT THUMB",   "STICK - PUSH FAR TO RUN" },
        { "CHAINSAW",     "ATTACK, TAP AGAIN: COMBO" },
        { "WINGS",        "JUMP" },
        { "",             "SLASH, BACKHAND, SLAM" },
        { "RIGHT THUMB",  "DRAG TO TURN THE CAMERA" },
        { "",             "CAMERA FOLLOWS YOU" },
        { "BONES",        "PAUSE" },
    };
    static const char *pad_lines[][2] = {               /* Xbox controller / keyboard (PC) */
        { "LEFT STICK",   "MOVE - PUSH FAR TO RUN" },
        { "X",            "CHAINSAW, AGAIN: COMBO" },
        { "A",            "JUMP" },
        { "RIGHT STICK",  "TURN THE CAMERA" },
        { "LB  RB",       "SWING THE CAMERA" },
        { "KEYS",         "WASD SPACE F SHIFT Q E" },
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

void game_step(u32 pad)
{
    u32 press = pad & ~prev_pad;
    cur_pad = pad;
    prev_pad = pad;
    frame_count++;
    rumble = rumble * 3 / 4;
    flow(pad, press);
    audio_update();
}

void game_frame(u32 pad)
{
    FrameBuf *fb = &frames[cur];
    int by = cur * SCREEN_H;
    PROJ pr;

    game_step(pad);

    ot_clear(fb);
    gpu_set_draw_env(0, by, SCREEN_W, SCREEN_H, 0, by, 1);
    if (gs == GS_TITLE) {
        title_draw(fb, frame_count);
    } else if (gs == GS_CONTROLS) {
        ui_title_bg(fb, 60, OT_LEN - 1);
        embers_draw(fb, frame_count, 30);
        draw_controls(fb);
    } else {
        draw_world(fb, &pr);
    }
    if (cine.active) {
        /* letterbox, a red flash on the cut, then FATALITY */
        int f = cine.vt16 >> 4;
        quad_g(fb, 0, 0, 320, 26, RGB(0, 0, 0), RGB(0, 0, 0), NO_SEMI, 1);
        quad_g(fb, 0, 214, 320, 240, RGB(0, 0, 0), RGB(0, 0, 0), NO_SEMI, 1);
        if (f >= 84 && f < 92) { int a = (92 - f) * 9; quad_g(fb, 0, 26, 320, 214, RGB(a, a / 10, a / 12), RGB(a, a / 10, a / 12), SEMI_ADD, 3); }
        if (f >= 32 && f < 34) quad_g(fb, 0, 26, 320, 214, RGB(70, 6, 4), RGB(70, 6, 4), SEMI_ADD, 3);
        if (cine.title_t) {
            /* FATALITY slams in large, settles, glows */
            int sc = cine.title_t < 3 ? 6 : cine.title_t < 6 ? 5 : 4, y = 96 - sc * 5;
            quad_g(fb, 0, y - 10, 320, y + sc * 10 + 10, RGB(0, 0, 0), RGB(40, 2, 2), SEMI_SUB, 3);
            quad_g(fb, 0, y - 12, 320, y - 10, RGB(200, 30, 10), RGB(200, 30, 10), NO_SEMI, 1);
            quad_g(fb, 0, y + sc * 10 + 10, 320, y + sc * 10 + 12, RGB(200, 30, 10), RGB(200, 30, 10), NO_SEMI, 1);
            text_hell(fb, "FATALITY", 160, y, sc, frame_count, 1);
        }
    } else if (gs != GS_TITLE && gs != GS_CONTROLS) {
        draw_hud(fb);
        if (hurt_flash > 0) quad_g(fb, 0, 0, 320, 240, RGB(hurt_flash * 9, 0, 0), RGB(hurt_flash * 5, 0, 0), SEMI_ADD, 3);
        if (dead_t) {
            quad_g(fb, 0, 0, 320, 240, RGB(dead_t, dead_t + 10, dead_t + 10), RGB(dead_t, dead_t + 10, dead_t + 10), SEMI_SUB, 3);
            text_hell(fb, "YOU DIED", 160, 100, 3, frame_count, 1);
        }
        if (fatal_ready >= 0 && !dead_t && gs == GS_PLAY) {             /* the prompt */
            static const char *how[3] = { "TRIANGLE + SQUARE", "Y + X    KEYS R + F", "TAP THE SIGIL" };
            text_hell(fb, "FATALITY", 160, 44, 2, frame_count, 1);
            text_center(fb, how[touch_ui ? 2 : pad_style], 160, 68, 1, 150 + (isin(frame_count * 160) * 100 >> 12), 1);
        }
        if (gs == GS_PAUSE) {
            quad_g(fb, 0, 0, 320, 240, RGB(90, 100, 100), RGB(90, 100, 100), SEMI_SUB, 4);
            text_hell(fb, "PAUSED", 160, 80, 3, frame_count, 2);
            ui_plate(fb, 160, 118, 200, 1, frame_count, 8);
            ui_plate(fb, 160, 156, 200, 0, frame_count, 8);
            text_center(fb, touch_ui ? "BONES  RESUME" : "START  RESUME", 161, 129, 1, 255, 2);
            text_center(fb, touch_ui ? "WINGS  QUIT TO TITLE" : "A  QUIT TO TITLE", 161, 167, 1, 170, 2);
        }
        if (touch_ui) draw_touch(fb);
    }

    gpu_draw_ot(fb);
    disp_x = 0; disp_y = by;
    cur ^= 1;
}

int game_ram_used(void)
{
    return (int)(sizeof(frames) + sizeof(pl) + sizeof(DVERT) * (size_t)demon.nverts + sizeof(DPRIM) * (size_t)demon.nprims
                 + sizeof(demon_bones));
}

PSX_STATIC_ASSERT(ram_budget, sizeof(frames) + sizeof(pl) < RAM_BUDGET);
