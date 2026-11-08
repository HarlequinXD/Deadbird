#include "fighter.h"
#include "rig.h"
#include "ik.h"

/* ------------------------------------------------------------------ move table
 * name, clip, active from/to, cancel_from, hit bone, offset xyz, radius, damage,
 * hitstun, blockstun, reaction, push, next on LIGHT, next on HEAVY, lunge from/to, lunge speed
 * Combos:  L,L,L (jab, hook, rake)   L,L,H (... skull hammer)   H,H (haymaker, stomp)   fwd+H (horn gore)
 */
const Move moves[MV_COUNT] = {
    { "CLAW JAB",     CLIP_JAB,      5,  9, 10, DEMON_BONE_FIST_L,   45, 0, 0,   150,  45, 16, 10, REACT_HIGH, 10, MV_HOOK, MV_HAYMAKER, 0, 0, 0 },
    { "HELL HOOK",    CLIP_HOOK,     8, 12, 13, DEMON_BONE_FIST_R,  -45, 0, 0,   160,  55, 18, 11, REACT_HIGH, 14, MV_RAKE, MV_HAMMER,   6, 10, 8 },
    { "RAKE",         CLIP_RAKE,     9, 14, 30, DEMON_BONE_FIST_R,  -45, 0, 0,   170,  70, 22, 12, REACT_BODY, 22, -1, -1,           8, 12, 6 },
    { "SKULL HAMMER", CLIP_HAMMER,  14, 19, 44, DEMON_BONE_FIST_R,  -45, 0, 0,   200, 120, 30, 16, REACT_DOWN, 30, -1, -1,          12, 16, 10 },
    { "HAYMAKER",     CLIP_HAYMAKER,15, 20, 24, DEMON_BONE_FIST_R,  -45, 0, 0,   180, 110, 24, 14, REACT_HIGH, 30, -1, MV_STOMP,   13, 18, 10 },
    { "HOOF STOMP",   CLIP_STOMP,   13, 18, 42, DEMON_BONE_HOOF_R,    0, 110, 40, 190, 100, 26, 14, REACT_BODY, 46, -1, -1,          0, 0, 0 },
    { "HORN GORE",    CLIP_GORE,    13, 21, 48, DEMON_BONE_HEAD,      0, -150, 70, 210, 140, 30, 18, REACT_DOWN, 40, -1, -1,          6, 20, 30 },
};

#define WALK_F_SPEED 16
#define WALK_B_SPEED 11
#define BLEND_FRAMES 5

static void set_clip(Fighter *f, int clip, s32 rate, int restart)
{
    if (f->clip != clip || restart) {
        f->pose_from = f->pose_now;          /* cross-fade from where we are */
        f->blend = f->blend_len = BLEND_FRAMES;
        f->clip = clip;
        f->anim_t = rate < 0 ? ((s32)clips[clip].length << 8) - 1 : 0;
    }
    f->rate = rate;
}

void fighter_set_state(Fighter *f, int st)
{
    f->state = st;
    f->t = 0;
    switch (st) {
    case ST_IDLE:      set_clip(f, CLIP_IDLE, 256, 0); break;
    case ST_WALK_F:    set_clip(f, CLIP_WALK, 256, 0); break;
    case ST_WALK_B:    set_clip(f, CLIP_WALK, -180, 0); break;
    case ST_GUARD:     set_clip(f, CLIP_GUARD, 256, 0); break;
    case ST_BLOCKSTUN: set_clip(f, CLIP_BLOCKSTUN, 256, 1); break;
    case ST_FALL:      set_clip(f, CLIP_FALL, 256, 1); break;
    case ST_KO:        set_clip(f, CLIP_FALL, 256, 1); break;
    case ST_LIE:       set_clip(f, CLIP_LIE, 256, 0); break;
    case ST_GETUP:     set_clip(f, CLIP_GETUP, 256, 1); break;
    case ST_WIN:       set_clip(f, CLIP_WIN, 256, 1); break;
    case ST_TAUNT:     set_clip(f, CLIP_TAUNT, 256, 1); break;
    default: break;
    }
}

static void start_move(Fighter *f, int mv)
{
    f->move = mv;
    f->move_hit = 0;
    f->queued = 0;
    f->state = ST_ATTACK;
    f->t = 0;
    set_clip(f, moves[mv].clip, 256, 1);
}

void fighter_reset(Fighter *f, int side, s32 x, u16 clut, const char *name)
{
    int wins = f->wins;
    memset(f, 0, sizeof(*f));
    f->wins = wins;
    f->side = side; f->x = x; f->z = 0;
    f->facing = side ? -1 : 1;
    f->hp = f->trail = 1000;
    f->clut = clut; f->name = name;
    f->clip = -1;
    anim_sample(&clips[CLIP_IDLE], 0, &f->pose_now);
    fighter_set_state(f, ST_IDLE);
    f->blend = 0;
}

int fighter_is_attacking(const Fighter *f) { return f->state == ST_ATTACK; }

int fighter_can_act(const Fighter *f)
{
    return f->state == ST_IDLE || f->state == ST_WALK_F || f->state == ST_WALK_B || f->state == ST_GUARD;
}

void fighter_input(Fighter *f, u32 pad) { f->prev = f->pad; f->pad = pad; }

void fighter_update(Fighter *f, const Fighter *opp, int frozen)
{
    u32 press = f->pad & ~f->prev;
    u32 fwd = f->facing > 0 ? PAD_RIGHT : PAD_LEFT;
    u32 back = f->facing > 0 ? PAD_LEFT : PAD_RIGHT;

    if (f->hitstop > 0) { f->hitstop--; return; }
    if (frozen) {                                   /* round intro / KO: only animate */
        f->anim_t += f->rate;
        return;
    }

    /* face the opponent whenever free to turn */
    if (fighter_can_act(f) && opp) f->facing = opp->x >= f->x ? 1 : -1;

    switch (f->state) {
    case ST_IDLE: case ST_WALK_F: case ST_WALK_B: case ST_GUARD:
        if (press & BTN_LIGHT) { start_move(f, MV_JAB); break; }
        if (press & BTN_HEAVY) { start_move(f, (f->pad & fwd) ? MV_GORE : MV_HAYMAKER); break; }
        if (f->pad & BTN_GUARD)      { if (f->state != ST_GUARD) fighter_set_state(f, ST_GUARD); }
        else if (f->pad & fwd)       { if (f->state != ST_WALK_F) fighter_set_state(f, ST_WALK_F); }
        else if (f->pad & back)      { if (f->state != ST_WALK_B) fighter_set_state(f, ST_WALK_B); }
        else if (f->state != ST_IDLE) fighter_set_state(f, ST_IDLE);
        if (f->state == ST_WALK_F) f->x += WALK_F_SPEED * f->facing;
        if (f->state == ST_WALK_B) f->x -= WALK_B_SPEED * f->facing;
        break;

    case ST_ATTACK: {
        const Move *mv = &moves[f->move];
        if (f->t >= 2) {                            /* buffer the next button of the string */
            if (press & BTN_LIGHT) f->queued = BTN_LIGHT;
            if (press & BTN_HEAVY) f->queued = BTN_HEAVY;
        }
        if (f->t >= mv->lunge_from && f->t < mv->lunge_to) f->x += mv->lunge * f->facing;
        if (f->queued && f->t >= mv->cancel_from) {
            int nx = f->queued == BTN_LIGHT ? mv->next_light : mv->next_heavy;
            if (nx >= 0) { start_move(f, nx); break; }
            f->queued = 0;
        }
        if (f->t >= clips[mv->clip].length) { f->combo = 0; fighter_set_state(f, ST_IDLE); }
        break;
    }

    case ST_BLOCKSTUN:
        if (f->t >= f->stun_len) fighter_set_state(f, (f->pad & BTN_GUARD) ? ST_GUARD : ST_IDLE);
        break;
    case ST_HITSTUN:
        if (f->t >= f->stun_len) fighter_set_state(f, ST_IDLE);
        break;
    case ST_FALL:
        if (f->t >= clips[CLIP_FALL].length) { fighter_set_state(f, ST_LIE); f->stun_len = 34; }
        break;
    case ST_LIE:
        if (f->t >= f->stun_len) fighter_set_state(f, ST_GETUP);
        break;
    case ST_GETUP:
        if (f->t >= clips[CLIP_GETUP].length) fighter_set_state(f, ST_IDLE);
        break;
    case ST_KO:
        if (f->t == clips[CLIP_FALL].length) set_clip(f, CLIP_LIE, 256, 0);
        break;
    case ST_TAUNT:
        if (f->t >= clips[CLIP_TAUNT].length) fighter_set_state(f, ST_IDLE);
        break;
    default: break;
    }

    /* knockback slide with friction */
    f->x += f->vx;
    f->vx = f->vx * 13 / 16;
    if (f->combo_show > 0) f->combo_show--;
    if (f->trail > f->hp) f->trail -= 4;
    if (f->trail < f->hp) f->trail = f->hp;

    f->anim_t += f->rate;
    f->t++;
}

void fighter_pose(Fighter *f, const Model *m)
{
    AnimPose cur;
    VECTOR off;
    MATRIX root, ry;

    anim_sample(&clips[f->clip], f->anim_t, &cur);
    if (f->blend > 0) {
        anim_blend(&f->pose_from, &cur, (f->blend_len - f->blend) * ONE / f->blend_len, &f->pose_now);
        if (f->hitstop == 0) f->blend--;
    } else {
        f->pose_now = cur;
    }
    rig_solve(m, &f->pose_now, f->bone_pose, &off);

    /* fighter root: position on the floor, turned to face +X or -X */
    mat_rot_y(&ry, f->facing > 0 ? 1024 : -1024);
    mat_identity(&root);
    mat_translate(&root, off.vx, off.vy, off.vz);
    mat_translate(&ry, f->x, 0, f->z);
    mat_mul(&ry, &root, &root);
    model_skeleton(m, &root, f->bone_pose, f->world);
}

/* world-space point on a bone */
static void bone_point(const Fighter *f, int b, s16 x, s16 y, s16 z, VECTOR *out)
{
    SVECTOR o;
    o.vx = x; o.vy = y; o.vz = z;
    mat_apply(&f->world[b], &o, out);
}

static int sphere_hit(const VECTOR *a, s32 ra, const VECTOR *b, s32 rb)
{
    s32 dx = (a->vx - b->vx) >> 2, dy = (a->vy - b->vy) >> 2, dz = (a->vz - b->vz) >> 2, r = (ra + rb) >> 2;
    return dx * dx + dy * dy + dz * dz <= r * r;
}

int fighter_check_hit(Fighter *f, Fighter *d, HitEvent *ev)
{
    const Move *mv;
    VECTOR hb, hurt[3];
    static const s16 hr[3] = { 230, 240, 170 };
    int i, guarding;

    memset(ev, 0, sizeof(*ev));
    if (f->state != ST_ATTACK || f->move_hit || f->hitstop) return 0;
    mv = &moves[f->move];
    if (f->t < mv->active_from || f->t > mv->active_to) return 0;
    if (d->state == ST_FALL || d->state == ST_LIE || d->state == ST_GETUP || d->state == ST_KO) return 0;

    bone_point(f, mv->bone, mv->hx, mv->hy, mv->hz, &hb);
    bone_point(d, DEMON_BONE_PELVIS, 0, -60, 0, &hurt[0]);
    bone_point(d, DEMON_BONE_TORSO, 0, -260, 30, &hurt[1]);
    bone_point(d, DEMON_BONE_HEAD, 0, -110, 20, &hurt[2]);
    for (i = 0; i < 3; i++) if (sphere_hit(&hb, mv->radius, &hurt[i], hr[i])) break;
    if (i == 3) return 0;

    f->move_hit = 1;
    ev->at = hb;
    ev->power = mv->damage;
    guarding = (d->state == ST_GUARD || d->state == ST_BLOCKSTUN) && d->facing == -f->facing;
    if (guarding) {
        ev->blocked = 1;
        d->stun_len = mv->blockstun;
        fighter_set_state(d, ST_BLOCKSTUN);
        d->vx = f->facing * mv->push / 2;
        f->hitstop = d->hitstop = 4;
        return 1;
    }

    ev->hit = 1;
    d->hp -= mv->damage;
    d->vx = f->facing * mv->push;
    f->hitstop = d->hitstop = mv->damage >= 100 ? 9 : 6;
    f->combo++;
    f->combo_show = 70;
    if (d->hp <= 0) {
        d->hp = 0;
        ev->ko = 1;
        d->vx = f->facing * 40;
        fighter_set_state(d, ST_KO);
        f->hitstop = d->hitstop = 16;
        return 1;
    }
    if (mv->react == REACT_DOWN) {
        fighter_set_state(d, ST_FALL);
    } else {
        d->state = ST_HITSTUN;
        d->t = 0;
        d->stun_len = mv->hitstun;
        set_clip(d, mv->react == REACT_HIGH ? CLIP_HIT_HIGH : CLIP_HIT_BODY, 256, 1);
    }
    d->combo = 0;
    return 1;
}
