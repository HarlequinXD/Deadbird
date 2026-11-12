/*
 * The FSM action library. Every action is a thin wrapper round a framework API call (fw.h), so a
 * state machine made in the editor does exactly what C code calling the API would. The table
 * also describes each action's parameters for the editor's menus and the project loader.
 *
 * An action returns 1 when it's done and 0 to run again next tick (only "do" actions wait).
 * Senses ("If...") raise an event instead of branching: the state's transitions pick it up.
 */
#include "fsm.h"
#include "fwsfx.h"

static FwActor *player(void) { return fw_actor(FW_PLAYER); }
static int cmp(s32 a, int op, s32 b)
{
    switch (op) {
    case OP_EQ: return a == b;
    case OP_NE: return a != b;
    case OP_LT: return a < b;
    case OP_LE: return a <= b;
    case OP_GT: return a > b;
    default:    return a >= b;
    }
}
static int speed_or(FwActor *a, s32 v) { const FwProto *p = fw_proto(a->proto); return v ? (int)v : p && p->speed ? p->speed : 30; }
static u32 rng = 0x9e3779b9;
static int rnd(int n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return n > 0 ? (int)(rng % (u32)n) : 0; }

/* ---- flow */
static int a_wait(FwActor *a, const s32 *g) { return ++a->wait >= g[0]; }
static int a_wait_random(FwActor *a, const s32 *g) { if (!a->wait) a->wait = -(g[0] + rnd((int)(g[1] - g[0] + 1))); return ++a->wait >= 0; }
static int a_send(FwActor *a, const s32 *g)
{
    switch (g[1]) {
    case TGT_PLAYER: fw_event(player(), (int)g[0]); break;
    case TGT_ALL: fw_event_all((int)g[0], 0); break;
    case TGT_ENEMIES: fw_event_all((int)g[0], 1); break;
    case TGT_OWNER: fw_event(fw_actor(a->owner), (int)g[0]); break;
    default: fw_event(a, (int)g[0]); break;
    }
    return 1;
}
static int a_goto(FwActor *a, const s32 *g) { if (g[0] >= 0) fsm_enter(a, (int)g[0]); return 1; }
static int a_if_random(FwActor *a, const s32 *g) { if (rnd(100) < g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_time(FwActor *a, const s32 *g) { if (a->state_t >= g[0]) fw_event(a, (int)g[1]); return 1; }

/* ---- movement */
static int a_forward(FwActor *a, const s32 *g) { fw_move_forward(a, speed_or(a, g[0])); return 1; }
static int a_chase(FwActor *a, const s32 *g) { FwActor *p = player(); if (p) { fw_turn_toward(a, p->x, p->z, 120); fw_move_forward(a, speed_or(a, g[0])); } return 1; }
static int a_flee(FwActor *a, const s32 *g) { FwActor *p = player(); if (p) { fw_turn_toward(a, 2 * a->x - p->x, 2 * a->z - p->z, 120); fw_move_forward(a, speed_or(a, g[0])); } return 1; }
static int a_face(FwActor *a, const s32 *g) { FwActor *p = player(); if (p) fw_turn_toward(a, p->x, p->z, g[0] ? (int)g[0] : 4096); return 1; }
static int a_turn(FwActor *a, const s32 *g) { fw_turn(a, (int)g[0]); return 1; }
static int a_wander(FwActor *a, const s32 *g)
{
    if (a->state_t % 90 == 0 || (a->flags & FA_HITWALL)) a->var[6] = (a->yaw + 1024 + rnd(2048)) & 4095;
    fw_turn_toward(a, a->x + isin(a->var[6]) * 8, a->z + icos(a->var[6]) * 8, 60);
    fw_move_forward(a, speed_or(a, g[0]) / 2);
    return 1;
}
static int a_jump(FwActor *a, const s32 *g) { fw_jump(a, g[0] ? (int)g[0] : 70); return 1; }
static int a_stop(FwActor *a, const s32 *g) { (void)g; fw_stop(a); return 1; }
static int a_move_to(FwActor *a, const s32 *g)
{
    s32 dx = g[0] - a->x, dz = g[1] - a->z;
    fw_turn_toward(a, g[0], g[1], 200);
    fw_move_toward(a, g[0], g[1], speed_or(a, g[2]));
    return dx * dx / 256 + dz * dz / 256 < 300 * 300 / 256;
}
static int a_teleport(FwActor *a, const s32 *g) { a->x = g[0]; a->z = g[1]; return 1; }
static int a_rise(FwActor *a, const s32 *g) { a->y -= g[0]; return 1; }

/* ---- senses */
static int a_if_near(FwActor *a, const s32 *g) { FwActor *p = player(); if (p && fw_dist(a, p) <= g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_far(FwActor *a, const s32 *g) { FwActor *p = player(); if (p && fw_dist(a, p) > g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_see(FwActor *a, const s32 *g) { FwActor *p = player(); if (p && !(p->flags & FA_DEAD) && fw_can_see(a, p, g[0], 900)) fw_event(a, (int)g[1]); return 1; }
static int a_if_facing(FwActor *a, const s32 *g) { FwActor *p = player(); int d; if (!p) return 1; d = fw_angle_to(a, p->x, p->z); if (d < 0) d = -d; if (d <= g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_health(FwActor *a, const s32 *g) { if (cmp(a->hp, (int)g[0], g[1])) fw_event(a, (int)g[2]); return 1; }
static int a_if_var(FwActor *a, const s32 *g) { if (g[0] >= 0 && g[0] < FW_VARS && cmp(a->var[g[0]], (int)g[1], g[2])) fw_event(a, (int)g[3]); return 1; }
static int a_if_global(FwActor *a, const s32 *g) { if (g[0] >= 0 && g[0] < FW_GLOBALS && cmp(fw_global[g[0]], (int)g[1], g[2])) fw_event(a, (int)g[3]); return 1; }
static int a_if_button(FwActor *a, const s32 *g) { if (eng_in()->press & (u32)g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_held(FwActor *a, const s32 *g) { if (eng_in()->pad & (u32)g[0]) fw_event(a, (int)g[1]); return 1; }
static int a_if_ground(FwActor *a, const s32 *g) { if (a->flags & FA_GROUNDED) fw_event(a, (int)g[0]); return 1; }
static int a_if_player_dead(FwActor *a, const s32 *g) { FwActor *p = player(); if (p && (p->flags & FA_DEAD)) fw_event(a, (int)g[0]); return 1; }
static int a_if_kills(FwActor *a, const s32 *g) { if (cmp(fw_game.kills, (int)g[0], g[1])) fw_event(a, (int)g[2]); return 1; }
static int a_if_score(FwActor *a, const s32 *g) { if (cmp(fw_game.score, (int)g[0], g[1])) fw_event(a, (int)g[2]); return 1; }

/* ---- combat */
static int a_shoot(FwActor *a, const s32 *g) { fw_shoot(a, (int)g[0], g[1] ? (int)g[1] : 120, (int)g[2], 0); return 1; }
static int a_shoot_player(FwActor *a, const s32 *g) { fw_shoot(a, (int)g[0], g[1] ? (int)g[1] : 100, (int)g[2], 1); return 1; }
static int a_melee(FwActor *a, const s32 *g) { fw_melee(a, g[0] ? g[0] : 900, (int)g[1]); return 1; }
static int a_hitscan(FwActor *a, const s32 *g) { fw_hitscan(a, g[0] ? g[0] : 20000, (int)g[1], 0); return 1; }
static int a_damage_player(FwActor *a, const s32 *g) { fw_damage(player(), (int)g[0], fw_actor_index(a)); return 1; }
static int a_damage_self(FwActor *a, const s32 *g) { fw_damage(a, (int)g[0], -1); return 1; }
static int a_heal(FwActor *a, const s32 *g) { fw_heal(a, (int)g[0]); return 1; }
static int a_heal_player(FwActor *a, const s32 *g) { (void)a; fw_heal(player(), (int)g[0]); return 1; }
static int a_die(FwActor *a, const s32 *g) { (void)g; fw_damage(a, a->hp > 0 ? a->hp : 1, -1); return 1; }

/* ---- actors */
static int a_spawn(FwActor *a, const s32 *g)
{
    s32 x = a->x + ((isin(a->yaw) * g[1] + icos(a->yaw) * g[2]) >> 12), z = a->z + ((icos(a->yaw) * g[1] - isin(a->yaw) * g[2]) >> 12);
    int i = fw_spawn((int)g[0], x, a->y, z, a->yaw);
    if (i >= 0) fw_actor(i)->owner = (s16)fw_actor_index(a);
    return 1;
}
static int a_destroy(FwActor *a, const s32 *g) { (void)g; fw_destroy(a); return 1; }
static int a_visible(FwActor *a, const s32 *g) { if (g[0]) a->flags |= FA_VISIBLE; else a->flags &= (u16)~FA_VISIBLE; return 1; }
static int a_solid(FwActor *a, const s32 *g) { if (g[0]) a->flags |= FA_SOLID; else a->flags &= (u16)~FA_SOLID; return 1; }
static int a_color(FwActor *a, const s32 *g) { a->r = (u8)g[0]; a->g = (u8)g[1]; a->b = (u8)g[2]; return 1; }
static int a_size(FwActor *a, const s32 *g) { a->size = (s16)g[0]; return 1; }
static int a_spin(FwActor *a, const s32 *g) { fw_turn(a, g[0] ? (int)g[0] : 40); return 1; }

/* ---- variables */
static int a_set_var(FwActor *a, const s32 *g) { if (g[0] >= 0 && g[0] < FW_VARS) a->var[g[0]] = g[1]; return 1; }
static int a_add_var(FwActor *a, const s32 *g) { if (g[0] >= 0 && g[0] < FW_VARS) a->var[g[0]] += g[1]; return 1; }
static int a_set_global(FwActor *a, const s32 *g) { (void)a; if (g[0] >= 0 && g[0] < FW_GLOBALS) fw_global[g[0]] = g[1]; return 1; }
static int a_add_global(FwActor *a, const s32 *g) { (void)a; if (g[0] >= 0 && g[0] < FW_GLOBALS) fw_global[g[0]] += g[1]; return 1; }

/* ---- effects */
static int a_sound(FwActor *a, const s32 *g) { fw_sound((int)g[0], g[1] ? (int)g[1] : 3000, a->x, a->z); return 1; }
static int a_message(FwActor *a, const s32 *g) { (void)a; fw_message(fw_text((int)g[0]), g[1] ? (int)g[1] : 120); return 1; }
static int a_shake(FwActor *a, const s32 *g) { (void)a; fw_shake(g[0] ? (int)g[0] : 10); return 1; }
static int a_flash(FwActor *a, const s32 *g) { (void)a; fw_flash((int)g[0], (int)g[1], (int)g[2]); return 1; }
static int a_rumble(FwActor *a, const s32 *g) { (void)a; eng_rumble((int)g[0]); return 1; }

/* ---- the game */
static int a_score(FwActor *a, const s32 *g) { (void)a; fw_game.score += g[0]; return 1; }
static int a_key(FwActor *a, const s32 *g) { fw_game.keys |= 1 << ((g[0] < 0 ? a->var[0] : g[0]) & 7); return 1; }
static int a_win(FwActor *a, const s32 *g) { (void)a; (void)g; fw_game.won = 1; return 1; }
static int a_lose(FwActor *a, const s32 *g) { (void)a; (void)g; fw_game.lost = 1; return 1; }
static int a_anim(FwActor *a, const s32 *g) { a->var[5] = g[0]; return 1; }     /* animation clips: milestone 4 */

#define N ARG_NUM
#define E ARG_EVENT
#define P ARG_PROTO
#define S ARG_SOUND
#define B ARG_BUTTON
#define T ARG_TEXT
#define V ARG_VAR
#define G ARG_GLOBAL
#define O ARG_OP
#define X ARG_TARGET
#define ST ARG_STATE
const FwActionDef fw_actions[] = {
    /* flow */
    { "Wait", a_wait, 1, { N }, "ticks", "pause the do-list (60 ticks = 1 s)", "Flow" },
    { "WaitRandom", a_wait_random, 2, { N, N }, "min max", "pause a random time", "Flow" },
    { "SendEvent", a_send, 2, { E, X }, "event target", "raise an event: self, player, all, enemies, owner", "Flow" },
    { "Goto", a_goto, 1, { ST }, "state", "switch state now", "Flow" },
    { "IfRandom", a_if_random, 2, { N, E }, "percent event", "raise the event this often", "Flow" },
    { "IfTimeInState", a_if_time, 2, { N, E }, "ticks event", "raise once the state has run this long", "Flow" },
    /* movement */
    { "MoveForward", a_forward, 1, { N }, "speed", "walk the way it faces", "Move" },
    { "ChasePlayer", a_chase, 1, { N }, "speed", "turn to the player and walk at them", "Move" },
    { "FleePlayer", a_flee, 1, { N }, "speed", "walk away from the player", "Move" },
    { "FacePlayer", a_face, 1, { N }, "rate", "turn toward the player (rate a tick, 0 = at once)", "Move" },
    { "Turn", a_turn, 1, { N }, "rate", "turn every tick (4096 = full circle)", "Move" },
    { "Wander", a_wander, 1, { N }, "speed", "stroll about, turning at walls", "Move" },
    { "MoveTo", a_move_to, 3, { N, N, N }, "x z speed", "walk to a point (do: waits until there)", "Move" },
    { "Teleport", a_teleport, 2, { N, N }, "x z", "jump to a point", "Move" },
    { "Jump", a_jump, 1, { N }, "speed", "jump if on the ground", "Move" },
    { "Stop", a_stop, 0, { 0 }, "", "stand still", "Move" },
    { "Rise", a_rise, 1, { N }, "amount", "move up (flyers)", "Move" },
    /* senses */
    { "IfPlayerNear", a_if_near, 2, { N, E }, "dist event", "the player is within dist", "Sense" },
    { "IfPlayerFar", a_if_far, 2, { N, E }, "dist event", "the player is further than dist", "Sense" },
    { "IfSeePlayer", a_if_see, 2, { N, E }, "dist event", "the player is in front, in range, not behind a wall", "Sense" },
    { "IfFacingPlayer", a_if_facing, 2, { N, E }, "angle event", "it faces the player within angle", "Sense" },
    { "IfHealth", a_if_health, 3, { O, N, E }, "op value event", "compare its health", "Sense" },
    { "IfVar", a_if_var, 4, { V, O, N, E }, "var op value event", "compare one of its variables", "Sense" },
    { "IfGlobal", a_if_global, 4, { G, O, N, E }, "global op value event", "compare a game variable", "Sense" },
    { "IfButton", a_if_button, 2, { B, E }, "button event", "a button was pressed this tick", "Sense" },
    { "IfButtonHeld", a_if_held, 2, { B, E }, "button event", "a button is held", "Sense" },
    { "IfOnGround", a_if_ground, 1, { E }, "event", "standing on something", "Sense" },
    { "IfPlayerDead", a_if_player_dead, 1, { E }, "event", "the player has died", "Sense" },
    { "IfKills", a_if_kills, 3, { O, N, E }, "op value event", "compare the kill count", "Sense" },
    { "IfScore", a_if_score, 3, { O, N, E }, "op value event", "compare the score", "Sense" },
    /* combat */
    { "Shoot", a_shoot, 3, { P, N, N }, "shot speed damage", "fire a projectile the way it faces", "Combat" },
    { "ShootAtPlayer", a_shoot_player, 3, { P, N, N }, "shot speed damage", "fire a projectile at the player", "Combat" },
    { "Melee", a_melee, 2, { N, N }, "range damage", "hit what's in front", "Combat" },
    { "Hitscan", a_hitscan, 2, { N, N }, "range damage", "an instant shot along its facing", "Combat" },
    { "DamagePlayer", a_damage_player, 1, { N }, "amount", "hurt the player", "Combat" },
    { "DamageSelf", a_damage_self, 1, { N }, "amount", "hurt itself", "Combat" },
    { "Heal", a_heal, 1, { N }, "amount", "heal itself", "Combat" },
    { "HealPlayer", a_heal_player, 1, { N }, "amount", "heal the player", "Combat" },
    { "Die", a_die, 0, { 0 }, "", "drop dead (raises DIED)", "Combat" },
    /* actors */
    { "Spawn", a_spawn, 3, { P, N, N }, "type ahead side", "make an actor beside it", "Actor" },
    { "Destroy", a_destroy, 0, { 0 }, "", "remove it", "Actor" },
    { "SetVisible", a_visible, 1, { N }, "on", "show or hide it", "Actor" },
    { "SetSolid", a_solid, 1, { N }, "on", "whether things bump into it", "Actor" },
    { "SetColor", a_color, 3, { N, N, N }, "r g b", "its colour", "Actor" },
    { "SetSize", a_size, 1, { N }, "size", "how big it's drawn", "Actor" },
    { "Spin", a_spin, 1, { N }, "rate", "turn in place (pickups)", "Actor" },
    { "PlayAnim", a_anim, 1, { N }, "clip", "play an animation clip (models: coming)", "Actor" },
    /* variables */
    { "SetVar", a_set_var, 2, { V, N }, "var value", "set one of its variables", "Data" },
    { "AddVar", a_add_var, 2, { V, N }, "var amount", "add to one of its variables", "Data" },
    { "SetGlobal", a_set_global, 2, { G, N }, "global value", "set a game variable", "Data" },
    { "AddGlobal", a_add_global, 2, { G, N }, "global amount", "add to a game variable", "Data" },
    /* effects */
    { "PlaySound", a_sound, 2, { S, N }, "sound volume", "play a sound where it is", "Effect" },
    { "Message", a_message, 2, { T, N }, "text ticks", "show a line of text", "Effect" },
    { "Shake", a_shake, 1, { N }, "amount", "shake the camera", "Effect" },
    { "Flash", a_flash, 3, { N, N, N }, "r g b", "flash the screen", "Effect" },
    { "Rumble", a_rumble, 1, { N }, "strength", "rumble the controller", "Effect" },
    /* the game */
    { "AddScore", a_score, 1, { N }, "points", "add to the score", "Game" },
    { "GiveKey", a_key, 1, { N }, "colour", "give the player a key (0-7; -1 = the colour the level gave it, v0)", "Game" },
    { "WinGame", a_win, 0, { 0 }, "", "the level is complete", "Game" },
    { "LoseGame", a_lose, 0, { 0 }, "", "game over", "Game" },
};
const int fw_action_count = (int)(sizeof(fw_actions) / sizeof(fw_actions[0]));

int fw_action_find(const char *name)
{
    int i;
    for (i = 0; i < fw_action_count; i++) {
        const char *a = fw_actions[i].name, *b = name;
        while (*a && (*a == *b || (*a ^ 32) == *b)) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}
