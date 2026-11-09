/* The editor's map code against the real thing: maps/e1.map and maps/test.map parsed by
   game/edmap.c and compiled must equal what tools/make_map.py made (gen/map_*.c), byte for byte;
   then written back out, read in again and compiled, still equal. Fails on any difference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edmap.h"
#include "gen_lab.h"

static EmMap A, B;
static char text[1 << 20];
static int fails;

static void same(const char *what, const void *a, const void *b, size_t n, int count)
{
    size_t i;
    if (!memcmp(a, b, n)) return;
    for (i = 0; i < n && ((const unsigned char *)a)[i] == ((const unsigned char *)b)[i]; i++) {}
    printf("  FAIL %s differ (%d items, first byte %zu)\n", what, count, i);
    fails++;
}

static void compare(const char *name, const MapDef *ref, const MapDef *got)
{
    int i;
    printf("%s: %d/%d sectors, %d/%d points, %d/%d things\n", name, got->nsecs, ref->nsecs, got->npts, ref->npts, got->nthings, ref->nthings);
    if (got->nsecs != ref->nsecs || got->npts != ref->npts || got->nthings != ref->nthings || got->nrooms != ref->nrooms) { printf("  FAIL counts\n"); fails++; return; }
    same("points", ref->pts, got->pts, sizeof(MapPt) * (size_t)ref->npts, ref->npts);
    for (i = 0; i < ref->npts; i++) {
        const MapWall *a = &ref->walls[i], *b = &got->walls[i];
        if (a->next != b->next || a->mid != b->mid || a->low != b->low || a->up != b->up || a->flags != b->flags) {
            printf("  FAIL wall %d: ref %d %d %d %d %d, got %d %d %d %d %d\n", i, a->next, a->mid, a->low, a->up, a->flags, b->next, b->mid, b->low, b->up, b->flags);
            fails++; break;
        }
    }
    for (i = 0; i < ref->nsecs; i++) {
        const MapSector *a = &ref->secs[i], *b = &got->secs[i];
        if (a->p0 != b->p0 || a->np != b->np || a->floor != b->floor || a->ceil != b->ceil || a->fslope != b->fslope || a->cslope != b->cslope ||
            a->fslope_h != b->fslope_h || a->cslope_h != b->cslope_h || a->ftex != b->ftex || a->ctex != b->ctex || a->room != b->room ||
            a->kind != b->kind || a->lock != b->lock || a->ka != b->ka || a->kb != b->kb) {
            printf("  FAIL sector %d: floor %d/%d ceil %d/%d ftex %d/%d ctex %d/%d kind %d/%d\n", i, a->floor, b->floor, a->ceil, b->ceil, a->ftex, b->ftex, a->ctex, b->ctex, a->kind, b->kind);
            fails++; break;
        }
    }
    same("things", ref->things, got->things, sizeof(MapThing) * (size_t)ref->nthings, ref->nthings);
    for (i = 0; i < ref->nrooms; i++) if (strcmp(ref->rooms[i], got->rooms[i])) { printf("  FAIL room %d: %s vs %s\n", i, ref->rooms[i], got->rooms[i]); fails++; }
}

/* the props stream has no length: copy the compiled one so the next compile can't overwrite it */
static int props_len(const s16 *p)
{
    static const int ARGS[] = { 0, 1, 10, 13, 0, 4, 4, 4, 2, 0, 7, 8, 5, 6, 14, 16, 4, 3, 3, 5, 4, 1 };
    int n = 0;
    for (;;) {
        int op = p[n++];
        if (op == OP_END) return n;
        n += ARGS[op];
    }
}

static void run(const char *name, const char *src, const MapDef *ref)
{
    const MapDef *d;
    int len, sec, wall;
    static s16 props_copy[1 << 16];
    strcpy(A.name, name);
    if (!em_parse(&A, src)) { printf("%s: FAIL parse: %s\n", name, em_error); fails++; return; }
    if (em_check(&A, &sec, &wall)) { printf("%s: FAIL check: %s (sector %d wall %d)\n", name, em_error, sec, wall); fails++; return; }
    d = em_compile(&A, 1);
    if (!d) { printf("%s: FAIL compile: %s\n", name, em_error); fails++; return; }
    {   /* pad the ref compare: both streams must match up to and including OP_END */
        int nr = props_len(ref->props), ng = props_len(d->props);
        if (nr != ng) { printf("  FAIL props length %d vs %d\n", nr, ng); fails++; }
        memcpy(props_copy, d->props, sizeof(s16) * (size_t)ng);
        memset(props_copy + ng, 0x7f, 2);
    }
    {
        int k, nr = props_len(ref->props);
        for (k = 0; k < nr; k++) if (ref->props[k] != d->props[k]) { printf("  FAIL props differ at %d: %d vs %d\n", k, ref->props[k], d->props[k]); fails++; break; }
    }
    {
        MapDef r2 = *ref; static s16 zero[1];
        (void)zero;
        r2.props = d->props;                 /* props already compared */
        compare(name, &r2, d);
    }
    len = em_write(&A, text, sizeof(text));
    if (len < 0) { printf("%s: FAIL write\n", name); fails++; return; }
    if (!em_parse(&B, text)) { printf("%s: FAIL re-parse of the written map: %s\n", name, em_error); fails++; return; }
    d = em_compile(&B, 1);
    if (!d) { printf("%s: FAIL re-compile: %s\n", name, em_error); fails++; return; }
    {
        int k, nr = props_len(ref->props);
        for (k = 0; k < nr; k++) if (ref->props[k] != d->props[k]) { printf("  FAIL round-trip props differ at %d\n", k); fails++; break; }
        { MapDef r2 = *ref; r2.props = d->props; printf("  (after save + load) "); compare(name, &r2, d); }
    }
    printf("  saved text: %d bytes\n", len);
    if (getenv("EDTEST_DUMP")) { FILE *f = fopen(getenv("EDTEST_DUMP"), "w"); if (f) { fwrite(text, 1, (size_t)len, f); fclose(f); } }
}

int main(void)
{
    static char src[1 << 18];
    FILE *f;
    size_t n;
    run("e1", em_src_e1, &map_e1);
    f = fopen("../../maps/test.map", "rb");
    if (!f) f = fopen("maps/test.map", "rb");
    if (f) { n = fread(src, 1, sizeof(src) - 1, f); src[n] = 0; fclose(f); run("test", src, &map_test); }
    else { printf("FAIL: maps/test.map not found\n"); fails++; }
    /* a fresh level checks out and compiles */
    em_new_level(&A, "NEW");
    {
        int s, w;
        if (em_check(&A, &s, &w) || !em_compile(&A, 1)) { printf("new level: FAIL %s\n", em_error); fails++; }
        else printf("new level: ok\n");
    }
    {   /* the editing operations on a fresh room */
        int sec, wall, k, before;
        s32 rx[4] = { -1024, 1024, 1024, -1024 }, rz[4] = { -1024, -1024, 1024, 1024 };
#define OK(what) do { if (em_check_geom(&A, &sec, &wall) || !em_compile(&A, 1)) { printf("ops: FAIL after %s: %s\n", what, em_error); fails++; } else printf("ops: %s ok (%d sectors)\n", what, A.nsecs); } while (0)
        em_new_level(&A, "OPS");
        k = em_carve(&A, 0, rx, rz, 4);
        if (k < 0) { printf("ops: FAIL carve: %s\n", em_error); fails++; }
        else { A.secs[k].floor = 512; if (A.secs[k].np != 4) { printf("ops: FAIL carved piece has %d corners\n", A.secs[k].np); fails++; } }
        OK("carve a platform in the middle");
        { int s2 = em_sector_at(&A, -3500, 3500), w2, wi; em_near_wall(&A, -4096, 3500, 300, &w2, &wi); (void)s2;
          k = em_extrude(&A, w2, wi, 4096); if (k < 0) { printf("ops: FAIL extrude: %s\n", em_error); fails++; } }
        OK("extrude the west wall 4 m");
        before = A.nsecs;
        k = em_sector_at(&A, -6000, 3500);
        if (em_stairs(&A, k, 3, 1024, 0) <= 0) { printf("ops: FAIL stairs: %s\n", em_error); fails++; }
        OK("stairs going west");
        printf("ops: stairs made %d steps\n", A.nsecs - before + 1);
        { int w2, wi; em_near_wall(&A, -4100, 3500, 300, &w2, &wi);
          k = em_join(&A, w2, wi); if (k < 0 || !k) { printf("ops: join returned %d: %s\n", k, em_error); } }
        OK("join the first step back into the room");
        em_new_level(&A, "CUT");
        if (!em_cut(&A, 0, -4096, 0, 4096, 1024)) { printf("ops: FAIL cut: %s\n", em_error); fails++; }
        OK("cut the room in two");
        { s32 px[1] = { 4096 }, pz[1] = { 4096 }; em_move_points(&A, px, pz, 1, 1024, 1024); }
        OK("move a corner");
        if (em_delete_corner(&A, 5120, 5120) <= 0) printf("ops: delete corner: %s\n", em_error);
        OK("delete a corner");
        { s32 bx[4] = { -1024, 6000, 6000, -1024 }, bz[4] = { -9000, -9000, -2000, -2000 };   /* overlapping: must be refused */
          em_add_sector(&A, bx, bz, 4, -1);
          if (!em_check_geom(&A, &sec, &wall)) { printf("ops: FAIL an overlapping sector was accepted\n"); fails++; } else printf("ops: overlap refused: %s\n", em_error); }
#undef OK
    }
    printf(fails ? "edtest: %d FAILURES\n" : "edtest: all passed\n", fails);
    return fails ? 1 : 0;
}
