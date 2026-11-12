/*
 * The interact button (Deadbird's A): doors and lifts on level 1 wait for level_use, a lift
 * keeps carrying her once it's set off, doors shut again after she leaves; and the framework's
 * default mode still opens doors by walking up to them.
 */
#include <stdio.h>
#include <stdlib.h>
#include "level.h"
#include "lab_data.h"

static int fails;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }
static void think(s32 x, s32 y, s32 z, int n) { while (n-- > 0) { level_think(x, y, z, 0); level_update(); } }

int main(void)
{
    int si, door = -1, lift = -1;
    level_init();
    for (si = 0; si < lab.nsecs; si++) {
        const LabSec *s = &lab.secs[si];
        if (s->kind == 1 && !s->lock && !(s->flags & SF_SECRET) && door < 0) door = si;
        if (s->kind == 2 && lift < 0) lift = si;
    }
    expect(door >= 0 && lift >= 0, "level 1 has an unlocked door and a lift");
    if (door < 0 || lift < 0) return 1;
    level_set_use_mode(1);
    level_init();
    {   /* the door: stand beside it */
        const LabSec *s = &lab.secs[door];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2, y = -s->k0, c0, c1;
        s32 x = s->x1 - s->x0 < s->z1 - s->z0 ? s->x0 - 700 : cx, z = s->x1 - s->x0 < s->z1 - s->z0 ? cz : s->z0 - 700;
        c0 = level_sector_ceil(door, cx, cz);
        think(x, y, z, 90);
        expect(level_sector_ceil(door, cx, cz) == c0, "a door stays shut when she just walks up to it");
        expect(level_use(x, y, z, 0) == LU_DOOR, "A beside a door opens it");
        think(x, y, z, 60);
        c1 = level_sector_ceil(door, cx, cz);
        expect(c1 == c0 - s->ka, "the door opens all the way");
        think(x + 20000, y, z, 400);
        expect(level_sector_ceil(door, cx, cz) == c0, "it shuts again once she's gone");
    }
    {   /* the lift: stand on it */
        const LabSec *s = &lab.secs[lift];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2, f0, f1, y;
        int i;
        think(cx + 30000, 0, cz, 200);                 /* let the opening ride (if it's this one) finish */
        f0 = level_sector_floor(lift, cx, cz);
        y = f0;
        think(cx, y, cz, 120);
        expect(level_sector_floor(lift, cx, cz) == f0, "a lift waits for A while she stands on it");
        expect(level_use(cx, y, cz, 0) == LU_LIFT, "A on the lift sets it off");
        for (i = 0; i < 600; i++) { level_think(cx, y, cz, 0); level_update(); y = level_sector_floor(lift, cx, cz); }   /* she rides its deck */
        f1 = level_sector_floor(lift, cx, cz);
        printf("      lift floor %d -> %d (map: %d .. %d)\n", (int)-f0, (int)-f1, s->ka, s->kb);
        expect(f1 != f0 && (-f1 == s->ka || -f1 == s->kb), "it carries her all the way to the other end");
        think(cx, y, cz, 200);
        expect(level_sector_floor(lift, cx, cz) == f1, "and stays there until she presses A again");
        expect(level_use(cx, y, cz, 0) == LU_LIFT, "A again takes it back");
        for (i = 0; i < 600; i++) { level_think(cx, y, cz, 0); level_update(); y = level_sector_floor(lift, cx, cz); }
        expect(level_sector_floor(lift, cx, cz) == f0, "back where it started");
    }
    level_set_use_mode(0);
    level_init();
    {   /* the framework's mode: doors open by themselves */
        const LabSec *s = &lab.secs[door];
        s32 cx = (s->x0 + s->x1) / 2, cz = (s->z0 + s->z1) / 2, c0 = level_sector_ceil(door, cx, cz);
        s32 x = s->x1 - s->x0 < s->z1 - s->z0 ? s->x0 - 700 : cx, z = s->x1 - s->x0 < s->z1 - s->z0 ? cz : s->z0 - 700;
        think(x, -s->k0, z, 60);
        expect(level_sector_ceil(door, cx, cz) < c0, "without use mode a door still opens as she comes near");
    }
    printf(fails ? "usetest: %d FAILURES\n" : "usetest: all passed\n", fails);
    return fails ? 1 : 0;
}
