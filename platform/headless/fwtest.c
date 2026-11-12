/*
 * The gameplay framework, tested: loads HELL GATE (examples/fps) and checks the state machines,
 * the action library, combat and winning, with screenshots. Fails (exit 1) on a regression.
 *   fwtest <out dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include "fwrt.h"
#include "fwproj.h"
#include "fsm.h"
#include "gpu.h"
#include "snd.h"
#include "hellgate_game.h"

extern const FwWorld sector_world;
void sector_world_init(void);
const App game_app = FW_RUNTIME_APP("FWTEST", 0, "fwtest");

static int fails;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static void run(u32 pad, int n) { static s16 mix[736]; while (n-- > 0) { eng_frame(pad); snd_mix(mix, 368); } }

static void shot(const char *dir, const char *name)
{
    static unsigned char row[SCREEN_W_MAX * 3 + 4];
    unsigned char h[54] = { 'B', 'M' };
    char path[512];
    int x, y, dx, dy, pitch = (SCREEN_W * 3 + 3) & ~3, size = 54 + pitch * SCREEN_H;
    const u16 *v = gpu_vram();
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    if (!(f = fopen(path, "wb"))) return;
    eng_display_area(&dx, &dy);
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255; h[10] = 54; h[14] = 40;
    h[18] = SCREEN_W & 255; h[19] = SCREEN_W >> 8; h[22] = SCREEN_H; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = SCREEN_H - 1; y >= 0; y--) {
        for (x = 0; x < SCREEN_W; x++) {
            u16 c = v[(dy + y) * VRAM_W + dx + x];
            row[x * 3] = (unsigned char)((c >> 10 & 31) << 3); row[x * 3 + 1] = (unsigned char)((c >> 5 & 31) << 3); row[x * 3 + 2] = (unsigned char)((c & 31) << 3);
        }
        fwrite(row, 1, (size_t)pitch, f);
    }
    fclose(f);
}

static int find_type(const char *name, int alive)
{
    int i, p = fw_proto_find(name);
    for (i = 1; i < FW_ACTORS; i++) { FwActor *a = fw_actor(i); if (a && a->proto == p && (!alive || !(a->flags & FA_DEAD))) return i; }
    return -1;
}
static const char *state_name(const FwActor *a) { return a->state >= 0 ? fsm_s[a->state].name : "-"; }

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    FwActor *pl, *imp;
    int ii, k, alerted = 0, clawed = 0, hp0;
    FwProject pj;
    eng_init();
    sector_world_init();
    fw_set_world(&sector_world);

    /* the loader: a good project, then broken ones */
    expect(!fw_project_load("fsm a\n state s\n  do Bogus 1\nend\n", 33, &pj), "an unknown action is an error");
    printf("      (%s)\n", fw_project_error());
    expect(!fw_project_load("fsm a\n state s\n  on HIT nowhere\nend\n", 36, &pj), "a transition to a missing state is an error");
    expect(fw_runtime_load(HELLGATE, (int)sizeof(HELLGATE) - 1), "HELL GATE loads");
    printf("      %d types, %d machines, %d states, %d actions, %d actions in the library\n", fw_proto_count(), fsm_nm, fsm_ns, fsm_na, fw_action_count);
    expect(find_type("imp", 1) >= 0, "the map's troopers became imps");
    expect(find_type("exit", 0) >= 0, "the map's exit became the exit type");

    /* an imp: put the player in front of it, at a distance */
    pl = fw_actor(FW_PLAYER);
    ii = find_type("imp", 1);
    imp = fw_actor(ii);
    expect(imp && !strcmp(state_name(imp), "idle"), "imps start idle");
    {   /* a clear spot 3.5 m from it, in its line of sight */
        int d, best = -1;
        for (d = 0; d < 4096 && best < 0; d += 256) {
            VECTOR a, b;
            s32 x = imp->x + (isin(d) * 3500 >> 12), z = imp->z + (icos(d) * 3500 >> 12);
            s32 g = sector_world.ground(x, z, imp->y - 300);
            a.vx = imp->x; a.vy = imp->y - 1200; a.vz = imp->z; b.vx = x; b.vy = g - 1200; b.vz = z;
            if (g - imp->y > 200 || imp->y - g > 200 || sector_world.clip(&a, &b, 0) < 4090) continue;
            best = d; pl->x = x; pl->z = z; pl->y = g;
        }
        expect(best >= 0, "found a clear spot near an imp");
    }
    imp->yaw = ratan2(pl->x - imp->x, pl->z - imp->z);
    fw_look(ratan2(imp->x - pl->x, imp->z - pl->z) - pl->yaw);
    {
        VECTOR a, b;
        a.vx = imp->x; a.vy = imp->y - 1200; a.vz = imp->z; b.vx = pl->x; b.vy = pl->y - 1200; b.vz = pl->z;
        printf("      see %d, clip %d, angle %d, imp y %d pl y %d ground at imp %d\n", fw_can_see(imp, pl, 9000, 2048), sector_world.clip(&a, &b, 0), fw_angle_to(imp, pl->x, pl->z), (int)imp->y, (int)pl->y, (int)sector_world.ground(imp->x, imp->z, imp->y - 300));
    }
    for (k = 0; k < 240; k++) {
        run(0, 1);
        if (!strcmp(state_name(imp), "alert")) alerted = 1;
        if (!strcmp(state_name(imp), "claw")) clawed = 1;
        if (k == 20) shot(out, "fw_1_imp_sees_you");
    }
    printf("      imp is %s (state %d fsm %d), player hp %d, dist %d, imp hp %d ev %x st_t %d x %d\n", state_name(imp), imp->state, imp->fsm, (int)pl->hp, (int)fw_dist(pl, imp), (int)imp->hp, imp->events, imp->state_t, (int)imp->x);
    expect(alerted, "the imp sees the player (IfSeePlayer -> alert)");
    expect(clawed, "the imp closes in and claws (ChasePlayer, IfPlayerNear -> claw)");
    expect(pl->hp < 100, "the claws hurt (Melee)");
    shot(out, "fw_2_close");
    /* shoot it until it dies */
    hp0 = (int)imp->hp;
    fw_look(ratan2(imp->x - pl->x, imp->z - pl->z) - pl->yaw);
    for (k = 0; k < 200 && !(imp->flags & FA_DEAD); k++) { fw_look(ratan2(imp->x - pl->x, imp->z - pl->z) - pl->yaw); run(PAD_SQUARE, 1); if (k == 3) shot(out, "fw_3_firing"); }
    expect(imp->hp < hp0, "the gun hits (Hitscan)");
    expect((imp->flags & FA_DEAD) && !strcmp(state_name(imp), "dead"), "the imp dies (DIED -> dead)");
    run(0, 5);
    expect(fw_game.score == 100 && fw_game.kills == 1, "a kill scores (AddScore)");
    run(0, 200);
    expect(!fw_actor(ii) || fw_actor(ii)->proto != imp->proto || !fw_actor(ii)->used, "the body is removed (Wait, Destroy)");

    /* walk into the exit */
    {
        int e = find_type("exit", 0);
        FwActor *x = fw_actor(e);
        pl->x = x->x; pl->z = x->z; pl->y = sector_world.ground(pl->x, pl->z, x->y - 300); pl->hp = 100; pl->flags &= (u16)~FA_DEAD; fw_game.lost = 0;
        fw_look(-pl->yaw);
        run(0, 2);
        for (k = 0; k < 30 && !fw_game.won; k++) run(0, 1);
        printf("      exit state %s used %d dist %d won %d lost %d hp %d\n", state_name(x), x->used, (int)fw_dist(pl, x), fw_game.won, fw_game.lost, (int)pl->hp);
        run(0, 40);
        shot(out, "fw_4_exit");
        expect(fw_game.won, "reaching the exit wins (IfPlayerNear -> WinGame)");
    }
    printf(fails ? "fwtest: %d FAILURES\n" : "fwtest: all passed\n", fails);
    return fails ? 1 : 0;
}
