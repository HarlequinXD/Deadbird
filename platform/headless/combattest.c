/* Combat system test: drives the real player + chainsaw combo against
   1. a dummy enemy target in front of her (each swing must hit it exactly once, hp must drop)
   2. a wall (the blade must report a wall / prop hit and kick her back)
   3. open floor (the overhead slam must report a floor hit)
   Exits non-zero on failure (run by CI). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "gte.h"
#include "model.h"
#include "demon_mesh.h"       /* (first: with the vertex data) */
#include "player.h"
#include "level.h"
#include "combat.h"

static Player pl;
static Model m;
static int hits_on_dummy, last_move_hit[4];
static CombatTarget *dummy_ptr;

static void dummy_hit(CombatTarget *t, const Hit *h)
{
    t->hp -= h->damage;
    t->x += h->dir_x * (h->knockback / 16) * 12 >> 12;            /* knocked back like an enemy would be */
    t->z += h->dir_z * (h->knockback / 16) * 12 >> 12;
    if (t->hp <= 0) { t->hp = 0; t->alive = 0; }
    hits_on_dummy++;
    last_move_hit[h->move & 3]++;
}

static void blade(VECTOR *a, VECTOR *b)
{
    SVECTOR s;
    s.vx = (s16)(DEMON_MOUNT_X + DEMON_MOUNT_DX * 90 / 4096); s.vy = (s16)DEMON_MOUNT_Y; s.vz = (s16)(DEMON_MOUNT_Z + DEMON_MOUNT_DZ * 90 / 4096);
    mat_apply(&pl.world[DEMON_MOUNT_BONE], &s, a);
    s.vx = (s16)(DEMON_MOUNT_X + DEMON_MOUNT_DX * 480 / 4096); s.vz = (s16)(DEMON_MOUNT_Z + DEMON_MOUNT_DZ * 480 / 4096);
    mat_apply(&pl.world[DEMON_MOUNT_BONE], &s, b);
}

/* run the full combo (pressing attack every few frames); returns event counts by kind */
static void combo(int *count)
{
    Attack at;
    int f, reached3 = 0;
    memset(&at, 0, sizeof(at));
    memset(count, 0, sizeof(int) * 5);
    for (f = 0; f < 140; f++) {
        u32 pad = (!reached3 && f % 6 == 0) ? BTN_ATTACK : 0;     /* one full combo */
        CombatEvent ev[4];
        int n, i;
        player_update(&pl, pad, 0, 0);
        player_pose(&pl, &m);
        if (pl.atk == 3) reached3 = 1;
        if (pl.atk_event >= 1 && pl.atk_event <= 3) attack_begin(&at, TEAM_PLAYER, pl.atk_event, 20, 400, 10, 70);
        if (!pl.atk) { attack_end(&at); continue; }
        if (getenv("DBG2") && pl.atk_event) printf("  f%d event %d at x %d (dummy x %d)\n", f, pl.atk_event, (int)pl.x, (int)dummy_ptr->x);
        if (getenv("DBG") && pl.atk == 3) { VECTOR a, b; blade(&a, &b); printf("  slam t%d live%d tip y %d base y %d (floor %d)\n", pl.atk_t, pl.atk_active, (int)b.vy, (int)a.vy, (int)pl.y); }
        if (!pl.atk_active) continue;
        {
            VECTOR a, b;
            blade(&a, &b);
            at.ox = pl.x; at.oz = pl.z;
            n = attack_update(&at, &a, &b, ev, 4);
        }
        for (i = 0; i < n; i++) {
            count[ev[i].kind]++;
            if (ev[i].kind != COMBAT_HIT_TARGET) player_hit_react(&pl, ev[i].nrm.vx, ev[i].nrm.vz, ev[i].nrm.vy ? 0 : 288, 4);
            else player_hit_react(&pl, 0, 0, 0, 5);
        }
    }
}

static int area_named(const char *n)
{
    int a;
    for (a = 0; a < level_area_count(); a++) if (!strcmp(level_area(a)->name, n)) return a;
    return -1;
}

int main(void)
{
    static CombatTarget dummy;
    dummy_ptr = &dummy;
    int c[5], fail = 0, a;
    const LabArea *ar;
    m.verts = (const DVERT *)demon_verts; m.nverts = DEMON_NVERTS;
    m.prims = (const DPRIM *)demon_prims; m.nprims = DEMON_NPRIMS;
    m.bones = (const DBONE *)demon_bones; m.nbones = DEMON_NBONES;
    player_init(&m);
    a = area_named("Turbine Hall");
    ar = level_area(a);

    /* 1. dummy enemy right in front of her, out in the open hall */
    combat_reset();
    player_reset(&pl, ar->x0 + 2500, (ar->z0 + ar->z1) / 2, 1024);          /* facing +X */
    player_update(&pl, 0, 0, 0); player_pose(&pl, &m);
    memset(&dummy, 0, sizeof(dummy));
    dummy.x = pl.x + 800; dummy.y = pl.y; dummy.z = pl.z; dummy.radius = 220; dummy.height = 1900;
    dummy.hp = dummy.max_hp = 1000; dummy.team = TEAM_ENEMY; dummy.on_hit = dummy_hit;
    combat_add(&dummy); dummy_ptr = &dummy;
    combo(c);
    printf("dummy: %d hits (swing 1:%d 2:%d 3:%d), hp %d/%d, floor hits %d\n", hits_on_dummy,
           last_move_hit[1], last_move_hit[2], last_move_hit[3], dummy.hp, dummy.max_hp, c[COMBAT_HIT_FLOOR]);
    if (last_move_hit[1] != 1 || last_move_hit[2] != 1 || last_move_hit[3] != 1) { printf("FAIL: each swing should hit the dummy exactly once\n"); fail = 1; }
    if (dummy.hp >= dummy.max_hp) { printf("FAIL: no damage\n"); fail = 1; }

    /* 2. a wall: stand close to the hall's north wall, facing it */
    combat_reset();
    player_reset(&pl, ar->x0 + 4000, ar->z1 - 450, 0);                       /* facing +Z, wall ahead */
    player_update(&pl, 0, 0, 0); player_pose(&pl, &m);
    combo(c);
    printf("wall: wall %d, prop %d, floor %d hits\n", c[COMBAT_HIT_WALL], c[COMBAT_HIT_PROP], c[COMBAT_HIT_FLOOR]);
    if (c[COMBAT_HIT_WALL] + c[COMBAT_HIT_PROP] == 0) { printf("FAIL: swinging into a wall never hit it\n"); fail = 1; }

    /* 3. open floor: the slam must bite the floor */
    combat_reset();
    player_reset(&pl, ar->x0 + 2500, (ar->z0 + ar->z1) / 2, 1024);
    player_update(&pl, 0, 0, 0); player_pose(&pl, &m);
    combo(c);
    printf("open floor: floor %d, wall %d, prop %d hits\n", c[COMBAT_HIT_FLOOR], c[COMBAT_HIT_WALL], c[COMBAT_HIT_PROP]);
    if (c[COMBAT_HIT_FLOOR] == 0) { printf("FAIL: the slam never hit the floor\n"); fail = 1; }

    if (!fail) printf("combat test passed\n");
    return fail;
}
