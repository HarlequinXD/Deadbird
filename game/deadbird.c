/*
 * Deadbird - third-person shooter test level.
 * Title (painted backdrop + menu) -> controls -> play. Sound effects and ambience via snd.h.
 * Pure fixed-point C, no floats, no malloc: PS1-portable.
 */
#include "deadbird.h"
#include "engine.h"
#include "gte.h"
#include "gpu.h"
#include "model.h"
#include "player.h"
#include "level.h"
#include "ui.h"
#include "snd.h"
#include "sfx_ids.h"
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
#ifndef GEN_BAKED
#include "editor.h"
#include "gen.h"                      /* gen_sfx: the sounds, synthesised at start-up */
#else
#include "sfx_adpcm.h"                /* the PS1: the same sounds, baked as SPU ADPCM */
#endif


static Model demon, saw_body, saw_chain[SAW_CHAIN_FRAMES];
static int chain_ph;                   /* chainsaw chain: strip frame = chain_ph >> 4 */
static int land_shake_t = 99, land_shake_amp;   /* camera jolt: landings and blade hits */
static Attack saw_attack;                 /* the chainsaw's live hitbox (combat.h) */
static CombatTarget player_tgt;           /* Deadbird as a target for the troopers' lasers */
static int php = 100, hurt_flash, dead_t;  /* her health, red flash when hit, death timer */
static int pad_style;                     /* button names in prompts: 0 PlayStation, 1 Xbox / keyboard */
static int fatal_ready = -1;              /* a reeling trooper close enough to finish */
static u32 keys;                          /* keycards she carries: bit 1 cyan, 2 amber, 3 crimson */
static int lock_ei = -1;                  /* the trooper her combo is committed to */
static s32 iabs(s32 v) { return v < 0 ? -v : v; }
#define LOCK_RANGE 5200                   /* how far a chainsaw press reaches for a trooper */
#define LOCK_KEEP  7000                   /* a committed lock breaks past this */
static const char *msg; static int msg_t, msg_col;   /* a line of text on the HUD (pickups, locked doors) */
static int key_flash_t, key_flash_col;    /* the HUD's key slot flashes when a door wants a key she lacks */
/* FATALITY cutscene (vt = cutscene frames in 1/16, slowed in the grind) */
static struct { int active, vt16, ei, base_yaw, title_t, side; s32 cx, cz, dir_x, dir_z, ex, ey, ez; } cine;   /* ei < 0: the reactor */
static int play_frames, end_kills, end_troopers, end_secrets, end_secrets_n;   /* the level's clock and the score screen */
static s32 exit_x, exit_z; static int has_exit;
static Player pl;
static int gs, gs_t, frame_count, shake, rumble;
static s32 cam_yaw, cam_dist, cam_h, cam_side, cam_clip = 2900;
static s32 cam_fy; static int cam_fy_ok;          /* the height the camera follows: her height, smoothed (stairs don't jolt it) */
static u32 prev_pad, cur_pad;
static int stick_on, stick_ox, stick_oy, stick_x, stick_y;   /* floating touch stick (screen px) */
static int an_lx, an_ly, an_rx, an_ry;                       /* gamepad sticks, -4096..4096 */
static int playtest;                      /* playing a level from the editor: back to it after */
static int custom_map;                    /* the level built is the editor's, not level 1 */
#define STICK_R 38
static int debug_side_cam;
static int touch_ui;
static int can_use;                             /* something in reach answers A (the touch USE button shows) */
static int pause_sel, pause_tap = -1;          /* the pause menu: 0 resume, 1 quit / back to the editor */

/* ------------------------------------------------------------------ touch layout */
/* the stick runs / walks by how far it's pushed and swiping the right half turns the camera,
   so the pad only needs: chainsaw attack (big, under the thumb), jump, pause */
typedef struct { s16 x, y, r; u32 button; } TouchButton;
static TouchButton touch_buttons[] = {                        /* placed by touch_layout */
    { 0, 0, 25, BTN_ATTACK },
    { 0, 0, 17, BTN_JUMP },
    { 0, 0, 12, PAD_START },
    { 0, 0, 22, BTN_USE },                                 /* USE: doors, lifts; the FATALITY sigil while one is possible */
    { 0, 0, 15, BTN_DODGE },                               /* dodge roll */
};
static const int touch_button_count = (int)(sizeof(touch_buttons) / sizeof(touch_buttons[0]));
/* thumb-sized and anchored to the screen's corners, however wide the frame is: the chainsaw
   under the right thumb, jump and dodge round it, pause in the top corner */
static void touch_layout(void)
{
    int W = SCREEN_W;
    touch_buttons[0].x = (s16)(W - 52);  touch_buttons[0].y = 186;
    touch_buttons[1].x = (s16)(W - 112); touch_buttons[1].y = 212;
    touch_buttons[2].x = (s16)(W - 22);  touch_buttons[2].y = 22;
    touch_buttons[3].x = (s16)(W - 118); touch_buttons[3].y = 134;
    touch_buttons[4].x = (s16)(W - 42);  touch_buttons[4].y = 124;
}
#define STICK_HOME_X 74
#define STICK_HOME_Y 172

static u32 touch_hit(int x, int y)
{
    int i;
    u32 bits = 0;
    touch_layout();
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        int dx = x - b->x, dy = y - b->y;
        if (b->button == BTN_USE && !can_use) continue;           /* hidden: nothing to use */
        if (dx * dx + dy * dy <= (b->r + 7) * (b->r + 7)) bits |= b->button;
    }
    return bits;
}

static void touch_look(int dx) { if (gs == GS_PLAY) { cam_yaw = (cam_yaw + dx * 12) & 4095; } }
static void touch_tap(int x, int y)
{
    int cx = SCREEN_W / 2;
    if (gs == GS_TITLE) title_tap(x, y);
    else if (gs == GS_PAUSE) {   /* the plates are buttons */
        if (x > cx - 110 && x < cx + 110 && y >= 112 && y < 150) pause_tap = 0;      /* like the title: tap to pick, */
        else if (x > cx - 110 && x < cx + 110 && y >= 150 && y < 190) pause_tap = 1; /* tap again to choose */
    } else if (gs == GS_CONTROLS || gs == GS_END) eng_tap(PAD_CROSS);
}
static int stick_as_dpad(void) { return gs != GS_PLAY && gs != GS_EDITOR; }
#ifndef GEN_BAKED
static int  pointer_mode(void) { return gs == GS_EDITOR; }
static void pointer(int id, int ev, int x, int y, int kind) { if (gs == GS_EDITOR) editor_pointer(id, ev, x, y, kind); }
static void wheel(int steps) { if (gs == GS_EDITOR) editor_wheel(steps); }
static void typed(int ch) { if (gs == GS_EDITOR) editor_char(ch); }
#endif
static int fps_shown;
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
/* test: her at (x, z) facing north, trooper i at dist from her, `ang` (4096 = 360) off her facing, fresh */
void game_debug_setup_duel(int i, s32 x, s32 z, s32 dist, int ang)
{
    Enemy *e = enemy_get(i);
    if (!e) return;
    pl.x = x; pl.z = z; pl.x16 = x * 16; pl.z16 = z * 16; pl.y = level_ground(x, z, -20000); pl.y16 = pl.y * 16;
    pl.yaw = 0; pl.vx = pl.vz = 0; cam_yaw = 0;
    e->x = x + (isin(ang) * dist >> 12); e->z = z + (icos(ang) * dist >> 12); e->y = pl.y;
    e->body.x = e->x; e->body.z = e->z; e->body.x16 = e->x * 16; e->body.z16 = e->z * 16; e->body.y = e->y; e->body.y16 = e->y * 16;
    e->body.vx = e->body.vz = 0;
    e->tgt.hp = e->tgt.max_hp; e->tgt.alive = 1; e->state = E_FIGHT; e->t = 0; e->los = 1;
    e->tgt.x = e->x; e->tgt.y = e->y; e->tgt.z = e->z;
    php = 100; lock_ei = -1;
    {   /* the other troopers out of the way */
        int k;
        for (k = 0; k < enemy_count(); k++) if (k != i) { Enemy *o = enemy_get(k); o->state = E_DEAD; o->tgt.alive = 0; }
    }
}
s32 game_debug_player_x(void) { return pl.x; }
int game_debug_enemy_state(int i) { Enemy *e = enemy_get(i); return e ? e->state * 1000 + e->tgt.hp : -1; }
int game_debug_fatality(void) { return cine.active ? (cine.vt16 >> 4) : (fatal_ready >= 0 ? -2 : -1); }
int game_debug_player_hp(void) { return php; }
int game_debug_state(void) { return gs; }
int game_debug_goto_exit(void)
{
    if (!has_exit) return 0;
    pl.x = exit_x; pl.z = exit_z - 3600; pl.x16 = pl.x * 16; pl.z16 = pl.z * 16;
    pl.y = level_ground(pl.x, pl.z, -20000); pl.y16 = pl.y * 16;
    php = 100;
    return 1;
}

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
    player_reset(&pl, sx, sz, syaw); cam_fy_ok = 0;
    memset(&player_tgt, 0, sizeof(player_tgt));
    player_tgt.radius = 210; player_tgt.height = 1800; player_tgt.hp = player_tgt.max_hp = 100;
    player_tgt.team = TEAM_PLAYER; player_tgt.on_hit = player_hit;
    player_tgt.x = pl.x; player_tgt.y = pl.y; player_tgt.z = pl.z;
    combat_add(&player_tgt);
    enemy_reset();
    php = 100; hurt_flash = 0; dead_t = 0; fatal_ready = -1;
    keys = 0; msg = 0; msg_t = 0; key_flash_t = 0; play_frames = 0; lock_ei = -1;
    {
        int i;
        has_exit = 0;
        for (i = 0; i < level_thing_count(); i++)
            if (level_thing(i)->type == 4) { exit_x = level_thing(i)->x; exit_z = level_thing(i)->z; has_exit = 1; }
    }
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
#ifndef GEN_BAKED
    if (custom_map) { level_load_map(0); custom_map = 0; }   /* level 1 back after the editor's play test */
    playtest = 0;
#endif
    level_init();
    fx_reset();
    level_spawn(&sx, &sz, &syaw);
    player_reset(&pl, sx - 600, sz + 600, syaw + 300); cam_fy_ok = 0;
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

static void app_init(void)
{
    int b;
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
    level_set_use_mode(1);                         /* doors and lifts answer the interact button (A) */
    hud_init();
    ui_init();
#ifndef GEN_BAKED
    gen_sfx();
    snd_init(sfx_pcm, SFX_COUNT);
#else
    snd_init(sfx_adpcm, SFX_COUNT);
#endif
    {
        EnemyHooks hk;
        memset(&hk, 0, sizeof(hk));
        hk.sound = enemy_sound;
        enemy_init(&hk);
    }
    title_init();
    cam_dist = 2700; cam_h = 1650;
    go_title();
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
static const int SAW_DMG[3] = { 26, 30, 52 }, SAW_KNOCK[3] = { 0, 0, 60 * 16 }, SAW_STAGGER[3] = { 28, 30, 40 };   /* 1-2 hold him in place for the next */
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
    int play = gs == GS_PLAY, menu = gs == GS_TITLE || gs == GS_CONTROLS || gs == GS_EDITOR;
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
    if (lock_ei >= 0 && n < 4 && pl.atk_t >= pl_swing_strike(pl.atk) + 1) {   /* locked on and the blade is down: it lands */
        Enemy *le = enemy_get(lock_ei);
        s32 dx = le->x - pl.x, dz = le->z - pl.z;
        if (iabs(dx) < 1900 && iabs(dz) < 1900 && dx * dx / 16 + dz * dz / 16 < 1900 * 1900 / 16) {
            VECTOR w;
            enemy_waist(le, &w);
            n += attack_force(&saw_attack, &le->tgt, &w, &ev[n]);
        }
    }
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
            if (lock_ei >= 0) continue;                           /* mid-combo on a trooper: just sparks */
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

/* she's at the generator (the level's exit), on its floor, alive: A there starts the ending */
static int near_generator(void)
{
    return has_exit && pl.grounded && php > 0 && iabs(pl.x - exit_x) < 4300 && iabs(pl.z - exit_z) < 4300 &&
           iabs(pl.y - level_ground(exit_x, exit_z, pl.y)) < 600 &&
           (pl.x - exit_x) * (pl.x - exit_x) / 16 + (pl.z - exit_z) * (pl.z - exit_z) / 16 < 4300 * 4300 / 16;
}

/* the ending: she walks up to the reactor's core and puts the saw through it */
static void core_start(void)
{
    s32 dx = exit_x - pl.x, dz = exit_z - pl.z, d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    if (d < 64) { dx = isin(pl.yaw); dz = icos(pl.yaw); d = 4096; }
    memset(&cine, 0, sizeof(cine));
    cine.active = 1; cine.ei = -1;
    cine.dir_x = dx * 4096 / d; cine.dir_z = dz * 4096 / d;
    pl.x = exit_x - (cine.dir_x * 2650 >> 12); pl.z = exit_z - (cine.dir_z * 2650 >> 12);
    pl.y = level_ground(pl.x, pl.z, pl.y - 400);
    pl.x16 = pl.x * 16; pl.z16 = pl.z * 16; pl.y16 = pl.y * 16;
    pl.yaw = player_input_angle(cine.dir_x, cine.dir_z) & 4095;
    cine.base_yaw = pl.yaw;
    cine.cx = pl.x + (cine.dir_x * 500 >> 12); cine.cz = pl.z + (cine.dir_z * 500 >> 12);    /* just in front of her (outside the core) */
    {   /* film from the side with more room */
        VECTOR c, a;
        int sd, best = -1;
        c.vx = cine.cx; c.vy = pl.y - 1500; c.vz = cine.cz;
        cine.side = 1;
        for (sd = -1; sd <= 1; sd += 2) {
            int look = (cine.base_yaw + sd * 1000) & 4095, t;
            a.vx = cine.cx - (isin(look) * 4400 >> 12); a.vy = c.vy - 900; a.vz = cine.cz - (icos(look) * 4400 >> 12);
            t = level_clip_segment(&c, &a, 120);
            if (t > best) { best = t; cine.side = sd; }
        }
    }
    attack_end(&saw_attack);
    fatal_ready = -1;
    snd_play(SFX_SWING, 4096, 0, 2600);
}

/* her swing frame, his reaction and the effects for cutscene frame f (called once per frame) */
static void cine_frame(int f)
{
    Enemy *e = cine.ei >= 0 ? enemy_get(cine.ei) : 0;
    VECTOR w, at;
    int wgt = 4096;
    if (f > 146) { wgt = 4096 - (f - 146) * 64; if (wgt < 0) wgt = 0; }
    if (!e) {                                          /* the reactor: the same kill, energy instead of blood */
        player_script(&pl, wgt ? 4 : 0, f < 160 ? f : 160, wgt);
        pl.shake = f >= 33 && f < 80 ? 2800 + (isin(f * 300) * 1000 >> 12) : (f >= 80 && f < 86 ? 3800 : 0);
        player_pose(&pl, &demon);
        at.vx = pl.x + (cine.dir_x * 1500 >> 12); at.vy = pl.y - 1100; at.vz = pl.z + (cine.dir_z * 1500 >> 12);
        if (f >= 32 && f < 84) {
            fx_spray(&at, 2, 3, (-cine.dir_z * 20 >> 12) + (rnd(21) - 10), -16 - rnd(10), (cine.dir_x * 20 >> 12) + (rnd(21) - 10), 18);
            fx_spray(&at, 1, 4, -cine.dir_x * 12 >> 12, -8, -cine.dir_z * 12 >> 12, 24);
            if (f == 32) { snd_play(SFX_ZAP, 4096, 0, 2400); snd_play(SFX_RIP, 4096, 0, 3000); land_shake_t = 0; land_shake_amp = 110; }
            else if (f % 10 == 0) snd_play(SFX_ZAP, 3200, 0, 2200 + rnd(800));
            rumble = 160;
        }
        if (f == 84 || f == 100 || f == 118) {          /* the core bursts */
            at.vy -= 1200;
            fx_spray(&at, 2, 50, 0, -30, 0, 40);
            fx_spray(&at, 1, 30, 0, -20, 0, 40);
            snd_play(SFX_SLAM, 4096, 0, f == 84 ? 3000 : 2400); snd_play(SFX_AMB_ALARM, 3600, 0, 3000);
            land_shake_t = 0; land_shake_amp = f == 84 ? 220 : 160; rumble = 255;
        }
        if (f == 120) { snd_play(SFX_STINGER, 4096, 0, 3400); cine.title_t = 1; }
        fx_update();
        return;
    }
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
    if ((cine.vt16 >> 4) >= 236 && cine.ei < 0) {      /* the plant is finished: the score */
        int i;
        cine.active = 0;
        end_kills = end_troopers = 0;
        for (i = 0; i < enemy_count(); i++) {
            const Enemy *e = enemy_get(i);
            end_troopers++;
            if (e->state == E_DEAD || e->state == E_CUT) end_kills++;
        }
        end_secrets_n = level_secrets(&end_secrets);
        gs = GS_END; gs_t = 0;
        return;
    }
    if ((cine.vt16 >> 4) >= 236) {                     /* back to the game */
        cine.active = 0;
        player_script(&pl, 0, 0, 0);
        pl.atk = 0; pl.atk_w = 0; pl.shake = 0;
        cam_yaw = pl.yaw;
    }
}


static void simulate(u32 pad)
{
    int myaw, mmag;
    u32 use_press;
    if (dead_t) { pad = 0; if (++dead_t > 150) { start_play(); return; } }
    use_press = pad & ~pl.prev & BTN_USE;              /* A, just pressed */
    camera_update(pad);
    read_move(pad, &myaw, &mmag);
    {   /* auto-targeting: the nearest trooper within reach, roughly where she's facing or the stick
           points, is locked. A chainsaw press turns her onto him, and the lock holds for the whole
           combo: every swing slides her in to striking distance of him (player.c), so 3-hit
           combos land without lining anything up */
        int i, ref = mmag > 300 ? myaw : pl.yaw, best_i = -1;
        s32 best = 0x7fffffff;
        const Enemy *le = lock_ei >= 0 ? enemy_get(lock_ei) : 0;
        if (le) {
            VECTOR ca, cb;
            ca.vx = pl.x; ca.vy = pl.y - 1200; ca.vz = pl.z;
            cb.vx = le->x; cb.vy = le->y - 1200; cb.vz = le->z;
            if (level_clip_segment(&ca, &cb, 0) < 4000) le = 0, lock_ei = -1;
        }
        if (le && (!pl.atk || le->state == E_DEAD || le->state == E_CUT || le->state == E_FATAL ||
                   iabs(le->x - pl.x) > LOCK_KEEP || iabs(le->z - pl.z) > LOCK_KEEP)) le = 0, lock_ei = -1;
        if (!le) {
            for (i = 0; i < enemy_count(); i++) {
                const Enemy *e = enemy_get(i);
                s32 dx = e->x - pl.x, dz = e->z - pl.z, d;
                int a, diff;
                if (e->state == E_DEAD || e->state == E_CUT || e->state == E_FATAL) continue;
                if (iabs(e->y - pl.y) > 900 || iabs(dx) > LOCK_RANGE || iabs(dz) > LOCK_RANGE) continue;
                d = (s32)isqrt((u32)((dx >> 2) * (dx >> 2) + (dz >> 2) * (dz >> 2))) << 2;
                if (d > LOCK_RANGE) continue;
                a = player_input_angle(dx, dz) & 4095;
                diff = ((a - ref + 2048) & 4095) - 2048;
                if (diff < 0) diff = -diff;
                if (diff > 1600 && d > 1800) continue;          /* close by, any direction will do */
                {   /* never through a wall or a shut door */
                    VECTOR ca, cb;
                    ca.vx = pl.x; ca.vy = pl.y - 1200; ca.vz = pl.z;
                    cb.vx = e->x; cb.vy = e->y - 1200; cb.vz = e->z;
                    if (level_clip_segment(&ca, &cb, 0) < 4000) continue;
                }
                if (d + diff * 2 < best) { best = d + diff * 2; best_i = i; }
            }
            if (best_i >= 0 && (pad & ~pl.prev & BTN_ATTACK)) lock_ei = best_i;    /* the swing commits to him */
            le = best_i >= 0 ? enemy_get(best_i) : 0;
        }
        if (le) {
            s32 dx = le->x - pl.x, dz = le->z - pl.z;
            pl.lock_yaw = player_input_angle(dx, dz) & 4095;
            pl.lock_dist = ((s32)isqrt((u32)((dx >> 2) * (dx >> 2) + (dz >> 2) * (dz >> 2))) << 2) | 1;
            pl.lock_x = le->x; pl.lock_z = le->z;
            if (pad & ~pl.prev & BTN_ATTACK) { myaw = pl.lock_yaw; mmag = 4096; }
        } else pl.lock_dist = 0;
    }
    if (use_press && !(fatal_ready >= 0 && pl.grounded) && !near_generator()) {   /* A: the door / lift / secret door in reach */
        int u = level_use(pl.x, pl.y, pl.z, keys);
        if (u == LU_SECRET) show_msg("A SECRET DOOR", 0);
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
    if (fatal_ready >= 0 && use_press && pl.grounded) cine_start(fatal_ready);
    else if (use_press && near_generator()) core_start();          /* A at the generator: the ending */
    can_use = !cine.active && php > 0 && (fatal_ready >= 0 || near_generator() || level_can_use(pl.x, pl.y, pl.z) != LU_NONE);
    play_frames++;
    if (hurt_flash > 0) hurt_flash--;
    level_update();
    fx_update();
    combat_update();
}

static void back_to_editor(void)
{
#ifndef GEN_BAKED
    fx_reset();
    gs = GS_EDITOR; gs_t = 0;
    editor_resume();
#endif
}

static void flow(u32 pad, u32 press)
{
    gs_t++;
    switch (gs) {
    case GS_TITLE: {
        int c = title_update(pad, press);
        if (c == MENU_PLAY) start_play();
        else if (c == MENU_CONTROLS) { gs = GS_CONTROLS; gs_t = 0; }
#ifndef GEN_BAKED
        else if (c == MENU_EDITOR) { editor_open(pad_style, touch_ui); gs = GS_EDITOR; gs_t = 0; }
#endif
        break;
    }
#ifndef GEN_BAKED
    case GS_EDITOR: {
        int r;
        editor_style(pad_style, touch_ui);
        r = editor_update(pad, press, an_lx, an_ly, an_rx, an_ry);
        if (r == ED_EXIT) { snd_play(SFX_UI_MOVE, 3000, 0, 3600); go_title(); }
        else if (r == ED_PLAYTEST) {
            level_load_map(editor_playtest_map());
            custom_map = 1; playtest = 1;
            snd_play(SFX_UI_OK, 3600, 0, 3400);
            start_play();
        }
        break;
    }
#endif
    case GS_CONTROLS:
        if (gs_t > 10 && (press & (PAD_CROSS | PAD_START | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE))) {
            gs = GS_TITLE; gs_t = 0; title_reset(); snd_play(SFX_UI_MOVE, 3000, 0, 3600);
        }
        break;
    case GS_PLAY:
        if ((press & PAD_START) && !cine.active) { gs = GS_PAUSE; gs_t = 0; pause_sel = 0; pause_tap = -1; snd_play(SFX_PAUSE, 3600, 0, 4096); break; }
        if (cine.active) cine_step();
        else simulate(pad);
        break;
    case GS_END:
        fx_update();
        if (gs_t > 90 && (press & (PAD_CROSS | PAD_START | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE | BTN_JUMP))) {
            snd_play(SFX_UI_OK, 3600, 0, 3400);
            if (playtest) back_to_editor(); else go_title();
        }
        break;
    case GS_PAUSE:
        /* like the main menu: up / down picks a plate, A chooses it; START or B resumes */
        {
            int choose = -1;
            if (pause_tap >= 0) { if (pause_tap == pause_sel) choose = pause_sel; else { pause_sel = pause_tap; snd_play(SFX_UI_MOVE, 3000, 0, 4096); } pause_tap = -1; }
            if (press & (PAD_UP | PAD_DOWN)) { pause_sel ^= 1; snd_play(SFX_UI_MOVE, 3000, 0, 4096); }
            if (press & PAD_CROSS) choose = pause_sel;
            if (press & (PAD_START | PAD_CIRCLE)) choose = 0;
            if (choose == 0) { gs = GS_PLAY; gs_t = 0; snd_play(SFX_PAUSE, 2600, 0, 5200); }
            else if (choose == 1) { snd_play(SFX_UI_OK, 3600, 0, 3400); if (playtest) back_to_editor(); else go_title(); }
        }
        break;
    }
}

/* ------------------------------------------------------------------ rendering */
/* "LABEL a SEP b" with b padded to `pad` digits (no printf: the Windows build has no C library) */
static void score_line(char *o, const char *label, int a, const char *sep, int b, int pad)
{
    char t[12];
    int n = 0, k;
    while (*label) *o++ = *label++;
    k = 0; do { t[k++] = (char)('0' + a % 10); a /= 10; } while (a && k < 10);
    while (k) *o++ = t[--k];
    while (*sep) *o++ = *sep++;
    k = 0; do { t[k++] = (char)('0' + b % 10); b /= 10; n++; } while ((b || n < pad) && k < 10);
    while (k) *o++ = t[--k];
    *o = 0;
}
static void draw_touch(FrameBuf *fb)
{
    static const int icon_of[] = { UI_ICON_SAW, UI_ICON_JUMP, UI_ICON_PAUSE, 0, UI_ICON_SPRINT };
    int i;
    touch_layout();
    /* floating stick: a runic iron ring where the thumb landed, the demon eye follows the thumb */
    if (stick_on) {
        s32 dx = stick_x - stick_ox, dy = stick_y - stick_oy, len = (s32)isqrt((u32)(dx * dx + dy * dy));
        if (len > STICK_R) { dx = dx * STICK_R / len; dy = dy * STICK_R / len; }
        ui_stick(fb, stick_ox, stick_oy, stick_ox + dx, stick_oy + dy, STICK_R, 1, frame_count);
    } else {
        ui_stick(fb, STICK_HOME_X, STICK_HOME_Y, STICK_HOME_X, STICK_HOME_Y, STICK_R, 0, frame_count);
    }
    for (i = 0; i < touch_button_count; i++) {
        const TouchButton *b = &touch_buttons[i];
        if (b->button == BTN_USE) {                                  /* USE, or the FATALITY sigil, pulsing */
            int g;
            if (!can_use) continue;                                  /* only near something to use */
            if (fatal_ready < 0) {
                ui_button(fb, b->x, b->y, 15, UI_ICON_NONE, (cur_pad & b->button) != 0, frame_count);
                text_center(fb, "USE", b->x + 1, b->y - 3, 1, 230, 0);
                continue;
            }
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
    if (!level_tank(&c)) return;   /* no tank in this level: nobody to hold captive */
    fk_bones(&demon, &p, pose, &off);
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
    if (cine.ei < 0) {     /* the reactor: over her shoulder at the core, in on the cut, then wide as it bursts */
        if (f < 32)      { look = cine.base_yaw + sd * (900 + f * 2); R_ = 4400 - f * 10; H = 900; ty = cy - 500; }
        else if (f < 84) { look = cine.base_yaw + sd * (1000 + (f - 32)); R_ = 3600 - (f - 32) * 6; H = 300; ty = cy - 300; }
        else             { look = cine.base_yaw + sd * (750 - (f - 84)); R_ = 4600 + (f - 84) * 4; H = 1800; ty = cy - 1900; }
    }
    else if (f < 32) { look = cine.base_yaw + sd * (700 + f * 4); R_ = 2700 - f * 10; H = 1000; ty = cy; }
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
    {
        s32 want = pl.y + pl.step_off, d = want - cam_fy;
        if (!cam_fy_ok || d > 3000 || d < -3000) { cam_fy = want; cam_fy_ok = 1; }
        else cam_fy += pl.grounded ? d / 6 : d / 3;     /* steps glide; a jump is followed closely */
    }
    tgt.vx = (s16)(-(pl.x + rxv) + sx); tgt.vy = (s16)(-(cam_fy - cam_h + 300) + sy); tgt.vz = (s16)(-(pl.z + rzv));
    view.t[0] = view.t[1] = view.t[2] = 0;
    mat_apply(&view, &tgt, &tv);
    {
        /* indoor camera: pull in in front of walls, props and the ceiling */
        VECTOR off, back, head, campos;
        s32 d;
        int f;
        off.vx = 0; off.vy = 300; off.vz = cam_dist;
        mat_rot_tapply(&view, &off, &back);
        campos.vx = (pl.x + rxv) - back.vx; campos.vy = (cam_fy - cam_h + 300) - back.vy; campos.vz = (pl.z + rzv) - back.vz;
        head.vx = pl.x; head.vy = cam_fy - 1500; head.vz = pl.z;
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

/* ------------------------------------------------------------------ the health bar
   a horned bone sigil with a burning eye, an iron-and-bone frame with thorns, a blood fill in a
   dark notched trough, a draining damage trail, and a shine that sweeps left to right with a
   crackle of lightning at its head; under 35% the whole bar throbs like a heartbeat */
#define HB_X 6                   /* the frame sprite (128x20) */
#define HB_Y 6
#define HB_FX (HB_X + 24)        /* the trough / fill (90x8) inside it */
#define HB_FY (HB_Y + 7)
#define HB_W 90
static int hp_trail = 100, hp_trail_wait;
static u32 hash3(int a, int b) { u32 h = (u32)a * 73856093u ^ (u32)b * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15); }
/* an additive kit sprite at x..x+w (cell texels 1:1), cropped to the fill [lo, hi) */
static void hb_glow(FrameBuf *fb, int cell, int x, int w, int h, int lo, int hi, int k, int flip, int ot)
{
    int x0 = x, x1 = x + w, u0 = 0, u1 = w;
    if (x0 < lo) { u0 += lo - x0; x0 = lo; }
    if (x1 > hi) { u1 -= x1 - hi; x1 = hi; }
    if (x1 <= x0) return;
    if (flip) { int t = u0; u0 = w - u1; u1 = w - t; }
    ui_kit_part(fb, cell, x0, HB_FY, x1, HB_FY + h, u0, 0, u1, h, k, k, k, flip, ot);
}
static void draw_health(FrameBuf *fb, int t)
{
    int w = php * HB_W / 100, wt, low = php > 0 && php < 35;
    int beat = low ? (isin(t * 180) > 2600 ? 60 : isin(t * 180) > 0 ? 24 : 0) : 0;
    /* the damage trail: holds a moment, then drains down to the health */
    if (php >= hp_trail) { hp_trail = php; hp_trail_wait = 0; }
    else if (++hp_trail_wait > 24) hp_trail -= 1;
    wt = hp_trail * HB_W / 100;
    if (w < 0) w = 0;

    ui_kit(fb, KIT_HB_FRAME, HB_X, HB_Y, 128, 128, 128, 7);
    if (wt > w)   /* the trail: the blood sprite burnt orange */
        ui_kit_part(fb, KIT_HB_FILL, HB_FX + w, HB_FY, HB_FX + wt, HB_FY + 8, w, 0, wt, 8, 255, 210, 120, 0, 6);
    if (w > 0) {
        int k = 128 + beat, period = low ? 70 : 110, ph = t % period, head = -18 + ph * (HB_W + 36) / period;
        ui_kit_part(fb, KIT_HB_FILL, HB_FX, HB_FY, HB_FX + w, HB_FY + 8, 0, 0, w, 8, k, k - beat / 2, k - beat / 2, 0, 5);
        /* the shine sweeping left to right, lightning crackling just behind it */
        hb_glow(fb, KIT_HB_SHINE, HB_FX + head, 16, 8, HB_FX, HB_FX + w, 128, 0, 4);
        if (head > 4 && (hash3(t >> 1, 3) & 3)) {
            int f = (int)(hash3(t >> 2, 9) & 1), kb = 90 + (int)(hash3(t, 5) % 60);
            hb_glow(fb, KIT_HB_BOLT, HB_FX + head - 20, 22, 8, HB_FX, HB_FX + w, kb, f, 3);
        }
        if ((hash3(t >> 3, 11) & 7) == 0)   /* a stray arc somewhere along the blood */
            hb_glow(fb, KIT_HB_BOLT, HB_FX + (int)(hash3(t >> 3, 12) % (u32)(w + 1)) - 11, 22, 8, HB_FX, HB_FX + w, 70, (int)(hash3(t >> 3, 13) & 1), 3);
    }
    {   /* the sigil's eye: a slow burn, a fast throb when she's low */
        int g = 90 + (isin(t * (low ? 200 : 70)) * 60 >> 12);
        ui_kit(fb, KIT_HB_EYE, HB_X + 3, HB_Y + 9, g, g, g, 4);
    }
}

static void draw_hud(FrameBuf *fb)
{
    draw_health(fb, frame_count);
    {   /* keycard slots under the bar: lit when carried, flashing when a door wants one */
        static const u8 KC[4][3] = { { 0, 0, 0 }, { 60, 220, 255 }, { 255, 170, 40 }, { 240, 40, 60 } };
        int c;
        for (c = 1; c <= 3; c++) {
            int x = HB_FX + (c - 1) * 21, y = HB_Y + 21, have = (keys >> c) & 1, flash = key_flash_t && key_flash_col == c && ((key_flash_t >> 3) & 1);
            ui_kit(fb, KIT_KEY_SLOT, x, y, 128, 128, 128, 7);
            if (have) ui_kit(fb, KIT_KEY_CARD, x + 2, y + 2, KC[c][0], KC[c][1], KC[c][2], 6);
            else if (flash) ui_kit(fb, KIT_KEY_CARD, x + 2, y + 2, KC[c][0] / 3, KC[c][1] / 3, KC[c][2] / 3, 6);
        }
    }
    if (msg && msg_t > 0) {
        if (msg_t > 20 || (msg_t >> 2) & 1) text_center(fb, msg, SCREEN_W / 2, touch_ui ? 96 : 172, 1, msg_col ? 230 : 200, 4);
    }
    if (fps_shown > 0) {
        char b[12];
        b[0] = 'F'; b[1] = 'P'; b[2] = 'S'; b[3] = ' ';
        b[4] = (char)('0' + fps_shown / 10 % 10); b[5] = (char)('0' + fps_shown % 10); b[6] = 0;
        text(fb, b, SCREEN_W - (touch_ui ? 104 : 58), 12, 1, 200, 4);
    }
}

static void draw_controls(FrameBuf *fb)
{
    static const char *lines[][2] = {
        { "LEFT THUMB",   "STICK - PUSH FAR TO RUN" },
        { "CHAINSAW",     "ATTACK, TAP AGAIN: COMBO" },
        { "WINGS",        "JUMP" },
        { "SPRINT",       "DODGE ROLL" },
        { "USE",          "DOORS, LIFTS, FATALITY" },
        { "RIGHT THUMB",  "DRAG TO TURN THE CAMERA" },
        { "",             "CAMERA FOLLOWS YOU" },
        { "BONES",        "PAUSE" },
    };
    static const char *pad_lines[][2] = {               /* Xbox controller / keyboard (PC) */
        { "LEFT STICK",   "MOVE - PUSH FAR TO RUN" },
        { "X",            "CHAINSAW, AGAIN: COMBO" },
        { "Y",            "JUMP" },
        { "B",            "DODGE ROLL" },
        { "A",            "DOORS, LIFTS, FATALITY" },
        { "RIGHT STICK",  "TURN THE CAMERA" },
        { "LB  RB",       "SWING THE CAMERA" },
        { "KEYS",         "WASD F SPACE C E  Q R" },
        { "START",        "PAUSE" },
    };
    const char *(*L)[2] = touch_ui ? lines : pad_lines;
    int i;
    int ox = SCREEN_W / 2 - 160;
    if (!ui_nine(fb, KIT_PANEL, ox + 12, 28, ox + 308, 220, 128, 9)) {   /* (PS1: no kit page) */
        quad_g(fb, ox + 18, 34, ox + 302, 214, RGB(30, 4, 6), RGB(8, 1, 2), NO_SEMI, 8);
        quad_g(fb, ox + 16, 32, ox + 304, 216, RGB(160, 40, 20), RGB(60, 8, 4), NO_SEMI, 9);
    }
    text_hell(fb, "CONTROLS", SCREEN_W / 2, 42, 2, frame_count, 6);
    for (i = 0; i < 9; i++) {
        text(fb, L[i][0], ox + 28, 70 + i * 14, 1, 230, 6);
        text(fb, L[i][1], ox + 128, 70 + i * 14, 1, 140, 6);
    }
    if ((frame_count >> 4) & 1) text_center(fb, touch_ui ? "TAP TO GO BACK" : "PRESS ANY BUTTON", SCREEN_W / 2, 200, 1, 160, 6);
}

static void app_tick(void)
{
    const Input *in = eng_in();
    u32 pad = in->pad, press = in->press;
    an_lx = in->lx; an_ly = in->ly; an_rx = in->rx; an_ry = in->ry;
    stick_on = in->stick_on; stick_ox = in->stick_ox; stick_oy = in->stick_oy; stick_x = in->stick_x; stick_y = in->stick_y;
    touch_ui = in->touch_ui; pad_style = in->pad_style; fps_shown = in->fps;
    cur_pad = pad;
    prev_pad = pad;
    frame_count++;
    rumble = rumble * 3 / 4;
    flow(pad, press);
    audio_update();
    eng_rumble(rumble);
}

static void app_draw(FrameBuf *fb)
{
    PROJ pr;

    if (gs == GS_TITLE) {
        title_draw(fb, frame_count);
#ifndef GEN_BAKED
    } else if (gs == GS_EDITOR) {
        editor_draw(fb, frame_count);
#endif
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
        quad_g(fb, 0, 0, SCREEN_W, 26, RGB(0, 0, 0), RGB(0, 0, 0), NO_SEMI, 1);
        quad_g(fb, 0, 214, SCREEN_W, 240, RGB(0, 0, 0), RGB(0, 0, 0), NO_SEMI, 1);
        if (f >= 84 && f < 92) { int a = (92 - f) * 9; quad_g(fb, 0, 26, SCREEN_W, 214, RGB(a, a / 10, a / 12), RGB(a, a / 10, a / 12), SEMI_ADD, 3); }
        if (cine.ei < 0 && f >= 84 && f < 130) {          /* the reactor's flash: white, then orange */
            int a = f < 92 ? 255 - (f - 84) * 12 : (130 - f) * 3;
            quad_g(fb, 0, 26, SCREEN_W, 214, RGB(a, a * 7 / 10, a / 3), RGB(a, a / 2, a / 6), SEMI_ADD, 3);
        }
        if (f >= 32 && f < 34) quad_g(fb, 0, 26, SCREEN_W, 214, RGB(70, 6, 4), RGB(70, 6, 4), SEMI_ADD, 3);
        if (cine.title_t) {
            /* FATALITY slams in large, settles, glows */
            int sc = cine.title_t < 3 ? 6 : cine.title_t < 6 ? 5 : 4, y = 96 - sc * 5;
            quad_g(fb, 0, y - 10, SCREEN_W, y + sc * 10 + 10, RGB(0, 0, 0), RGB(40, 2, 2), SEMI_SUB, 3);
            quad_g(fb, 0, y - 12, SCREEN_W, y - 10, RGB(200, 30, 10), RGB(200, 30, 10), NO_SEMI, 1);
            quad_g(fb, 0, y + sc * 10 + 10, SCREEN_W, y + sc * 10 + 12, RGB(200, 30, 10), RGB(200, 30, 10), NO_SEMI, 1);
            text_hell(fb, cine.ei < 0 ? "PLANT DESTROYED" : "FATALITY", SCREEN_W / 2, y, cine.ei < 0 ? (sc > 3 ? 3 : sc) : sc, frame_count, 1);
        }
    } else if (gs == GS_END) {
        /* the score: time, kills, secrets */
        char b[32];
        int sec = play_frames / 60;
        if (ui_nine(fb, KIT_PANEL, SCREEN_W / 2 - 130, 28, SCREEN_W / 2 + 130, 212, 128, 6)) {
            int k;
            for (k = 0; k < 3; k++) ui_nine(fb, KIT_FIELD, SCREEN_W / 2 - 78, 95 + k * 20, SCREEN_W / 2 + 78, 111 + k * 20, 128, 5);
        } else quad_g(fb, 0, 0, SCREEN_W, 240, RGB(120, 130, 130), RGB(150, 160, 160), SEMI_SUB, 3);
        text_hell(fb, "PLANT DESTROYED", SCREEN_W / 2, 46, 2, frame_count, 2);
        score_line(b, "TIME      ", sec / 60, ":", sec % 60, 2);
        text(fb, b, SCREEN_W / 2 - 64, 100, 1, 220, 2);
        score_line(b, "KILLS     ", end_kills, " / ", end_troopers, 1);
        text(fb, b, SCREEN_W / 2 - 64, 120, 1, 220, 2);
        score_line(b, "SECRETS   ", end_secrets, " / ", end_secrets_n, 1);
        text(fb, b, SCREEN_W / 2 - 64, 140, 1, 220, 2);
        if (gs_t > 90 && ((frame_count >> 4) & 1)) text_center(fb, touch_ui ? "TAP TO CONTINUE" : "PRESS ANY BUTTON", SCREEN_W / 2, 188, 1, 160, 2);
    } else if (gs != GS_TITLE && gs != GS_CONTROLS && gs != GS_EDITOR) {
        draw_hud(fb);
        if (hurt_flash > 0) quad_g(fb, 0, 0, SCREEN_W, 240, RGB(hurt_flash * 9, 0, 0), RGB(hurt_flash * 5, 0, 0), SEMI_ADD, 3);
        if (dead_t) {
            quad_g(fb, 0, 0, SCREEN_W, 240, RGB(dead_t, dead_t + 10, dead_t + 10), RGB(dead_t, dead_t + 10, dead_t + 10), SEMI_SUB, 3);
            text_hell(fb, "YOU DIED", SCREEN_W / 2, 100, 3, frame_count, 1);
        }
        if (fatal_ready >= 0 && !dead_t && gs == GS_PLAY) {             /* the prompt */
            static const char *how[3] = { "PRESS CROSS", "PRESS A    KEY E", "TAP THE SIGIL" };
            text_hell(fb, "FATALITY", SCREEN_W / 2, 44, 2, frame_count, 1);
            text_center(fb, how[touch_ui ? 2 : pad_style], SCREEN_W / 2, 68, 1, 150 + (isin(frame_count * 160) * 100 >> 12), 1);
        }
        else if (!dead_t && gs == GS_PLAY && !cine.active && near_generator()) {      /* at the generator */
            static const char *how[3] = { "PRESS CROSS TO DESTROY IT", "PRESS A TO DESTROY IT   KEY E", "TAP USE TO DESTROY IT" };
            text_hell(fb, "THE GENERATOR", SCREEN_W / 2, 44, 2, frame_count, 1);
            text_center(fb, how[touch_ui ? 2 : pad_style], SCREEN_W / 2, 68, 1, 150 + (isin(frame_count * 160) * 100 >> 12), 1);
        }
        if (gs == GS_PAUSE) {
            if (!ui_nine(fb, KIT_PANEL, SCREEN_W / 2 - 140, 56, SCREEN_W / 2 + 140, 200, 128, 10))
                quad_g(fb, 0, 0, SCREEN_W, 240, RGB(90, 100, 100), RGB(90, 100, 100), SEMI_SUB, 4);
            text_hell(fb, "PAUSED", SCREEN_W / 2, 74, 3, frame_count, 2);
            ui_plate(fb, SCREEN_W / 2, 118, 220, pause_sel == 0, frame_count, 8);
            ui_plate(fb, SCREEN_W / 2, 156, 220, pause_sel == 1, frame_count, 8);
            text_center(fb, "RESUME", SCREEN_W / 2 + 1, 129, 1, pause_sel == 0 ? 255 : 120, 2);
            text_center(fb, playtest ? "BACK TO EDITOR" : "QUIT TO TITLE", SCREEN_W / 2 + 1, 167, 1, pause_sel == 1 ? 255 : 120, 2);
        }
        if (touch_ui) draw_touch(fb);
    }
}

static int ram_used(void)
{
    return (int)(sizeof(pl) + sizeof(DVERT) * (size_t)demon.nverts + sizeof(DPRIM) * (size_t)demon.nprims
                 + sizeof(demon_bones));
}

PSX_STATIC_ASSERT(ram_budget, sizeof(pl) < RAM_BUDGET);

/* ------------------------------------------------------------------ the game, as the engine sees it */
static const KeyBind KEYS[] = {          /* Windows: playing */
    { 'W', 0, 0, PAD_UP }, { KEY_UP, 0, 0, PAD_UP }, { 'S', 0, 0, PAD_DOWN }, { KEY_DOWN, 0, 0, PAD_DOWN },
    { 'A', 0, 0, PAD_LEFT }, { KEY_LEFT, 0, 0, PAD_LEFT }, { 'D', 0, 0, PAD_RIGHT }, { KEY_RIGHT, 0, 0, PAD_RIGHT },
    { ' ', 0, 0, PAD_TRIANGLE }, { KEY_SHIFT, 0, 0, PAD_R2 },           /* jump, sprint */
    { 'E', 0, 0, PAD_CROSS },                                              /* interact */
    { 'C', 0, 0, PAD_CIRCLE },                                             /* dodge roll */
    { 'F', 0, 0, PAD_SQUARE }, { 'J', 0, 0, PAD_SQUARE }, { KEY_CTRL, 0, 0, PAD_SQUARE }, { KEY_MOUSE_L, 0, 0, PAD_SQUARE },   /* chainsaw */
    { 'Q', 0, 0, PAD_L1 }, { 'R', 0, 0, PAD_R1 },                          /* swing the camera */
    { KEY_ENTER, 0, 0, PAD_START }, { KEY_ESC, 0, 0, PAD_START }, { KEY_BACKSPACE, 0, 0, PAD_SELECT },
    { 0, 0, 0, 0 }
};
static const KeyBind KEYS_EDITOR[] = {   /* Windows: the level editor (letters also type into its name box) */
    { KEY_UP, 0, 0, PAD_UP }, { KEY_DOWN, 0, 0, PAD_DOWN }, { KEY_LEFT, 0, 0, PAD_LEFT }, { KEY_RIGHT, 0, 0, PAD_RIGHT },
    { 'W', KB_LY_POS, 1, 0 }, { 'S', KB_LY_NEG, 1, 0 }, { 'A', KB_LX_NEG, 1, 0 }, { 'D', KB_LX_POS, 1, 0 },   /* fly in 3D */
    { ' ', 0, 1, PAD_CROSS }, { KEY_ENTER, 0, 1, PAD_CROSS }, { KEY_ESC, 0, 1, PAD_CIRCLE }, { KEY_BACKSPACE, 0, 1, PAD_CIRCLE },
    { KEY_TAB, 0, 1, PAD_TRIANGLE }, { 'I', 0, 1, PAD_SQUARE }, { 'Q', 0, 1, PAD_L1 }, { 'E', 0, 1, PAD_R1 },
    { KEY_PGDN, 0, 1, PAD_L2 }, { '-', 0, 1, PAD_L2 }, { KEY_PGUP, 0, 1, PAD_R2 }, { '+', 0, 1, PAD_R2 },
    { 'V', 0, 1, PAD_SELECT }, { KEY_F1, 0, 1, PAD_SELECT }, { 'M', 0, 1, PAD_START }, { KEY_F10, 0, 1, PAD_START },
    { 0, 0, 0, 0 }
};

const App game_app = {
    "DEADBIRD",
    app_init, app_tick, app_draw,
    touch_hit, touch_tap, touch_look, 45,
#ifndef GEN_BAKED
    pointer_mode, pointer, wheel, typed,
#else
    0, 0, 0, 0,
#endif
    stick_as_dpad,
    KEYS, KEYS_EDITOR,
    "levels", ".map",
    ram_used,
    0, 0, 0, 0,                          /* no full-resolution UI */
};
