/*
 * The sector world for framework games: Deadbird's sector-map level (game/level.c: the map
 * builder, lighting, renderer, collision, doors and lifts) behind the FwWorld interface.
 * Milestone 2 moves this world into the engine itself.
 */
#include "fw.h"
#include "fwsfx.h"
#include "level.h"
#include "map.h"

/* levels come from the app's loader when it sets one (the Studio's: a .map file through the map
   builder); without one, or if it fails, Deadbird's level 1 */
static int (*map_loader)(const char *name);
void sector_world_set_loader(int (*load)(const char *name)) { map_loader = load; }
static void w_load(const char *name)
{
    if (map_loader && map_loader(name ? name : "")) return;     /* "" : the loader restores level 1 */
    level_init();
}
static void w_spawn(s32 *x, s32 *z, int *yaw) { level_spawn(x, z, yaw); }
static s32  w_ground(s32 x, s32 z, s32 y) { return level_ground(x, z, y); }
static int  w_collide(s32 *x, s32 *z, s32 y, s32 r, s32 *nx, s32 *nz) { return level_collide(x, z, y, r, nx, nz); }
static int  w_clip(const VECTOR *a, const VECTOR *b, s32 m) { return level_clip_segment(a, b, m); }
static void w_draw(FrameBuf *fb, const MATRIX *v, const PROJ *pr, int t) { level_draw(fb, v, pr, t); }

/* doors and lifts run off the player; the map's keys and shards are the level's own pickups */
static void w_update(s32 px, s32 py, s32 pz)
{
    int arg, type;
    static const char *const KEY[4] = { "A KEY", "CYAN KEY", "AMBER KEY", "CRIMSON KEY" };
    level_think(px, py, pz, (u32)fw_game.keys);
    level_update();
    type = level_pickup(px, py, pz, &arg);
    if (type == TH_KEY) { fw_game.keys |= 1 << (arg & 7); fw_message(KEY[arg & 3], 120); fw_sound(FWS_PICKUP, 3500, px, pz); }
    else if (type == TH_SHARD) { fw_game.score += 250; fw_message("SECRET FOUND", 120); fw_sound(FWS_PICKUP, 3500, px, pz); }
}

/* placed things the game spawns actors for (keys and shards stay the level's) */
static int w_thing_count(void) { return level_thing_count(); }
static int w_thing(int i, int *type, int *arg, s32 *x, s32 *z, int *yaw)
{
    const LabThing *t = level_thing(i);
    if (t->type == TH_KEY || t->type == TH_SHARD || t->type == TH_START) return 0;
    *type = t->type; *arg = t->arg; *x = t->x; *z = t->z; *yaw = t->yaw;
    return 1;
}
static int w_thing_type(const char *n)
{
    static const char *const N[] = { "start", "trooper", "key", "shard", "exit", "way" };
    int i, k;
    for (i = 0; i < 6; i++) {
        for (k = 0; N[i][k] && (N[i][k] == n[k] || N[i][k] == (n[k] | 32)); k++) {}
        if (!N[i][k] && !n[k]) return i;
    }
    return -1;
}

static void hook_sound(int id, int vol, s32 x, s32 z, int pitch) { (void)id; (void)pitch; fw_sound(FWS_CLICK, vol / 2, x, z); }
static void hook_locked(int colour) { (void)colour; fw_message("LOCKED - FIND THE KEY", 90); }

const FwWorld sector_world = {
    w_load, w_spawn, w_ground, w_collide, w_clip, w_update, w_draw, w_thing_count, w_thing, w_thing_type,
};

void sector_world_init(void)
{
    static const LevelHooks hk = { hook_sound, hook_locked };
    level_set_hooks(&hk);
}
