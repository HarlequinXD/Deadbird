/*
 * The Studio's Geometry tab: the level's sectors from above (and a 3D fly-through), drawn and
 * edited with the engine's sector-map core (game/edmap.c: the same .map format the builder reads).
 *
 * Tools: SELECT (tap a sector or thing; drag corners and things), DRAW (tap corners, tap the
 * first to close: a new sector, or one cut into the room it's inside), BOX (drag a rectangle),
 * SPLIT (two points on a sector's outline cut it in two), PLACE (the player start, enemies, keys,
 * secrets, the exit). The inspector raises floors and ceilings, makes doors and lifts, paints
 * floor and ceiling tiles, builds stairs. Every edit is checked against the builder's rules and
 * undone if it breaks them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "geom.h"
#include "edmap.h"
#include "sui.h"
#include "files.h"
#include "level.h"
#include "gte.h"
#include "draw2d.h"

static EmMap M;
static char mapfile[32], text[1 << 18];
static int loaded, dirty, tool, sel_sec = -1, sel_thing = -1, place_type = TH_TROOPER, place_arg = 1;
static float ppm = 20;                        /* canvas pixels per metre */
static s32 vx, vz;                            /* the view's centre (map units) */
static s32 dx_[EM_SV], dz_[EM_SV]; static int ndraw;      /* a shape being drawn */
static int split_n; static s32 split_x, split_z;
static int drag = -1; static s32 drag_x, drag_z, drag_ox, drag_oz; static int drag_pushed, panning, boxing;
static s32 box_x0, box_z0, box_x1, box_z1;
static u8 portal[EM_SECS][EM_SV]; static int portals_ok;
static char toast_[96];
static int fit_pending;
static float ex_x, ex_y, ex_w, ex_h;          /* the TOP / 3D switch over the map: not map input */
static void (*say_)(const char *);
/* undo: the map as text */
#define UNDO_N 24
static char *undo_[UNDO_N]; static int nundo;
/* 3D */
static int view3d, built3d;
static s32 cam_x, cam_y, cam_z; static int cam_yaw, cam_pitch;
static float gx_, gy_, gw_, gh_;

enum { T_SELECT, T_DRAW, T_BOX, T_SPLIT, T_PLACE, T_COUNT };

static void say(const char *s) { if (say_) say_(s); else snprintf(toast_, sizeof(toast_), "%.95s", s); }
void geom_set_toast(void (*f)(const char *)) { say_ = f; }
int  geom_loaded(void) { return loaded; }
int  geom_in3d(void) { return loaded && view3d; }
const char *geom_file(void) { return mapfile; }

/* ------------------------------------------------------------------ files and undo */
void geom_save(void)
{
    int n;
    if (!loaded || !dirty || !mapfile[0]) return;
    n = em_write(&M, text, (int)sizeof(text));
    if (n < 0 || !plat_file_save(mapfile, text, n)) say("COULDN'T SAVE THE LEVEL");
    dirty = 0;
}
int geom_open(const char *file)
{
    int n;
    loaded = 0; view3d = 0; built3d = 0; portals_ok = 0;
    sel_sec = sel_thing = -1; ndraw = 0; split_n = 0;
    while (nundo) free(undo_[--nundo]);
    snprintf(mapfile, sizeof(mapfile), "%s", file ? file : "");
    if (!mapfile[0]) return 0;
    n = plat_file_load(mapfile, text, (int)sizeof(text) - 1);
    if (n <= 0) return 0;
    text[n] = 0;
    if (!em_parse(&M, text)) { say(em_error); return 0; }
    loaded = 1;
    fit_pending = 1;                              /* the whole level in view, once the tab knows its size */
    return 1;
}
int geom_create(const char *file, int from_level1)
{
    snprintf(mapfile, sizeof(mapfile), "%s", file);
    if (from_level1) { if (!em_parse(&M, em_src_e1)) return 0; }
    else em_new_level(&M, "LEVEL");
    loaded = 1; dirty = 1;
    geom_save();
    return geom_open(file);
}
static void push_undo(void)
{
    int n = em_write(&M, text, (int)sizeof(text));
    char *c;
    if (n < 0) return;
    if (nundo == UNDO_N) { free(undo_[0]); memmove(undo_, undo_ + 1, sizeof(undo_[0]) * (UNDO_N - 1)); nundo--; }
    c = (char *)malloc((size_t)n + 1);
    if (!c) return;
    memcpy(c, text, (size_t)n); c[n] = 0;
    undo_[nundo++] = c;
}
static void pop_undo(void)
{
    if (!nundo) { say("NOTHING TO UNDO"); return; }
    em_parse(&M, undo_[--nundo]);
    free(undo_[nundo]);
    dirty = 1; portals_ok = 0; built3d = 0;
    if (sel_sec >= M.nsecs) sel_sec = -1;
    if (sel_thing >= M.nthings) sel_thing = -1;
}
/* after an edit that might break the map: keep it if the sectors still check out */
static int edited(void)
{
    int s, w;
    if (em_check_geom(&M, &s, &w)) { say(em_error); pop_undo(); return 0; }
    dirty = 1; portals_ok = 0; built3d = 0;
    return 1;
}

/* the play-test's level loader (sector_world): a project's .map, built; "" = level 1 */
int geom_load_level(const char *name)
{
    static char buf[1 << 18];
    EmMap *m;
    const MapDef *d;
    int n, ok = 0;
    built3d = 0;
    if (!name || !name[0]) { level_load_map(0); return 1; }
    if (loaded && !strcmp(name, mapfile)) geom_save();
    n = plat_file_load(name, buf, (int)sizeof(buf) - 1);
    if (n <= 0) { level_load_map(0); say("LEVEL FILE MISSING - PLAYING LEVEL 1"); return 1; }
    buf[n] = 0;
    m = (EmMap *)malloc(sizeof(EmMap));
    if (m && em_parse(m, buf) && (d = em_compile(m, 0)) != 0) { level_load_map(d); ok = 1; }
    if (!ok) { char e[128]; snprintf(e, sizeof(e), "LEVEL: %s", em_error); say(e); level_load_map(0); }
    free(m);
    return 1;
}

/* ------------------------------------------------------------------ view */
static float sx_(s32 x) { return gx_ + gw_ / 2 + (float)(x - vx) / 1024.0f * ppm; }
static float sy_(s32 z) { return gy_ + gh_ / 2 - (float)(z - vz) / 1024.0f * ppm; }
static s32 wx_(float sx) { return vx + (s32)((sx - gx_ - gw_ / 2) / ppm * 1024.0f); }
static s32 wz_(float sy) { return vz - (s32)((sy - gy_ - gh_ / 2) / ppm * 1024.0f); }
static s32 grid(void) { return ppm >= 14 ? 256 : 512; }
static s32 snap(s32 v) { s32 g = grid(); return (v >= 0 ? v + g / 2 : v - g / 2) / g * g; }

static void find_portals(void)
{
    int a, i, b, j;
    memset(portal, 0, sizeof(portal));
    for (a = 0; a < M.nsecs; a++) {
        const EmSector *s = &M.secs[a];
        for (i = 0; i < s->np; i++) {
            s32 x0 = s->x[i], z0 = s->z[i], x1 = s->x[(i + 1) % s->np], z1 = s->z[(i + 1) % s->np];
            for (b = 0; b < M.nsecs && !portal[a][i]; b++) {
                const EmSector *t = &M.secs[b];
                if (b == a) continue;
                for (j = 0; j < t->np; j++)
                    if (t->x[j] == x1 && t->z[j] == z1 && t->x[(j + 1) % t->np] == x0 && t->z[(j + 1) % t->np] == z0) { portal[a][i] = 1; break; }
            }
        }
    }
    portals_ok = 1;
}

/* a sector filled: ear clipping (its loop is counter-clockwise in map space) */
static void fill_sector(const EmSector *s, u32 col)
{
    float px[EM_SV], py[EM_SV];
    int idx[EM_SV], n = s->np, i, guard = 0;
    for (i = 0; i < n; i++) { px[i] = sx_(s->x[i]); py[i] = sy_(s->z[i]); idx[i] = i; }
    while (n > 3 && guard++ < 400) {
        int found = 0;
        for (i = 0; i < n; i++) {
            int a = idx[(i + n - 1) % n], b = idx[i], c = idx[(i + 1) % n], k, ear = 1;
            /* screen y is flipped, so a map CCW corner is clockwise here: cross < 0 is convex */
            float cr = (px[b] - px[a]) * (py[c] - py[a]) - (py[b] - py[a]) * (px[c] - px[a]);
            if (cr > 0) continue;
            for (k = 0; k < n && ear; k++) {
                int p = idx[k];
                float d0, d1, d2;
                if (p == a || p == b || p == c) continue;
                d0 = (px[b] - px[a]) * (py[p] - py[a]) - (py[b] - py[a]) * (px[p] - px[a]);
                d1 = (px[c] - px[b]) * (py[p] - py[b]) - (py[c] - py[b]) * (px[p] - px[b]);
                d2 = (px[a] - px[c]) * (py[p] - py[c]) - (py[a] - py[c]) * (px[p] - px[c]);
                if (d0 <= 0 && d1 <= 0 && d2 <= 0) ear = 0;
            }
            if (!ear) continue;
            sui_tri(px[a], py[a], px[b], py[b], px[c], py[c], col);
            memmove(&idx[i], &idx[i + 1], sizeof(int) * (size_t)(n - i - 1));
            n--; found = 1;
            break;
        }
        if (!found) break;
    }
    if (n == 3) sui_tri(px[idx[0]], py[idx[0]], px[idx[1]], py[idx[1]], px[idx[2]], py[idx[2]], col);
}

static u32 sector_col(const EmSector *s, int sel)
{
    int f = s->floor / 64;                      /* brighter the higher */
    int r = 52 + f / 2, g = 24 + f / 6, b = 18 + f / 8;
    if (s->kind == SK_DOOR) { r = 120; g = 86; b = 30; }
    if (s->kind == SK_LIFT) { r = 44; g = 92; b = 52; }
    if (sel) { r = 150; g = 52; b = 18; }
    if (r < 20) r = 20;
    if (r > 140) r = 140;
    if (g < 8) g = 8;
    if (g > 80) g = 80;
    if (b < 6) b = 6;
    if (b > 60) b = 60;
    if (sel) { r = 150; g = 52; b = 18; }
    return (u32)(r << 16 | g << 8 | b);
}

static void thing_icon(const EmThing *t, int sel)
{
    static const u32 KC[4] = { 0xA0A0A0, 0x3CDCFF, 0xFFAA28, 0xF0283C };
    float x = sx_(t->x), y = sy_(t->z);
    switch (t->type) {
    case TH_START:
        sui_tri(x + sinf(t->yaw * 6.2832f / 4096) * 9, y - cosf(t->yaw * 6.2832f / 4096) * 9, x - 6, y + 6, x + 6, y + 6, 0x9AD86A);
        break;
    case TH_TROOPER: sui_circle(x, y, 6, 0xD23C1E); sui_circle(x, y, 2, 0x200404); break;
    case TH_KEY: sui_tri(x, y - 7, x + 6, y, x - 6, y, KC[t->arg & 3]); sui_tri(x, y + 7, x + 6, y, x - 6, y, KC[t->arg & 3]); break;
    case TH_SHARD: sui_tri(x, y - 7, x + 5, y + 5, x - 5, y + 5, 0x6AE0E0); break;
    case TH_EXIT: sui_rect(x - 6, y - 6, 12, 12, 0x5BFF7A); sui_rect(x - 3, y - 3, 6, 6, 0x103018); break;
    default: sui_rect(x - 2, y - 2, 4, 4, 0x8A6656); break;
    }
    if (sel) sui_nine(SK_SELECT, x - 11, y - 11, 22, 22);
}

/* ------------------------------------------------------------------ edits */
static int convex_ccw(const s32 *x, const s32 *z, int n)
{
    int i, sign = 0;
    for (i = 0; i < n; i++) {
        long long ax = x[(i + 1) % n] - x[i], az = z[(i + 1) % n] - z[i], bx = x[(i + 2) % n] - x[(i + 1) % n], bz = z[(i + 2) % n] - z[(i + 1) % n];
        long long c = ax * bz - az * bx;
        if (c == 0) continue;
        if (!sign) sign = c > 0 ? 1 : -1;
        else if ((c > 0 ? 1 : -1) != sign) return 0;
    }
    return 1;
}
static void make_shape(const s32 *x, const s32 *z, int n)
{
    int host, i, all_in = 1, k;
    s32 cx = 0, cz = 0;
    for (i = 0; i < n; i++) {
        if (x[i] < -EM_LIMIT || x[i] > EM_LIMIT || z[i] < -EM_LIMIT || z[i] > EM_LIMIT) { say("THE MAP ENDS 31 M FROM THE MIDDLE"); return; }
        cx += x[i]; cz += z[i];
    }
    cx /= n; cz /= n;
    host = em_sector_at(&M, cx, cz);
    if (host >= 0) for (i = 0; i < n; i++) {
        const EmSector *s = &M.secs[host];
        int on = 0, w;
        for (w = 0; w < s->np; w++) {
            int jn = (w + 1) % s->np;
            long long ddx = s->x[jn] - s->x[w], ddz = s->z[jn] - s->z[w], cr = ddx * (z[i] - s->z[w]) - ddz * (x[i] - s->x[w]), t = ddx * (x[i] - s->x[w]) + ddz * (z[i] - s->z[w]);
            if (cr == 0 && t >= 0 && t <= ddx * ddx + ddz * ddz) on = 1;
        }
        if (!on && !em_inside(s, x[i], z[i])) all_in = 0;
    }
    push_undo();
    if (host >= 0 && all_in) {
        if (!convex_ccw(x, z, n)) { pop_undo(); say("INSIDE A ROOM, SHAPES MUST BE CONVEX"); return; }
        em_error[0] = 0;
        k = em_carve(&M, host, x, z, n);
        if (k < 0 || (k == 0 && em_error[0])) { say(em_error); pop_undo(); return; }
        if (edited()) { sel_sec = k; sel_thing = -1; say("A NEW SECTOR, CUT INTO THE ROOM"); }
        return;
    }
    k = em_add_sector(&M, x, z, n, -1);
    if (k < 0) { say(em_error[0] ? em_error : "COULDN'T MAKE THAT SECTOR"); pop_undo(); return; }
    em_fix_winding(&M.secs[k]);
    em_weld(&M);
    if (edited()) { sel_sec = k; sel_thing = -1; say("SECTOR MADE"); }
}

/* the closest point on sector si's outline to (x,z), on the 1/16 m grid */
static void on_outline(int si, s32 x, s32 z, s32 *ox, s32 *oz)
{
    const EmSector *s = &M.secs[si];
    float best = 1e30f;
    int w;
    *ox = x; *oz = z;
    for (w = 0; w < s->np; w++) {
        float ax = (float)s->x[w], az = (float)s->z[w], bx = (float)s->x[(w + 1) % s->np], bz = (float)s->z[(w + 1) % s->np];
        float ddx = bx - ax, ddz = bz - az, l = ddx * ddx + ddz * ddz, t = l > 0 ? ((x - ax) * ddx + (z - az) * ddz) / l : 0, px, pz, d;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        px = ax + ddx * t; pz = az + ddz * t;
        d = (px - x) * (px - x) + (pz - z) * (pz - z);
        if (d < best) {
            best = d;
            if (fabsf(ddx) < 1) { *ox = (s32)ax; *oz = (s32)(floorf(pz / 64 + 0.5f) * 64); }
            else if (fabsf(ddz) < 1) { *oz = (s32)az; *ox = (s32)(floorf(px / 64 + 0.5f) * 64); }
            else { *ox = (s32)px; *oz = (s32)pz; }
        }
    }
}

static void place_thing(s32 x, s32 z)
{
    int si = em_sector_at(&M, x, z), i;
    EmThing *t;
    if (si < 0) { say("THINGS GO INSIDE A SECTOR"); return; }
    (void)i;
    if (place_type == TH_START) {               /* one start: it moves */
        for (i = 0; i < M.nthings; i++) if (M.things[i].type == TH_START) { push_undo(); M.things[i].x = M.things[i].ax = (s16)x; M.things[i].z = M.things[i].az = (s16)z; sel_thing = i; sel_sec = -1; dirty = 1; built3d = 0; return; }
    }
    if (M.nthings >= EM_THINGS) { say("200 THINGS AT MOST"); return; }
    push_undo();
    t = &M.things[M.nthings];
    memset(t, 0, sizeof(*t));
    t->type = (u8)place_type; t->x = t->ax = (s16)x; t->z = t->az = (s16)z;
    if (place_type == TH_TROOPER) t->yaw = 2048;
    t->arg = (u8)(place_type == TH_KEY ? place_arg : place_type == TH_SHARD ? 1 : 0);
    sel_thing = M.nthings++; sel_sec = -1;
    dirty = 1; built3d = 0;
}

static int near_thing(float px, float py)
{
    int i, best = -1;
    float bd = 12 * 12;
    for (i = 0; i < M.nthings; i++) {
        float ddx = sx_(M.things[i].x) - px, ddy = sy_(M.things[i].z) - py, d = ddx * ddx + ddy * ddy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* ------------------------------------------------------------------ the 2D map */
static void fit_view(float gw, float gh)
{
    s32 x0 = 99999, x1 = -99999, z0 = 99999, z1 = -99999;
    int i, k;
    float a, b;
    for (i = 0; i < M.nsecs; i++) for (k = 0; k < M.secs[i].np; k++) {
        if (M.secs[i].x[k] < x0) x0 = M.secs[i].x[k];
        if (M.secs[i].x[k] > x1) x1 = M.secs[i].x[k];
        if (M.secs[i].z[k] < z0) z0 = M.secs[i].z[k];
        if (M.secs[i].z[k] > z1) z1 = M.secs[i].z[k];
    }
    if (x1 < x0) { vx = vz = 0; ppm = 20; return; }
    vx = (x0 + x1) / 2; vz = (z0 + z1) / 2;
    a = (gw - 40) / ((x1 - x0) / 1024.0f + 1); b = (gh - 40) / ((z1 - z0) / 1024.0f + 1);
    ppm = a < b ? a : b;
    if (ppm < 4) ppm = 4;
    if (ppm > 40) ppm = 40;
}

static void map_view(float gx, float gy, float gw, float gh)
{
    const SuiPtr *p = sui_ptr();
    int i, k;
    gx_ = gx; gy_ = gy; gw_ = gw; gh_ = gh;
    if (fit_pending) { fit_view(gw, gh); fit_pending = 0; }
    if (!portals_ok) find_portals();
    sui_rect(gx, gy, gw, gh, 0x0E0807);
    sui_clip(gx, gy, gw, gh);
    {   /* the grid: 1 m lines, brighter every 4 m */
        s32 x0 = wx_(gx) / 1024 - 1, x1 = wx_(gx + gw) / 1024 + 1, z0 = wz_(gy + gh) / 1024 - 1, z1 = wz_(gy) / 1024 + 1, m;
        if (ppm >= 8) {
            for (m = x0; m <= x1; m++) sui_rect(floorf(sx_(m * 1024)), gy, 1, gh, (m % 4) ? 0x1C0E0B : 0x2C1610);
            for (m = z0; m <= z1; m++) sui_rect(gx, floorf(sy_(m * 1024)), gw, 1, (m % 4) ? 0x1C0E0B : 0x2C1610);
        }
    }
    {   /* where the map ends (+-31.25 m) */
        float l = sx_(-EM_LIMIT), r = sx_(EM_LIMIT), t = sy_(EM_LIMIT), b = sy_(-EM_LIMIT);
        sui_rect(l, t, r - l, 1, 0x7A1A10); sui_rect(l, b, r - l, 1, 0x7A1A10);
        sui_rect(l, t, 1, b - t, 0x7A1A10); sui_rect(r, t, 1, b - t, 0x7A1A10);
    }
    for (i = 0; i < M.nsecs; i++) fill_sector(&M.secs[i], sector_col(&M.secs[i], i == sel_sec));
    for (i = 0; i < M.nsecs; i++) {
        const EmSector *s = &M.secs[i];
        for (k = 0; k < s->np; k++) {
            int n = (k + 1) % s->np;
            if (portal[i][k]) sui_line(sx_(s->x[k]), sy_(s->z[k]), sx_(s->x[n]), sy_(s->z[n]), 1, 0x6A3828);
            else sui_line(sx_(s->x[k]), sy_(s->z[k]), sx_(s->x[n]), sy_(s->z[n]), 2, i == sel_sec ? 0xFFD070 : 0xD8C2A0);
        }
    }
    if (sel_sec >= 0 && sel_sec < M.nsecs)              /* its corners, to drag */
        for (k = 0; k < M.secs[sel_sec].np; k++) sui_rect(sx_(M.secs[sel_sec].x[k]) - 2, sy_(M.secs[sel_sec].z[k]) - 2, 5, 5, 0xFFF0D2);
    for (i = 0; i < M.nthings; i++) thing_icon(&M.things[i], i == sel_thing);
    /* the shape being drawn */
    if (tool == T_DRAW && ndraw) {
        for (k = 0; k + 1 < ndraw; k++) sui_line(sx_(dx_[k]), sy_(dz_[k]), sx_(dx_[k + 1]), sy_(dz_[k + 1]), 2, C_ACCENT);
        if (sui_in(gx, gy, gw, gh)) sui_line(sx_(dx_[ndraw - 1]), sy_(dz_[ndraw - 1]), sx_(snap(wx_(p->x))), sy_(snap(wz_(p->y))), 1, 0xFFB060);
        for (k = 0; k < ndraw; k++) sui_rect(sx_(dx_[k]) - 3, sy_(dz_[k]) - 3, 6, 6, k == 0 ? 0xFFF0D2 : C_ACCENT);
    }
    if (tool == T_BOX && boxing) {
        float a = sx_(box_x0), b = sy_(box_z0), c = sx_(box_x1), d = sy_(box_z1);
        sui_rect(fminf(a, c), fminf(b, d), fabsf(c - a), fabsf(d - b), 0x80FF6A2B);
        sui_rrect_line(fminf(a, c), fminf(b, d), fabsf(c - a), fabsf(d - b), 0, 2, C_ACCENT);
    }
    if (tool == T_SPLIT && split_n) sui_rect(sx_(split_x) - 3, sy_(split_z) - 3, 7, 7, C_ACCENT);
    if (sui_in(gx, gy, gw, gh) && (tool == T_DRAW || tool == T_BOX || tool == T_PLACE)) {   /* the snapped cursor */
        float cx = sx_(snap(wx_(p->x))), cy = sy_(snap(wz_(p->y)));
        sui_rect(cx - 5, cy, 11, 1, 0xFFF0D2); sui_rect(cx, cy - 5, 1, 11, 0xFFF0D2);
    }

    /* input */
    if (p->fingers >= 2) {                              /* two fingers: zoom (and pan) */
        if (p->pinch != 1) { ppm *= p->pinch; if (ppm < 4) ppm = 4; if (ppm > 80) ppm = 80; }
        vx -= (s32)(p->dx / ppm * 1024); vz += (s32)(p->dy / ppm * 1024);
        drag = -1; boxing = 0; panning = 0;
    } else if (p->pressed && sui_in(gx, gy, gw, gh) && !sui_in(ex_x, ex_y, ex_w, ex_h) && !sui_captured()) {
        s32 x = wx_(p->x), z = wz_(p->y);
        int si, pi, th;
        drag = -1; panning = 0; drag_pushed = 0;
        if (tool == T_SELECT) {
            th = near_thing(p->x, p->y);
            if (th >= 0) { drag = 1000 + th; drag_ox = M.things[th].x; drag_oz = M.things[th].z; sui_capture(&drag); }
            else if (em_near_point(&M, x, z, (s32)(10 / ppm * 1024), &si, &pi)) {
                drag = 0; drag_x = drag_ox = M.secs[si].x[pi]; drag_z = drag_oz = M.secs[si].z[pi]; sui_capture(&drag);
            } else { panning = 1; sui_capture(&panning); }
        } else if (tool == T_BOX) { boxing = 1; box_x0 = box_x1 = snap(x); box_z0 = box_z1 = snap(z); sui_capture(&boxing); }
        else { panning = 1; sui_capture(&panning); }
    }
    if (p->down && p->fingers < 2) {
        if (sui_captured() == &panning && p->moved) { vx -= (s32)(p->dx / ppm * 1024); vz += (s32)(p->dy / ppm * 1024); }
        if (sui_captured() == &boxing) { box_x1 = snap(wx_(p->x)); box_z1 = snap(wz_(p->y)); }
        if (sui_captured() == &drag && p->moved && drag >= 0) {
            s32 nx = snap(wx_(p->x)), nz = snap(wz_(p->y));
            if (!drag_pushed) { push_undo(); drag_pushed = 1; }
            if (drag >= 1000) { EmThing *t = &M.things[drag - 1000]; t->x = (s16)wx_(p->x); t->z = (s16)wz_(p->y); dirty = 1; built3d = 0; }
            else if (nx != drag_x || nz != drag_z) {
                s32 px = drag_x, pz = drag_z;
                if (em_move_points(&M, &px, &pz, 1, nx - drag_x, nz - drag_z) > 0) { drag_x = nx; drag_z = nz; portals_ok = 0; built3d = 0; dirty = 1; }
            }
        }
    }
    if (p->released) {
        s32 x = wx_(p->x), z = wz_(p->y);
        if (sui_captured() == &drag && drag >= 0 && drag < 1000 && drag_pushed) edited();
        if (sui_captured() == &drag && drag >= 1000 && !p->moved) { sel_thing = drag - 1000; sel_sec = -1; }
        if (sui_captured() == &drag && drag == 0 && !p->moved) { sel_sec = em_sector_at(&M, x, z); sel_thing = -1; }   /* a tap on a corner */
        if (sui_captured() == &boxing) {
            boxing = 0;
            if (box_x0 != box_x1 && box_z0 != box_z1) {
                s32 bx[4], bz[4], x0 = box_x0 < box_x1 ? box_x0 : box_x1, x1 = box_x0 < box_x1 ? box_x1 : box_x0, z0 = box_z0 < box_z1 ? box_z0 : box_z1, z1 = box_z0 < box_z1 ? box_z1 : box_z0;
                bx[0] = x0; bz[0] = z0; bx[1] = x1; bz[1] = z0; bx[2] = x1; bz[2] = z1; bx[3] = x0; bz[3] = z1;
                make_shape(bx, bz, 4);
            }
        }
        if (sui_captured() == &panning && !p->moved && sui_in(gx, gy, gw, gh)) {   /* a tap */
            if (tool == T_SELECT) {
                int th = near_thing(p->x, p->y);
                if (th >= 0) { sel_thing = th; sel_sec = -1; }
                else { sel_sec = em_sector_at(&M, x, z); sel_thing = -1; }
            } else if (tool == T_DRAW) {
                s32 sx = snap(x), sz = snap(z);
                float d0x = ndraw ? sx_(dx_[0]) - p->x : 99, d0y = ndraw ? sy_(dz_[0]) - p->y : 99;
                if (ndraw >= 3 && d0x * d0x + d0y * d0y < 14 * 14) { s32 tx[EM_SV], tz[EM_SV]; int n = ndraw; memcpy(tx, dx_, sizeof(s32) * (size_t)n); memcpy(tz, dz_, sizeof(s32) * (size_t)n); ndraw = 0; make_shape(tx, tz, n); }
                else if (ndraw < EM_SV && (!ndraw || sx != dx_[ndraw - 1] || sz != dz_[ndraw - 1])) { dx_[ndraw] = sx; dz_[ndraw] = sz; ndraw++; }
            } else if (tool == T_SPLIT) {
                int si = sel_sec >= 0 ? sel_sec : em_sector_at(&M, x, z);
                if (si < 0) say("TAP INSIDE A SECTOR'S EDGE");
                else {
                    s32 ox, oz;
                    on_outline(si, x, z, &ox, &oz);
                    if (!split_n) { split_n = 1; split_x = ox; split_z = oz; sel_sec = si; say("NOW THE OTHER SIDE"); }
                    else {
                        split_n = 0;
                        push_undo();
                        if (!em_cut(&M, si, split_x, split_z, ox, oz)) { say(em_error); pop_undo(); }
                        else if (edited()) say("SPLIT IN TWO");
                    }
                }
            } else if (tool == T_PLACE) place_thing(snap(x), snap(z));
        }
        drag = -1; panning = 0;
    }
    sui_unclip();
}

/* ------------------------------------------------------------------ the 3D view */
static void build3d(void)
{
    const MapDef *d = em_compile(&M, 0);
    if (!d) { say(em_error); view3d = 0; return; }
    level_load_map(d);
    built3d = 1;
}
static void enter3d(void)
{
    int i, si;
    cam_x = vx; cam_z = vz; cam_yaw = 0; cam_pitch = 80;
    if (sel_sec >= 0) { const EmSector *s = &M.secs[sel_sec]; s32 sx = 0, sz = 0; for (i = 0; i < s->np; i++) { sx += s->x[i]; sz += s->z[i]; } cam_x = sx / s->np; cam_z = sz / s->np; }
    else for (i = 0; i < M.nthings; i++) if (M.things[i].type == TH_START) { cam_x = M.things[i].x; cam_z = M.things[i].z; cam_yaw = M.things[i].yaw; }
    si = em_sector_at(&M, cam_x, cam_z);
    cam_y = -((si >= 0 ? em_floor_at(&M, si, cam_x, cam_z) : 0) + 1700);
    view3d = 1;
    build3d();
}

void geom_draw3d(FrameBuf *fb, int t)
{
    MATRIX rx, ry, v;
    SVECTOR p; VECTOR o;
    PROJ pr;
    if (!view3d || !built3d) return;
    mat_rot_x(&rx, cam_pitch & 4095);
    mat_rot_y(&ry, (-cam_yaw) & 4095);
    mat_mul(&rx, &ry, &v);
    v.t[0] = v.t[1] = v.t[2] = 0;
    p.vx = (s16)(-cam_x >> 1); p.vy = (s16)(-cam_y >> 1); p.vz = (s16)(-cam_z >> 1);   /* in halves: s16 range */
    mat_apply(&v, &p, &o);
    v.t[0] = o.vx * 2; v.t[1] = o.vy * 2; v.t[2] = o.vz * 2;
    pr.h = 250; pr.ofx = SCREEN_W / 2; pr.ofy = SCREEN_H / 2; pr.near_z = 120;
    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(4, 3, 6), RGB(10, 3, 3), NO_SEMI, OT_LEN - 1);
    level_debug_all_rooms(1);
    level_update();
    level_draw(fb, &v, &pr, t);
    level_debug_all_rooms(0);
}

static void view_3d(float gx, float gy, float gw, float gh)
{
    static const char *const B[6] = { "FORWARD", "BACK", "LEFT", "RIGHT", "UP", "DOWN" };
    const SuiPtr *p = sui_ptr();
    float fw = (float)SCREEN_W, fh = (float)SCREEN_H, sc = gw / fw > gh / fh ? gw / fw : gh / fh, iw = fw * sc, ih = fh * sc;
    int i;
    if (!built3d) build3d();
    sui_clip(gx, gy, gw, gh);
    sui_frame_image(gx + (gw - iw) / 2, gy + (gh - ih) / 2, iw, ih);
    sui_unclip();
    if (p->down && p->moved && sui_in(gx, gy, gw, gh) && !sui_captured() && p->fingers < 2) {   /* drag: look */
        cam_yaw = (cam_yaw + (int)(p->dx * 6)) & 4095;
        cam_pitch -= (int)(p->dy * 6);
        if (cam_pitch > 900) cam_pitch = 900;
        if (cam_pitch < -900) cam_pitch = -900;
    }
    for (i = 0; i < 6; i++) {
        float bx = gx + 10 + (i % 2) * 86, by = gy + gh - 116 + (i / 2) * 36;
        sui_button(B[i], bx, by, 82, 32, B[i], BTN_PLAIN);
        if (sui_pressing(B[i])) {
            s32 sp = 120, fx = isin(cam_yaw) * sp >> 12, fz = icos(cam_yaw) * sp >> 12;
            if (i == 0) { cam_x += fx; cam_z += fz; }
            if (i == 1) { cam_x -= fx; cam_z -= fz; }
            if (i == 2) { cam_x -= fz; cam_z += fx; }
            if (i == 3) { cam_x += fz; cam_z -= fx; }
            if (i == 4) cam_y -= sp;
            if (i == 5) cam_y += sp;
        }
    }
    sui_nine(SK_PLAQUE, gx + 8, gy + 8, 292, 24);
    sui_text(FONT_MONO, 10, gx + 16, gy + 15, "DRAG TO LOOK - HOLD THE BUTTONS TO FLY", C_MUTED);
}

/* ------------------------------------------------------------------ the inspector */
static int kc_x;
static void inspector(float x, float y, float w, float h)
{
    static const char *const KIND[3] = { "ROOM", "DOOR", "LIFT" };
    static const char *const TH[5] = { "START", "ENEMY", "KEY", "SECRET", "EXIT" };
    static const int THT[5] = { TH_START, TH_TROOPER, TH_KEY, TH_SHARD, TH_EXIT };
    static const char *const KEYN[4] = { "NONE", "CYAN", "AMBER", "CRIMSON" };
    char b[64];
    float cy = y + 12;
    int i;
    (void)kc_x;
    sui_nine(SK_PANEL, x, y - 2, w + 6, h + 6);
    sui_clip(x, y, w, h);
    if (tool == T_PLACE) {
        sui_text(FONT_FIRE, 20, x + 12, cy - 2, "Place", C_TEXT);
        sui_label(x + 12, cy + 30, "WHAT");
        for (i = 0; i < 5; i++)
            if (sui_button(TH[i], x + 10 + (i % 3) * 92, cy + 48 + (i / 3) * 36, 88, 32, TH[i], place_type == THT[i] ? BTN_CHIP_ON : BTN_CHIP)) place_type = THT[i];
        if (place_type == TH_KEY) {
            sui_label(x + 12, cy + 128, "KEY COLOUR");
            for (i = 1; i < 4; i++) if (sui_button(KEYN[i], x + 10 + (i - 1) * 92, cy + 146, 88, 32, KEYN[i], place_arg == i ? BTN_CHIP_ON : BTN_CHIP)) place_arg = i;
        }
        sui_text(FONT_REGULAR, 11, x + 12, cy + 196, "TAP INSIDE A SECTOR TO PLACE IT.", C_MUTED);
        sui_text(FONT_REGULAR, 11, x + 12, cy + 212, "ENEMIES BECOME THE TYPE WHOSE", C_MUTED);
        sui_text(FONT_REGULAR, 11, x + 12, cy + 228, "THING IS TROOPER (SCENE TAB).", C_MUTED);
        sui_unclip();
        return;
    }
    if (sel_thing >= 0 && sel_thing < M.nthings) {
        EmThing *t = &M.things[sel_thing];
        const char *nm = t->type == TH_START ? "Player start" : t->type == TH_TROOPER ? "Enemy" : t->type == TH_KEY ? "Key" : t->type == TH_SHARD ? "Secret" : t->type == TH_EXIT ? "Exit" : "Thing";
        sui_text(FONT_FIRE, 20, x + 12, cy - 2, nm, C_TEXT);
        snprintf(b, sizeof(b), "AT %.1f, %.1f M", t->x / 1024.0, t->z / 1024.0);
        sui_text(FONT_MONO, 11, x + 12, cy + 30, b, C_MUTED);
        sui_label(x + 12, cy + 56, "FACING");
        if (sui_button("yawl", x + 10, cy + 74, 60, 32, "<", BTN_PLAIN)) { push_undo(); t->yaw = (s16)((t->yaw - 512) & 4095); dirty = 1; }
        snprintf(b, sizeof(b), "%d DEG", t->yaw * 360 / 4096);
        sui_nine(SK_FIELD, x + 76, cy + 74, w - 152, 32);
        sui_text_c(FONT_MONO, 12, x + 76 + (w - 152) / 2, cy + 85, b, C_TEXT);
        if (sui_button("yawr", x + w - 70, cy + 74, 60, 32, ">", BTN_PLAIN)) { push_undo(); t->yaw = (s16)((t->yaw + 512) & 4095); dirty = 1; }
        if (t->type == TH_KEY) {
            sui_label(x + 12, cy + 120, "KEY COLOUR");
            for (i = 1; i < 4; i++) if (sui_button(&KEYN[i], x + 10 + (i - 1) * 92, cy + 138, 88, 32, KEYN[i], t->arg == i ? BTN_CHIP_ON : BTN_CHIP)) { push_undo(); t->arg = (u8)i; dirty = 1; }
        }
        if (t->type != TH_START && sui_button("delthing", x + 10, y + h - 44, w - 20, 34, "Delete", BTN_DANGER)) {
            push_undo();
            memmove(&M.things[sel_thing], &M.things[sel_thing + 1], sizeof(EmThing) * (size_t)(M.nthings - sel_thing - 1));
            M.nthings--; sel_thing = -1; dirty = 1; built3d = 0;
        }
        sui_unclip();
        return;
    }
    if (sel_sec < 0 || sel_sec >= M.nsecs) {
        static const char *const HELP[T_COUNT][3] = {
            { "TAP A SECTOR OR A THING.", "DRAG CORNERS AND THINGS.", "DRAG EMPTY SPACE TO PAN." },
            { "TAP CORNERS ON THE GRID.", "TAP THE FIRST ONE TO CLOSE IT.", "INSIDE A ROOM IT'S CUT INTO IT." },
            { "DRAG A RECTANGLE.", "INSIDE A ROOM IT'S CUT INTO IT,", "ELSE IT'S A NEW SECTOR." },
            { "TAP A SECTOR'S EDGE, THEN THE", "OTHER SIDE: IT'S CUT IN TWO", "ALONG THAT LINE." },
            { "", "", "" } };
        char st[64];
        sui_text(FONT_FIRE, 20, x + 12, cy - 2, "Level", C_TEXT);
        snprintf(st, sizeof(st), "%d SECTORS, %d THINGS", M.nsecs, M.nthings);
        sui_text(FONT_MONO, 11, x + 12, cy + 30, st, C_MUTED);
        for (i = 0; i < 3; i++) sui_text(FONT_REGULAR, 11, x + 12, cy + 60 + i * 16, HELP[tool][i], C_TEXT);
        sui_text(FONT_REGULAR, 11, x + 12, cy + 122, "TWO FINGERS: ZOOM AND PAN.", C_MUTED);
        sui_unclip();
        return;
    }
    {
        EmSector *s = &M.secs[sel_sec];
        static const char *const HL[3] = { "FLOOR", "CEILING", "DOOR OPENS" };
        int k;
        snprintf(b, sizeof(b), "SECTOR %d", sel_sec);
        sui_text(FONT_FIRE, 20, x + 12, cy - 2, b, C_TEXT);
        if (s->name[0]) sui_text(FONT_MONO, 11, x + 12, cy + 22, s->name, C_MUTED);
        for (k = 0; k < 2; k++) {
            int ceil = k == 1 && s->kind != SK_DOOR, door = k == 1 && s->kind == SK_DOOR;
            s16 *v = k == 0 ? &s->floor : door ? &s->ka : &s->ceil;
            float ry = cy + 40 + k * 38;
            sui_text(FONT_REGULAR, 11, x + 12, ry + 10, HL[door ? 2 : k], C_MUTED);
            if (sui_button(HL[k], x + w - 168, ry, 44, 32, "-", BTN_PLAIN)) {
                push_undo(); *v = (s16)(*v - 205); if (k == 0 && s->kind == SK_DOOR) s->ceil = s->floor;
                if (k == 0 && s->kind == SK_LIFT) { s->ka -= 205; s->kb -= 205; }
                if (!ceil || *v > s->floor) edited(); else { *v = (s16)(s->floor + 205); edited(); }
            }
            snprintf(b, sizeof(b), "%.1f M", *v / 1024.0);
            sui_nine(SK_FIELD, x + w - 120, ry, 66, 32);
            sui_text_c(FONT_MONO, 12, x + w - 87, ry + 11, b, C_TEXT);
            if (sui_button((const char *)HL[k] + 1, x + w - 50, ry, 44, 32, "+", BTN_PLAIN)) {
                push_undo(); *v = (s16)(*v + 205); if (k == 0 && s->kind == SK_DOOR) s->ceil = s->floor;
                if (k == 0 && s->kind == SK_LIFT) { s->ka += 205; s->kb += 205; }
                if (k == 0 && s->kind == SK_ROOM && s->floor >= s->ceil) s->ceil = (s16)(s->floor + 205);
                edited();
            }
        }
        sui_label(x + 12, cy + 122, "TYPE");
        for (i = 0; i < 3; i++)
            if (sui_button(KIND[i], x + 10 + i * 92, cy + 140, 88, 30, KIND[i], s->kind == i ? BTN_CHIP_ON : BTN_CHIP) && s->kind != i) {
                push_undo();
                s->kind = (u8)i;
                if (i == SK_DOOR) { if (!s->ka) s->ka = (s16)(s->ceil > s->floor ? s->ceil - s->floor : 4096); s->ceil = s->floor; }
                else if (i == SK_LIFT) { s->ka = s->floor; if (s->kb <= s->ka) s->kb = (s16)(s->floor + 2048); if (s->ceil <= s->floor) s->ceil = (s16)(s->kb + 4096); }
                else { if (s->ceil <= s->floor) s->ceil = (s16)(s->floor + (s->ka > 0 ? s->ka : 4096)); s->lock = 0; s->ka = s->kb = 0; }
                edited();
            }
        if (s->kind == SK_DOOR) {
            sui_text(FONT_REGULAR, 11, x + 12, cy + 186, "NEEDS", C_MUTED);
            for (i = 0; i < 4; i++) if (sui_button((const char *)&KEYN[i] + 1, x + 56 + i * 58, cy + 178, 56, 28, KEYN[i], s->lock == i ? BTN_CHIP_ON : BTN_CHIP)) { push_undo(); s->lock = (u8)i; edited(); }
        }
        sui_label(x + 12, cy + 216, "FLOOR TILE");
        {
            int tile = s->f.def;
            if (sui_button("ftl", x + 10, cy + 234, 40, 30, "<", BTN_PLAIN)) { push_undo(); s->f.def = (u8)((tile + em_tile_count - 1) % em_tile_count); edited(); }
            sui_nine(SK_FIELD, x + 54, cy + 234, w - 108, 30);
            sui_text_c(FONT_MONO, 10, x + 54 + (w - 108) / 2, cy + 244, em_tile_names[s->f.def < em_tile_count ? s->f.def : 0], C_TEXT);
            if (sui_button("ftr", x + w - 50, cy + 234, 40, 30, ">", BTN_PLAIN)) { push_undo(); s->f.def = (u8)((tile + 1) % em_tile_count); edited(); }
        }
        sui_label(x + 12, cy + 272, "CEILING TILE");
        {
            int tile = s->c.def;
            if (sui_button("ctl", x + 10, cy + 290, 40, 30, "<", BTN_PLAIN)) { push_undo(); s->c.def = (u8)((tile + em_tile_count - 1) % em_tile_count); edited(); }
            sui_nine(SK_FIELD, x + 54, cy + 290, w - 108, 30);
            sui_text_c(FONT_MONO, 10, x + 54 + (w - 108) / 2, cy + 300, em_tile_names[s->c.def < em_tile_count ? s->c.def : 0], C_TEXT);
            if (sui_button("ctr", x + w - 50, cy + 290, 40, 30, ">", BTN_PLAIN)) { push_undo(); s->c.def = (u8)((tile + 1) % em_tile_count); edited(); }
        }
        {
            float by = cy + 330, bw = (w - 26) / 2;
            if (sui_button("stairs", x + 10, by, bw, 32, "Stairs", BTN_PLAIN)) {
                /* climbing north by a metre, 0.2 m a step */
                push_undo();
                if (!em_stairs(&M, sel_sec, 0, s->floor + 1024, 0)) { say(em_error); pop_undo(); } else if (edited()) say("STAIRS: A METRE UP TO THE NORTH");
            }
            if (sui_button("delsec", x + 16 + bw, by, bw, 32, "Delete", BTN_DANGER)) {
                push_undo(); em_delete_sector(&M, sel_sec); sel_sec = -1;
                if (edited()) say("SECTOR DELETED");
            }
        }
    }
    sui_unclip();
}

/* ------------------------------------------------------------------ the tab */
void geom_tab(float x, float y, float w, float h, int *wants_create)
{
    static const char *const TOOLS[T_COUNT] = { "SELECT", "DRAW", "BOX", "SPLIT", "PLACE" };
    float iw = 300, rail = 64;
    int i;
    *wants_create = 0;
    if (!loaded) {
        float cx = x + w / 2;
        sui_nine(SK_PANEL, cx - 260, y + 70, 520, 170);
        sui_text_c(FONT_FIRE, 20, cx, y + 92, "This game has no level yet", C_TEXT);
        sui_text_c(FONT_REGULAR, 12, cx, y + 126, "Start from Deadbird's level 1 or from one empty room.", C_MUTED);
        if (sui_button("mk1", cx - 220, y + 160, 210, 40, "Copy level 1", BTN_PRIMARY)) *wants_create = 1;
        if (sui_button("mk0", cx + 10, y + 160, 210, 40, "Empty room", BTN_PLAIN)) *wants_create = 2;
        return;
    }
    /* the tool rail */
    sui_nine(SK_BAR, x - 6, y - 2, rail + 6, h + 6);
    for (i = 0; i < T_COUNT; i++)
        if (sui_button(TOOLS[i], x + 6, y + 8 + i * 42, rail - 12, 38, TOOLS[i], tool == i ? BTN_TAB_ON : BTN_CHIP)) {
            tool = i; ndraw = 0; split_n = 0; view3d = 0;
            if (i == T_PLACE) sel_sec = -1;
        }
    if (sui_button("undo", x + 6, y + h - 46, rail - 12, 38, "UNDO", BTN_GHOST)) pop_undo();
    /* top / 3D */
    ex_x = x + w - iw - 144; ex_y = y + 4; ex_w = 144; ex_h = 36;
    if (view3d) view_3d(x + rail, y, w - rail - iw, h);
    else map_view(x + rail, y, w - rail - iw, h);
    if (sui_button("v2d", x + w - iw - 140, y + 8, 64, 28, "TOP", view3d ? BTN_CHIP : BTN_CHIP_ON)) view3d = 0;
    if (sui_button("v3d", x + w - iw - 72, y + 8, 64, 28, "3D", view3d ? BTN_CHIP_ON : BTN_CHIP) && !view3d) enter3d();
    inspector(x + w - iw, y, iw, h);
}

/* tests */
const EmMap *geom_map(void) { return &M; }
int  geom_selected(void) { return sel_sec; }
void geom_test_view(s32 x, s32 z, float px_per_m) { vx = x; vz = z; ppm = px_per_m; }
