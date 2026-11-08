/* Movement stress test: drives the player with pseudo-random stick input around the test level
   for a long time and fails on glitches: teleports, falling through the floor, getting stuck
   inside walls, wild pose values. */
#include <stdio.h>
#include "gte.h"
#include "model.h"
#include "player.h"
#include "level.h"
#undef DEMON_MESH_NO_DATA
#include "demon_mesh.h"

static Player pl;
static u32 rnd = 12345;
static u32 rn(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 16; }

int main(void)
{
    Model m;
    int f, yaw = 0, mag = 0, hold = 0, fails = 0, flick = 0, air_starts = 0;
    int stuck = 0, since = 0, prev_g = 1, max_jump = 0, max_drop = 0, max_turn = 0;
    s32 px, py, pz;
    u32 pad = 0;
    level_init();
    m.verts = (const DVERT *)demon_verts; m.nverts = DEMON_NVERTS;
    m.prims = (const DPRIM *)demon_prims; m.nprims = DEMON_NPRIMS;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    player_init(&m);
    { s32 sx, sz; int sy; level_spawn(&sx, &sz, &sy); player_reset(&pl, sx, sz, sy); }
    px = pl.x; py = pl.y; pz = pl.z;
    for (f = 0; f < 60 * 60 * 5; f++) {
        if (--hold <= 0) {
            hold = 10 + (int)(rn() % 90); since = 0;
            yaw = (int)(rn() % 4096);
            switch (rn() % 5) { case 0: mag = 0; break; case 1: case 2: mag = 2400; break; default: mag = 4096; }
            pad = (rn() % 9 == 0) ? BTN_JUMP : 0;
        } else pad &= ~BTN_JUMP;
        since++;
        player_update(&pl, pad, yaw, mag);
        player_pose(&pl, &m);
        {
            s32 dx = pl.x - px, dz = pl.z - pz, d2 = dx * dx + dz * dz;
            int d = (int)isqrt((u32)d2);
            if (d > max_jump) max_jump = d;
            if (d > 200) { if (fails++ < 10) printf("frame %d: teleported %d units from %d %d to %d %d y %d\n", f, d, (int)px, (int)pz, (int)pl.x, (int)pl.z, (int)pl.y); }
            if (pl.y > 1) { if (fails++ < 10) printf("frame %d: below the floor (y %d)\n", f, (int)pl.y); }
            if (pl.y < -3000) { if (fails++ < 10) printf("frame %d: flew away (y %d)\n", f, (int)pl.y); }
        }
        {   /* stuck inside geometry for more than a few frames? (stepping off a stair edge can
               leave her overlapping for a frame while she settles) */
            s32 cx = pl.x, cz = pl.z, nx, nz;
            if (!level_collide(&cx, &cz, pl.y, 150, &nx, &nz)) stuck = 0;
            else if (++stuck > 3) { if (fails++ < 3) printf("frame %d: inside a wall at %d %d y %d (pushed to %d %d)\n", f, (int)pl.x, (int)pl.z, (int)pl.y, (int)cx, (int)cz); }
        }
        if (pl.grounded != prev_g) { flick++; if (!pl.grounded) air_starts++; }
        if (pl.drop > max_drop) max_drop = (int)pl.drop;
        if (pl.turn > max_turn) max_turn = pl.turn;
        if (-pl.turn > max_turn) max_turn = -pl.turn;
        if (pl.drop < -40 || pl.drop > 500) { if (fails++ < 10) printf("frame %d: grounding drop %d\n", f, (int)pl.drop); }
        prev_g = pl.grounded;
        px = pl.x; py = pl.y; pz = pl.z;
    }
    /* scenario 0: walking / running straight on flat ground: a planted hoof must stay put */
    level_debug_flat(1);
    {
        int pass, worst = 0;
        for (pass = 0; pass < 2; pass++) {
            s32 prev[2][2] = { { 0, 0 }, { 0, 0 } };
            int was[2] = { 0, 0 }, si;
            player_reset(&pl, -3000, -3400, 0);
            for (f = 0; f < 150; f++) {
                player_update(&pl, 0, 0, pass ? 4096 : 2400);
                player_pose(&pl, &m);
                for (si = 0; si < 2; si++) {
                    const MATRIX *h = &pl.world[si ? DEMON_BONE_HOOF_R : DEMON_BONE_HOOF_L];
                    int planted = !pl.foot_swing[si] && f > 60;
                    if (planted && was[si]) {
                        s32 dx = h->t[0] - prev[si][0], dz = h->t[2] - prev[si][1];
                        int d = (int)isqrt((u32)(dx * dx + dz * dz));
                        if (d > worst) worst = d;
                    }
                    was[si] = planted; prev[si][0] = h->t[0]; prev[si][1] = h->t[2];
                }
            }
        }
        printf("foot slide test: planted hooves move at most %d units a frame (run speed is 70)\n", worst);
        if (worst > 12) { printf("FAIL: planted feet slide\n"); fails++; }
    }
    /* scenario 1: running, then the stick goes 90 degrees right: she must turn and go that way */
    player_reset(&pl, -3000, -3000, 0);
    for (f = 0; f < 60; f++) { player_update(&pl, 0, 0, 4096); player_pose(&pl, &m); }
    for (f = 0; f < 25; f++) { player_update(&pl, 0, 1024, 4096); player_pose(&pl, &m); }
    {
        int dv = player_input_angle(pl.vx, pl.vz);
        printf("turn test: facing %d, moving %d (want ~1024), speed %d\n", pl.yaw, dv, (int)pl.speed);
        if (pl.yaw < 900 || pl.yaw > 1150 || dv < 900 || dv > 1150 || pl.speed < 50) { printf("FAIL: can't turn while running\n"); fails++; }
    }
    level_debug_flat(0);
    /* scenario 2: walk into the loading dock's south wall at 45 degrees: she must slide along
       it, facing along it, walking */
    player_reset(&pl, -17500, -29500, 2048);
    for (f = 0; f < 60; f++) { player_update(&pl, 0, 1536, 2400); player_pose(&pl, &m); }
    {
        int dv = player_input_angle(pl.vx, pl.vz);
        printf("wall slide test: x %d z %d, facing %d, moving %d (want ~1024), speed %d, phase %d\n",
               (int)pl.x, (int)pl.z, pl.yaw, dv, (int)pl.speed, pl.phase);
        if (pl.x < -17000 || pl.yaw < 850 || pl.yaw > 1200 || pl.speed < 12) { printf("FAIL: doesn't walk along the wall\n"); fails++; }
    }
    /* scenario 3: the whole level, spawn -> reactor, running from waypoint to waypoint: every
       doorway and hall must be passable, and it tells us how long the level is */
    for (int pass = 0; pass < 2; pass++) {
        s32 sx, sz, wx, wz;
        int sy, wp = 1, frames = 0, last_wp_frame = 0, ok = 1;
        level_spawn(&sx, &sz, &sy);
        player_reset(&pl, sx, sz, sy);
        while (wp < level_route_count()) {
            s32 dx, dz;
            level_route_point(wp, &wx, &wz);
            dx = wx - pl.x; dz = wz - pl.z;
            if (dx * (long long)dx + dz * (long long)dz < 450LL * 450) { wp++; last_wp_frame = frames; continue; }
            player_update(&pl, 0, player_input_angle(dx, dz), pass ? 4096 : 2400);
            player_pose(&pl, &m);
            if (++frames - last_wp_frame > 900) {
                s32 px2, pz2; level_route_point(wp - 1, &px2, &pz2);
                printf("FAIL: route blocked between waypoints %d (%d,%d) and %d (%d,%d); stuck at %d %d\n",
                       wp - 1, (int)px2, (int)pz2, wp, (int)wx, (int)wz, (int)pl.x, (int)pl.z);
                fails++; ok = 0; break;
            }
        }
        if (ok) printf("route test: spawn to reactor %s in %d frames (%d s)\n", pass ? "running" : "walking", frames, frames / 60);
    }
    printf("%d random frames: max move %d u/f, max turn %d, max drop %d, %d jumps/falls, %d failures\n",
           60 * 60 * 5, max_jump, max_turn, max_drop, air_starts, fails);
    (void)py; (void)flick;
    return fails ? 1 : 0;
}
