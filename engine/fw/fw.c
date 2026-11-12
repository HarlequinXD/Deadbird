/*
 * The gameplay framework (see fw.h): actors, physics against the world, the player controller,
 * cameras, combat, events, sounds and the HUD.
 */
#include "fw.h"
#include "fsm.h"
#include "fwsfx.h"
#include "draw2d.h"
#include "snd.h"
#include "ik.h"

static FwActor  act[FW_ACTORS];
static FwProto  protos[FW_PROTOS];
static int      nprotos;
static char     ev_names[FW_EVENTS][16];
static int      nevents;
static char     texts[FW_TEXTS][40];
static int      ntexts;
static const FwWorld *world;
s32 fw_global[FW_GLOBALS];
FwGame fw_game;
FwPlayerCfg fw_player_cfg;

/* camera, effects */
static int cam_yaw, cam_pitch, cam_clip = 2600;
static int msg_t, shake_t, flash_t, flash_r, flash_g, flash_b, fire_cd, muzzle_t, bob, hurt_t, end_t;
static const char *msg_s;
static u32 frame;
static u32 rng = 0x2545f491;

static int rnd(int n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return n > 0 ? (int)(rng % (u32)n) : 0; }
static int iabs(int v) { return v < 0 ? -v : v; }
static void copy_name(char *d, const char *s, int n) { int i; for (i = 0; i < n - 1 && s[i]; i++) d[i] = s[i]; d[i] = 0; }
static int  same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ------------------------------------------------------------------ registry */
void fw_set_world(const FwWorld *w) { world = w; }
const FwWorld *fw_world(void) { return world; }
FwActor *fw_actor(int i) { return i >= 0 && i < FW_ACTORS && act[i].used ? &act[i] : 0; }
int  fw_actor_index(const FwActor *a) { return (int)(a - act); }
FwProto *fw_proto(int i) { return i >= 0 && i < nprotos ? &protos[i] : 0; }
int  fw_proto_count(void) { return nprotos; }
int  fw_proto_find(const char *name) { int i; for (i = 0; i < nprotos; i++) if (same(protos[i].name, name)) return i; return -1; }
int  fw_proto_add(const char *name)
{
    FwProto *p;
    int i = fw_proto_find(name);
    if (i >= 0) return i;
    if (nprotos >= FW_PROTOS) return -1;
    p = &protos[nprotos];
    memset(p, 0, sizeof(*p));
    copy_name(p->name, name, 16);
    p->r = 200; p->g = 200; p->b = 200; p->shape = SHAPE_BOX;
    p->radius = 300; p->height = 1500; p->size = 600; p->hp = 10;
    p->flags = FA_SOLID | FA_GRAVITY | FA_VISIBLE;
    p->fsm = -1; p->thing = -1;
    return nprotos++;
}

int fw_event_id(const char *name)
{
    int i;
    for (i = 0; i < nevents; i++) if (same(ev_names[i], name)) return i;
    if (nevents >= FW_EVENTS) return -1;
    copy_name(ev_names[nevents], name, 16);
    return nevents++;
}
const char *fw_event_name(int id) { return id >= 0 && id < nevents ? ev_names[id] : "?"; }
void fw_event(FwActor *a, int ev) { if (a && ev >= 0 && ev < FW_EVENTS) a->events |= 1u << ev; }
void fw_event_all(int ev, u32 tags)
{
    int i;
    for (i = 0; i < FW_ACTORS; i++) if (act[i].used && (!tags || (act[i].tags & tags))) fw_event(&act[i], ev);
}

int fw_text_add(const char *s)
{
    int i;
    for (i = 0; i < ntexts; i++) if (same(texts[i], s)) return i;
    if (ntexts >= FW_TEXTS) return 0;
    copy_name(texts[ntexts], s, 40);
    return ntexts++;
}
const char *fw_text(int i) { return i >= 0 && i < ntexts ? texts[i] : ""; }

/* ------------------------------------------------------------------ actors */
int fw_spawn(int proto, s32 x, s32 y, s32 z, int yaw)
{
    const FwProto *p = fw_proto(proto);
    FwActor *a;
    int i;
    if (!p) return -1;
    for (i = 1; i < FW_ACTORS; i++) if (!act[i].used) break;
    if (i >= FW_ACTORS) return -1;
    a = &act[i];
    memset(a, 0, sizeof(*a));
    a->used = 1; a->proto = (u8)proto; a->flags = p->flags;
    a->x = x; a->y = y; a->z = z; a->yaw = yaw & 4095;
    a->radius = p->radius; a->height = p->height; a->size = p->size; a->shape = p->shape;
    a->hp = a->hp_max = p->hp; a->tags = p->tags; a->damage = p->damage; a->life = p->life;
    a->r = p->r; a->g = p->g; a->b = p->b;
    a->owner = -1;
    a->fsm = p->fsm; a->state = -1;
    if (a->fsm >= 0 && a->fsm < fsm_nm && fsm_m[a->fsm].ns) fsm_enter(a, fsm_m[a->fsm].s0);
    a->events = 1u << EV_SPAWNED;
    return i;
}

void fw_destroy(FwActor *a) { if (a && fw_actor_index(a) != FW_PLAYER) a->used = 0; }

/* ------------------------------------------------------------------ movement */
void fw_move_forward(FwActor *a, int speed) { a->vx = speed * isin(a->yaw) >> 12; a->vz = speed * icos(a->yaw) >> 12; }
int  fw_angle_to(const FwActor *a, s32 x, s32 z)
{
    int want = ratan2((x - a->x) >> 2, (z - a->z) >> 2);
    return ((want - a->yaw + 2048) & 4095) - 2048;
}
void fw_turn(FwActor *a, int rate) { a->yaw = (a->yaw + rate) & 4095; }
void fw_turn_toward(FwActor *a, s32 x, s32 z, int rate)
{
    int d = fw_angle_to(a, x, z);
    if (d > rate) d = rate;
    if (d < -rate) d = -rate;
    a->yaw = (a->yaw + d) & 4095;
}
void fw_move_toward(FwActor *a, s32 x, s32 z, int speed)
{
    s32 dx = x - a->x, dz = z - a->z, d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4))) << 4;
    if (d < 1) { a->vx = a->vz = 0; return; }
    if (d < speed) speed = d;
    a->vx = dx * speed / d; a->vz = dz * speed / d;
}
void fw_jump(FwActor *a, int speed) { if (a->flags & FA_GROUNDED) { a->vy = -speed; a->flags &= (u16)~FA_GROUNDED; } }
void fw_stop(FwActor *a) { a->vx = a->vz = 0; }

s32 fw_dist(const FwActor *a, const FwActor *b)
{
    s32 dx = (a->x - b->x) >> 4, dz = (a->z - b->z) >> 4;
    return (s32)isqrt((u32)(dx * dx + dz * dz)) << 4;
}

int fw_can_see(const FwActor *a, const FwActor *b, s32 range, int fov)
{
    VECTOR p, q;
    if (fw_dist(a, b) > range) return 0;
    if (fov < 2048 && iabs(fw_angle_to(a, b->x, b->z)) > fov) return 0;
    if (!world || !world->clip) return 1;
    p.vx = a->x; p.vy = a->y - a->height * 3 / 4; p.vz = a->z;
    q.vx = b->x; q.vy = b->y - b->height * 3 / 4; q.vz = b->z;
    return world->clip(&p, &q, 0) >= 4000;
}

/* ------------------------------------------------------------------ combat */
void fw_damage(FwActor *a, int amount, int from)
{
    if (!a || (a->flags & FA_DEAD) || amount <= 0) return;
    a->hp -= amount;
    a->flash = 8;
    if (fw_actor_index(a) == FW_PLAYER) { hurt_t = 12; shake_t = 6; eng_rumble(150); fw_sound(FWS_HIT, 3600, a->x, a->z); }
    else fw_sound(FWS_HIT, 3000, a->x, a->z);
    if (from >= 0 && from < FW_ACTORS) a->var[FW_VARS - 1] = from;      /* v7: who hit it last */
    if (a->hp <= 0) {
        a->hp = 0;
        a->flags |= FA_DEAD;
        fw_event(a, EV_DIED);
        if (fw_actor_index(a) != FW_PLAYER) { fw_game.kills++; fw_sound(FWS_DIE, 3800, a->x, a->z); }
    } else fw_event(a, EV_HIT);
}
void fw_heal(FwActor *a, int amount) { if (a && !(a->flags & FA_DEAD)) { a->hp += amount; if (a->hp > a->hp_max) a->hp = a->hp_max; } }

int fw_shoot(FwActor *a, int proto, int speed, int damage, int at_player)
{
    FwActor *s, *pl = &act[FW_PLAYER];
    s32 ey = a->y - a->height * 2 / 3;
    int i = fw_spawn(proto, a->x + (isin(a->yaw) * (a->radius + 120) >> 12), ey, a->z + (icos(a->yaw) * (a->radius + 120) >> 12), a->yaw);
    if (i < 0) return -1;
    s = &act[i];
    s->flags |= FA_PROJECTILE; s->flags &= (u16)~(FA_GRAVITY | FA_SOLID);
    s->owner = (s16)fw_actor_index(a);
    s->damage = (s16)(damage ? damage : s->damage);
    if (!s->life) s->life = 240;
    if (at_player && pl->used) {
        s32 dx = pl->x - s->x, dy = (pl->y - pl->height / 2) - ey, dz = pl->z - s->z;
        s32 d = (s32)isqrt((u32)((dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4) + (dz >> 4) * (dz >> 4))) << 4;
        if (d < 1) d = 1;
        s->vx = dx * speed / d; s->vy = dy * speed / d; s->vz = dz * speed / d;
    } else {
        s->vx = speed * isin(a->yaw) >> 12; s->vz = speed * icos(a->yaw) >> 12;
        if (fw_actor_index(a) == FW_PLAYER) s->vy = speed * isin(cam_pitch) >> 12;
    }
    fw_sound(FWS_SHOOT, 2600, a->x, a->z);
    return i;
}

/* Doom-style: the nearest target in a narrow cone along the facing (any height), not behind a wall */
int fw_hitscan(FwActor *a, s32 range, int damage, int pitch)
{
    int i, best = -1;
    s32 bd = range;
    (void)pitch;
    for (i = 0; i < FW_ACTORS; i++) {
        FwActor *t = &act[i];
        s32 d;
        int off;
        if (!t->used || t == a || (t->flags & (FA_DEAD | FA_PROJECTILE | FA_PICKUP)) || !(t->flags & FA_SOLID)) continue;
        d = fw_dist(a, t);
        if (d > bd || d < 1) continue;
        off = iabs(fw_angle_to(a, t->x, t->z));
        if ((s32)off * d > (s32)(t->radius + 160) * 652) continue;    /* off-axis by more than its radius */
        if (!fw_can_see(a, t, range, 2048)) continue;
        bd = d; best = i;
    }
    if (best >= 0) fw_damage(&act[best], damage, fw_actor_index(a));
    return best;
}

int fw_melee(FwActor *a, s32 range, int damage)
{
    int i, n = 0;
    for (i = 0; i < FW_ACTORS; i++) {
        FwActor *t = &act[i];
        if (!t->used || t == a || (t->flags & (FA_DEAD | FA_PROJECTILE | FA_PICKUP))) continue;
        if (fw_dist(a, t) > range + t->radius || iabs(fw_angle_to(a, t->x, t->z)) > 700) continue;
        if (fw_actor_index(a) != FW_PLAYER && i != FW_PLAYER) continue;    /* enemies hit the player only */
        fw_damage(t, damage, fw_actor_index(a)); n++;
    }
    return n;
}

/* ------------------------------------------------------------------ effects */
void fw_message(const char *t, int ticks) { msg_s = t; msg_t = ticks; }
void fw_shake(int amount) { if (amount > shake_t) shake_t = amount; }
void fw_flash(int r, int g, int b) { flash_r = r; flash_g = g; flash_b = b; flash_t = 10; }
void fw_sound(int id, int vol, s32 x, s32 z)
{
    const FwActor *p = &act[FW_PLAYER];
    s32 dx = (x - p->x) >> 4, dz = (z - p->z) >> 4, d = (s32)isqrt((u32)(dx * dx + dz * dz)) << 4;
    int pan;
    if (d > 2000) vol = (int)(vol * (d > 20000 ? 200 : 4096 - (d - 2000) * 3896 / 18000) >> 12);
    /* pan: the side of the camera it's on */
    pan = (int)((dx * icos(cam_yaw) - dz * isin(cam_yaw)) >> 12) * 4096 / (iabs((int)d >> 4) + 200);
    if (pan > 3000) pan = 3000;
    if (pan < -3000) pan = -3000;
    snd_play(id, vol, d < 300 ? 0 : pan, 4096 - 200 + rnd(400));
}

/* ------------------------------------------------------------------ the player */
void fw_player_reset(void)
{
    FwActor *p = &act[FW_PLAYER];
    int pi = fw_proto_find("player");
    const FwProto *pp = fw_proto(pi);
    s32 x = 0, z = 0;
    int yaw = 0;
    if (world && world->spawn) world->spawn(&x, &z, &yaw);
    memset(p, 0, sizeof(*p));
    p->used = 1; p->proto = (u8)(pi < 0 ? 0 : pi);
    p->flags = FA_SOLID | FA_GRAVITY;
    p->x = x; p->z = z; p->yaw = yaw;
    p->y = world && world->ground ? world->ground(x, z, -4000) : 0;
    p->radius = pp ? pp->radius : 300; p->height = pp ? pp->height : 1700;
    p->hp = p->hp_max = pp ? pp->hp : 100;
    p->tags = pp ? pp->tags : 0;
    p->r = pp ? pp->r : 200; p->g = pp ? pp->g : 60; p->b = pp ? pp->b : 40; p->size = pp ? pp->size : 500;
    p->shape = pp ? pp->shape : SHAPE_PILLAR;
    if (fw_player_cfg.mode == CAM_THIRD_PERSON) p->flags |= FA_VISIBLE;
    p->owner = -1;
    p->fsm = pp ? pp->fsm : -1; p->state = -1;
    if (p->fsm >= 0 && fsm_m[p->fsm].ns) fsm_enter(p, fsm_m[p->fsm].s0);
    cam_yaw = yaw; cam_pitch = 0;
}

void fw_look(int d) { cam_yaw = (cam_yaw + d) & 4095; }

static void player_control(void)
{
    const Input *in = eng_in();
    const FwPlayerCfg *c = &fw_player_cfg;
    FwActor *p = &act[FW_PLAYER];
    int mx = in->lx, my = in->ly, mag, speed, fw_, rt;
    if (in->stick_on) {                                  /* the floating touch stick */
        mx = (in->stick_x - in->stick_ox) * 4096 / 38; my = (in->stick_oy - in->stick_y) * 4096 / 38;
        if (mx > 4096) mx = 4096;
        if (mx < -4096) mx = -4096;
        if (my > 4096) my = 4096;
        if (my < -4096) my = -4096;
    }
    if (in->pad & PAD_UP) my = 4096;
    if (in->pad & PAD_DOWN) my = -4096;
    rt = in->rx;
    if (c->mode == CAM_FIRST_PERSON) {                   /* d-pad left / right strafe; L1 / R1 turn */
        if (in->pad & PAD_LEFT) mx = -4096;
        if (in->pad & PAD_RIGHT) mx = 4096;
    } else {
        if (in->pad & PAD_LEFT) mx = -4096;
        if (in->pad & PAD_RIGHT) mx = 4096;
    }
    if (in->pad & PAD_L1) rt = -4096;
    if (in->pad & PAD_R1) rt = 4096;
    cam_yaw = (cam_yaw + rt * c->turn / 4096) & 4095;
    if (c->mode == CAM_FIRST_PERSON) {
        cam_pitch -= in->ry * 30 >> 12;
        if (!in->ry && !in->touch_ui) cam_pitch -= cam_pitch / 12;          /* drifts back level */
        if (in->touch_ui) cam_pitch = 0;
        if (cam_pitch > 500) cam_pitch = 500;
        if (cam_pitch < -500) cam_pitch = -500;
    }
    if (p->flags & FA_DEAD) { p->vx = p->vz = 0; return; }
    mag = (int)isqrt((u32)(mx * mx + my * my) >> 0);
    if (mag > 4096) mag = 4096;
    speed = (in->pad & PAD_R2) || mag > 3800 ? c->run : c->walk;
    if (mag < 400) { p->vx = p->vz = 0; }
    else if (c->mode == CAM_FIRST_PERSON) {
        s32 sx = isin(cam_yaw), sz = icos(cam_yaw);
        p->yaw = cam_yaw;
        p->vx = (s32)(((s32)my * sx + (s32)mx * sz) >> 12) * speed >> 12;
        p->vz = (s32)(((s32)my * sz - (s32)mx * sx) >> 12) * speed >> 12;
    } else {                                             /* third person: run the way the stick points, from the camera */
        int dir = (cam_yaw + ratan2(mx, my)) & 4095, d = ((dir - p->yaw + 2048) & 4095) - 2048;
        p->yaw = (p->yaw + (d > 220 ? 220 : d < -220 ? -220 : d)) & 4095;
        fw_ = speed * mag >> 12;
        p->vx = fw_ * isin(dir) >> 12; p->vz = fw_ * icos(dir) >> 12;
    }
    if (c->mode == CAM_FIRST_PERSON) p->yaw = cam_yaw;
    if (mag >= 400 && (p->flags & FA_GROUNDED)) bob += speed;
    if ((in->press & PAD_CROSS) && (p->flags & FA_GROUNDED)) { fw_jump(p, c->jump); fw_sound(FWS_JUMP, 2000, p->x, p->z); }
    if (fire_cd > 0) fire_cd--;
    if ((in->pad & PAD_SQUARE) && fire_cd == 0) {
        fire_cd = c->fire_rate;
        muzzle_t = 4;
        if (c->range > 0) { fw_hitscan(p, c->range, c->damage, cam_pitch); fw_sound(FWS_SHOOT, 3400, p->x, p->z); }
        else if (c->shot_proto >= 0) fw_shoot(p, c->shot_proto, c->shot_speed, c->damage, 0);
        fw_shake(2);
    }
}

/* ------------------------------------------------------------------ physics */
static void physics(FwActor *a, int idx)
{
    const FwPlayerCfg *c = &fw_player_cfg;
    s32 nx, nz, g;
    int was_ground = (a->flags & FA_GROUNDED) != 0;
    a->x += a->vx; a->z += a->vz;
    if (world && world->collide && (a->flags & (FA_SOLID | FA_PROJECTILE))) {
        s32 r = (a->flags & FA_PROJECTILE) ? 60 : a->radius;
        if (world->collide(&a->x, &a->z, a->y - 200, r, &nx, &nz)) {
            a->flags |= FA_HITWALL;
            fw_event(a, EV_WALL);
            if (a->flags & FA_PROJECTILE) { fw_sound(FWS_HIT, 1500, a->x, a->z); a->used = 0; return; }
        } else a->flags &= (u16)~FA_HITWALL;
    }
    if (a->flags & FA_GRAVITY) {
        a->vy += c->gravity;
        if (a->vy > 400) a->vy = 400;
    }
    a->y += a->vy;
    g = world && world->ground ? world->ground(a->x, a->z, a->y - 300) : 0;
    if (a->flags & FA_PROJECTILE) {
        if (a->y > g) { fw_sound(FWS_HIT, 1500, a->x, a->z); a->used = 0; }
        return;
    }
    if (a->y >= g || ((a->flags & FA_GRAVITY) && was_ground && a->vy >= 0 && a->y > g - 220)) {   /* land / follow steps down */
        if (!was_ground && a->vy > 60) { fw_event(a, EV_LANDED); if (idx == FW_PLAYER) fw_sound(FWS_LAND, 1800, a->x, a->z); }
        a->y = g; if (a->vy > 0) a->vy = 0;
        a->flags |= FA_GROUNDED;
    } else a->flags &= (u16)~FA_GROUNDED;
    if (!(a->flags & FA_GRAVITY) && a->y > g) a->y = g;
}

static void contacts(void)
{
    int i, j;
    FwActor *p = &act[FW_PLAYER];
    for (i = 1; i < FW_ACTORS; i++) {
        FwActor *a = &act[i];
        if (!a->used) continue;
        if (a->flags & FA_PROJECTILE) {             /* a shot: the first body it meets, not its owner's side */
            for (j = 0; j < FW_ACTORS; j++) {
                FwActor *t = &act[j];
                s32 rr;
                if (!t->used || j == i || j == a->owner || (t->flags & (FA_PROJECTILE | FA_DEAD | FA_PICKUP)) || !(t->flags & FA_SOLID)) continue;
                if ((a->owner == FW_PLAYER) == (j == FW_PLAYER)) continue;
                if (a->owner != FW_PLAYER && j != FW_PLAYER) continue;
                rr = t->radius + 80;
                if (iabs(a->x - t->x) > rr || iabs(a->z - t->z) > rr || a->y < t->y - t->height - 100 || a->y > t->y + 100) continue;
                if (fw_dist(a, t) > rr) continue;
                fw_damage(t, a->damage, a->owner);
                a->used = 0;
                break;
            }
            continue;
        }
        if (a->flags & FA_DEAD) continue;
        /* touching the player */
        if (p->used && !(p->flags & FA_DEAD)) {
            s32 rr = a->radius + p->radius;
            if (iabs(a->x - p->x) < rr && iabs(a->z - p->z) < rr && fw_dist(a, p) < rr &&
                a->y - a->height < p->y && p->y - p->height < a->y) {
                fw_event(a, EV_TOUCH);
                fw_event(p, EV_TOUCH);
                if ((a->flags & FA_SOLID) && !(a->flags & FA_PICKUP)) {    /* push the player out */
                    s32 dx = p->x - a->x, dz = p->z - a->z, d = fw_dist(a, p);
                    if (d < 1) { dx = 1; d = 1; }
                    p->x = a->x + dx * rr / d; p->z = a->z + dz * rr / d;
                }
            }
        }
        /* solids keep apart */
        if (a->flags & FA_SOLID)
            for (j = i + 1; j < FW_ACTORS; j++) {
                FwActor *b = &act[j];
                s32 rr, d, dx, dz;
                if (!b->used || !(b->flags & FA_SOLID) || (b->flags & (FA_PROJECTILE | FA_DEAD | FA_PICKUP))) continue;
                rr = a->radius + b->radius;
                if (iabs(a->x - b->x) >= rr || iabs(a->z - b->z) >= rr) continue;
                d = fw_dist(a, b);
                if (d >= rr) continue;
                dx = b->x - a->x; dz = b->z - a->z;
                if (d < 1) { dx = 1; d = 1; }
                a->x -= dx * (rr - d) / d / 2; a->z -= dz * (rr - d) / d / 2;
                b->x += dx * (rr - d) / d / 2; b->z += dz * (rr - d) / d / 2;
            }
    }
}

/* ------------------------------------------------------------------ running */
static int font_up;
void fw_init(void)
{
    static const char *const builtin[EV_USER] = { "FINISHED", "HIT", "DIED", "TOUCH", "LANDED", "WALL", "SPAWNED", "TIMER" };
    int i;
    memset(act, 0, sizeof(act));
    nprotos = 0; nevents = 0; ntexts = 0;
    for (i = 0; i < EV_USER; i++) fw_event_id(builtin[i]);
    fw_text_add("");
    memset(fw_global, 0, sizeof(fw_global));
    memset(&fw_game, 0, sizeof(fw_game));
    fsm_clear();
    memset(&fw_player_cfg, 0, sizeof(fw_player_cfg));
    fw_player_cfg.mode = CAM_FIRST_PERSON;
    fw_player_cfg.walk = 40; fw_player_cfg.run = 72; fw_player_cfg.jump = 70; fw_player_cfg.gravity = 5;
    fw_player_cfg.eye = 1450; fw_player_cfg.cam_dist = 2600; fw_player_cfg.cam_height = 1700; fw_player_cfg.turn = 70;
    fw_player_cfg.fire_rate = 12; fw_player_cfg.damage = 10; fw_player_cfg.range = 20000;
    fw_player_cfg.shot_proto = -1; fw_player_cfg.shot_speed = 160;
    if (!font_up) { fw_font_init(); fw_sfx_init(); font_up = 1; }
}

void fw_start(void)
{
    int i, n;
    for (i = 0; i < FW_ACTORS; i++) act[i].used = 0;
    if (world && world->load) world->load("");
    fw_player_reset();
    n = world && world->thing_count ? world->thing_count() : 0;
    for (i = 0; i < n; i++) {
        int type, arg = 0, yaw, k;
        s32 x, z;
        if (!world->thing(i, &type, &arg, &x, &z, &yaw)) continue;
        for (k = 0; k < nprotos; k++)
            if (protos[k].thing == type) {
                s32 y = world->ground ? world->ground(x, z, -4000) : 0;
                int a = fw_spawn(k, x, y, z, yaw);
                if (a >= 0) act[a].var[0] = arg;
                break;
            }
    }
    msg_t = shake_t = flash_t = hurt_t = end_t = 0;
    memset(&fw_game, 0, sizeof(fw_game));
}

void fw_tick(void)
{
    int i;
    frame++;
    if (!fw_game.won && !fw_game.lost) player_control();
    else act[FW_PLAYER].vx = act[FW_PLAYER].vz = 0;
    for (i = 0; i < FW_ACTORS; i++) {
        FwActor *a = &act[i];
        if (!a->used) continue;
        if (i != FW_PLAYER && !(a->flags & FA_PROJECTILE)) { a->vx = a->vz = 0; }   /* actions set movement each tick */
        if (a->flash) a->flash--;
        if (a->life && --a->life == 0) { a->used = 0; continue; }
        fsm_tick(a);
    }
    for (i = 0; i < FW_ACTORS; i++) if (act[i].used) physics(&act[i], i);
    contacts();
    if (world && world->update) world->update(act[FW_PLAYER].x, act[FW_PLAYER].y, act[FW_PLAYER].z);
    if ((act[FW_PLAYER].flags & FA_DEAD) && !fw_game.lost && !fw_game.won) { fw_game.lost = 1; fw_message("YOU DIED", 600); }
    if (fw_game.won || fw_game.lost) end_t++;
    if (msg_t) msg_t--;
    if (shake_t) shake_t--;
    if (flash_t) flash_t--;
    if (hurt_t) hurt_t--;
    if (muzzle_t) muzzle_t--;
}

/* ------------------------------------------------------------------ drawing */
static MATRIX view;
static PROJ pr;

static int fog(s32 z) { return z < 6000 ? 128 : z > 22000 ? 0 : 128 - (int)((z - 6000) * 128 / 16000); }

/* a box (or pillar) standing on (x,y,z), half width hw, height h, turned by yaw, gouraud lit */
static void draw_box(FrameBuf *fb, const FwActor *a, s32 hw, s32 h, int r, int g, int b)
{
    static const u8 F[6][4] = { { 4, 5, 0, 1 }, { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 6, 2 }, { 1, 5, 3, 7 }, { 2, 3, 6, 7 } };
    static const u8 LIT[6] = { 140, 110, 70, 90, 100, 40 };          /* top, front, back, left, right, bottom */
    s16 sx[8], sy[8];
    s32 sz[8];
    int i, k, cs = icos(a->yaw), sn = isin(a->yaw), ok = 0;
    for (i = 0; i < 8; i++) {
        SVECTOR v; VECTOR c;
        s32 lx = (i & 1) ? hw : -hw, lz = (i & 4) ? -hw : hw, ly = (i & 2) ? 0 : -h;
        s32 wx = a->x + ((lx * cs + lz * sn) >> 12), wz = a->z + ((-lx * sn + lz * cs) >> 12);
        /* relative to the camera keeps it in 16 bits */
        v.vx = (s16)0; v.vy = 0; v.vz = 0;
        c.vx = view.t[0] + ((view.m[0][0] * wx + view.m[0][1] * (a->y + ly) + view.m[0][2] * wz) >> 12);
        c.vy = view.t[1] + ((view.m[1][0] * wx + view.m[1][1] * (a->y + ly) + view.m[1][2] * wz) >> 12);
        c.vz = view.t[2] + ((view.m[2][0] * wx + view.m[2][1] * (a->y + ly) + view.m[2][2] * wz) >> 12);
        (void)v;
        sz[i] = c.vz;
        if (c.vz < pr.near_z || !rot_trans_pers(&pr, &c, &sx[i], &sy[i])) return;
        if (sx[i] > -200 && sx[i] < SCREEN_W + 200 && sy[i] > -200 && sy[i] < SCREEN_H + 200) ok = 1;
    }
    if (!ok) return;
    for (i = 0; i < 6; i++) {
        const u8 *f = F[i];
        POLY *p;
        s32 z = (sz[f[0]] + sz[f[1]] + sz[f[2]] + sz[f[3]]) >> 2;
        int l = LIT[i] * fog(z) >> 7;
        if (normal_clip(sx[f[0]], sy[f[0]], sx[f[1]], sy[f[1]], sx[f[2]], sy[f[2]]) <= 0) continue;
        p = prim_alloc(fb);
        if (!p) return;
        memset(p, 0, sizeof(*p));
        p->code = PF_QUAD | PF_GOURAUD;
        for (k = 0; k < 4; k++) {
            int lk = k < 2 ? l : l * 3 / 4;
            p->v[k].x = sx[f[k]]; p->v[k].y = sy[f[k]];
            p->v[k].r = (u8)(r * lk >> 7 > 255 ? 255 : r * lk >> 7);
            p->v[k].g = (u8)(g * lk >> 7 > 255 ? 255 : g * lk >> 7);
            p->v[k].b = (u8)(b * lk >> 7 > 255 ? 255 : b * lk >> 7);
        }
        ot_add(fb, (int)(z >> 4), p);
    }
}

static int project(s32 x, s32 y, s32 z, s16 *sx, s16 *sy, s32 *sz)
{
    VECTOR c;
    c.vx = view.t[0] + ((view.m[0][0] * x + view.m[0][1] * y + view.m[0][2] * z) >> 12);
    c.vy = view.t[1] + ((view.m[1][0] * x + view.m[1][1] * y + view.m[1][2] * z) >> 12);
    c.vz = view.t[2] + ((view.m[2][0] * x + view.m[2][1] * y + view.m[2][2] * z) >> 12);
    *sz = c.vz;
    if (c.vz < pr.near_z) return 0;
    return rot_trans_pers(&pr, &c, sx, sy);
}

/* a glowing diamond facing the camera (orbs, shots) */
static void draw_orb(FrameBuf *fb, const FwActor *a)
{
    s16 x, y;
    s32 z;
    int r, k, i;
    if (!project(a->x, a->y - a->size / 2, a->z, &x, &y, &z)) return;
    r = (int)(a->size * pr.h / (z > 1 ? z : 1)) / 2;
    if (r < 2) r = 2;
    if (r > 200) r = 200;
    k = fog(z);
    for (i = 0; i < 4096; i += 512) {
        tri_f(fb, x, y, x + (icos(i) * r >> 12), y + (isin(i) * r >> 12), x + (icos(i + 512) * r >> 12), y + (isin(i + 512) * r >> 12),
              RGB(a->r * k >> 7, a->g * k >> 7, a->b * k >> 7), (a->flags & FA_PROJECTILE) ? SEMI_ADD : NO_SEMI, (int)(z >> 4));
    }
    if (a->flags & FA_PROJECTILE)
        tri_f(fb, x - r / 2, y, x + r / 2, y, x, y - r / 2, RGB(255, 255, 220), SEMI_ADD, (int)(z >> 4) - 1);
}

static void draw_shadow(FrameBuf *fb, const FwActor *a)
{
    s16 x0, y0, x1, y1, x2, y2, x3, y3;
    s32 z, g = world && world->ground ? world->ground(a->x, a->z, a->y - 100) : a->y, r = a->radius;
    if (!project(a->x - r, g - 4, a->z, &x0, &y0, &z) || !project(a->x + r, g - 4, a->z, &x1, &y1, &z) ||
        !project(a->x, g - 4, a->z - r, &x2, &y2, &z) || !project(a->x, g - 4, a->z + r, &x3, &y3, &z)) return;
    tri_f(fb, x0, y0, x2, y2, x1, y1, RGB(60, 60, 60), SEMI_SUB, (int)(z >> 4) + 2);
    tri_f(fb, x0, y0, x1, y1, x3, y3, RGB(60, 60, 60), SEMI_SUB, (int)(z >> 4) + 2);
}

static void draw_actor(FrameBuf *fb, const FwActor *a)
{
    int r = a->r, g = a->g, b = a->b;
    if (a->flash) { r = g = b = 255; }
    if (a->flags & FA_DEAD) { r /= 3; g /= 3; b /= 3; }
    switch (a->shape) {
    case SHAPE_ORB: draw_orb(fb, a); break;
    case SHAPE_PILLAR: draw_box(fb, a, a->size / 3, (a->flags & FA_DEAD) ? a->size / 4 : a->height, r, g, b); break;
    case SHAPE_BOX: draw_box(fb, a, a->size / 2, (a->flags & FA_DEAD) ? a->size / 4 : a->size, r, g, b); break;
    default: break;
    }
    if (a->shape != SHAPE_NONE && !(a->flags & FA_PROJECTILE)) draw_shadow(fb, a);
}

/* first person: a gun at the bottom of the screen, bobbing, flashing when it fires */
static void draw_gun(FrameBuf *fb)
{
    int cx = SCREEN_W / 2 + 40 + (isin(bob * 3) * 6 >> 12), by = SCREEN_H + 4 + (iabs(icos(bob * 3)) * 5 >> 12);
    int kick = muzzle_t * 3;
    if (muzzle_t) {
        int i;
        for (i = 0; i < 4096; i += 512)
            tri_f(fb, cx, by - 74 + kick, cx + (icos(i + muzzle_t * 300) * 22 >> 12), by - 74 + kick + (isin(i + muzzle_t * 300) * 16 >> 12),
                  cx + (icos(i + 512) * 10 >> 12), by - 74 + kick + (isin(i + 512) * 8 >> 12), RGB(255, 200, 90), SEMI_ADD, 3);
    }
    quad_g(fb, cx - 10, by - 70 + kick, cx + 10, by - 30 + kick, RGB(110, 110, 120), RGB(50, 50, 60), NO_SEMI, 4);    /* barrel */
    quad_g(fb, cx - 18, by - 36 + kick, cx + 18, by + kick, RGB(80, 70, 70), RGB(30, 26, 28), NO_SEMI, 4);           /* body */
    quad_g(fb, cx - 6, by - 72 + kick, cx + 6, by - 68 + kick, RGB(20, 20, 20), RGB(20, 20, 20), NO_SEMI, 3);          /* muzzle */
    quad_g(fb, cx + 4, by - 30 + kick, cx + 26, by + 10 + kick, RGB(90, 50, 40), RGB(40, 20, 16), NO_SEMI, 5);        /* grip */
}

static void draw_hud(FrameBuf *fb)
{
    const FwActor *p = &act[FW_PLAYER];
    char b[32];
    int w = p->hp_max > 0 ? p->hp * 80 / p->hp_max : 0, i, n;
    /* health */
    quad_g(fb, 8, SCREEN_H - 18, 92, SCREEN_H - 8, RGB(10, 10, 10), RGB(10, 10, 10), NO_SEMI, 3);
    if (w > 0) quad_g(fb, 10, SCREEN_H - 16, 10 + w, SCREEN_H - 10, RGB(230, 60, 40), RGB(140, 20, 10), NO_SEMI, 2);
    n = p->hp; i = 0;
    if (n < 0) n = 0;
    b[i++] = 'H'; b[i++] = 'P'; b[i++] = ' ';
    if (n >= 100) b[i++] = (char)('0' + n / 100 % 10);
    if (n >= 10) b[i++] = (char)('0' + n / 10 % 10);
    b[i++] = (char)('0' + n % 10); b[i] = 0;
    fw_text_draw(fb, b, 96, SCREEN_H - 18, RGB(255, 220, 200), 2);
    /* score */
    n = (int)fw_game.score; i = 0;
    b[i++] = 'S'; b[i++] = 'C'; b[i++] = 'O'; b[i++] = 'R'; b[i++] = 'E'; b[i++] = ' ';
    {
        char t[12]; int k = 0;
        if (n < 0) n = 0;
        do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 10);
        while (k) b[i++] = t[--k];
        b[i] = 0;
    }
    fw_text_draw(fb, b, SCREEN_W - 8 - fw_text_width(b), 8, RGB(255, 230, 140), 2);
    if (fw_player_cfg.mode == CAM_FIRST_PERSON) {         /* crosshair */
        int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
        quad_g(fb, cx - 5, cy, cx - 2, cy + 1, RGB(255, 255, 255), RGB(255, 255, 255), NO_SEMI, 2);
        quad_g(fb, cx + 2, cy, cx + 5, cy + 1, RGB(255, 255, 255), RGB(255, 255, 255), NO_SEMI, 2);
        quad_g(fb, cx, cy - 5, cx + 1, cy - 2, RGB(255, 255, 255), RGB(255, 255, 255), NO_SEMI, 2);
        quad_g(fb, cx, cy + 2, cx + 1, cy + 5, RGB(255, 255, 255), RGB(255, 255, 255), NO_SEMI, 2);
    }
    if (msg_t && msg_s) fw_text_draw(fb, msg_s, SCREEN_W / 2 - fw_text_width(msg_s) / 2, SCREEN_H / 2 - 40, RGB(255, 255, 255), 1);
    if (fw_game.won && end_t > 20) fw_text_draw(fb, "LEVEL COMPLETE", SCREEN_W / 2 - 42, SCREEN_H / 2 - 20, RGB(255, 230, 120), 1);
    if (hurt_t) quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(hurt_t * 10, 0, 0), RGB(hurt_t * 6, 0, 0), SEMI_ADD, 5);
    if (flash_t) quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(flash_r * flash_t / 10, flash_g * flash_t / 10, flash_b * flash_t / 10),
                        RGB(flash_r * flash_t / 10, flash_g * flash_t / 10, flash_b * flash_t / 10), SEMI_ADD, 5);
    if (fw_game.lost) quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(end_t > 60 ? 60 : end_t, 0, 0), RGB(end_t > 60 ? 60 : end_t, 0, 0), SEMI_SUB, 6);
}

void fw_draw(FrameBuf *fb)
{
    const FwActor *p = &act[FW_PLAYER];
    const FwPlayerCfg *c = &fw_player_cfg;
    MATRIX rx, ry;
    SVECTOR nt; VECTOR tv;
    s32 ex, ey, ez;
    int i, sx = 0, sy = 0;
    if (shake_t) { sx = rnd(9) - 4; sy = rnd(9) - 4; }
    pr.h = c->mode == CAM_FIRST_PERSON ? 200 : 250; pr.ofx = SCREEN_W / 2 + sx; pr.ofy = SCREEN_H / 2 + sy; pr.near_z = 100;
    if (c->mode == CAM_FIRST_PERSON) {
        ex = p->x; ey = p->y - (p->flags & FA_DEAD ? 300 : c->eye) - (iabs(isin(bob * 3)) * 30 >> 12); ez = p->z;
        mat_rot_x(&rx, cam_pitch & 4095);
    } else {
        /* third person: behind and above, pulled in front of walls */
        VECTOR head, want;
        s32 d;
        int f;
        head.vx = p->x; head.vy = p->y - p->height + 200; head.vz = p->z;
        want.vx = p->x - (isin(cam_yaw) * c->cam_dist >> 12); want.vy = p->y - c->cam_height; want.vz = p->z - (icos(cam_yaw) * c->cam_dist >> 12);
        f = world && world->clip ? world->clip(&head, &want, 160) : 4096;
        d = c->cam_dist * f / 4096 - 100;
        if (d < 400) d = 400;
        if (d < cam_clip) cam_clip = d; else cam_clip += (d - cam_clip) / 10;
        ex = p->x - (isin(cam_yaw) * cam_clip >> 12); ey = p->y - c->cam_height; ez = p->z - (icos(cam_yaw) * cam_clip >> 12);
        mat_rot_x(&rx, 260);
    }
    mat_rot_y(&ry, (-cam_yaw) & 4095);
    mat_mul(&rx, &ry, &view);
    view.t[0] = view.t[1] = view.t[2] = 0;
    nt.vx = 0; nt.vy = 0; nt.vz = 0; (void)nt;
    {   /* translation: -R * eye (32-bit, the world is bigger than 16 bits) */
        tv.vx = -((view.m[0][0] * ex + view.m[0][1] * ey + view.m[0][2] * ez) >> 12);
        tv.vy = -((view.m[1][0] * ex + view.m[1][1] * ey + view.m[1][2] * ez) >> 12);
        tv.vz = -((view.m[2][0] * ex + view.m[2][1] * ey + view.m[2][2] * ez) >> 12);
        view.t[0] = tv.vx; view.t[1] = tv.vy; view.t[2] = tv.vz;
    }
    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(4, 4, 8), RGB(1, 1, 2), NO_SEMI, OT_LEN - 1);
    if (world && world->draw) world->draw(fb, &view, &pr, (int)frame);
    for (i = 0; i < FW_ACTORS; i++) {
        const FwActor *a = &act[i];
        if (!a->used || !(a->flags & FA_VISIBLE)) continue;
        if (i == FW_PLAYER && c->mode == CAM_FIRST_PERSON) continue;
        draw_actor(fb, a);
    }
    if (c->mode == CAM_FIRST_PERSON && !(p->flags & FA_DEAD)) draw_gun(fb);
    draw_hud(fb);
}

int fw_ram_used(void) { return (int)(sizeof(act) + sizeof(protos) + sizeof(fsm_m) + sizeof(fsm_s) + sizeof(fsm_a) + sizeof(fsm_t)) + fw_sfx_ram(); }
