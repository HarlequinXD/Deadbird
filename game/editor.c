/*
 * The in-game level editor (see editor.h). Gamepad first, at the PS1's 320x240:
 *
 *   map   d-pad / left stick: cursor   right stick: pan   L2 / R2: zoom
 *         X: use the tool   O: cancel   /\: tools   []: inspect what's under the cursor
 *         L1 / R1: previous / next tool   SELECT: 3D view   START: menu
 *   panels: up / down picks a row, left / right changes it, X opens it, O closes
 *
 * Touch: the prompt bar's buttons are tappable, a tap on the map puts the cursor there, a drag
 * pans, two fingers zoom. Mouse: the cursor follows it, a click uses the tool, right-drag pans,
 * the wheel zooms. Everything edits an EmMap (game/edmap.h) - the .map format level 1 is made in.
 */
#ifndef GEN_BAKED
#include "edint.h"
#include "fx.h"
#include "ui.h"
#include "snd.h"
#include "sfx_ids.h"
#include "level.h"
#include "vram.h"
#include "gen_lab.h"
#include <stdlib.h>
#include <stddef.h>

EmMap ED;
int ed_pad_style, ed_touch, ed_t, ed_lx, ed_ly, ed_rx, ed_ry, ed_dirty;
s32 ed_cx, ed_cz;
EdPtr ed_ptr[16]; int ed_nptr, ed_wheel;
int ed_mouse_x, ed_mouse_y, ed_mouse_on;

/* ------------------------------------------------------------------ layout */
/* laid out each frame for the frame's width (320..512) and the input: on a touch screen the map
   sits between a tool rail (left) and an action rail (right) with big buttons and taller rows;
   with a gamepad / mouse the map is the whole width and a prompt bar runs along the bottom */
static int top_h = 14, bar_y = 226, map_x0, map_x1 = 320, row_h = 11, touch_lay;
#define TOP_H   top_h
#define BAR_Y   bar_y
#define MAP_Y0  TOP_H
#define MAP_Y1  BAR_Y
#define MAP_X0  map_x0
#define MAP_X1  map_x1
#define MAP_CX  ((map_x0 + map_x1) / 2)
#define MAP_CY  ((MAP_Y0 + MAP_Y1) / 2)
#define RAIL_L  60                      /* touch: the tool rail */
#define RAIL_R  76                      /* touch: the action rail */

enum { M_LIST, M_MAP, M_3D };
static int mode, want;                          /* want: what editor_update returns */
static void layout(void)
{
    touch_lay = ed_touch && mode != M_LIST;
    if (touch_lay) { top_h = 20; bar_y = SCREEN_H; map_x0 = mode == M_3D ? 0 : RAIL_L; map_x1 = SCREEN_W - RAIL_R; row_h = 18; }
    else { top_h = 14; bar_y = 226; map_x0 = 0; map_x1 = SCREEN_W; row_h = 11; }
}
int ed_rail_r(void) { return touch_lay ? RAIL_R : 0; }
int ed_bar_y(void) { return bar_y; }
int ed_top_h(void) { return top_h; }
static char file_name[32];
static const MapDef *play_def;

/* view */
static const int ZOOMS[] = { 4, 6, 8, 12, 16, 24, 32, 48, 64 };
#define NZOOM ((int)(sizeof(ZOOMS) / sizeof(ZOOMS[0])))
static int zi = 4;
static s32 vx, vz;                              /* map units at the centre of the map area */
static const s32 GRIDS[] = { 256, 512, 1024, 2048 };
static int gi = 1;
static s32 stick_ax, stick_az;                  /* the left stick's sub-grid travel */
static int show_way;

/* tools */
enum { T_DRAW, T_RECT, T_FLOOR, T_CEIL, T_EXTRUDE, T_CUT, T_ADDPT, T_MOVE, T_DELETE, T_JOIN, T_STAIRS, T_PLACE, T_TURN, T_COUNT };
static const char *const TOOL_NAME[T_COUNT] = { "DRAW", "RECTANGLE", "FLOOR", "CEILING", "EXTRUDE", "CUT", "ADD CORNER", "MOVE", "DELETE", "JOIN", "STAIRS", "PLACE", "TURN" };
static const char *const TOOL_HELP[T_COUNT] = {
    "X EACH CORNER. X THE FIRST TO CLOSE",
    "X ONE CORNER THEN THE OTHER",
    "X RAISES THE FLOOR. O LOWERS IT",
    "X RAISES THE CEILING. O LOWERS IT",
    "X A WALL THEN HOW FAR OUT",
    "X TWO POINTS ON A SECTOR'S WALLS",
    "X ON A WALL TO SPLIT IT",
    "X TO PICK UP. X AGAIN TO DROP",
    "X DELETES WHAT'S UNDER THE CURSOR",
    "X A WALL: ITS TWO SECTORS BECOME ONE",
    "X A RECTANGLE: STEPS CLIMB TO THE CURSOR",
    "X PLACES IT. TRIANGLE CHOOSES WHAT",
    "X TURNS A THING OR PROP",
};
static int tool = T_DRAW, stage;
static s32 pend_x[EM_SV], pend_z[EM_SV]; static int npend;
static s32 anc_x, anc_z; static int anc_s = -1, anc_w = -1;
/* MOVE */
enum { K_NONE, K_SECTOR, K_WALL, K_CORNER, K_THING, K_LIGHT, K_SPRITE, K_PROP };
static int mv_kind, mv_i; static s32 mv_x0, mv_z0, mv_px[EM_SV], mv_pz[EM_SV]; static int mv_n;
/* PLACE */
enum { P_START, P_TROOPER, P_KEY_C, P_KEY_A, P_KEY_R, P_SHARD, P_SECRET, P_EXIT, P_LIGHT, P_GLOW, P_PROP0 };
#define P_COUNT (P_PROP0 + PK_COUNT)
static int place = P_TROOPER;

/* overlays */
enum { OV_NONE, OV_TOOLS, OV_MENU, OV_INSPECT, OV_ROWS, OV_TILES, OV_KEYS, OV_CONFIRM, OV_HELP };
static int ov, ov_sel, ov_scroll;
static char toast[64]; static int toast_t;

/* touch / mouse */
static u32 tap_press, touch_hold;
static int drag_id = -1, drag_x, drag_y, drag_moved, drag_kind, drag_hit, pinch_d;
static int fing_x[4], fing_y[4], fing_on[4];
#define H_PROMPT 1000
#define H_TOP    1100
#define H_ROW    1200
#define H_TILE   1500
#define H_KEY    1700
#define H_LIST   1800
#define H_DPAD   1900
#define H_TOOLR 2400
static const int RAIL_TOOLS[] = { T_DRAW, T_RECT, T_FLOOR, T_CEIL, T_MOVE, T_DELETE, T_PLACE, -1 };
#define NRAIL ((int)(sizeof(RAIL_TOOLS) / sizeof(RAIL_TOOLS[0])))


/* ------------------------------------------------------------------ small helpers */
static s32 iabs32(s32 v) { return v < 0 ? -v : v; }
static s32 snapv(s32 v, s32 g) { return v >= 0 ? (v + g / 2) / g * g : -((-v + g / 2) / g * g); }
static s32 clampv(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }
static void scopy(char *d, const char *s, int cap) { int i = 0; while (s[i] && i < cap - 1) { d[i] = s[i]; i++; } d[i] = 0; }
void ed_toast(const char *m) { scopy(toast, m, sizeof(toast)); toast_t = 150; }
static void upcase(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }
static void err_toast(void) { char b[96]; scopy(b, em_error[0] ? em_error : "CAN'T DO THAT", 96); upcase(b); ed_toast(b); snd_play(SFX_UI_MOVE, 2600, 0, 2400); }

/* ------------------------------------------------------------------ undo: packed copies of the map */
#define UNDO_N 40
static unsigned char *undo_buf[UNDO_N], *redo_buf[UNDO_N];
static int nundo, nredo;
#define PACK_LIST(X) X(secs, nsecs) X(rows, nrows) X(wcells, nwcells) X(fcells, nfcells) X(things, nthings) \
                     X(lights, nlights) X(sprites, nsprites) X(props, nprops)
static unsigned char *pack(const EmMap *m)
{
    size_t n = offsetof(EmMap, secs);
    unsigned char *b, *p;
#define SZ(a, c) n += sizeof(m->a[0]) * (size_t)m->c;
    PACK_LIST(SZ)
#undef SZ
    b = (unsigned char *)malloc(n);
    if (!b) return 0;
    memcpy(b, m, offsetof(EmMap, secs)); p = b + offsetof(EmMap, secs);
#define CP(a, c) memcpy(p, m->a, sizeof(m->a[0]) * (size_t)m->c); p += sizeof(m->a[0]) * (size_t)m->c;
    PACK_LIST(CP)
#undef CP
    return b;
}
static void unpack(EmMap *m, const unsigned char *b)
{
    const unsigned char *p = b + offsetof(EmMap, secs);
    memcpy(m, b, offsetof(EmMap, secs));
#define CP(a, c) memcpy(m->a, p, sizeof(m->a[0]) * (size_t)m->c); p += sizeof(m->a[0]) * (size_t)m->c;
    PACK_LIST(CP)
#undef CP
}
static void clear_stack(unsigned char **s, int *n) { while (*n) free(s[--*n]); }
void ed_undo_push(void)
{
    unsigned char *b = pack(&ED);
    if (!b) return;
    if (nundo == UNDO_N) { free(undo_buf[0]); memmove(undo_buf, undo_buf + 1, sizeof(undo_buf[0]) * (UNDO_N - 1)); nundo--; }
    undo_buf[nundo++] = b;
    clear_stack(redo_buf, &nredo);
}
static void undo_drop(void) { if (nundo) free(undo_buf[--nundo]); }      /* the last push, unused */
static void undo_restore_last(void) { if (nundo) { unpack(&ED, undo_buf[nundo - 1]); undo_drop(); } }
static int need3d = 1;
void ed_changed(void) { ed_dirty = 1; need3d = 1; }
static void do_undo(int redo)
{
    unsigned char **from = redo ? redo_buf : undo_buf, **to = redo ? undo_buf : redo_buf;
    int *nf = redo ? &nredo : &nundo, *nt = redo ? &nundo : &nredo;
    unsigned char *cur;
    if (!*nf) { ed_toast(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
    cur = pack(&ED);
    if (cur) { if (*nt == UNDO_N) { free(to[0]); memmove(to, to + 1, sizeof(to[0]) * (UNDO_N - 1)); (*nt)--; } to[(*nt)++] = cur; }
    unpack(&ED, from[--*nf]); free(from[*nf]);
    stage = 0; npend = 0; ed_changed();
    ed_toast(redo ? "REDONE" : "UNDONE");
}

/* an edit that may break the map: done inside a snapshot, undone if the sectors don't check out */
static int geom_ok(void)
{
    int s, w;
    if (em_check_geom(&ED, &s, &w)) { undo_restore_last(); err_toast(); return 0; }
    ed_changed();
    return 1;
}

/* ------------------------------------------------------------------ files */
static char *big;                                  /* save / load text buffer */
#define BIG_CAP (1 << 19)
static char lst[64][32]; static int nlst;
static EmMap *peek;                                 /* level titles for the list */
static char lst_title[64][24];
static int lst_sel, lst_act, lst_confirm;

static void list_refresh(void)
{
    int i;
    nlst = plat_file_list(lst, 64);
    if (nlst < 0) nlst = 0;
    if (!big) big = (char *)malloc(BIG_CAP);
    if (!peek) peek = (EmMap *)malloc(sizeof(EmMap));
    for (i = 0; i < nlst; i++) {
        int n = big ? plat_file_load(lst[i], big, BIG_CAP - 1) : -1, k;
        scopy(lst_title[i], lst[i], 24);
        for (k = 0; lst_title[i][k]; k++) if (lst_title[i][k] == '.') lst_title[i][k] = 0;
        if (n > 0) {   /* the title from the file's `name` line */
            const char *p = big; big[n] = 0;
            while (*p) {
                if (p[0] == 'n' && p[1] == 'a' && p[2] == 'm' && p[3] == 'e' && p[4] == ' ') {
                    int j = 0; p += 5;
                    while (*p && *p != '\n' && *p != '\r' && j < 23) lst_title[i][j++] = *p++;
                    lst_title[i][j] = 0; break;
                }
                while (*p && *p != '\n') p++;
                if (*p) p++;
            }
        }
        upcase(lst_title[i]);
    }
}
static void new_file_name(char *o)
{
    int k, i;
    for (k = 1; k < 100; k++) {
        char nm[32] = "level"; int clash = 0;
        nm[5] = (char)('0' + k / 10); nm[6] = (char)('0' + k % 10); nm[7] = 0;
        eu_str(nm + 7, ".map");
        for (i = 0; i < nlst; i++) if (!strcmp(lst[i], nm)) clash = 1;
        if (!clash) { scopy(o, nm, 32); return; }
    }
    scopy(o, "level99.map", 32);
}
static int save_now(void)
{
    int n;
    if (!big) big = (char *)malloc(BIG_CAP);
    if (!big) return 0;
    em_rows_gc(&ED);
    n = em_write(&ED, big, BIG_CAP);
    if (n < 0) { ed_toast("TOO BIG TO SAVE"); return 0; }
    if (!plat_file_save(file_name, big, n)) { ed_toast("COULDN'T SAVE"); return 0; }
    ed_dirty = 0;
    return 1;
}

/* ------------------------------------------------------------------ view transform */
static int ppm(void) { return ZOOMS[zi]; }
static s32 sx_of(s32 x) { return MAP_CX + (s32)(((long long)(x - vx) * ppm()) >> 10); }
static s32 sy_of(s32 z) { return MAP_CY - (s32)(((long long)(z - vz) * ppm()) >> 10); }
static s32 wx_of(int sx) { return vx + (s32)(((long long)(sx - MAP_CX) << 10) / ppm()); }
static s32 wz_of(int sy) { return vz - (s32)(((long long)(sy - MAP_CY) << 10) / ppm()); }
static s32 grid(void) { return GRIDS[gi]; }
static s32 pick_r(void) { s32 r = (8 << 10) / ppm(); return r < 300 ? 300 : r; }
static void follow_cursor(void)
{
    s32 x = sx_of(ed_cx), y = sy_of(ed_cz), mx = 100, my = 70;
    if (x < MAP_CX - mx) vx -= ((MAP_CX - mx - x) << 10) / ppm();
    if (x > MAP_CX + mx) vx += ((x - MAP_CX - mx) << 10) / ppm();
    if (y < MAP_CY - my) vz += ((MAP_CY - my - y) << 10) / ppm();
    if (y > MAP_CY + my) vz -= ((y - MAP_CY - my) << 10) / ppm();
}
static void cursor_to(s32 x, s32 z)
{
    ed_cx = clampv(snapv(x, grid()), -EM_LIMIT, EM_LIMIT); ed_cz = clampv(snapv(z, grid()), -EM_LIMIT, EM_LIMIT);
}
static void zoom_by(int d, int sx, int sy)
{
    s32 wx = wx_of(sx), wz = wz_of(sy);
    zi = clampv(zi + d, 0, NZOOM - 1);
    vx = wx - (((s32)(sx - MAP_CX) << 10) / ppm()); vz = wz + (((s32)(sy - MAP_CY) << 10) / ppm());
}

/* ------------------------------------------------------------------ picking */
typedef struct { int kind, i, j; } Pick;
static Pick pick_at(s32 x, s32 z, int allow_sector)
{
    Pick p; int i; s32 r = pick_r(); long long best;
    p.kind = K_NONE; p.i = p.j = -1;
#define D2(ax, az) ((long long)((ax) - x) * ((ax) - x) + (long long)((az) - z) * ((az) - z))
    best = (long long)r * r;
    for (i = 0; i < ED.nthings; i++) { if (ED.things[i].type == TH_WAY && !show_way) continue; if (D2(ED.things[i].x, ED.things[i].z) < best) { best = D2(ED.things[i].x, ED.things[i].z); p.kind = K_THING; p.i = i; } }
    for (i = 0; i < ED.nlights; i++) { if (D2(ED.lights[i].x, ED.lights[i].z) < best) { best = D2(ED.lights[i].x, ED.lights[i].z); p.kind = K_LIGHT; p.i = i; } }
    for (i = 0; i < ED.nsprites; i++) { if (D2(ED.sprites[i].x, ED.sprites[i].z) < best) { best = D2(ED.sprites[i].x, ED.sprites[i].z); p.kind = K_SPRITE; p.i = i; } }
    for (i = 0; i < ED.nprops; i++) { s32 px, pz; em_prop_xz(&ED.props[i], &px, &pz); if (D2(px, pz) < best) { best = D2(px, pz); p.kind = K_PROP; p.i = i; } }
#undef D2
    if (p.kind != K_NONE) return p;
    {
        int si, pi;
        if (em_near_point(&ED, x, z, r * 3 / 4, &si, &pi)) { p.kind = K_CORNER; p.i = si; p.j = pi; return p; }
        if (em_near_wall(&ED, x, z, r / 2, &si, &pi)) { p.kind = K_WALL; p.i = si; p.j = pi; return p; }
    }
    if (allow_sector) { i = em_sector_at(&ED, x, z); if (i >= 0) { p.kind = K_SECTOR; p.i = i; } }
    return p;
}

/* ------------------------------------------------------------------ the 2D map */
static int earclip_i(const s32 *x, const s32 *z, int n, int (*tri)[3])
{
    int idx[EM_SV], m = 0, nt = 0, i, j, guard = 0;
    for (i = 0; i < n; i++) idx[m++] = i;
    while (m > 3 && guard++ < 200) {
        int found = 0;
        for (i = 0; i < m && !found; i++) {
            int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m], ok = 1;
            long long cr = (long long)(x[b] - x[a]) * (z[c] - z[a]) - (long long)(z[b] - z[a]) * (x[c] - x[a]);
            if (cr <= 0) continue;
            for (j = 0; j < m && ok; j++) {
                int q = idx[j];
                long long d0, d1, d2;
                if (q == a || q == b || q == c) continue;
                d0 = (long long)(x[b] - x[a]) * (z[q] - z[a]) - (long long)(z[b] - z[a]) * (x[q] - x[a]);
                d1 = (long long)(x[c] - x[b]) * (z[q] - z[b]) - (long long)(z[c] - z[b]) * (x[q] - x[b]);
                d2 = (long long)(x[a] - x[c]) * (z[q] - z[c]) - (long long)(z[a] - z[c]) * (x[q] - x[c]);
                if (d0 >= 0 && d1 >= 0 && d2 >= 0) ok = 0;
            }
            if (!ok) continue;
            tri[nt][0] = a; tri[nt][1] = b; tri[nt][2] = c; nt++;
            for (j = i; j < m - 1; j++) idx[j] = idx[j + 1];
            m--; found = 1;
        }
        if (!found) break;
    }
    if (m == 3) { tri[nt][0] = idx[0]; tri[nt][1] = idx[1]; tri[nt][2] = idx[2]; nt++; }
    return nt;
}
static u32 mix(u32 a, u32 b, int k)   /* k 0..256 */
{
    int r = (int)((a >> 16) & 255), g = (int)((a >> 8) & 255), bl = (int)(a & 255);
    r += (((int)((b >> 16) & 255) - r) * k) >> 8; g += (((int)((b >> 8) & 255) - g) * k) >> 8; bl += (((int)(b & 255) - bl) * k) >> 8;
    return RGB(r, g, bl);
}
static const u32 KEYC[4] = { 0, RGB(40, 210, 240), RGB(250, 170, 40), RGB(235, 40, 55) };
static int sel_kind = K_NONE, sel_i = -1, sel_j = -1;      /* what the inspector is showing */

static void sector_colour(int si, u32 *c, s32 lo, s32 hi)
{
    const EmSector *s = &ED.secs[si];
    int k = hi > lo ? (int)((s->floor - lo) * 256 / (hi - lo)) : 128;
    *c = mix(RGB(52, 22, 20), RGB(150, 122, 100), k);
    if (s->kind == SK_DOOR) *c = s->lock ? mix(RGB(20, 14, 14), KEYC[s->lock], 150) : RGB(120, 86, 50);
    else if (s->kind == SK_LIFT) *c = RGB(70, 110, 60);
    else if (s->ceil <= s->floor) *c = RGB(26, 24, 28);
}

static void draw_thing_mark(const EmThing *t, int hot)
{
    s32 x = sx_of(t->x), y = sy_of(t->z);
    u32 c = t->type == TH_START ? RGB(90, 240, 100) : t->type == TH_TROOPER ? RGB(235, 60, 45) : t->type == TH_KEY ? KEYC[t->arg & 3] :
            t->type == TH_SHARD ? (t->arg ? RGB(255, 120, 255) : RGB(170, 120, 255)) : t->type == TH_EXIT ? RGB(255, 210, 40) : RGB(110, 120, 110);
    int r = t->type == TH_WAY ? 2 : 4;
    if (x < -20 || x > 340 || y < 0 || y > 260) return;
    if (t->type == TH_KEY) { eu_rect((int)x - 3, (int)y - 4, (int)x + 4, (int)y + 4, c, L_THING); eu_frame((int)x - 4, (int)y - 5, (int)x + 5, (int)y + 5, RGB(10, 6, 6), L_THING + 1); }
    else if (t->type == TH_EXIT) { eu_tri(x, y - 6, x + 6, y + 4, x - 6, y + 4, c, -1, L_THING); }
    else { eu_disc((int)x, (int)y, r + 1, RGB(10, 6, 6), -1, L_THING + 1); eu_disc((int)x, (int)y, r, c, -1, L_THING); }
    if (t->type == TH_START || t->type == TH_TROOPER) {
        int ex = (int)x + (isin(t->yaw) * 9 >> 12), ey = (int)y - (icos(t->yaw) * 9 >> 12);
        eu_line((int)x, (int)y, ex, ey, c, 1, L_THING);
        if (t->type == TH_TROOPER && (t->ax != t->x || t->az != t->z)) eu_line_semi((int)x, (int)y, (int)sx_of(t->ax), (int)sy_of(t->az), RGB(90, 30, 20), L_THING + 2);
    }
    if (hot) eu_ring((int)x, (int)y, 7, RGB(255, 200, 80), L_THING - 1);
}
static void draw_prop_mark(const EmProp *p, int hot)
{
    u32 c = hot ? RGB(255, 200, 80) : RGB(100, 140, 170);
    s32 x0 = sx_of(p->x0), y0 = sy_of(p->z0), x1 = sx_of(p->x1), y1 = sy_of(p->z1);
    switch (p->kind) {
    case PK_BOX: case PK_BOXF: case PK_MACHINE: {
        s32 a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0, cc = y0 < y1 ? y0 : y1, d = y0 < y1 ? y1 : y0;
        eu_line((int)a, (int)cc, (int)b, (int)cc, c, 1, L_PROP); eu_line((int)a, (int)d, (int)b, (int)d, c, 1, L_PROP);
        eu_line((int)a, (int)cc, (int)a, (int)d, c, 1, L_PROP); eu_line((int)b, (int)cc, (int)b, (int)d, c, 1, L_PROP);
        eu_line((int)a, (int)cc, (int)b, (int)d, mix(c, 0, 140), 1, L_PROP);
        break; }
    case PK_RAIL: eu_line((int)x0, (int)y0, (int)x1, (int)y1, c, 1, L_PROP); break;
    case PK_TANK: eu_ring((int)x0, (int)y0, 3 * ppm() + 2, c, L_PROP); break;
    case PK_CORE: eu_ring((int)x0, (int)y0, 2 * ppm() + 2, c, L_PROP); break;
    case PK_CYL: eu_ring((int)x0, (int)y0, (int)((p->h1 * ppm()) >> 10) + 2, c, L_PROP); break;
    default: {
        int r = p->kind == PK_CRATE ? (int)((p->h0 * ppm()) >> 11) + 1 : p->kind == PK_POD ? ppm() / 2 + 2 : 3;
        eu_frame((int)x0 - r, (int)y0 - r, (int)x0 + r + 1, (int)y0 + r + 1, c, L_PROP);
        if (p->kind == PK_CONSOLE || p->kind == PK_RACK || p->kind == PK_POD || p->kind == PK_CELL) {
            static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
            eu_line((int)x0, (int)y0, (int)x0 + DX[p->dir & 3] * (r + 4), (int)y0 + DY[p->dir & 3] * (r + 4), c, 1, L_PROP);
        }
        break; }
    }
}

static void draw_map(void)
{
    int si, i, j, k;
    s32 lo = 32767, hi = -32768, g = grid();
    eu_clip.x0 = MAP_X0; eu_clip.y0 = MAP_Y0; eu_clip.x1 = MAP_X1; eu_clip.y1 = MAP_Y1;
    eu_rect_g(MAP_X0, MAP_Y0, MAP_X1, MAP_Y1, RGB(14, 6, 7), RGB(30, 8, 6), L_BG);
    /* grid: the snap grid faint, the 2 m tile grid stronger, the origin hot */
    {
        s32 step = g, x0 = wx_of(MAP_X0), x1 = wx_of(MAP_X1), z0 = wz_of(MAP_Y1), z1 = wz_of(MAP_Y0), v;
        while ((step * ppm() >> 10) < 6) step *= 2;
        for (v = (x0 / step - 1) * step; v <= x1; v += step) {
            u32 c = v == 0 ? RGB(110, 40, 26) : v % 2048 == 0 ? RGB(48, 20, 16) : RGB(30, 12, 11);
            if (sx_of(v) >= MAP_X0 && sx_of(v) < MAP_X1) eu_rect((int)sx_of(v), MAP_Y0, (int)sx_of(v) + 1, MAP_Y1, c, L_GRID);
        }
        for (v = (z0 / step - 1) * step; v <= z1; v += step) {
            u32 c = v == 0 ? RGB(110, 40, 26) : v % 2048 == 0 ? RGB(48, 20, 16) : RGB(30, 12, 11);
            eu_rect(MAP_X0, (int)sy_of(v), MAP_X1, (int)sy_of(v) + 1, c, L_GRID);
        }
    }
    for (si = 0; si < ED.nsecs; si++) { if (ED.secs[si].floor < lo) lo = ED.secs[si].floor; if (ED.secs[si].floor > hi) hi = ED.secs[si].floor; }
    {
        int under = em_sector_at(&ED, ed_cx, ed_cz);
        for (si = 0; si < ED.nsecs; si++) {
            const EmSector *s = &ED.secs[si];
            s32 x[EM_SV], z[EM_SV];
            int tri[EM_SV][3], nt;
            u32 c;
            s32 bx0 = 99999, bx1 = -99999, bz0 = 99999, bz1 = -99999;
            for (i = 0; i < s->np; i++) { x[i] = s->x[i]; z[i] = s->z[i]; if (x[i] < bx0) bx0 = x[i]; if (x[i] > bx1) bx1 = x[i]; if (z[i] < bz0) bz0 = z[i]; if (z[i] > bz1) bz1 = z[i]; }
            if (sx_of(bx1) < MAP_X0 || sx_of(bx0) > MAP_X1 || sy_of(bz0) < MAP_Y0 || sy_of(bz1) > MAP_Y1) continue;
            sector_colour(si, &c, lo, hi);
            if (si == under && !ov) c = mix(c, RGB(255, 200, 150), 40);
            if (sel_kind == K_SECTOR && sel_i == si && ov == OV_INSPECT) c = mix(c, RGB(255, 120, 40), 90);
            nt = earclip_i(x, z, s->np, tri);
            for (k = 0; k < nt; k++)
                eu_tri(sx_of(x[tri[k][0]]), sy_of(z[tri[k][0]]), sx_of(x[tri[k][1]]), sy_of(z[tri[k][1]]), sx_of(x[tri[k][2]]), sy_of(z[tri[k][2]]), c, -1, L_FILL);
            /* walls */
            for (i = 0; i < s->np; i++) {
                int jn = (i + 1) % s->np, other = -1, oj, q;
                u32 wc;
                for (oj = 0; oj < ED.nsecs && other < 0; oj++) {
                    const EmSector *o = &ED.secs[oj];
                    if (oj == si) continue;
                    for (q = 0; q < o->np; q++) { int l = (q + 1) % o->np; if (o->x[q] == s->x[jn] && o->z[q] == s->z[jn] && o->x[l] == s->x[i] && o->z[l] == s->z[i]) { other = oj; break; } }
                }
                if (other < 0) wc = RGB(235, 220, 200);
                else if (other < si) continue;                         /* drawn from the other side */
                else wc = iabs32(ED.secs[other].floor - s->floor) > 220 || ED.secs[other].kind != SK_ROOM || s->kind != SK_ROOM ? RGB(230, 120, 50) : RGB(96, 56, 44);
                if (s->w[i].flags & (WF_GLASS | WF_BARS)) wc = RGB(120, 200, 255);
                eu_line((int)sx_of(s->x[i]), (int)sy_of(s->z[i]), (int)sx_of(s->x[jn]), (int)sy_of(s->z[jn]), wc, other < 0 && ppm() >= 16 ? 2 : 1, L_WALL);
            }
            /* corners */
            if (ppm() >= 12)
                for (i = 0; i < s->np; i++) eu_rect((int)sx_of(s->x[i]) - 1, (int)sy_of(s->z[i]) - 1, (int)sx_of(s->x[i]) + 2, (int)sy_of(s->z[i]) + 2, RGB(250, 236, 220), L_VERT);
            /* floor height label */
            if (ppm() >= 24 && s->np) {
                s32 cx = 0, cz = 0; char b[24], *o = b;
                for (i = 0; i < s->np; i++) { cx += s->x[i]; cz += s->z[i]; }
                cx /= s->np; cz /= s->np;
                if (em_inside(s, cx, cz) && iabs32(sx_of(bx1) - sx_of(bx0)) > 28) {
                    o = eu_m(o, s->floor);
                    if (s->kind == SK_DOOR) o = eu_str(o, " D"); else if (s->kind == SK_LIFT) o = eu_str(o, " L");
                    eu_text_c(b, (int)sx_of(cx), (int)sy_of(cz) - 4, RGB(20, 10, 8), L_MAP_TEXT);
                }
            }
        }
    }
    /* the wall the inspector shows, the slope hinge */
    if (ov == OV_INSPECT && (sel_kind == K_WALL || sel_kind == K_SECTOR) && sel_i >= 0 && sel_i < ED.nsecs) {
        const EmSector *s = &ED.secs[sel_i];
        int w = sel_kind == K_WALL ? sel_j : -1;
        for (k = 0; k < 2; k++) {
            int hw = k == 0 ? w : (s->fslope >= 0 ? s->fslope : s->cslope);
            if (hw < 0 || hw >= s->np) continue;
            j = (hw + 1) % s->np;
            eu_line((int)sx_of(s->x[hw]), (int)sy_of(s->z[hw]), (int)sx_of(s->x[j]), (int)sy_of(s->z[j]), k ? RGB(120, 255, 140) : RGB(255, 200, 60), 2, L_SEL);
        }
    }
    /* lights, halos, props, things */
    for (i = 0; i < ED.nlights; i++) {
        const EmLight *L = &ED.lights[i];
        int hot = ov == OV_INSPECT && sel_kind == K_LIGHT && sel_i == i;
        u32 c = RGB(255, 230, 120);
        if (L->grid) {
            s32 a = sx_of(L->x), b = sy_of(L->z), cc = sx_of(L->x1), d = sy_of(L->z1);
            eu_line((int)a, (int)b, (int)cc, (int)b, RGB(90, 80, 40), 1, L_PROP); eu_line((int)a, (int)d, (int)cc, (int)d, RGB(90, 80, 40), 1, L_PROP);
            eu_line((int)a, (int)b, (int)a, (int)d, RGB(90, 80, 40), 1, L_PROP); eu_line((int)cc, (int)b, (int)cc, (int)d, RGB(90, 80, 40), 1, L_PROP);
        }
        eu_rect((int)sx_of(L->x) - 1, (int)sy_of(L->z) - 1, (int)sx_of(L->x) + 2, (int)sy_of(L->z) + 2, c, L_THING);
        if (hot) eu_ring((int)sx_of(L->x), (int)sy_of(L->z), (int)((L->rad * ppm()) >> 10), RGB(120, 100, 40), L_THING + 1);
    }
    for (i = 0; i < ED.nsprites; i++) eu_disc((int)sx_of(ED.sprites[i].x), (int)sy_of(ED.sprites[i].z), 2, RGB(255, 120, 140), -1, L_THING);
    for (i = 0; i < ED.nprops; i++) draw_prop_mark(&ED.props[i], (ov == OV_INSPECT && sel_kind == K_PROP && sel_i == i) || (mv_kind == K_PROP && mv_i == i && stage));
    for (i = 0; i < ED.nthings; i++) if (ED.things[i].type != TH_WAY || show_way)
        draw_thing_mark(&ED.things[i], (ov == OV_INSPECT && sel_kind == K_THING && sel_i == i) || (mv_kind == K_THING && mv_i == i && stage));
    /* tool previews */
    {
        u32 hot = RGB(255, 150, 40);
        int cxs = (int)sx_of(ed_cx), cys = (int)sy_of(ed_cz);
        if (tool == T_DRAW && npend) {
            for (i = 0; i + 1 < npend; i++) eu_line((int)sx_of(pend_x[i]), (int)sy_of(pend_z[i]), (int)sx_of(pend_x[i + 1]), (int)sy_of(pend_z[i + 1]), hot, 2, L_SEL);
            eu_line_semi((int)sx_of(pend_x[npend - 1]), (int)sy_of(pend_z[npend - 1]), cxs, cys, RGB(160, 80, 20), L_SEL);
            for (i = 0; i < npend; i++) eu_rect((int)sx_of(pend_x[i]) - 2, (int)sy_of(pend_z[i]) - 2, (int)sx_of(pend_x[i]) + 3, (int)sy_of(pend_z[i]) + 3, i ? hot : RGB(100, 255, 120), L_SEL - 1);
        }
        if (tool == T_RECT && stage) {
            int a = (int)sx_of(anc_x), b = (int)sy_of(anc_z);
            eu_rect_semi(a < cxs ? a : cxs, b < cys ? b : cys, a < cxs ? cxs : a, b < cys ? cys : b, RGB(60, 24, 6), SEMI_ADD, L_SEL + 1);
            eu_line(a, b, cxs, b, hot, 1, L_SEL); eu_line(a, cys, cxs, cys, hot, 1, L_SEL); eu_line(a, b, a, cys, hot, 1, L_SEL); eu_line(cxs, b, cxs, cys, hot, 1, L_SEL);
        }
        if (tool == T_CUT && stage) eu_line((int)sx_of(anc_x), (int)sy_of(anc_z), cxs, cys, hot, 2, L_SEL);
        if (tool == T_EXTRUDE && stage && anc_s >= 0 && anc_s < ED.nsecs) {
            const EmSector *s = &ED.secs[anc_s];
            int jn = (anc_w + 1) % s->np;
            eu_line((int)sx_of(s->x[anc_w]), (int)sy_of(s->z[anc_w]), (int)sx_of(s->x[jn]), (int)sy_of(s->z[jn]), hot, 2, L_SEL);
        }
    }
    /* the cursor: an amber ring with a cross */
    {
        int x = (int)sx_of(ed_cx), y = (int)sy_of(ed_cz);
        u32 c = mix(RGB(255, 170, 40), RGB(255, 240, 160), 128 + (isin(ed_t * 120) >> 5));
        eu_ring(x, y, 5, c, L_CURSOR);
        eu_rect(x, y, x + 1, y + 1, c, L_CURSOR);
        eu_rect(x - 9, y, x - 6, y + 1, c, L_CURSOR); eu_rect(x + 7, y, x + 10, y + 1, c, L_CURSOR);
        eu_rect(x, y - 9, x + 1, y - 6, c, L_CURSOR); eu_rect(x, y + 7, x + 1, y + 10, c, L_CURSOR);
    }
    eu_clip.x0 = 0; eu_clip.y0 = 0; eu_clip.x1 = SCREEN_W; eu_clip.y1 = SCREEN_H;
}

/* what's under the cursor, top left of the map */
static void draw_info(void)
{
    char a[48], b[48], *o;
    Pick p = pick_at(ed_cx, ed_cz, 1);
    a[0] = b[0] = 0;
    switch (p.kind) {
    case K_SECTOR: case K_WALL: case K_CORNER: {
        int si = p.kind == K_SECTOR ? p.i : em_sector_at(&ED, ed_cx, ed_cz);
        if (si < 0) si = p.i;
        if (si >= 0) {
            const EmSector *s = &ED.secs[si];
            o = eu_str(a, s->name); upcase(a);
            o = eu_str(o, s->kind == SK_DOOR ? " DOOR" : s->kind == SK_LIFT ? " LIFT" : "");
            if (s->room < ED.nrooms) { o = eu_str(o, "  "); o = eu_str(o, ED.rooms[s->room].name); }
            o = eu_str(b, "FLOOR "); o = eu_m(o, s->floor);
            if (s->kind != SK_DOOR) { o = eu_str(o, "  CEIL "); o = eu_m(o, s->ceil); }
            else { o = eu_str(o, "  OPENS "); o = eu_m(o, s->ka); }
            if (p.kind == K_WALL) eu_str(o, "  WALL"); else if (p.kind == K_CORNER) eu_str(o, "  CORNER");
        }
        break; }
    case K_THING: {
        const EmThing *t = &ED.things[p.i];
        o = eu_str(a, em_thing_names[t->type % 6]); upcase(a);
        if (t->type == TH_KEY) { o = eu_str(o, t->arg == 1 ? " CYAN" : t->arg == 2 ? " AMBER" : " CRIMSON"); }
        if (t->type == TH_SHARD && t->arg) o = eu_str(o, " SECRET");
        break; }
    case K_LIGHT: eu_str(a, ED.lights[p.i].grid ? "LIGHT GRID" : "LIGHT"); break;
    case K_SPRITE: eu_str(a, "GLOW"); break;
    case K_PROP: eu_str(a, em_prop_names[ED.props[p.i].kind]); upcase(a); break;
    default: eu_str(a, "OUTSIDE"); break;
    }
    {
        int w = eu_text_w(a) > eu_text_w(b) ? eu_text_w(a) : eu_text_w(b);
        eu_kit(KIT_PLAQUE, MAP_X0 + 1, MAP_Y0 + 1, MAP_X0 + 10 + w, MAP_Y0 + (b[0] ? 28 : 17), L_MAP_TEXT + 1);
        eu_text(a, MAP_X0 + 5, MAP_Y0 + 4, RGB(255, 210, 160), L_MAP_TEXT);
        if (b[0]) eu_text(b, MAP_X0 + 5, MAP_Y0 + 15, RGB(200, 170, 150), L_MAP_TEXT);
    }
    {   /* cursor position, bottom right */
        char c[32]; o = eu_m(c, ed_cx); o = eu_str(o, " "); o = eu_m(o, ed_cz);
        eu_text(c, MAP_X1 - 4 - eu_text_w(c), MAP_Y1 - 11, RGB(150, 110, 90), L_MAP_TEXT);
    }
}

/* ------------------------------------------------------------------ tools */
static int like_sector(const s32 *x, const s32 *z, int n)
{
    int i, si, best = -1;
    for (i = 0; i < n && best < 0; i++) {
        int pi;
        if (em_near_point(&ED, x[i], z[i], 2, &si, &pi)) best = si;
    }
    if (best < 0) { int wi; if (em_near_wall(&ED, x[0], z[0], 300, &si, &wi)) best = si; }
    return best;
}
static int convex(const s32 *x, const s32 *z, int n)
{
    int i, pos = 0, neg = 0;
    for (i = 0; i < n; i++) {
        int a = i, b = (i + 1) % n, c = (i + 2) % n;
        long long cr = (long long)(x[b] - x[a]) * (z[c] - z[b]) - (long long)(z[b] - z[a]) * (x[c] - x[b]);
        if (cr > 0) pos = 1;
        if (cr < 0) neg = 1;
    }
    return !(pos && neg);
}
/* a new outline: inside one sector -> carved out of it; else a new sector beside the others */
static void make_shape(const s32 *x, const s32 *z, int n)
{
    int host = -1, i, all_in = 1, k;
    s32 cx = 0, cz = 0;
    for (i = 0; i < n; i++) { cx += x[i]; cz += z[i]; }
    cx /= n; cz /= n;
    host = em_sector_at(&ED, cx, cz);
    if (host >= 0) {
        for (i = 0; i < n; i++) {
            int on = 0, w;
            const EmSector *s = &ED.secs[host];
            for (w = 0; w < s->np; w++) {
                int jn = (w + 1) % s->np;
                long long dx = s->x[jn] - s->x[w], dz = s->z[jn] - s->z[w], cr = dx * (z[i] - s->z[w]) - dz * (x[i] - s->x[w]), t = dx * (x[i] - s->x[w]) + dz * (z[i] - s->z[w]);
                if (cr == 0 && t >= 0 && t <= dx * dx + dz * dz) on = 1;
            }
            if (!on && !em_inside(s, x[i], z[i])) all_in = 0;
        }
    }
    ed_undo_push();
    if (host >= 0 && all_in) {
        if (!convex(x, z, n)) { undo_drop(); ed_toast("INSIDE A SECTOR, SHAPES MUST BE CONVEX"); return; }
        k = em_carve(&ED, host, x, z, n);
        if (k < 0) { undo_restore_last(); err_toast(); return; }
        if (!geom_ok()) return;
        sel_kind = K_SECTOR; sel_i = k;
        ed_toast("NEW SECTOR CUT INTO THE ROOM");
        return;
    }
    k = em_add_sector(&ED, x, z, n, like_sector(x, z, n));
    if (k < 0) { undo_restore_last(); err_toast(); return; }
    em_weld(&ED);
    if (!geom_ok()) return;
    ed_toast("SECTOR MADE");
}

static void place_at(s32 x, s32 z)
{
    int si = em_sector_at(&ED, x, z);
    if (si < 0 && place < P_LIGHT) { ed_toast("THINGS GO INSIDE A SECTOR"); return; }
    ed_undo_push();
    if (place < P_LIGHT) {
        EmThing t; int i, ex = -1;
        memset(&t, 0, sizeof(t));
        t.type = (u8)(place == P_START ? TH_START : place == P_TROOPER ? TH_TROOPER : place <= P_KEY_R ? TH_KEY : place <= P_SECRET ? TH_SHARD : TH_EXIT);
        t.arg = (u8)(place == P_KEY_C ? 1 : place == P_KEY_A ? 2 : place == P_KEY_R ? 3 : place == P_SECRET ? 1 : 0);
        t.x = t.ax = (s16)x; t.z = t.az = (s16)z;
        if (t.type == TH_TROOPER) t.yaw = 2048;
        if (t.type == TH_START) for (i = 0; i < ED.nthings; i++) if (ED.things[i].type == TH_START) ex = i;
        if (ex >= 0) { t.yaw = ED.things[ex].yaw; ED.things[ex] = t; ed_toast("START MOVED"); }
        else if (ED.nthings >= EM_THINGS) { undo_drop(); ed_toast("TOO MANY THINGS"); return; }
        else { ED.things[ED.nthings++] = t; }
    } else if (place == P_LIGHT) {
        EmLight L; memset(&L, 0, sizeof(L));
        if (ED.nlights >= EM_LIGHTS) { undo_drop(); ed_toast("TOO MANY LIGHTS"); return; }
        L.x = (s16)x; L.z = (s16)z; L.h = (s16)((si >= 0 ? ED.secs[si].ceil : 4096) - 512); L.r = 85; L.g = 82; L.b = 80; L.rad = 6144;
        ED.lights[ED.nlights++] = L;
    } else if (place == P_GLOW) {
        EmSprite S; memset(&S, 0, sizeof(S));
        if (ED.nsprites >= EM_SPRITES) { undo_drop(); ed_toast("16 GLOWS AT MOST"); return; }
        S.x = (s16)x; S.z = (s16)z; S.h = (s16)((si >= 0 ? ED.secs[si].ceil : 4096) - 400); S.size = 260; S.r = 255; S.g = 120; S.b = 60;
        ED.sprites[ED.nsprites++] = S;
    } else {
        EmProp p;
        if (ED.nprops >= EM_PROPS) { undo_drop(); ed_toast("TOO MANY PROPS"); return; }
        em_prop_defaults(&p, place - P_PROP0, x, z);
        if (p.kind == PK_BOX || p.kind == PK_CYL || p.kind == PK_FAN || p.kind == PK_RAIL) {   /* absolute heights: on this floor */
            s32 f = si >= 0 ? em_floor_at(&ED, si, x, z) : 0;
            if (p.kind == PK_BOX) { p.h0 = (s16)f; p.h1 = (s16)(f + 1024); }
            else if (p.kind == PK_RAIL) p.h0 = (s16)f;
            else p.h0 = (s16)(p.kind == PK_FAN ? (si >= 0 ? ED.secs[si].ceil - 1024 : f + 3072) : f);
        }
        ED.props[ED.nprops++] = p;
    }
    ed_changed();
    snd_play(SFX_UI_OK, 2600, 0, 4600);
}

/* a sector's floor or ceiling up / down a step (0.2 m: a step she can climb); held: one undo */
static int height_key = -1;
int ed_height(int si, int ceil, int d)
{
    EmSector *s;
    s32 step = 205;
    if (si < 0 || si >= ED.nsecs) return 0;
    s = &ED.secs[si];
    if (height_key != si * 2 + ceil) { ed_undo_push(); height_key = si * 2 + ceil; }
    if (ceil) {
        if (s->kind == SK_DOOR) { s->ka = (s16)clampv(s->ka + d * step, 512, 16384); }
        else { s32 c = s->ceil + d * step; if (c < s->floor) { ed_toast("THE CEILING CAN'T GO BELOW THE FLOOR"); return 0; } s->ceil = (s16)clampv(c, -30000, 30000); }
        if (s->cslope >= 0) s->cslope_h = (s16)(s->cslope_h + d * step);
    } else {
        s32 f = s->floor + d * step;
        if (s->kind != SK_DOOR && f > s->ceil) { ed_toast("THE FLOOR CAN'T GO ABOVE THE CEILING"); return 0; }
        s->floor = (s16)clampv(f, -30000, 30000);
        if (s->kind == SK_DOOR) s->ceil = s->floor;
        if (s->kind == SK_LIFT) { s->ka = (s16)(s->ka + d * step); s->kb = (s16)(s->kb + d * step); }
        if (s->fslope >= 0) s->fslope_h = (s16)(s->fslope_h + d * step);
    }
    ed_changed();
    {
        char b[48], *o = eu_str(b, ceil ? (s->kind == SK_DOOR ? "DOOR OPENS TO " : "CEILING ") : "FLOOR ");
        eu_m(o, ceil ? (s->kind == SK_DOOR ? s->ka : s->ceil) : s->floor);
        ed_toast(b);
    }
    return 1;
}
void ed_height_done(void) { height_key = -1; }

static int pick_wall_at(s32 x, s32 z, int *si, int *wi) { return em_near_wall(&ED, x, z, pick_r(), si, wi); }

/* the cursor snapped onto the nearest wall of a sector (for cuts and new corners) */
static int onto_wall(s32 x, s32 z, s32 *ox, s32 *oz, int *si, int *wi)
{
    const EmSector *s;
    int jn, pi;
    long long dx, dz, t, L2;
    if (em_near_point(&ED, x, z, pick_r() / 2, si, &pi)) { *ox = ED.secs[*si].x[pi]; *oz = ED.secs[*si].z[pi]; *wi = pi; return 1; }
    if (!pick_wall_at(x, z, si, wi)) return 0;
    s = &ED.secs[*si]; jn = (*wi + 1) % s->np;
    dx = s->x[jn] - s->x[*wi]; dz = s->z[jn] - s->z[*wi]; L2 = dx * dx + dz * dz;
    t = (x - s->x[*wi]) * dx + (z - s->z[*wi]) * dz;
    if (L2 <= 0) return 0;
    *ox = s->x[*wi] + (s32)(dx * t / L2); *oz = s->z[*wi] + (s32)(dz * t / L2);
    return 1;
}

static void tool_apply(void)
{
    s32 x = ed_cx, z = ed_cz;
    switch (tool) {
    case T_DRAW:
        if (npend >= 3 && pend_x[0] == x && pend_z[0] == z) { make_shape(pend_x, pend_z, npend); npend = 0; return; }
        { int i; for (i = 0; i < npend; i++) if (pend_x[i] == x && pend_z[i] == z) { ed_toast("THAT CORNER IS ALREADY IN"); return; } }
        if (npend >= EM_SV) { ed_toast("48 CORNERS AT MOST"); return; }
        pend_x[npend] = x; pend_z[npend] = z; npend++;
        snd_play(SFX_UI_MOVE, 2400, 0, 5000);
        return;
    case T_RECT:
        if (!stage) { anc_x = x; anc_z = z; stage = 1; return; }
        stage = 0;
        if (x == anc_x || z == anc_z) { ed_toast("THAT'S FLAT"); return; }
        {
            s32 px[4], pz[4];
            px[0] = anc_x < x ? anc_x : x; pz[0] = anc_z < z ? anc_z : z; px[1] = anc_x < x ? x : anc_x; pz[1] = pz[0];
            px[2] = px[1]; pz[2] = anc_z < z ? z : anc_z; px[3] = px[0]; pz[3] = pz[2];
            make_shape(px, pz, 4);
        }
        return;
    case T_EXTRUDE:
        if (!stage) { if (!pick_wall_at(x, z, &anc_s, &anc_w)) { ed_toast("PUT THE CURSOR ON A WALL"); return; } stage = 1; return; }
        stage = 0;
        {
            const EmSector *s = &ED.secs[anc_s];
            int jn = (anc_w + 1) % s->np, k;
            long long ex = s->x[jn] - s->x[anc_w], ez = s->z[jn] - s->z[anc_w];
            long long d = ((x - s->x[anc_w]) * ez - (z - s->z[anc_w]) * ex);         /* to the right (outward), x |e| */
            s32 L = (s32)isqrt((u32)((ex * ex + ez * ez) >> 4)) * 4, dist;
            if (L <= 0) return;
            dist = (s32)(d / L);
            dist = snapv(dist, grid());
            if (dist < grid()) { ed_toast("MOVE THE CURSOR OUT PAST THE WALL"); return; }
            ed_undo_push();
            k = em_extrude(&ED, anc_s, anc_w, dist);
            if (k < 0) { undo_restore_last(); err_toast(); return; }
            em_weld(&ED);
            if (geom_ok()) ed_toast("EXTRUDED");
        }
        return;
    case T_CUT: {
        s32 ox, oz; int si, wi;
        if (!onto_wall(x, z, &ox, &oz, &si, &wi)) { ed_toast("PUT THE CURSOR ON A WALL"); return; }
        if (!stage) { anc_x = ox; anc_z = oz; stage = 1; return; }
        stage = 0;
        si = em_sector_at(&ED, (anc_x + ox) / 2, (anc_z + oz) / 2);
        if (si < 0) { ed_toast("THE CUT MUST RUN INSIDE A SECTOR"); return; }
        ed_undo_push();
        if (!em_cut(&ED, si, anc_x, anc_z, ox, oz)) { undo_restore_last(); err_toast(); return; }
        if (geom_ok()) ed_toast("CUT IN TWO");
        return; }
    case T_ADDPT: {
        s32 ox, oz; int si, wi;
        if (!pick_wall_at(x, z, &si, &wi)) { ed_toast("PUT THE CURSOR ON A WALL"); return; }
        if (!onto_wall(x, z, &ox, &oz, &si, &wi)) return;
        ed_undo_push();
        {
            const EmSector *s = &ED.secs[si];
            int i;
            for (i = 0; i < s->np; i++) if (s->x[i] == ox && s->z[i] == oz) { undo_drop(); ed_toast("THERE'S A CORNER THERE"); return; }
        }
        if (!em_insert_point(&ED, si, wi + 1, ox, oz)) { undo_restore_last(); ed_toast("TOO MANY CORNERS"); return; }
        em_weld(&ED);
        if (geom_ok()) ed_toast("CORNER ADDED");
        return; }
    case T_MOVE:
        if (!stage) {
            Pick p = pick_at(x, z, 1);
            int i;
            if (p.kind == K_NONE) { ed_toast("NOTHING TO MOVE HERE"); return; }
            ed_undo_push();
            mv_kind = p.kind; mv_i = p.i; mv_x0 = x; mv_z0 = z; mv_n = 0;
            if (p.kind == K_CORNER) { mv_px[0] = ED.secs[p.i].x[p.j]; mv_pz[0] = ED.secs[p.i].z[p.j]; mv_n = 1; }
            else if (p.kind == K_WALL) { const EmSector *s = &ED.secs[p.i]; int jn = (p.j + 1) % s->np; mv_px[0] = s->x[p.j]; mv_pz[0] = s->z[p.j]; mv_px[1] = s->x[jn]; mv_pz[1] = s->z[jn]; mv_n = 2; }
            else if (p.kind == K_SECTOR) { const EmSector *s = &ED.secs[p.i]; for (i = 0; i < s->np; i++) { mv_px[i] = s->x[i]; mv_pz[i] = s->z[i]; } mv_n = s->np; }
            stage = 1;
            return;
        }
        stage = 0;
        {
            s32 dx = x - mv_x0, dz = z - mv_z0;
            if (!dx && !dz) { undo_drop(); return; }
            if (mv_kind == K_THING) { EmThing *t = &ED.things[mv_i]; s32 ox = t->x, oz = t->z; t->x = (s16)(t->x + dx); t->z = (s16)(t->z + dz); t->ax = (s16)(t->ax + t->x - ox); t->az = (s16)(t->az + t->z - oz); }
            else if (mv_kind == K_LIGHT) { EmLight *L = &ED.lights[mv_i]; L->x = (s16)(L->x + dx); L->z = (s16)(L->z + dz); if (L->grid) { L->x1 = (s16)(L->x1 + dx); L->z1 = (s16)(L->z1 + dz); } }
            else if (mv_kind == K_SPRITE) { ED.sprites[mv_i].x = (s16)(ED.sprites[mv_i].x + dx); ED.sprites[mv_i].z = (s16)(ED.sprites[mv_i].z + dz); }
            else if (mv_kind == K_PROP) em_prop_move(&ED.props[mv_i], dx, dz);
            else {
                if (em_move_points(&ED, mv_px, mv_pz, mv_n, dx, dz) < 0) { undo_restore_last(); ed_toast("OUTSIDE THE MAP'S LIMITS"); return; }
                em_weld(&ED);
                if (!geom_ok()) return;
            }
            ed_changed();
        }
        return;
    case T_DELETE: {
        Pick p = pick_at(x, z, 1);
        int i;
        if (p.kind == K_NONE || p.kind == K_WALL) {
            if (p.kind == K_WALL) { i = em_sector_at(&ED, x, z); if (i >= 0) { p.kind = K_SECTOR; p.i = i; } }
            if (p.kind != K_SECTOR) { ed_toast("NOTHING TO DELETE HERE"); return; }
        }
        ed_undo_push();
        switch (p.kind) {
        case K_THING: for (i = p.i; i < ED.nthings - 1; i++) ED.things[i] = ED.things[i + 1]; ED.nthings--; break;
        case K_LIGHT: for (i = p.i; i < ED.nlights - 1; i++) ED.lights[i] = ED.lights[i + 1]; ED.nlights--; break;
        case K_SPRITE: for (i = p.i; i < ED.nsprites - 1; i++) ED.sprites[i] = ED.sprites[i + 1]; ED.nsprites--; break;
        case K_PROP: for (i = p.i; i < ED.nprops - 1; i++) ED.props[i] = ED.props[i + 1]; ED.nprops--; break;
        case K_CORNER:
            if (em_delete_corner(&ED, ED.secs[p.i].x[p.j], ED.secs[p.i].z[p.j]) <= 0) { undo_restore_last(); err_toast(); return; }
            if (!geom_ok()) return;
            break;
        case K_SECTOR: em_delete_sector(&ED, p.i); break;
        default: break;
        }
        sel_kind = K_NONE;
        ed_changed();
        ed_toast("DELETED");
        return; }
    case T_JOIN: {
        int si, wi, k;
        if (!pick_wall_at(x, z, &si, &wi)) { ed_toast("PUT THE CURSOR ON THE WALL BETWEEN THEM"); return; }
        ed_undo_push();
        k = em_join(&ED, si, wi);
        if (k < 0 || !em_error[0] ? 0 : 1) { }
        if (k < 0 || (k == 0 && em_error[0] && ED.nsecs == 0)) { undo_restore_last(); err_toast(); return; }
        if (!geom_ok()) return;
        ed_toast("JOINED");
        return; }
    case T_STAIRS: {
        int si = em_sector_at(&ED, x, z), dir, oth, i;
        s32 cx = 0, cz = 0, to, ox, oz;
        if (si < 0) { ed_toast("PUT THE CURSOR IN A RECTANGLE"); return; }
        {
            const EmSector *s = &ED.secs[si];
            s32 mnx = 99999, mxx = -99999, mnz = 99999, mxz = -99999;
            for (i = 0; i < s->np; i++) { cx += s->x[i]; cz += s->z[i]; if (s->x[i] < mnx) mnx = s->x[i]; if (s->x[i] > mxx) mxx = s->x[i]; if (s->z[i] < mnz) mnz = s->z[i]; if (s->z[i] > mxz) mxz = s->z[i]; }
            cx /= s->np; cz /= s->np;
            /* climb toward the side the cursor is nearest */
            {
                s32 dn = mxz - z, ds = z - mnz, de = mxx - x, dw = x - mnx, best = dn;
                dir = 0;
                if (de < best) { best = de; dir = 1; }
                if (ds < best) { best = ds; dir = 2; }
                if (dw < best) { dir = 3; }
            }
            ox = dir == 1 ? mxx + 64 : dir == 3 ? mnx - 64 : cx; oz = dir == 0 ? mxz + 64 : dir == 2 ? mnz - 64 : cz;
            oth = em_sector_at(&ED, ox, oz);
            to = oth >= 0 && ED.secs[oth].floor != s->floor ? ED.secs[oth].floor : s->floor + 1024;
        }
        ed_undo_push();
        if (em_stairs(&ED, si, dir, to, 0) <= 0) { undo_restore_last(); err_toast(); return; }
        if (geom_ok()) ed_toast("STAIRS BUILT");
        return; }
    case T_FLOOR: case T_CEIL: {
        int si = em_sector_at(&ED, x, z);
        if (si < 0) { ed_toast("PUT THE CURSOR IN A SECTOR"); return; }
        ed_height(si, tool == T_CEIL, 1);
        return; }
    case T_PLACE: place_at(x, z); return;
    case T_TURN: {
        Pick p = pick_at(x, z, 0);
        if (p.kind == K_THING) { ed_undo_push(); ED.things[p.i].yaw = (s16)((ED.things[p.i].yaw + 512) & 4095); ed_changed(); }
        else if (p.kind == K_PROP) { ed_undo_push(); ED.props[p.i].dir = (u8)((ED.props[p.i].dir + 1) & 3); ed_changed(); }
        else ed_toast("PUT THE CURSOR ON A THING OR PROP");
        return; }
    }
}
static void tool_cancel(void)
{
    if (tool == T_FLOOR || tool == T_CEIL) {
        int si = em_sector_at(&ED, ed_cx, ed_cz);
        if (si < 0) ed_toast("PUT THE CURSOR IN A SECTOR"); else ed_height(si, tool == T_CEIL, -1);
        return;
    }
    if (tool == T_DRAW && npend) { npend--; ed_toast(npend ? "CORNER REMOVED" : "DRAWING CANCELLED"); return; }
    if (stage) {
        if (tool == T_MOVE) undo_drop();
        stage = 0; ed_toast("CANCELLED");
    }
}
static void set_tool(int t)
{
    if (tool == T_MOVE && stage) undo_drop();
    tool = (t + T_COUNT) % T_COUNT; stage = 0; npend = 0;
    ed_toast(TOOL_HELP[tool]);
}

/* ------------------------------------------------------------------ prompt bar */
static u32 pr_b[8]; static const char *pr_l[8]; static int npr;
void ed_prompts(const u32 *b, const char *const *l, int n) { int i; npr = n > 8 ? 8 : n; for (i = 0; i < npr; i++) { pr_b[i] = b[i]; pr_l[i] = l[i]; } }
u32 ed_prompt_taps(void) { u32 t = tap_press; return t; }
/* touch: the prompts as a rail of big buttons down the right side; the first is the main action
   (lit), each labelled with what it does now; held down they stay pressed (repeating changes) */
static int rail_skip(u32 b)
{
    if (b & (PAD_START | PAD_SELECT)) return 1;                       /* the top bar has MENU and 3D */
    if (b == PAD_TRIANGLE && mode == M_MAP && !ov) return 1;         /* the tool rail has the tools */
    if ((b & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN) || (b & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT)) return 1;   /* tap the panel instead */
    return 0;
}
static void draw_rail(void)
{
    int i, y = TOP_H + 6, x0 = SCREEN_W - RAIL_R + 4, x1 = SCREEN_W - 4, first = 1, n = 0, hb, gap = 5;
    eu_kit(KIT_BAR, SCREEN_W - RAIL_R, TOP_H - 2, SCREEN_W, SCREEN_H, L_BAR);
    for (i = 0; i < npr; i++) if (!rail_skip(pr_b[i])) n++;
    hb = n > 1 ? (SCREEN_H - y - 4 - 36 - (n - 1) * gap) / (n - 1) : 26;      /* as tall as fits, 26 at most */
    if (hb > 26) hb = 26;
    if (hb < 18) { gap = 3; hb = (SCREEN_H - y - 4 - 36 - (n - 1) * gap) / (n - 1); }
    for (i = 0; i < npr; i++) {
        int h = first ? 36 : hb, lit = (touch_hold & pr_b[i]) != 0;
        char l[12];
        if (rail_skip(pr_b[i])) continue;
        if (y + h > SCREEN_H - 4) break;
        scopy(l, pr_l[i], 10);
        if (first) {   /* the main action: a hot plate with its badge */
            eu_kit(lit ? KIT_BTN_LIT : KIT_BTN_HOT, x0, y, x1, y + h, L_BAR_TEXT + 2);
            eu_badge(pr_b[i], (x0 + x1) / 2 - eu_badge_w(pr_b[i], ed_pad_style) / 2, y + 5, ed_pad_style, L_BAR_TEXT);
            eu_text_c(l, (x0 + x1) / 2 + 1, y + 22, RGB(255, 240, 210), L_BAR_TEXT);
        } else {
            eu_button(x0, y, x1, y + h, l, lit, L_BAR_TEXT + 1);
        }
        eu_hit(x0 - 3, y - 2, SCREEN_W, y + h + 2, H_PROMPT + i);
        y += h + gap; first = 0;
    }
}
static void draw_prompts(void)
{
    int i, x = 3;
    if (touch_lay) { draw_rail(); return; }
    eu_kit(KIT_BAR, 0, BAR_Y, SCREEN_W, SCREEN_H, L_BAR);
    for (i = 0; i < npr; i++) {
        int w = eu_badge_w(pr_b[i], ed_pad_style) + 3 + eu_text_w(pr_l[i]);
        if (x + w > SCREEN_W - 2) break;
        eu_badge(pr_b[i], x, BAR_Y + 3, ed_pad_style, L_BAR_TEXT);
        eu_text(pr_l[i], x + eu_badge_w(pr_b[i], ed_pad_style) + 3, BAR_Y + 3, RGB(230, 200, 170), L_BAR_TEXT);
        eu_hit(x - 2, BAR_Y, x + w + 3, SCREEN_H, H_PROMPT + i);
        x += w + 9;
    }
}
static void draw_toast(void)
{
    if (toast_t <= 0 || !toast[0] || ov != OV_NONE) return;
    {
        int w = eu_text_w(toast) + 10, y = BAR_Y - 20, x = (MAP_X0 + MAP_X1 - w) / 2;
        if (x < 0) x = 0;
        eu_kit(KIT_PLAQUE, x - 3, y - 3, x + w + 3, y + 17, L_POP_BG - 2);
        eu_text_c(toast, x + w / 2, y + 3, toast_t > 20 || (toast_t >> 2) & 1 ? RGB(255, 220, 180) : RGB(120, 90, 70), L_POP_BG - 4);
    }
}

/* ------------------------------------------------------------------ top bar */
enum { TB_MENU, TB_VIEW, TB_UNDO, TB_ZOUT, TB_ZIN, TB_REDO };
static void draw_top(const char *what)
{
    static const char *const L[6] = { "MENU", "3D", "UNDO", "-", "+", "REDO" };
    static const int ORDER_T[6] = { TB_ZIN, TB_ZOUT, TB_REDO, TB_UNDO, TB_VIEW, TB_MENU }, ORDER_P[5] = { TB_ZIN, TB_ZOUT, TB_UNDO, TB_VIEW, TB_MENU };
    int i, x = SCREEN_W - 2, nb = touch_lay ? 6 : 5, used = 0;
    char b[48], *o;
    eu_kit(KIT_BAR, 0, 0, SCREEN_W, TOP_H, L_BAR - 1);
    o = eu_str(b, what);
    eu_text(b, 3, (TOP_H - 8) / 2, RGB(255, 170, 70), L_BAR_TEXT);
    o = eu_str(b, ED.name); if (ed_dirty) eu_str(o, "*");
    upcase(b);
    for (i = 0; i < nb; i++) {   /* the buttons, right to left */
        int k = touch_lay ? ORDER_T[i] : ORDER_P[i];
        int w = eu_text_w(L[k]) + (touch_lay ? 14 : 6);
        if (touch_lay && w < 26) w = 26;
        eu_button(x - w, 1, x, TOP_H - 1, k == TB_VIEW && mode == M_3D ? "2D" : L[k], 0, L_BAR_TEXT + 1);
        eu_hit(x - w, 0, x, TOP_H + 4, H_TOP + k);
        x -= w + 2; used += w + 2;
    }
    {   /* as much of the name as fits left of the buttons */
        int room = (SCREEN_W - used - 8 - (3 + eu_text_w(what) + 10)) / 7;
        if (room < 0) room = 0;
        if ((int)strlen(b) > room) b[room] = 0;
    }
    eu_text(b, 3 + eu_text_w(what) + 10, (TOP_H - 8) / 2, RGB(190, 150, 130), L_BAR_TEXT);
}

/* ------------------------------------------------------------------ overlays: shared list panel */
#define PX1 (SCREEN_W - 2 - (touch_lay ? RAIL_R : 0))
#define PX0 (PX1 - (touch_lay ? 236 : SCREEN_W > 400 ? 200 : 162))
#define PY0 (TOP_H + 2)
#define PY1 (BAR_Y - 2)
#define ROW_H row_h
#define H_ADJ 2100
static int (*panel_adjustable)(int);     /* the inspector: rows that get - / + buttons on touch */
static int panel_rows(void) { return (PY1 - PY0 - 18 - (touch_lay ? 10 : 0)) / ROW_H; }
static void list_panel(const char *title, int n, const char *(*label)(int, char *), const char *(*value)(int, char *), int x0)
{
    int i, rows = panel_rows();
    char a[48], b[48];
    if (ov_sel >= n) ov_sel = n - 1;
    if (ov_sel < 0) ov_sel = 0;
    if (ov_sel < ov_scroll) ov_scroll = ov_sel;
    if (ov_sel >= ov_scroll + rows) ov_scroll = ov_sel - rows + 1;
    eu_panel(x0, PY0, PX1, PY1, L_POP_BG);
    eu_text(title, x0 + 5, PY0 + 5, RGB(255, 160, 60), L_POP_TEXT);
    for (i = 0; i < rows && ov_scroll + i < n; i++) {
        int k = ov_scroll + i, y = PY0 + 17 + i * ROW_H, lit = k == ov_sel;
        const char *l = label(k, a), *v = value ? value(k, b) : 0;
        int adj = touch_lay && panel_adjustable && panel_adjustable(k), vx1 = adj ? PX1 - 54 : PX1 - 5, ty = y + (ROW_H - 8) / 2 - 1;
        if (v) {   /* the value gives way where it would run into the label */
            int room = (vx1 - x0 - 12 - eu_text_w(l)) / 7;
            if (room < 1) room = 1;
            if ((int)strlen(v) > room) { b[room] = 0; if (room > 1) b[room - 1] = '.'; v = b; }
        }
        if (lit) eu_kit(KIT_ROW, x0 + 3, y - 1, PX1 - 3, y + ROW_H - 1, L_POP);
        eu_text(l, x0 + 6, ty, lit ? RGB(255, 236, 200) : RGB(200, 160, 140), L_POP_TEXT);
        if (v) eu_text(v, vx1 - eu_text_w(v), ty, lit ? RGB(255, 200, 120) : RGB(170, 120, 90), L_POP_TEXT);
        eu_hit(x0 + 2, y - 1, PX1 - 2, y + ROW_H - 1, H_ROW + k);
        if (adj) {   /* - / + (held: repeats, faster the longer) */
            eu_button(PX1 - 50, y, PX1 - 27, y + ROW_H - 2, "-", 0, L_POP_TEXT - 1);
            eu_button(PX1 - 25, y, PX1 - 2, y + ROW_H - 2, "+", 0, L_POP_TEXT - 1);
            eu_hit(PX1 - 52, y - 1, PX1 - 26, y + ROW_H - 1, H_ADJ + k * 2);
            eu_hit(PX1 - 26, y - 1, PX1, y + ROW_H - 1, H_ADJ + k * 2 + 1);
        }
    }
    if (ov_scroll > 0) eu_text_c("+", (x0 + PX1) / 2, PY0 + 9, RGB(255, 140, 60), L_POP_TEXT);
    if (ov_scroll + rows < n) eu_text_c(touch_lay ? "+ DRAG FOR MORE +" : "+", (x0 + PX1) / 2, PY1 - 9, RGB(255, 140, 60), L_POP_TEXT);
}
static int list_nav(u32 press, int n)
{
    if (press & PAD_UP) { ov_sel = (ov_sel + n - 1) % n; snd_play(SFX_UI_MOVE, 1800, 0, 4096); }
    if (press & PAD_DOWN) { ov_sel = (ov_sel + 1) % n; snd_play(SFX_UI_MOVE, 1800, 0, 4096); }
    if (press & PAD_L1) ov_sel = clampv(ov_sel - panel_rows(), 0, n - 1);
    if (press & PAD_R1) ov_sel = clampv(ov_sel + panel_rows(), 0, n - 1);
    return ov_sel;
}

/* ------------------------------------------------------------------ tool menu (/\) */
static const char *tools_label(int i, char *b)
{
    if (i < T_COUNT) { char *o = eu_str(b, i == tool ? "> " : "  "); eu_str(o, TOOL_NAME[i]); return b; }
    i -= T_COUNT;
    {
        static const char *const PN[P_PROP0] = { "START", "TROOPER", "KEY CYAN", "KEY AMBER", "KEY CRIMSON", "SHARD", "SECRET SHARD", "EXIT", "LIGHT", "GLOW" };
        char *o = eu_str(b, i == place ? " * " : "   ");
        if (i < P_PROP0) eu_str(o, PN[i]);
        else { o = eu_str(o, "PROP "); eu_str(o, em_prop_names[i - P_PROP0]); upcase(b); }
    }
    return b;
}
static void tools_update(u32 press)
{
    int n = T_COUNT + P_COUNT;
    list_nav(press, n);
    if (press & (PAD_CROSS | PAD_TRIANGLE)) {
        if (ov_sel < T_COUNT) { set_tool(ov_sel); ov = OV_NONE; }
        else { place = ov_sel - T_COUNT; set_tool(T_PLACE); ov = OV_NONE; }
        snd_play(SFX_UI_OK, 2600, 0, 4096);
    }
    if (press & PAD_CIRCLE) ov = OV_NONE;
}

/* ------------------------------------------------------------------ start menu */
enum { MN_SAVE, MN_PLAY, MN_3D, MN_UNDO, MN_REDO, MN_RENAME, MN_GRID, MN_WAY, MN_CHECK, MN_HELP, MN_EXIT, MN_COUNT };
static const char *menu_label(int i, char *b)
{
    static const char *const L[MN_COUNT] = { "SAVE", "PLAY TEST", "3D VIEW", "UNDO", "REDO", "RENAME LEVEL", "GRID", "ROUTES", "CHECK THE MAP", "CONTROLS", "EXIT EDITOR" };
    eu_str(b, L[i]); return b;
}
static const char *menu_value(int i, char *b)
{
    b[0] = 0;
    if (i == MN_GRID) { char *o = eu_m(b, grid()); eu_str(o, " M"); }
    if (i == MN_WAY) eu_str(b, show_way ? "ON" : "OFF");
    if (i == MN_SAVE && ed_dirty) eu_str(b, "UNSAVED");
    return b;
}
static void open_keys(const char *title, const char *init, int maxlen, void (*done)(const char *));
static void rename_done(const char *s) { if (s[0]) { ed_undo_push(); scopy(ED.name, s, 24); ed_dirty = 1; } }
static int try_playtest(void)
{
    int s, w;
    if (em_check(&ED, &s, &w)) { err_toast(); if (s >= 0) { s32 x = 0, z = 0; int i; for (i = 0; i < ED.secs[s].np; i++) { x += ED.secs[s].x[i]; z += ED.secs[s].z[i]; } cursor_to(x / ED.secs[s].np, z / ED.secs[s].np); follow_cursor(); } return 0; }
    play_def = em_compile(&ED, 1);
    if (!play_def) { err_toast(); return 0; }
    if (ed_dirty) save_now();
    need3d = 1;
    want = ED_PLAYTEST;
    return 1;
}
static void go_3d(void);
static void menu_update(u32 press)
{
    list_nav(press, MN_COUNT);
    if (press & PAD_CIRCLE) { ov = OV_NONE; return; }
    if (press & PAD_START) { ov = OV_NONE; return; }
    if ((press & (PAD_LEFT | PAD_RIGHT)) && ov_sel == MN_GRID) { gi = clampv(gi + (press & PAD_RIGHT ? 1 : -1), 0, 3); cursor_to(ed_cx, ed_cz); return; }
    if (!(press & PAD_CROSS)) return;
    snd_play(SFX_UI_OK, 2600, 0, 4096);
    switch (ov_sel) {
    case MN_SAVE: if (save_now()) ed_toast("SAVED"); ov = OV_NONE; break;
    case MN_PLAY: ov = OV_NONE; try_playtest(); break;
    case MN_3D: ov = OV_NONE; go_3d(); break;
    case MN_UNDO: do_undo(0); break;
    case MN_REDO: do_undo(1); break;
    case MN_RENAME: open_keys("LEVEL NAME", ED.name, 20, rename_done); break;
    case MN_GRID: gi = (gi + 1) % 4; cursor_to(ed_cx, ed_cz); break;
    case MN_WAY: show_way ^= 1; break;
    case MN_CHECK: { int s, w; if (em_check(&ED, &s, &w)) err_toast(); else ed_toast("THE MAP CHECKS OUT"); ov = OV_NONE; break; }
    case MN_HELP: ov = OV_HELP; break;
    case MN_EXIT: if (ed_dirty) save_now(); ov = OV_NONE; mode = M_LIST; list_refresh(); ui_title_bg_upload(); ov_sel = 0; lst_sel = 0; lst_act = 0; break;
    }
}

/* ------------------------------------------------------------------ on-screen keyboard */
static const char KB[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'!?/+<>";
#define KB_COLS 10
#define KB_N ((int)sizeof(KB) - 1)
static char kb_text[32], kb_title[24]; static int kb_max, kb_sel; static void (*kb_done)(const char *); static int kb_prev_ov;
static void open_keys(const char *title, const char *init, int maxlen, void (*done)(const char *))
{
    scopy(kb_title, title, 24); scopy(kb_text, init, 32); upcase(kb_text); kb_max = maxlen; kb_done = done; kb_sel = 0; kb_prev_ov = ov; ov = OV_KEYS;
}
static void kb_type(int ch)
{
    int n = (int)strlen(kb_text);
    if (ch == 8) { if (n) kb_text[n - 1] = 0; return; }
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (n < kb_max && ch >= 32 && ch < 127) { int i; for (i = 0; i < KB_N; i++) if (KB[i] == ch) { kb_text[n] = (char)ch; kb_text[n + 1] = 0; } }
}
static void kb_finish(int ok) { ov = kb_prev_ov == OV_KEYS ? OV_NONE : kb_prev_ov; if (ok && kb_done) kb_done(kb_text); }
static void keys_update(u32 press)
{
    int total = KB_N + 2;                           /* + DEL, OK */
    if (press & PAD_LEFT) kb_sel = (kb_sel + total - 1) % total;
    if (press & PAD_RIGHT) kb_sel = (kb_sel + 1) % total;
    if (press & PAD_UP) kb_sel = kb_sel >= KB_COLS ? kb_sel - KB_COLS : kb_sel;
    if (press & PAD_DOWN) kb_sel = kb_sel + KB_COLS < total ? kb_sel + KB_COLS : total - 1;
    if (press & PAD_SQUARE) kb_type(8);
    if (press & PAD_CROSS) { if (kb_sel < KB_N) kb_type(KB[kb_sel]); else if (kb_sel == KB_N) kb_type(8); else { kb_finish(1); return; } }
    if (press & PAD_START) { kb_finish(1); return; }
    if (press & PAD_CIRCLE) { kb_finish(0); return; }
}
static void keys_draw(void)
{
    int kw = touch_lay && SCREEN_W >= 400 ? 32 : 22, kh = touch_lay ? 22 : 15, w = KB_COLS * kw + 20, x0 = (SCREEN_W - w) / 2, y0 = touch_lay ? TOP_H + 4 : 60, i;
    eu_panel(x0, y0, x0 + w, y0 + 40 + ((KB_N + 2 + KB_COLS - 1) / KB_COLS) * kh + 4, L_POP_BG - 4);
    eu_text(kb_title, x0 + 6, y0 + 5, RGB(255, 160, 60), L_POP_TEXT - 2);
    eu_kit(KIT_FIELD, x0 + 6, y0 + 16, x0 + w - 6, y0 + 31, L_POP - 2);
    {
        char b[40]; char *o = eu_str(b, kb_text);
        if ((ed_t >> 4) & 1) eu_str(o, "-");
        eu_text(b, x0 + 9, y0 + 19, RGB(255, 236, 200), L_POP_TEXT - 3);
    }
    for (i = 0; i < KB_N + 2; i++) {
        int c = i % KB_COLS, r = i / KB_COLS, bx = x0 + 10 + c * kw, by = y0 + 36 + r * kh, bw = kw - 2;
        char t[4] = { 0, 0, 0, 0 };
        const char *l = t;
        if (i < KB_N) { t[0] = KB[i] == ' ' ? '_' : KB[i]; }
        else if (i == KB_N) { l = "DEL"; bw = kw * 2 - 2; }
        else { l = "OK"; bx += kw; bw = kw * 2 - 2; }
        eu_button(bx, by, bx + bw, by + kh - 2, l, i == kb_sel, L_POP - 4);
        eu_hit(bx, by, bx + bw, by + kh - 2, H_KEY + i);
    }
}

/* ------------------------------------------------------------------ texture picker */
static void (*tile_done)(int);
static int tile_prev_ov, tile_sel;
static int tile_ok(int t) { return t != LT_PROPS_FREE && !(LT_ANIMATED(t) && ((t - LT_WALL_GRAPH) & 1)); }
static int tiles_n, tiles_list[96];
void ed_tile_tex(int tile, u16 *tpage, u16 *clut, int *u, int *v)
{
    int k = tile / 16, t = tile % 16;
    *tpage = get_tpage(TP_4BIT, 0, LAB_PAGE_X(k), LAB_PAGE_Y(k));
    *clut = CLUT_ID(CL_LAB(k, t));
    *u = (t % 4) * 64; *v = (t / 4) * 64;
}
void ed_open_tiles(int current, void (*done)(int tile))
{
    int i;
    tiles_n = 0;
    for (i = 0; i < em_tile_count; i++) if (tile_ok(i)) tiles_list[tiles_n++] = i;
    tile_sel = 0;
    for (i = 0; i < tiles_n; i++) if (tiles_list[i] == current) tile_sel = i;
    tile_done = done; tile_prev_ov = ov; ov = OV_TILES;
}
static int tl_cols = 11;
#define TL_COLS tl_cols
static void tiles_update(u32 press)
{
    if (press & PAD_LEFT) tile_sel = (tile_sel + tiles_n - 1) % tiles_n;
    if (press & PAD_RIGHT) tile_sel = (tile_sel + 1) % tiles_n;
    if (press & PAD_UP) tile_sel = tile_sel >= TL_COLS ? tile_sel - TL_COLS : tile_sel;
    if (press & PAD_DOWN) tile_sel = tile_sel + TL_COLS < tiles_n ? tile_sel + TL_COLS : tile_sel;
    if (press & PAD_CROSS) { ov = tile_prev_ov == OV_TILES ? OV_NONE : tile_prev_ov; if (tile_done) tile_done(tiles_list[tile_sel]); snd_play(SFX_UI_OK, 2600, 0, 4096); return; }
    if (press & (PAD_CIRCLE | PAD_TRIANGLE)) { ov = tile_prev_ov == OV_TILES ? OV_NONE : tile_prev_ov; }
}
static void tiles_draw(void)
{
    int i, cell = touch_lay ? 30 : 26, x0, y0 = TOP_H + 4, avail = SCREEN_W - 12 - (touch_lay ? RAIL_R : 0);
    tl_cols = (avail - 10) / cell;
    if (tl_cols > 16) tl_cols = 16;
    while (((tiles_n + tl_cols - 1) / tl_cols) * cell + 22 > SCREEN_H - y0 - 4 && cell > 18) { cell -= 2; tl_cols = (avail - 10) / cell; if (tl_cols > 20) tl_cols = 20; }
    x0 = 6 + (avail - (TL_COLS * cell + 8)) / 2;
    char b[32];
    eu_panel(x0, y0, x0 + TL_COLS * cell + 8, y0 + ((tiles_n + TL_COLS - 1) / TL_COLS) * cell + 20, L_POP_BG - 6);
    eu_text("TEXTURES", x0 + 5, y0 + 4, RGB(255, 160, 60), L_POP_TEXT - 6);
    scopy(b, em_tile_names[tiles_list[tile_sel]], 32);
    eu_text(b, x0 + 70, y0 + 4, RGB(255, 230, 190), L_POP_TEXT - 6);
    for (i = 0; i < tiles_n; i++) {
        int c = i % TL_COLS, r = i / TL_COLS, x = x0 + 5 + c * cell, y = y0 + 16 + r * cell;
        u16 tp, cl; int u, v;
        ed_tile_tex(tiles_list[i], &tp, &cl, &u, &v);
        eu_tex(x + 1, y + 1, x + cell - 2, y + cell - 2, tp, cl, u, v, u + 63, v + 63, L_POP - 6);
        if (i == tile_sel) eu_kit(KIT_SEL, x - 2, y - 2, x + cell + 2, y + cell + 2, L_POP - 7);
        eu_hit(x, y, x + cell, y + cell, H_TILE + i);
    }
}

/* ------------------------------------------------------------------ rows editor: a wall's tile rows */
static EmRows rows_ed; static s16 *rows_target; static int rows_can_default, rows_r, rows_c, rows_prev_ov;
static char rows_title[32];
/* lines: one per row (bottom row last on screen, like a wall), then TOP, then NEW ROW, then DEFAULT */
static int rows_lines(void) { return rows_ed.nrows + 2 + rows_can_default; }
static int line_kind(int l, int *row)
{
    /* screen order top-down: [DEFAULT], TOP, rows nrows-1 .. 0, NEW ROW */
    int k = 0;
    if (rows_can_default) { if (l == 0) return 3; k = 1; }
    if (l == k) return 1;                                  /* TOP */
    if (l - k - 1 < rows_ed.nrows) { *row = rows_ed.nrows - 1 - (l - k - 1); return 0; }
    return 2;                                             /* NEW ROW */
}
static void rows_store(void)
{
    int i;
    ed_undo_push();
    i = em_rows_new(&ED, &rows_ed);
    if (i < 0) { undo_drop(); ed_toast("TOO MANY ROW SPECS"); return; }
    *rows_target = (s16)i;
    ed_changed();
}
static void rows_pick_done(int t)
{
    int kind, row = 0;
    kind = line_kind(rows_r, &row);
    if (kind == 1) rows_ed.top = (u8)t;
    else if (kind == 2) { if (rows_ed.nrows < EM_NROWS) { rows_ed.n[rows_ed.nrows] = 1; rows_ed.t[rows_ed.nrows][0] = (u8)t; rows_ed.nrows++; } }
    else if (kind == 0) {
        if (rows_c >= rows_ed.n[row]) { if (rows_ed.n[row] < EM_ROWLEN) rows_ed.t[row][rows_ed.n[row]++] = (u8)t; }
        else rows_ed.t[row][rows_c] = (u8)t;
    }
    rows_store();
}
static void open_rows(const char *title, s16 *target, int fallback, int can_default)
{
    rows_target = target; rows_can_default = can_default;
    if (*target >= 0) rows_ed = ED.rows[*target];
    else if (fallback >= 0) rows_ed = ED.rows[fallback];
    else { memset(&rows_ed, 0, sizeof(rows_ed)); rows_ed.top = 255; }
    scopy(rows_title, title, 32);
    rows_r = can_default ? 1 : 0; rows_c = 0; rows_prev_ov = ov; ov = OV_ROWS;
}
static void rows_update(u32 press)
{
    int n = rows_lines(), kind, row = 0, len;
    if (press & PAD_UP) { rows_r = (rows_r + n - 1) % n; rows_c = 0; }
    if (press & PAD_DOWN) { rows_r = (rows_r + 1) % n; rows_c = 0; }
    kind = line_kind(rows_r, &row);
    len = kind == 0 ? rows_ed.n[row] + (rows_ed.n[row] < EM_ROWLEN) : 1;
    if (press & PAD_LEFT) rows_c = (rows_c + len - 1) % len;
    if (press & PAD_RIGHT) rows_c = (rows_c + 1) % len;
    if (press & PAD_CROSS) {
        if (kind == 3) { ed_undo_push(); *rows_target = -1; ed_changed(); ov = rows_prev_ov; ed_toast("BACK TO THE SECTOR'S ROWS"); return; }
        ed_open_tiles(kind == 1 ? (rows_ed.top == 255 ? LT_TOP_BEAM : rows_ed.top) : kind == 0 && rows_c < rows_ed.n[row] ? rows_ed.t[row][rows_c] : LT_MID_PANEL, rows_pick_done);
        return;
    }
    if (press & PAD_SQUARE) {     /* remove the tile / the top / the row */
        if (kind == 1 && rows_ed.top != 255) { rows_ed.top = 255; rows_store(); }
        else if (kind == 0 && rows_c < rows_ed.n[row]) {
            int k;
            for (k = rows_c; k < rows_ed.n[row] - 1; k++) rows_ed.t[row][k] = rows_ed.t[row][k + 1];
            rows_ed.n[row]--;
            if (!rows_ed.n[row]) {   /* an empty row goes */
                for (k = row; k < rows_ed.nrows - 1; k++) { rows_ed.n[k] = rows_ed.n[k + 1]; memcpy(rows_ed.t[k], rows_ed.t[k + 1], EM_ROWLEN); }
                rows_ed.nrows--;
            }
            rows_store();
        }
    }
    if (press & (PAD_CIRCLE | PAD_TRIANGLE)) ov = rows_prev_ov;
}
static void rows_draw(void)
{
    int n = rows_lines(), l, cell = touch_lay ? 24 : 20, x0 = 30, y0 = TOP_H + 6, x1 = SCREEN_W - 30 - (touch_lay ? RAIL_R - 20 : 0);
    if (y0 + 22 + n * (cell + 4) > SCREEN_H - 4) cell = (SCREEN_H - 4 - y0 - 22) / n - 4;
    eu_panel(x0, y0, x1, y0 + 22 + n * (cell + 4), L_POP_BG - 6);
    eu_text(rows_title, x0 + 5, y0 + 5, RGB(255, 160, 60), L_POP_TEXT - 6);
    for (l = 0; l < n; l++) {
        int row = 0, kind = line_kind(l, &row), y = y0 + 18 + l * (cell + 4), k, x = x0 + 52;
        const char *lab = kind == 1 ? "TOP" : kind == 2 ? "NEW ROW" : kind == 3 ? "DEFAULT" : 0;
        char b[12];
        if (!lab) { char *o = eu_str(b, "ROW "); eu_int(o, row + 1); lab = b; }
        eu_text(lab, x0 + 6, y + 6, l == rows_r ? RGB(255, 230, 190) : RGB(180, 140, 120), L_POP_TEXT - 6);
        eu_hit(x0 + 2, y, x1 - 2, y + cell, H_ROW + l * 16 + 15);
        if (kind == 0 || kind == 1) {
            int cnt = kind == 0 ? rows_ed.n[row] : (rows_ed.top != 255);
            for (k = 0; k <= cnt && k < EM_ROWLEN; k++) {
                int t = kind == 0 ? (k < cnt ? rows_ed.t[row][k] : -1) : (k < cnt ? rows_ed.top : -1);
                if (kind == 1 && k == 1) break;
                if (t >= 0) { u16 tp, cl; int u, v; ed_tile_tex(t, &tp, &cl, &u, &v); eu_tex(x, y, x + cell, y + cell, tp, cl, u, v, u + 63, v + 63, L_POP - 6); }
                else if (kind == 0 || cnt == 0) { eu_rect(x, y, x + cell, y + cell, RGB(20, 8, 8), L_POP - 6); eu_text_c("+", x + cell / 2 + 1, y + 6, RGB(200, 120, 60), L_POP - 7); }
                if (l == rows_r && k == rows_c) eu_kit(KIT_SEL, x - 3, y - 3, x + cell + 3, y + cell + 3, L_POP - 8);
                eu_hit(x, y, x + cell, y + cell, H_ROW + l * 16 + k);
                x += cell + 3;
            }
        }
        if (l == rows_r && (kind == 2 || kind == 3)) eu_frame(x0 + 3, y - 2, x0 + 60, y + cell, RGB(255, 200, 80), L_POP - 8);
    }
}

/* ------------------------------------------------------------------ inspector ([]) */
enum {
    R_NAME, R_ROOM, R_ROOMNAME, R_KIND, R_FLOOR, R_CEIL, R_OPEN, R_LOCK, R_LOW, R_HIGH, R_FSLOPE, R_FSLOPEH, R_CSLOPE, R_CSLOPEH,
    R_FTEX, R_CTEX, R_FRULE, R_FRULEN, R_CRULE, R_CRULEN, R_WALLS, R_LOWS, R_UPS, R_CLEARF,
    R_W_MID, R_W_LOW, R_W_UP, R_W_GLASS, R_W_BARS, R_W_ALIGN, R_W_SHADOW, R_W_CLEAR, R_W_SECTOR,
    R_C_X, R_C_Z,
    R_T_TYPE, R_T_X, R_T_Z, R_T_YAW, R_T_ARG, R_T_PATROL,
    R_L_X, R_L_Z, R_L_H, R_L_R, R_L_G, R_L_B, R_L_RAD, R_L_X1, R_L_Z1, R_L_STEP,
    R_S_X, R_S_Z, R_S_H, R_S_SIZE, R_S_R, R_S_G, R_S_B, R_S_ANIM,
    R_P_FIELD
};
static int ins_rows[40], ins_n, ins_key = -1;
/* prop fields: which EmProp member each kind shows */
enum { F_X0, F_Z0, F_X1, F_Z1, F_H0, F_H1, F_H2, F_V0, F_V1, F_V2, F_DIR, F_T0, F_T1, F_T2, F_SCREEN, F_AXIS, F_REP, F_NOSHADOW, F_NOCOLL, F_ADD, F_HASH };
typedef struct { u8 kind, field; const char *label; } PField;
static const PField PF[] = {
    { PK_BOX, F_X0, "X1" }, { PK_BOX, F_Z0, "Z1" }, { PK_BOX, F_X1, "X2" }, { PK_BOX, F_Z1, "Z2" }, { PK_BOX, F_H0, "BOTTOM" }, { PK_BOX, F_H1, "TOP" },
    { PK_BOX, F_T0, "TOP TILE" }, { PK_BOX, F_T1, "SIDE TILE" }, { PK_BOX, F_NOSHADOW, "SHADOW" }, { PK_BOX, F_NOCOLL, "SOLID" },
    { PK_BOXF, F_X0, "X1" }, { PK_BOXF, F_Z0, "Z1" }, { PK_BOXF, F_X1, "X2" }, { PK_BOXF, F_Z1, "Z2" }, { PK_BOXF, F_H0, "BOTTOM" }, { PK_BOXF, F_H1, "TOP" },
    { PK_BOXF, F_T0, "TOP TILE" }, { PK_BOXF, F_T1, "SIDE TILE" }, { PK_BOXF, F_T2, "FRONT TILE" }, { PK_BOXF, F_DIR, "FRONT FACES" }, { PK_BOXF, F_V1, "GLOW" },
    { PK_BOXF, F_NOSHADOW, "SHADOW" }, { PK_BOXF, F_NOCOLL, "SOLID" },
    { PK_CRATE, F_X0, "X" }, { PK_CRATE, F_Z0, "Z" }, { PK_CRATE, F_H0, "SIZE" }, { PK_CRATE, F_H1, "HEIGHT" },
    { PK_CAN, F_X0, "X" }, { PK_CAN, F_Z0, "Z" },
    { PK_PILLAR, F_X0, "X" }, { PK_PILLAR, F_Z0, "Z" }, { PK_PILLAR, F_H0, "HEIGHT" },
    { PK_CONSOLE, F_X0, "X" }, { PK_CONSOLE, F_Z0, "Z" }, { PK_CONSOLE, F_DIR, "FACES" }, { PK_CONSOLE, F_SCREEN, "SCREEN" },
    { PK_MACHINE, F_X0, "X1" }, { PK_MACHINE, F_Z0, "Z1" }, { PK_MACHINE, F_X1, "X2" }, { PK_MACHINE, F_Z1, "Z2" }, { PK_MACHINE, F_H0, "HEIGHT" }, { PK_MACHINE, F_DIR, "FACES" },
    { PK_POD, F_X0, "X" }, { PK_POD, F_Z0, "Z" }, { PK_POD, F_DIR, "FACES" }, { PK_POD, F_V0, "PULSE" },
    { PK_RACK, F_X0, "X" }, { PK_RACK, F_Z0, "Z" }, { PK_RACK, F_DIR, "FACES" },
    { PK_FAN, F_X0, "X" }, { PK_FAN, F_Z0, "Z" }, { PK_FAN, F_H0, "HEIGHT" }, { PK_FAN, F_H1, "RADIUS" }, { PK_FAN, F_V0, "SPEED" },
    { PK_RAIL, F_X0, "X1" }, { PK_RAIL, F_Z0, "Z1" }, { PK_RAIL, F_X1, "X2" }, { PK_RAIL, F_Z1, "Z2" }, { PK_RAIL, F_H0, "FLOOR" },
    { PK_CYL, F_X0, "X" }, { PK_CYL, F_Z0, "Z" }, { PK_CYL, F_H0, "HEIGHT" }, { PK_CYL, F_H1, "RADIUS" }, { PK_CYL, F_H2, "LENGTH" },
    { PK_CYL, F_V0, "SIDES" }, { PK_CYL, F_T0, "TILE" }, { PK_CYL, F_AXIS, "AXIS" }, { PK_CYL, F_T1, "CAP TILE" }, { PK_CYL, F_REP, "REPEAT" },
    { PK_CYL, F_ADD, "GLOWING" }, { PK_CYL, F_V1, "ANIM" },
    { PK_TANK, F_X0, "X" }, { PK_TANK, F_Z0, "Z" },
    { PK_CORE, F_X0, "X" }, { PK_CORE, F_Z0, "Z" },
    { PK_CELL, F_X0, "X" }, { PK_CELL, F_Z0, "Z" }, { PK_CELL, F_DIR, "FACES" },
};
#define NPF ((int)(sizeof(PF) / sizeof(PF[0])))

static void build_rows(void)
{
    int n = 0;
#define R(x) ins_rows[n++] = (x)
    if (sel_kind == K_SECTOR) {
        const EmSector *s = &ED.secs[sel_i];
        R(R_NAME); R(R_ROOM); R(R_ROOMNAME); R(R_KIND); R(R_FLOOR);
        if (s->kind != SK_DOOR) R(R_CEIL);
        if (s->kind == SK_DOOR) { R(R_OPEN); R(R_LOCK); }
        if (s->kind == SK_LIFT) { R(R_LOW); R(R_HIGH); }
        R(R_FSLOPE); if (s->fslope >= 0) R(R_FSLOPEH);
        R(R_CSLOPE); if (s->cslope >= 0) R(R_CSLOPEH);
        R(R_FTEX); R(R_FRULE); if (s->f.nrules) R(R_FRULEN);
        R(R_CTEX); R(R_CRULE); if (s->c.nrules) R(R_CRULEN);
        R(R_WALLS); R(R_LOWS); R(R_UPS); R(R_CLEARF);
    } else if (sel_kind == K_WALL) {
        R(R_W_MID); R(R_W_LOW); R(R_W_UP); R(R_W_GLASS); R(R_W_BARS); R(R_W_ALIGN); R(R_W_SHADOW); R(R_W_CLEAR); R(R_W_SECTOR);
    } else if (sel_kind == K_CORNER) { R(R_C_X); R(R_C_Z); }
    else if (sel_kind == K_THING) {
        const EmThing *t = &ED.things[sel_i];
        R(R_T_TYPE); R(R_T_X); R(R_T_Z);
        if (t->type == TH_START || t->type == TH_TROOPER) R(R_T_YAW);
        if (t->type == TH_KEY || t->type == TH_SHARD) R(R_T_ARG);
        if (t->type == TH_TROOPER) R(R_T_PATROL);
    } else if (sel_kind == K_LIGHT) {
        R(R_L_X); R(R_L_Z); if (ED.lights[sel_i].grid) { R(R_L_X1); R(R_L_Z1); R(R_L_STEP); }
        R(R_L_H); R(R_L_R); R(R_L_G); R(R_L_B); R(R_L_RAD);
    } else if (sel_kind == K_SPRITE) { R(R_S_X); R(R_S_Z); R(R_S_H); R(R_S_SIZE); R(R_S_R); R(R_S_G); R(R_S_B); R(R_S_ANIM); }
    else if (sel_kind == K_PROP) {
        int i;
        for (i = 0; i < NPF && n < 38; i++) if (PF[i].kind == ED.props[sel_i].kind) R(R_P_FIELD + i);
    }
#undef R
    ins_n = n;
}
static const char *const DIRN[4] = { "NORTH", "EAST", "SOUTH", "WEST" };
static const char *ins_label(int k, char *b)
{
    static const char *const L[R_P_FIELD] = {
        "NAME", "ROOM", "ROOM NAME", "KIND", "FLOOR", "CEILING", "OPENS TO", "LOCK", "LIFT LOW", "LIFT HIGH", "FLOOR SLOPE", "SLOPE TO", "CEIL SLOPE", "SLOPE TO",
        "FLOOR TILE", "CEIL TILE", "FLOOR PATTERN", "EVERY", "CEIL PATTERN", "EVERY", "WALL ROWS", "STEP ROWS", "UPPER ROWS", "CLEAR PAINT",
        "WALL ROWS", "STEP ROWS", "UPPER ROWS", "GLASS", "BARS", "TILES LINE UP", "SHADOW", "CLEAR PAINT", "ITS SECTOR",
        "X", "Z",
        "TYPE", "X", "Z", "FACING", "KIND", "PATROL TO",
        "X", "Z", "HEIGHT", "RED", "GREEN", "BLUE", "RADIUS", "X2", "Z2", "EVERY",
        "X", "Z", "HEIGHT", "SIZE", "RED", "GREEN", "BLUE", "PULSE" };
    int r = ins_rows[k];
    eu_str(b, r >= R_P_FIELD ? PF[r - R_P_FIELD].label : L[r]);
    return b;
}
static int wcells_on(int si, int w) { int i, n = 0; for (i = 0; i < ED.nwcells; i++) if (ED.wcells[i].sec == si && (w < 0 || ED.wcells[i].wall == w)) n++; return n; }
static int fcells_on(int si) { int i, n = 0; for (i = 0; i < ED.nfcells; i++) if (ED.fcells[i].sec == si) n++; return n; }
static char *tile_name(char *o, int t) { if (t == 255) return eu_str(o, "NONE"); return eu_str(o, em_tile_names[t]); }
static s16 *pfield(EmProp *p, int f)
{
    switch (f) { case F_X0: return &p->x0; case F_Z0: return &p->z0; case F_X1: return &p->x1; case F_Z1: return &p->z1; case F_H0: return &p->h0;
    case F_H1: return &p->h1; case F_H2: return &p->h2; case F_V0: return &p->v0; case F_V1: return &p->v1; default: return &p->v2; }
}
static const char *ins_value(int k, char *b)
{
    int r = ins_rows[k];
    char *o = b;
    b[0] = 0;
    if (sel_kind == K_SECTOR) {
        const EmSector *s = &ED.secs[sel_i];
        switch (r) {
        case R_NAME: eu_str(b, s->name); upcase(b); break;
        case R_ROOM: o = eu_int(b, s->room + 1); break;
        case R_ROOMNAME: eu_str(b, s->room < ED.nrooms ? ED.rooms[s->room].name : "?"); upcase(b); break;
        case R_KIND: eu_str(b, s->kind == SK_DOOR ? "DOOR" : s->kind == SK_LIFT ? "LIFT" : "ROOM"); break;
        case R_FLOOR: eu_m(b, s->floor); break;
        case R_CEIL: eu_m(b, s->ceil); break;
        case R_OPEN: eu_m(b, s->ka); break;
        case R_LOCK: eu_str(b, s->lock == 1 ? "CYAN" : s->lock == 2 ? "AMBER" : s->lock == 3 ? "CRIMSON" : "NONE"); break;
        case R_LOW: eu_m(b, s->ka); break;
        case R_HIGH: eu_m(b, s->kb); break;
        case R_FSLOPE: if (s->fslope < 0) eu_str(b, "FLAT"); else { o = eu_str(b, "WALL "); eu_int(o, s->fslope); } break;
        case R_CSLOPE: if (s->cslope < 0) eu_str(b, "FLAT"); else { o = eu_str(b, "WALL "); eu_int(o, s->cslope); } break;
        case R_FSLOPEH: eu_m(b, s->fslope_h); break;
        case R_CSLOPEH: eu_m(b, s->cslope_h); break;
        case R_FTEX: tile_name(b, s->f.def); break;
        case R_CTEX: tile_name(b, s->c.def); break;
        case R_FRULE: if (s->f.nrules) tile_name(b, s->f.r[0].tile); else eu_str(b, "NONE"); break;
        case R_CRULE: if (s->c.nrules) tile_name(b, s->c.r[0].tile); else eu_str(b, "NONE"); break;
        case R_FRULEN: o = eu_int(b, s->f.r[0].sx); o = eu_str(o, " TILES"); break;
        case R_CRULEN: o = eu_int(b, s->c.r[0].sx); o = eu_str(o, " TILES"); break;
        case R_WALLS: case R_LOWS: case R_UPS: { int ri = r == R_WALLS ? s->walls : r == R_LOWS ? s->lows : s->ups; if (ri < 0) eu_str(b, "NONE"); else { o = eu_int(b, ED.rows[ri].nrows); eu_str(o, ED.rows[ri].nrows == 1 ? " ROW" : " ROWS"); } break; }
        case R_CLEARF: o = eu_int(b, fcells_on(sel_i) + wcells_on(sel_i, -1)); eu_str(o, " TILES"); break;
        }
    } else if (sel_kind == K_WALL) {
        const EmSector *s = &ED.secs[sel_i];
        const EmWall *w = &s->w[sel_j];
        switch (r) {
        case R_W_MID: eu_str(b, w->mid >= 0 ? "OWN" : "SECTOR'S"); break;
        case R_W_LOW: eu_str(b, w->low >= 0 ? "OWN" : "SECTOR'S"); break;
        case R_W_UP: eu_str(b, w->up >= 0 ? "OWN" : "SECTOR'S"); break;
        case R_W_GLASS: eu_str(b, w->flags & WF_GLASS ? "YES" : "NO"); break;
        case R_W_BARS: eu_str(b, w->flags & WF_BARS ? "YES" : "NO"); break;
        case R_W_ALIGN: eu_str(b, w->flags & WF_ALIGN_END ? "FROM END" : w->flags & WF_ALIGN_MID ? "CENTRED" : "ON GRID"); break;
        case R_W_SHADOW: eu_str(b, w->flags & WF_NOSHADOW ? "NO" : "YES"); break;
        case R_W_CLEAR: o = eu_int(b, wcells_on(sel_i, sel_j)); eu_str(o, " TILES"); break;
        case R_W_SECTOR: eu_str(b, s->name); upcase(b); break;
        }
    } else if (sel_kind == K_CORNER) { eu_m(b, r == R_C_X ? ED.secs[sel_i].x[sel_j] : ED.secs[sel_i].z[sel_j]); }
    else if (sel_kind == K_THING) {
        const EmThing *t = &ED.things[sel_i];
        switch (r) {
        case R_T_TYPE: eu_str(b, em_thing_names[t->type % 6]); upcase(b); break;
        case R_T_X: eu_m(b, t->x); break;
        case R_T_Z: eu_m(b, t->z); break;
        case R_T_YAW: eu_str(b, DIRN[((t->yaw + 512) & 4095) / 1024]); if (t->yaw & 1023) { o = b + strlen(b); o = eu_str(o, " "); eu_int(o, (long)t->yaw * 360 / 4096); } break;
        case R_T_ARG: eu_str(b, t->type == TH_KEY ? (t->arg == 1 ? "CYAN" : t->arg == 2 ? "AMBER" : "CRIMSON") : t->arg ? "SECRET" : "NORMAL"); break;
        case R_T_PATROL: if (t->ax == t->x && t->az == t->z) eu_str(b, "STANDS"); else { o = eu_m(b, t->ax); o = eu_str(o, " "); eu_m(o, t->az); } break;
        }
    } else if (sel_kind == K_LIGHT) {
        const EmLight *L = &ED.lights[sel_i];
        switch (r) {
        case R_L_X: eu_m(b, L->x); break; case R_L_Z: eu_m(b, L->z); break; case R_L_H: eu_m(b, L->h); break;
        case R_L_R: eu_int(b, L->r); break; case R_L_G: eu_int(b, L->g); break; case R_L_B: eu_int(b, L->b); break;
        case R_L_RAD: eu_m(b, L->rad); break; case R_L_X1: eu_m(b, L->x1); break; case R_L_Z1: eu_m(b, L->z1); break; case R_L_STEP: eu_m(b, L->step); break;
        }
    } else if (sel_kind == K_SPRITE) {
        const EmSprite *S = &ED.sprites[sel_i];
        switch (r) {
        case R_S_X: eu_m(b, S->x); break; case R_S_Z: eu_m(b, S->z); break; case R_S_H: eu_m(b, S->h); break; case R_S_SIZE: eu_m(b, S->size); break;
        case R_S_R: eu_int(b, S->r); break; case R_S_G: eu_int(b, S->g); break; case R_S_B: eu_int(b, S->b); break; case R_S_ANIM: eu_int(b, S->anim); break;
        }
    } else if (sel_kind == K_PROP) {
        EmProp *p = &ED.props[sel_i];
        int f = PF[r - R_P_FIELD].field;
        switch (f) {
        case F_DIR: eu_str(b, DIRN[p->dir & 3]); break;
        case F_T0: tile_name(b, p->t0); break; case F_T1: tile_name(b, p->t1); break; case F_T2: tile_name(b, p->t2); break;
        case F_SCREEN: eu_str(b, p->t0 ? "VITALS" : "GRAPH"); break;
        case F_AXIS: eu_str(b, p->axis == 0 ? "X" : p->axis == 2 ? "Z" : "UP"); break;
        case F_REP: eu_int(b, p->rep); break;
        case F_NOSHADOW: eu_str(b, p->flags & 1 ? "NO" : "YES"); break;
        case F_NOCOLL: eu_str(b, p->flags & 2 ? "NO" : "YES"); break;
        case F_ADD: eu_str(b, p->flags & 2 ? "YES" : "NO"); break;
        case F_V0: case F_V1: if (p->kind == PK_CYL || p->kind == PK_POD || p->kind == PK_FAN || p->kind == PK_BOXF) { eu_int(b, *pfield(p, f)); break; } /* fall through */
        default: eu_m(b, *pfield(p, f)); break;
        }
    }
    return b;
}
static void ins_undo(int key) { if (key != ins_key) { ed_undo_push(); ins_key = key; } }
static s16 *ins_tile_target; static u8 *ins_tile_target8;
static void ins_tile_done(int t) { ed_undo_push(); if (ins_tile_target8) *ins_tile_target8 = (u8)t; ed_changed(); ins_key = -1; }
static void roomname_done(const char *s) { if (s[0] && sel_kind == K_SECTOR) { ed_undo_push(); scopy(ED.rooms[ED.secs[sel_i].room].name, s, 24); ed_changed(); } }
static void sectorname_done(const char *s)
{
    char low[16]; int i;
    if (!s[0] || sel_kind != K_SECTOR) return;
    scopy(low, s, 16);
    for (i = 0; low[i]; i++) { if (low[i] >= 'A' && low[i] <= 'Z') low[i] = (char)(low[i] + 32); if (low[i] == ' ') low[i] = '_'; }
    for (i = 0; i < ED.nsecs; i++) if (i != sel_i && !strcmp(ED.secs[i].name, low)) { ed_toast("ANOTHER SECTOR HAS THAT NAME"); return; }
    ed_undo_push(); scopy(ED.secs[sel_i].name, low, 16); ed_dirty = 1;
}
static int patrol_pick;                      /* the next X sets trooper sel_i's patrol point */

/* left / right on a row (d = -1 / +1; big = held long: bigger steps) */
static void ins_adjust(int k, int d, int big)
{
    int r = ins_rows[k];
    s32 hstep = big ? 512 : 205, mstep = big ? 1024 : grid();
    if (sel_kind == K_SECTOR) {
        EmSector *s = &ED.secs[sel_i];
        ins_undo(r + sel_i * 64);
        switch (r) {
        case R_ROOM: {
            int nr = s->room + d;
            if (nr >= ED.nrooms) {
                if (ED.nrooms >= EM_ROOMS) { ed_toast("32 ROOMS AT MOST"); return; }
                { char id[8] = "r", nm[24] = "AREA "; eu_int(id + 1, ED.nrooms + 1); eu_int(nm + 5, ED.nrooms + 1);
                  scopy(ED.rooms[ED.nrooms].id, id, 16); scopy(ED.rooms[ED.nrooms].name, nm, 24); ED.nrooms++; ed_toast("NEW ROOM"); }
            }
            if (nr < 0) nr = ED.nrooms - 1;
            s->room = (u8)nr; break; }
        case R_KIND:
            s->kind = (u8)((s->kind + 3 + d) % 3);
            if (s->kind == SK_DOOR) { if (!s->ka) s->ka = (s16)(s->ceil > s->floor ? s->ceil - s->floor : 4096); s->ceil = s->floor; }
            else if (s->kind == SK_LIFT) { s->ka = s->floor; if (s->kb <= s->ka) s->kb = (s16)(s->floor + 2048); if (s->ceil <= s->floor) s->ceil = (s16)(s->kb + 4096); }
            else { if (s->ceil <= s->floor) s->ceil = (s16)(s->floor + (s->ka > 0 ? s->ka : 4096)); s->lock = 0; s->ka = s->kb = 0; }
            break;
        case R_FLOOR: s->floor = (s16)clampv(s->floor + d * hstep, -30000, 30000); if (s->kind == SK_DOOR) s->ceil = s->floor; break;
        case R_CEIL: s->ceil = (s16)clampv(s->ceil + d * hstep, s->floor, 32000); break;
        case R_OPEN: s->ka = (s16)clampv(s->ka + d * hstep, 512, 16384); break;
        case R_LOCK: s->lock = (u8)((s->lock + 4 + d) % 4); break;
        case R_LOW: s->ka = (s16)clampv(s->ka + d * hstep, -30000, s->kb - 205); break;
        case R_HIGH: s->kb = (s16)clampv(s->kb + d * hstep, s->ka + 205, 30000); break;
        case R_FSLOPE: s->fslope = (s8)(s->fslope + d < -1 ? s->np - 1 : s->fslope + d >= s->np ? -1 : s->fslope + d); if (s->fslope >= 0 && !s->fslope_h) s->fslope_h = (s16)(s->floor + 1024); break;
        case R_CSLOPE: s->cslope = (s8)(s->cslope + d < -1 ? s->np - 1 : s->cslope + d >= s->np ? -1 : s->cslope + d); if (s->cslope >= 0 && !s->cslope_h) s->cslope_h = (s16)(s->ceil + 1024); break;
        case R_FSLOPEH: s->fslope_h = (s16)(s->fslope_h + d * hstep); break;
        case R_CSLOPEH: s->cslope_h = (s16)(s->cslope_h + d * hstep); break;
        case R_FRULEN: case R_CRULEN: { EmRule *q = r == R_FRULEN ? &s->f.r[0] : &s->c.r[0]; int v = clampv(q->sx + d, 1, 16); q->sx = q->sz = (u8)v; q->ox %= v; q->oz %= v; break; }
        default: return;
        }
        ed_changed();
        return;
    }
    if (sel_kind == K_WALL) {
        EmWall *w = &ED.secs[sel_i].w[sel_j];
        ins_undo(r + sel_i * 64 + sel_j * 4096);
        switch (r) {
        case R_W_GLASS: w->flags ^= WF_GLASS; w->flags &= (u8)~WF_BARS; break;
        case R_W_BARS: w->flags ^= WF_BARS; w->flags &= (u8)~WF_GLASS; break;
        case R_W_ALIGN: { int a = w->flags & WF_ALIGN_END ? 1 : w->flags & WF_ALIGN_MID ? 2 : 0; a = (a + 3 + d) % 3; w->flags &= (u8)~(WF_ALIGN_END | WF_ALIGN_MID); w->flags |= (u8)(a == 1 ? WF_ALIGN_END : a == 2 ? WF_ALIGN_MID : 0); break; }
        case R_W_SHADOW: w->flags ^= WF_NOSHADOW; break;
        default: return;
        }
        ed_changed();
        return;
    }
    if (sel_kind == K_CORNER) {
        s32 px[1], pz[1];
        px[0] = ED.secs[sel_i].x[sel_j]; pz[0] = ED.secs[sel_i].z[sel_j];
        ed_undo_push();
        if (em_move_points(&ED, px, pz, 1, r == R_C_X ? d * mstep : 0, r == R_C_Z ? d * mstep : 0) < 0) { undo_restore_last(); return; }
        em_weld(&ED);
        if (geom_ok()) { int si, pi; if (em_near_point(&ED, px[0] + (r == R_C_X ? d * mstep : 0), pz[0] + (r == R_C_Z ? d * mstep : 0), 2, &si, &pi)) { sel_i = si; sel_j = pi; } }
        ins_key = -1;
        return;
    }
    if (sel_kind == K_THING) {
        EmThing *t = &ED.things[sel_i];
        ins_undo(r + sel_i * 64 + 100000);
        switch (r) {
        case R_T_TYPE: {
            int ty = t->type == TH_EXIT ? 0 : t->type + d;   /* start trooper key shard exit (not way) */
            if (ty < 0) ty = TH_EXIT;
            if (ty > TH_EXIT) ty = TH_START;
            t->type = (u8)ty; t->arg = (u8)(ty == TH_KEY ? 1 : 0); break; }
        case R_T_X: { s32 ox = t->x; t->x = (s16)clampv(t->x + d * mstep, -EM_LIMIT, EM_LIMIT); t->ax = (s16)(t->ax + t->x - ox); break; }
        case R_T_Z: { s32 oz = t->z; t->z = (s16)clampv(t->z + d * mstep, -EM_LIMIT, EM_LIMIT); t->az = (s16)(t->az + t->z - oz); break; }
        case R_T_YAW: t->yaw = (s16)((t->yaw + d * (big ? 1024 : 512)) & 4095); break;
        case R_T_ARG: if (t->type == TH_KEY) t->arg = (u8)((t->arg + 2 + d) % 3 + 1); else t->arg ^= 1; break;
        default: return;
        }
        ed_changed();
        return;
    }
    if (sel_kind == K_LIGHT) {
        EmLight *L = &ED.lights[sel_i];
        ins_undo(r + sel_i * 64 + 200000);
        switch (r) {
        case R_L_X: L->x = (s16)clampv(L->x + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_L_Z: L->z = (s16)clampv(L->z + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_L_X1: L->x1 = (s16)clampv(L->x1 + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_L_Z1: L->z1 = (s16)clampv(L->z1 + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_L_STEP: L->step = (s16)clampv(L->step + d * 512, 1024, 16384); break;
        case R_L_H: L->h = (s16)(L->h + d * hstep); break;
        case R_L_R: L->r = (u8)clampv(L->r + d * (big ? 20 : 5), 0, 255); break;
        case R_L_G: L->g = (u8)clampv(L->g + d * (big ? 20 : 5), 0, 255); break;
        case R_L_B: L->b = (u8)clampv(L->b + d * (big ? 20 : 5), 0, 255); break;
        case R_L_RAD: L->rad = (s16)clampv(L->rad + d * (big ? 2048 : 512), 512, 30000); break;
        default: return;
        }
        ed_changed();
        return;
    }
    if (sel_kind == K_SPRITE) {
        EmSprite *S = &ED.sprites[sel_i];
        ins_undo(r + sel_i * 64 + 300000);
        switch (r) {
        case R_S_X: S->x = (s16)clampv(S->x + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_S_Z: S->z = (s16)clampv(S->z + d * mstep, -EM_LIMIT, EM_LIMIT); break;
        case R_S_H: S->h = (s16)(S->h + d * hstep); break;
        case R_S_SIZE: S->size = (s16)clampv(S->size + d * 64, 64, 4096); break;
        case R_S_R: S->r = (u8)clampv(S->r + d * (big ? 32 : 8), 0, 255); break;
        case R_S_G: S->g = (u8)clampv(S->g + d * (big ? 32 : 8), 0, 255); break;
        case R_S_B: S->b = (u8)clampv(S->b + d * (big ? 32 : 8), 0, 255); break;
        case R_S_ANIM: S->anim = (u8)((S->anim + 8 + d) % 8); break;
        default: return;
        }
        ed_changed();
        return;
    }
    if (sel_kind == K_PROP) {
        EmProp *p = &ED.props[sel_i];
        int f = PF[r - R_P_FIELD].field;
        ins_undo(r + sel_i * 64 + 400000);
        switch (f) {
        case F_DIR: p->dir = (u8)((p->dir + 4 + d) & 3); break;
        case F_SCREEN: p->t0 ^= 1; break;
        case F_AXIS: p->axis = (u8)((p->axis + 3 + d) % 3); break;
        case F_REP: p->rep = (u8)clampv(p->rep + d, 1, 8); break;
        case F_NOSHADOW: p->flags ^= 1; break;
        case F_NOCOLL: case F_ADD: p->flags ^= 2; break;
        case F_T0: case F_T1: case F_T2: return;
        case F_V0: if (p->kind == PK_CYL) { p->v0 = (s16)clampv(p->v0 + d * 2, 3, 16); break; } if (p->kind == PK_POD) { p->v0 = (s16)clampv(p->v0 + d, 0, 7); break; } p->v0 = (s16)clampv(p->v0 + d * 5, 0, 200); break;
        case F_V1: p->v1 = (s16)clampv(p->v1 + d * (p->kind == PK_BOXF ? 10 : 1), 0, p->kind == PK_BOXF ? 250 : 7); break;
        case F_X0: case F_Z0: case F_X1: case F_Z1: { s16 *v = pfield(p, f); *v = (s16)clampv(*v + d * mstep, -EM_LIMIT, EM_LIMIT); break; }
        default: { s16 *v = pfield(p, f); *v = (s16)clampv(*v + d * (f == F_H1 && p->kind != PK_BOX && p->kind != PK_BOXF ? 128 : hstep), -30000, 30000); break; }
        }
        ed_changed();
    }
}
/* X on a row */
static void ins_activate(int k)
{
    int r = ins_rows[k];
    if (sel_kind == K_SECTOR) {
        EmSector *s = &ED.secs[sel_i];
        switch (r) {
        case R_NAME: open_keys("SECTOR NAME", s->name, 15, sectorname_done); return;
        case R_ROOMNAME: open_keys("ROOM NAME", s->room < ED.nrooms ? ED.rooms[s->room].name : "", 23, roomname_done); return;
        case R_FTEX: ins_tile_target8 = &s->f.def; ed_open_tiles(s->f.def, ins_tile_done); return;
        case R_CTEX: ins_tile_target8 = &s->c.def; ed_open_tiles(s->c.def, ins_tile_done); return;
        case R_FRULE: case R_CRULE: {
            EmFlat *f = r == R_FRULE ? &s->f : &s->c;
            if (!f->nrules) { ed_undo_push(); f->nrules = 1; f->r[0].tile = (u8)(r == R_FRULE ? LT_FLOOR_GRATE : LT_CEILING_LIGHT); f->r[0].sx = f->r[0].sz = 3; f->r[0].ox = f->r[0].oz = 0; ed_changed(); }
            ins_tile_target8 = &f->r[0].tile; ed_open_tiles(f->r[0].tile, ins_tile_done); return; }
        case R_WALLS: open_rows("SECTOR WALL ROWS", &s->walls, -1, 0); return;
        case R_LOWS: open_rows("SECTOR STEP ROWS", &s->lows, s->walls, 0); return;
        case R_UPS: open_rows("SECTOR UPPER ROWS", &s->ups, s->walls, 0); return;
        case R_CLEARF: {
            int i, n = 0;
            ed_undo_push();
            for (i = 0; i < ED.nfcells; i++) if (ED.fcells[i].sec != sel_i) ED.fcells[n++] = ED.fcells[i];
            ED.nfcells = n; n = 0;
            for (i = 0; i < ED.nwcells; i++) if (ED.wcells[i].sec != sel_i) ED.wcells[n++] = ED.wcells[i];
            ED.nwcells = n;
            if (s->f.nrules && r == R_CLEARF) { }
            ed_changed(); ed_toast("PAINTED TILES CLEARED"); return; }
        default: ins_adjust(k, 1, 0); return;
        }
    }
    if (sel_kind == K_WALL) {
        EmSector *s = &ED.secs[sel_i];
        EmWall *w = &s->w[sel_j];
        switch (r) {
        case R_W_MID: open_rows("THIS WALL'S ROWS", &w->mid, s->walls, 1); return;
        case R_W_LOW: open_rows("THIS WALL'S STEP ROWS", &w->low, s->lows >= 0 ? s->lows : s->walls, 1); return;
        case R_W_UP: open_rows("THIS WALL'S UPPER ROWS", &w->up, s->ups >= 0 ? s->ups : s->walls, 1); return;
        case R_W_CLEAR: {
            int i, n = 0;
            ed_undo_push();
            for (i = 0; i < ED.nwcells; i++) if (!(ED.wcells[i].sec == sel_i && ED.wcells[i].wall == sel_j)) ED.wcells[n++] = ED.wcells[i];
            ED.nwcells = n; ed_changed(); ed_toast("PAINTED TILES CLEARED"); return; }
        case R_W_SECTOR: sel_kind = K_SECTOR; ov_sel = 0; ov_scroll = 0; ins_key = -1; build_rows(); return;
        default: ins_adjust(k, 1, 0); return;
        }
    }
    if (sel_kind == K_THING && r == R_T_PATROL) {
        EmThing *t = &ED.things[sel_i];
        if (t->ax != t->x || t->az != t->z) { ed_undo_push(); t->ax = t->x; t->az = t->z; ed_changed(); ed_toast("HE STANDS GUARD"); return; }
        patrol_pick = 1; ov = OV_NONE; ed_toast("X WHERE HE PATROLS TO");
        return;
    }
    if (sel_kind == K_PROP) {
        EmProp *p = &ED.props[sel_i];
        int f = PF[r - R_P_FIELD].field;
        if (f == F_T0 || f == F_T1 || f == F_T2) {
            u8 *t = f == F_T0 ? &p->t0 : f == F_T1 ? &p->t1 : &p->t2;
            ins_tile_target8 = t; ed_open_tiles(*t == 255 ? LT_DARK_METAL : *t, ins_tile_done); return;
        }
    }
    ins_adjust(k, 1, 0);
}
static int ins_adjustable(int k)
{
    int r = ins_rows[k];
    switch (r) {
    case R_NAME: case R_ROOMNAME: case R_FTEX: case R_CTEX: case R_FRULE: case R_CRULE: case R_WALLS: case R_LOWS: case R_UPS: case R_CLEARF:
    case R_W_MID: case R_W_LOW: case R_W_UP: case R_W_CLEAR: case R_W_SECTOR: case R_T_PATROL: return 0;
    }
    if (r >= R_P_FIELD) { int f = PF[r - R_P_FIELD].field; if (f == F_T0 || f == F_T1 || f == F_T2) return 0; }
    return 1;
}
static int open_inspect(void)
{
    Pick p = pick_at(ed_cx, ed_cz, 1);
    if (p.kind == K_NONE) { ed_toast("NOTHING HERE TO INSPECT"); return 0; }
    sel_kind = p.kind; sel_i = p.i; sel_j = p.j;
    ov = OV_INSPECT; ov_sel = 0; ov_scroll = 0; ins_key = -1;
    (void)ins_tile_target;
    build_rows();
    return 1;
}
static int hold_lr, hold_t;
static void inspect_update(u32 pad, u32 press)
{
    if (sel_kind == K_NONE || (sel_kind == K_SECTOR && sel_i >= ED.nsecs) || (sel_kind == K_THING && sel_i >= ED.nthings) ||
        (sel_kind == K_PROP && sel_i >= ED.nprops) || (sel_kind == K_LIGHT && sel_i >= ED.nlights) || (sel_kind == K_SPRITE && sel_i >= ED.nsprites) ||
        ((sel_kind == K_WALL || sel_kind == K_CORNER) && (sel_i >= ED.nsecs || sel_j >= ED.secs[sel_i].np))) { ov = OV_NONE; return; }
    build_rows();
    if (!ins_n) { ov = OV_NONE; return; }
    if (press & (PAD_UP | PAD_DOWN)) ins_key = -1;
    list_nav(press, ins_n);
    if (pad & (PAD_LEFT | PAD_RIGHT)) {
        int d = pad & PAD_RIGHT ? 1 : -1;
        if (press & (PAD_LEFT | PAD_RIGHT)) { hold_t = 0; ins_adjust(ov_sel, d, 0); }
        else if (++hold_t > 18 && !(hold_t & 3)) ins_adjust(ov_sel, d, hold_t > 70);
    }
    (void)hold_lr;
    if (press & PAD_CROSS) ins_activate(ov_sel);
    if (press & (PAD_CIRCLE | PAD_SQUARE)) { ov = OV_NONE; ins_key = -1; }
}
static const char *ins_title(char *b)
{
    switch (sel_kind) {
    case K_SECTOR: { char *o = eu_str(b, "SECTOR "); eu_str(o, ED.secs[sel_i].name); upcase(b); return b; }
    case K_WALL: { char *o = eu_str(b, "WALL "); o = eu_int(o, sel_j); o = eu_str(o, " OF "); eu_str(o, ED.secs[sel_i].name); upcase(b); return b; }
    case K_CORNER: return eu_str(b, "CORNER"), b;
    case K_THING: eu_str(b, "THING"); return b;
    case K_LIGHT: eu_str(b, ED.lights[sel_i].grid ? "LIGHT GRID" : "LIGHT"); return b;
    case K_SPRITE: eu_str(b, "GLOW"); return b;
    case K_PROP: { char *o = eu_str(b, "PROP "); eu_str(o, em_prop_names[ED.props[sel_i].kind]); upcase(b); return b; }
    }
    b[0] = 0; return b;
}

/* ------------------------------------------------------------------ help */
static void help_draw(void)
{
    static const char *const L[] = {
        "D-PAD / LEFT STICK   MOVE THE CURSOR", "RIGHT STICK          PAN THE MAP", "L2  R2               ZOOM",
        "X / A                USE THE TOOL", "O / B                CANCEL  BACK", "TRIANGLE / Y         TOOLS AND THINGS",
        "SQUARE / X           INSPECT AND EDIT", "L1  R1               SWITCH TOOL", "SELECT / VIEW        3D VIEW",
        "START / MENU         SAVE  PLAY  UNDO", "", "KEYS: ARROWS SPACE ESC TAB I Q E", "PGUP PGDN  V 3D  M MENU  CTRL Z",
        "TOUCH: D-PAD  DRAG TO PAN  PINCH" };
    int i;
    eu_panel(14, TOP_H + 6, SCREEN_W - 14 - (touch_lay ? RAIL_R : 0), BAR_Y - 6, L_POP_BG);
    eu_text("CONTROLS", 20, TOP_H + 11, RGB(255, 160, 60), L_POP_TEXT);
    for (i = 0; i < (int)(sizeof(L) / sizeof(L[0])); i++) eu_text(L[i], 22, TOP_H + 26 + i * 13, RGB(220, 190, 160), L_POP_TEXT);
}

/* ------------------------------------------------------------------ the level list */
static void open_level(int which)
{
    /* which: -1 new, -2 copy of level 1, else a saved file */
    if (!big) big = (char *)malloc(BIG_CAP);
    if (which == -1) { em_new_level(&ED, "NEW LEVEL"); new_file_name(file_name); ed_dirty = 1; }
    else if (which == -2) {
        scopy(ED.name, "EXTRACTION PLANT COPY", 24);
        if (!em_parse(&ED, em_src_e1)) { err_toast(); em_new_level(&ED, "NEW LEVEL"); }
        scopy(ED.name, "EXTRACTION PLANT COPY", 24);
        new_file_name(file_name); ed_dirty = 1;
    } else {
        int n = big ? plat_file_load(lst[which], big, BIG_CAP - 1) : -1;
        if (n < 0) { ed_toast("COULDN'T READ IT"); return; }
        big[n] = 0;
        ED.name[0] = 0;
        if (!em_parse(&ED, big)) { err_toast(); return; }
        scopy(file_name, lst[which], 32); ed_dirty = 0;
    }
    clear_stack(undo_buf, &nundo); clear_stack(redo_buf, &nredo);
    {   /* the cursor on the start point */
        int i;
        ed_cx = ed_cz = 0;
        for (i = 0; i < ED.nthings; i++) if (ED.things[i].type == TH_START) { ed_cx = ED.things[i].x; ed_cz = ED.things[i].z; }
        cursor_to(ed_cx, ed_cz); vx = ed_cx; vz = ed_cz;
    }
    level_reload_textures();             /* the list's backdrop borrowed the texture pages */
    mode = M_MAP; ov = OV_NONE; tool = T_DRAW; stage = 0; npend = 0; need3d = 1; sel_kind = K_NONE;
    ed_toast(ed_touch ? "USE THE D-PAD TO MOVE THE CURSOR" : "START FOR THE MENU. TRIANGLE FOR TOOLS");
}
static int lst_rows(void) { return nlst + 2; }
static void list_update(u32 press)
{
    int n = lst_rows();
    if (lst_confirm) {
        if (press & PAD_CROSS) { plat_file_delete(lst[lst_sel - 2]); list_refresh(); lst_confirm = 0; lst_sel = 0; ed_toast("DELETED"); }
        if (press & (PAD_CIRCLE | PAD_START)) lst_confirm = 0;
        return;
    }
    if (press & PAD_UP) { lst_sel = (lst_sel + n - 1) % n; lst_act = 0; snd_play(SFX_UI_MOVE, 2600, 0, 4096); }
    if (press & PAD_DOWN) { lst_sel = (lst_sel + 1) % n; lst_act = 0; snd_play(SFX_UI_MOVE, 2600, 0, 4096); }
    if (lst_sel >= 2) {
        if (press & PAD_LEFT) lst_act = (lst_act + 2) % 3;
        if (press & PAD_RIGHT) lst_act = (lst_act + 1) % 3;
    }
    if (press & PAD_CIRCLE) { want = ED_EXIT; return; }
    if (press & (PAD_CROSS | PAD_START)) {
        snd_play(SFX_UI_OK, 3600, 0, 4096);
        if (lst_sel == 0) open_level(-1);
        else if (lst_sel == 1) open_level(-2);
        else if (lst_act == 0) open_level(lst_sel - 2);
        else if (lst_act == 1) {   /* play it straight away */
            int n2 = plat_file_load(lst[lst_sel - 2], big, BIG_CAP - 1);
            if (n2 < 0) { ed_toast("COULDN'T READ IT"); return; }
            big[n2] = 0; ED.name[0] = 0;
            if (!em_parse(&ED, big)) { err_toast(); return; }
            scopy(file_name, lst[lst_sel - 2], 32); ed_dirty = 0;
            clear_stack(undo_buf, &nundo); clear_stack(redo_buf, &nredo);
            level_reload_textures();
            mode = M_MAP; ov = OV_NONE;
            if (!try_playtest()) mode = M_LIST;
        } else lst_confirm = 1;
    }
}
static void list_draw(FrameBuf *fb, int t)
{
    int i, first = lst_sel > 5 ? lst_sel - 5 : 0;
    ui_title_bg(fb, 50, OT_LEN - 1);
    embers_draw(fb, t, 30);
    int wide = SCREEN_W >= 400, pw = wide ? 240 : 200, cx = wide ? SCREEN_W / 2 - 60 : 120;
    text_hell(fb, "LEVEL EDITOR", SCREEN_W / 2, 14, 2, t, 4);
    for (i = first; i < lst_rows() && i < first + 7; i++) {
        int y = 50 + (i - first) * 25, lit = i == lst_sel;
        const char *l = i == 0 ? "NEW LEVEL" : i == 1 ? "COPY OF LEVEL 1" : lst_title[i - 2];
        ui_plate(fb, cx, y, pw, lit, t, 12);
        text_center(fb, l, cx + 1, y + 7, 1, lit ? 255 : 130, 8);
        eu_hit(cx - pw / 2 - 20, y, cx + pw / 2 + 20, y + 24, H_LIST + i);
        if (lit && i >= 2) {   /* what to do with it */
            static const char *const A[3] = { "EDIT", "PLAY", "DELETE" };
            int k, bw = wide ? 44 : 28, bx = cx + pw / 2 + (wide ? 26 : 8);
            for (k = 0; k < 3; k++) {
                int w = k == 2 ? bw + (wide ? 8 : 4) : bw;
                eu_button(bx, y + 3, bx + w, y + 21, A[k], k == lst_act, 7);
                eu_hit(bx - 1, y, bx + w + 1, y + 24, H_LIST + 500 + k);
                bx += w + 3;
            }
        }
    }
    if (lst_confirm) {
        int x0 = SCREEN_W / 2 - 110;
        eu_panel(x0, 84, x0 + 220, 150, 3);
        eu_text_c("DELETE THIS LEVEL?", SCREEN_W / 2, 94, RGB(255, 200, 160), 2);
        eu_button(x0 + 20, 114, x0 + 100, 138, "DELETE", 1, 2);
        eu_button(x0 + 120, 114, x0 + 200, 138, "KEEP", 0, 2);
        eu_hit(x0 + 18, 112, x0 + 102, 140, H_LIST + 600);
        eu_hit(x0 + 118, 112, x0 + 202, 140, H_LIST + 601);
    }
    if (ed_touch && !lst_confirm) {   /* back to the title */
        eu_button(6, 6, 58, 26, "BACK", 0, 7);
        eu_hit(0, 0, 62, 30, H_LIST + 602);
    }
    {
        char b[64]; const char *w = plat_file_where();
        char *o = eu_str(b, "SAVED IN ");
        scopy(o, w ? w : "?", 48); upcase(b);
        eu_text(b, 4, 216, RGB(120, 80, 60), 4);
    }
}

/* ------------------------------------------------------------------ 3D switch */
static void go_3d(void)
{
    if (!ed_rebuild3d()) return;
    ov = OV_NONE; stage = 0; npend = 0;
    mode = M_3D;
    ed3d_enter(ed_cx, ed_cz);
}
int ed_rebuild3d(void)
{
    const MapDef *d;
    int s, w;
    if (!need3d) return 1;
    if (em_check_geom(&ED, &s, &w)) { err_toast(); return 0; }
    d = em_compile(&ED, 0);
    if (!d) { err_toast(); return 0; }
    level_load_map(d);
    need3d = 0;
    return 1;
}
int ed_overlay_busy(void) { return ov != OV_NONE; }

/* ------------------------------------------------------------------ input from the platform */
void editor_pointer(int id, int ev, int x, int y, int kind)
{
    if (ed_nptr < 16) { ed_ptr[ed_nptr].id = id; ed_ptr[ed_nptr].ev = ev; ed_ptr[ed_nptr].x = x; ed_ptr[ed_nptr].y = y; ed_ptr[ed_nptr].kind = kind; ed_nptr++; }
    if (kind != EK_TOUCH) { ed_mouse_x = x; ed_mouse_y = y; ed_mouse_on = 1; }
}
void editor_wheel(int steps) { ed_wheel += steps; }
static int chars[16], nchars;
void editor_char(int ch) { if (nchars < 16) chars[nchars++] = ch; }

/* a tap on a hit area -> pad presses / actions */
static void tap_hit(int id)
{
    if (id >= H_PROMPT && id < H_PROMPT + 8) { if (id - H_PROMPT < npr) tap_press |= pr_b[id - H_PROMPT]; return; }
    if (id >= H_TOP && id < H_TOP + 6) {
        switch (id - H_TOP) {
        case TB_MENU: tap_press |= PAD_START; break;
        case TB_VIEW: tap_press |= PAD_SELECT; break;
        case TB_UNDO: do_undo(0); break;
        case TB_REDO: do_undo(1); break;
        case TB_ZOUT: if (mode == M_MAP) zoom_by(-1, MAP_CX, MAP_CY); break;
        case TB_ZIN: if (mode == M_MAP) zoom_by(1, MAP_CX, MAP_CY); break;
        }
        return;
    }
    if (id >= H_ROW && id < H_ROW + 300) {
        if (ov == OV_ROWS) { int l = (id - H_ROW) / 16, c = (id - H_ROW) % 16; if (l == rows_r && (c == 15 || c == rows_c)) tap_press |= PAD_CROSS; rows_r = l; if (c != 15) rows_c = c; return; }
        if (ov_sel == id - H_ROW) tap_press |= PAD_CROSS; else ov_sel = id - H_ROW;
        return;
    }
    if (id >= H_TILE && id < H_TILE + 100) { if (tile_sel == id - H_TILE) tap_press |= PAD_CROSS; else tile_sel = id - H_TILE; return; }
    if (id >= H_KEY && id < H_KEY + 64) { kb_sel = id - H_KEY; tap_press |= PAD_CROSS; return; }
    if (id >= H_TOOLR && id < H_TOOLR + NRAIL) {
        int t = RAIL_TOOLS[id - H_TOOLR];
        if (t >= 0 && t != T_PLACE) set_tool(t);
        else { ov = OV_TOOLS; ov_sel = t == T_PLACE ? T_COUNT + place : tool; ov_scroll = t == T_PLACE ? T_COUNT : 0; }
        return;
    }
    if (id == H_LIST + 600) { tap_press |= PAD_CROSS; return; }
    if (id == H_LIST + 601 || id == H_LIST + 602) { tap_press |= PAD_CIRCLE; return; }
    if (id >= H_LIST + 500 && id < H_LIST + 503) { lst_act = id - H_LIST - 500; tap_press |= PAD_CROSS; return; }
    if (id >= H_LIST && id < H_LIST + 100) { if (lst_sel == id - H_LIST) tap_press |= PAD_CROSS; else { lst_sel = id - H_LIST; lst_act = 0; } return; }
}
static int in_map(int x, int y) { return y >= MAP_Y0 && y < MAP_Y1 && x >= MAP_X0 && x < MAP_X1; }
static void pointer_events(void)
{
    int i;
    for (i = 0; i < ed_nptr; i++) {
        EdPtr *e = &ed_ptr[i];
        int f = e->id & 3;
        if (e->kind == EK_TOUCH) {
            if (e->ev == EP_DOWN) { fing_on[f] = 1; fing_x[f] = e->x; fing_y[f] = e->y; }
            if (e->ev == EP_MOVE) { fing_x[f] = e->x; fing_y[f] = e->y; }
            if (e->ev == EP_UP) fing_on[f] = 0;
        }
        if (mode == M_3D && !ov) {   /* the 3D view reads the queue itself; only the bars' buttons here */
            if (e->ev == EP_DOWN && drag_id < 0 && eu_hit_at(e->x, e->y) >= 0) {
                drag_id = e->id; drag_kind = e->kind; drag_moved = 0; drag_hit = eu_hit_at(e->x, e->y);
                if (e->kind == EK_TOUCH && drag_hit >= H_PROMPT && drag_hit < H_PROMPT + 8 && drag_hit - H_PROMPT < npr) { touch_hold = pr_b[drag_hit - H_PROMPT]; tap_press |= touch_hold; }
            } else if (e->ev == EP_UP && e->id == drag_id) {
                if (touch_hold) touch_hold = 0; else tap_hit(drag_hit);
                drag_id = -1;
            }
            continue;
        }
        if (e->ev == EP_HOVER) {
            if (mode == M_MAP && !ov && in_map(e->x, e->y) && eu_hit_at(e->x, e->y) < 0 && drag_id < 0) cursor_to(wx_of(e->x), wz_of(e->y));
            continue;
        }
        if (e->ev == EP_DOWN) {
            if (drag_id >= 0 && e->kind == EK_TOUCH && fing_on[0] && fing_on[1]) {   /* second finger: pinch */
                int dx = fing_x[0] - fing_x[1], dy = fing_y[0] - fing_y[1];
                pinch_d = (int)isqrt((u32)(dx * dx + dy * dy));
                drag_moved = 1;
                continue;
            }
            drag_id = e->id; drag_kind = e->kind; drag_x = e->x; drag_y = e->y; drag_moved = 0;
            drag_hit = eu_hit_at(e->x, e->y);
            if (drag_hit >= H_DPAD && drag_hit < H_DPAD + 4) { touch_hold = drag_hit == H_DPAD ? PAD_UP : drag_hit == H_DPAD + 1 ? PAD_RIGHT : drag_hit == H_DPAD + 2 ? PAD_DOWN : PAD_LEFT; tap_press |= touch_hold; }
            if (e->kind == EK_TOUCH && drag_hit >= H_PROMPT && drag_hit < H_PROMPT + 8 && drag_hit - H_PROMPT < npr) { touch_hold = pr_b[drag_hit - H_PROMPT]; tap_press |= touch_hold; }
            if (drag_hit >= H_ADJ && drag_hit < H_ADJ + 200) { ov_sel = (drag_hit - H_ADJ) / 2; touch_hold = (drag_hit - H_ADJ) & 1 ? PAD_RIGHT : PAD_LEFT; tap_press |= touch_hold; }
            continue;
        }
        if (e->ev == EP_MOVE && e->id == drag_id) {
            int dx = e->x - drag_x, dy = e->y - drag_y;
            if (touch_hold) continue;
            if (fing_on[0] && fing_on[1] && e->kind == EK_TOUCH) {
                int ddx = fing_x[0] - fing_x[1], ddy = fing_y[0] - fing_y[1], d = (int)isqrt((u32)(ddx * ddx + ddy * ddy));
                if (pinch_d > 0 && d > pinch_d * 5 / 4) { zoom_by(1, (fing_x[0] + fing_x[1]) / 2, (fing_y[0] + fing_y[1]) / 2); pinch_d = d; }
                else if (pinch_d > 0 && d < pinch_d * 4 / 5) { zoom_by(-1, (fing_x[0] + fing_x[1]) / 2, (fing_y[0] + fing_y[1]) / 2); pinch_d = d; }
                continue;
            }
            if (!drag_moved && dx * dx + dy * dy > 25) drag_moved = 1;
            if (drag_moved && mode == M_MAP && !ov && (drag_hit < 0) && in_map(drag_x, drag_y)) {
                vx -= ((s32)dx << 10) / ppm(); vz += ((s32)dy << 10) / ppm();
                drag_x = e->x; drag_y = e->y;
            } else if (drag_moved && ov == OV_INSPECT) {      /* drag the panel's list */
                if (dy > 10) { ov_sel = ov_sel > 0 ? ov_sel - 1 : 0; drag_y = e->y; } else if (dy < -10) { ov_sel++; drag_y = e->y; }
            }
            continue;
        }
        if (e->ev == EP_UP && e->id == drag_id) {
            int hit = drag_hit;
            if (touch_hold) { touch_hold = 0; drag_id = -1; continue; }
            drag_id = -1;
            if (drag_moved) continue;
            if (hit >= 0) { tap_hit(hit); continue; }
            if (mode == M_MAP && !ov && in_map(e->x, e->y) && e->kind != EK_TOUCH) {   /* touch: only the d-pad moves it */
                cursor_to(wx_of(e->x), wz_of(e->y));
                if (e->kind == EK_MOUSE_L) tap_press |= PAD_CROSS;            /* a click uses the tool */
            } else if (ov && e->kind != EK_TOUCH) tap_press |= 0;
        }
    }
    if (mode == M_MAP && ed_wheel) { zoom_by(ed_wheel > 0 ? 1 : -1, ed_mouse_on ? ed_mouse_x : MAP_CX, ed_mouse_on ? ed_mouse_y : MAP_CY); }
}

/* ------------------------------------------------------------------ the map screen */
static int rep_t; static u32 rep_dir;
static void map_update(u32 pad, u32 press)
{
    u32 dirs = pad & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
    if (ov == OV_TOOLS) { tools_update(press); return; }
    if (ov == OV_MENU) { menu_update(press); return; }
    if (ov == OV_INSPECT) { inspect_update(pad, press); return; }
    if (ov == OV_ROWS) { rows_update(press); return; }
    if (ov == OV_TILES) { tiles_update(press); return; }
    if (ov == OV_KEYS) { keys_update(press); return; }
    if (ov == OV_HELP) { if (press) ov = OV_MENU; return; }
    /* cursor: d-pad steps (repeating), the left stick glides */
    if (dirs) {
        if (press & dirs) { rep_t = 0; rep_dir = dirs; }
        if ((press & dirs) || (++rep_t > 14 && !(rep_t & 1))) {
            s32 g = grid();
            if (dirs & PAD_UP) ed_cz += g;
            if (dirs & PAD_DOWN) ed_cz -= g;
            if (dirs & PAD_LEFT) ed_cx -= g;
            if (dirs & PAD_RIGHT) ed_cx += g;
            cursor_to(ed_cx, ed_cz); follow_cursor();
        }
    }
    if (ed_lx || ed_ly) {
        s32 sp = (16 << 10) / ppm() * 3;                             /* ~3 screen px a frame at full tilt... */
        stick_ax += ed_lx * sp >> 12; stick_az += ed_ly * sp >> 12;
        if (iabs32(stick_ax) >= grid() / 2 || iabs32(stick_az) >= grid() / 2) {
            s32 g = grid(), nx = stick_ax / (g / 2), nz = stick_az / (g / 2);
            nx = nx / 2 + (nx & 1 ? (nx > 0 ? 1 : 0) : 0); nz = nz / 2 + (nz & 1 ? (nz > 0 ? 1 : 0) : 0);
            if (nx) { ed_cx += nx * g; stick_ax -= nx * g; }
            if (nz) { ed_cz += nz * g; stick_az -= nz * g; }
            cursor_to(ed_cx, ed_cz); follow_cursor();
        }
    } else stick_ax = stick_az = 0;
    if (ed_rx || ed_ry) { vx += (ed_rx * 12 >> 12) * (1024 / ppm() + 1); vz += (ed_ry * 12 >> 12) * (1024 / ppm() + 1); }
    if (press & PAD_L2) zoom_by(-1, (int)sx_of(ed_cx), (int)sy_of(ed_cz));
    if (press & PAD_R2) zoom_by(1, (int)sx_of(ed_cx), (int)sy_of(ed_cz));
    if (press & PAD_L1) set_tool(tool - 1);
    if (press & PAD_R1) set_tool(tool + 1);
    if (patrol_pick && (press & (PAD_CROSS | PAD_CIRCLE))) {
        if ((press & PAD_CROSS) && sel_kind == K_THING && sel_i < ED.nthings) {
            ed_undo_push(); ED.things[sel_i].ax = (s16)ed_cx; ED.things[sel_i].az = (s16)ed_cz; ed_changed(); ed_toast("PATROL SET");
        }
        patrol_pick = 0; ov = OV_INSPECT;
        return;
    }
    if (press & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_TRIANGLE | PAD_SQUARE)) ed_height_done();
    if (press & PAD_CROSS) tool_apply();
    if (press & PAD_CIRCLE) tool_cancel();
    if (press & PAD_TRIANGLE) { ov = OV_TOOLS; ov_sel = tool == T_PLACE ? T_COUNT + place : tool; ov_scroll = 0; }
    if (press & PAD_SQUARE) open_inspect();
    if (press & PAD_START) { ov = OV_MENU; ov_sel = 0; ov_scroll = 0; }
    if (press & PAD_SELECT) go_3d();
    /* MOVE: the picked thing follows the cursor (shown at its new place) */
    (void)rep_dir;
}
static void map_prompts(void)
{
    static u32 b[8]; static const char *l[8];
    int n = 0;
#define P(bt, lb) do { b[n] = (bt); l[n] = (lb); n++; } while (0)
    switch (ov) {
    case OV_TOOLS: P(PAD_CROSS, "CHOOSE"); P(PAD_CIRCLE, "BACK"); P(PAD_UP | PAD_DOWN, "PICK"); break;
    case OV_MENU: P(PAD_CROSS, "OK"); P(PAD_CIRCLE, "BACK"); break;
    case OV_INSPECT: P(PAD_LEFT | PAD_RIGHT, "CHANGE"); P(PAD_CROSS, "EDIT"); P(PAD_CIRCLE, "DONE"); break;
    case OV_ROWS: P(PAD_CROSS, "TILE"); P(PAD_SQUARE, "REMOVE"); P(PAD_CIRCLE, "DONE"); break;
    case OV_TILES: P(PAD_CROSS, "USE"); P(PAD_CIRCLE, "BACK"); break;
    case OV_KEYS: P(PAD_CROSS, "TYPE"); P(PAD_SQUARE, "DELETE"); P(PAD_START, "OK"); P(PAD_CIRCLE, "CANCEL"); break;
    case OV_HELP: P(PAD_CIRCLE, "BACK"); break;
    default: {
        static const char *const APPLY[T_COUNT] = { "CORNER", "CORNER", "RAISE", "RAISE", "WALL", "POINT", "ADD", "PICK", "DELETE", "JOIN", "STAIRS", "PLACE", "TURN" };
        const char *a = APPLY[tool];
        if (tool == T_DRAW && npend >= 3 && pend_x[0] == ed_cx && pend_z[0] == ed_cz) a = "CLOSE";
        if (tool == T_MOVE && stage) a = "DROP";
        if (tool == T_EXTRUDE && stage) a = "EXTRUDE";
        if (patrol_pick) a = "PATROL HERE";
        P(PAD_CROSS, a);
        if (tool == T_FLOOR || tool == T_CEIL) P(PAD_CIRCLE, "LOWER");
        else if (stage || npend || patrol_pick) P(PAD_CIRCLE, npend && tool == T_DRAW ? "UNDO CORNER" : "CANCEL");
        P(PAD_TRIANGLE, "TOOLS"); P(PAD_SQUARE, "INSPECT"); P(PAD_START, "MENU");
        if (!(stage || npend)) P(PAD_SELECT, "3D");
        break; }
    }
#undef P
    ed_prompts(b, l, n);
}
/* touch: the tool rail down the left side - the everyday tools, MORE for the rest */
static void draw_tool_rail(void)
{
    static const char *const SHORT[T_COUNT] = { "DRAW", "RECT", "FLOOR", "CEIL", "EXTRUDE", "CUT", "CORNER", "MOVE", "DELETE", "JOIN", "STAIRS", "PLACE", "TURN" };
    int i, y = TOP_H + 4, h = (SCREEN_H - TOP_H - 8) / NRAIL - 3, in_rail = 0;
    eu_kit(KIT_BAR, 0, TOP_H - 2, RAIL_L, SCREEN_H, L_BAR);
    for (i = 0; i < NRAIL - 1; i++) if (RAIL_TOOLS[i] == tool) in_rail = 1;
    for (i = 0; i < NRAIL; i++) {
        int t = RAIL_TOOLS[i];
        const char *l = t >= 0 ? SHORT[t] : in_rail ? "MORE" : SHORT[tool];
        eu_button(4, y, RAIL_L - 5, y + h, l, t >= 0 ? t == tool : !in_rail, L_BAR_TEXT + 1);
        if (t < 0 && !in_rail) eu_text_c("...", (RAIL_L - 1) / 2, y + h - 8, RGB(255, 220, 160), L_BAR_TEXT - 1);
        eu_hit(0, y - 1, RAIL_L, y + h + 2, H_TOOLR + i);
        y += h + 3;
    }
}

static void draw_dpad(void)
{
    int cx = MAP_X0 + 32, cy = SCREEN_H - 34, k;
    static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
    /* the touch stick's forged ring (ui atlas) round a dark face */
    eu_disc(cx, cy, 24, RGB(40, 46, 46), SEMI_SUB, L_POP_BG + 3);
    ui_sprite(eu_fb(), cx - 31, cy - 31, 62, 62, 0, 0, 64, 64, 128, 128, 128, 0, 0, L_POP_BG + 2);
    for (k = 0; k < 4; k++) {
        int bx = cx + DX[k] * 17, by = cy + DY[k] * 17;
        u32 lit = (touch_hold == (k == 0 ? PAD_UP : k == 1 ? PAD_RIGHT : k == 2 ? PAD_DOWN : PAD_LEFT));
        if (lit) ui_sprite(eu_fb(), bx - 11, by - 11, 22, 22, 32, 144, 32, 32, 230, 110, 50, 1, 0, L_POP_BG + 1);
        eu_tri(bx + DX[k] * 6, by + DY[k] * 6, bx - DY[k] * 6 - DX[k] * 2, by + DX[k] * 6 - DY[k] * 2, bx + DY[k] * 6 - DX[k] * 2, by - DX[k] * 6 - DY[k] * 2, lit ? RGB(255, 170, 60) : RGB(160, 120, 100), -1, L_POP_BG + 1);
        eu_hit(bx - 12, by - 12, bx + 12, by + 12, H_DPAD + k);
    }
}
static void map_draw(void)
{
    char b[48];
    draw_map();
    if (ov != OV_TILES && ov != OV_ROWS && ov != OV_KEYS) draw_info();
    {
        char *o = eu_str(b, TOOL_NAME[tool]);
        if (tool == T_PLACE) { o = eu_str(o, " "); tools_label(T_COUNT + place, o); }
        upcase(b);
        draw_top(b);
    }
    switch (ov) {
    case OV_TOOLS: list_panel("TOOLS  /  PLACE", T_COUNT + P_COUNT, tools_label, 0, PX0); break;
    case OV_MENU: list_panel("EDITOR", MN_COUNT, menu_label, menu_value, PX0 + 30); break;
    case OV_INSPECT: { char t[48]; build_rows(); panel_adjustable = ins_adjustable; list_panel(ins_title(t), ins_n, ins_label, ins_value, PX0 - (touch_lay ? 0 : 20)); panel_adjustable = 0; break; }
    case OV_ROWS: rows_draw(); break;
    case OV_TILES: tiles_draw(); break;
    case OV_KEYS: keys_draw(); break;
    case OV_HELP: help_draw(); break;
    }
    if (touch_lay) draw_tool_rail();
    if (touch_lay && !ov) draw_dpad();
    map_prompts();
    draw_prompts();
    draw_toast();
}

/* ------------------------------------------------------------------ entry points */
void editor_open(int pad_style, int touch)
{
    ed_pad_style = pad_style; ed_touch = touch;
    mode = M_LIST; want = ED_STAY; lst_sel = 0; lst_act = 0; lst_confirm = 0;
    list_refresh();
}
void editor_resume(void)
{
    want = ED_STAY;
    level_reload_textures();
    mode = M_MAP; ov = OV_NONE; need3d = 1;
}
const MapDef *editor_playtest_map(void) { return play_def; }
void editor_style(int pad_style, int touch) { ed_pad_style = pad_style; ed_touch = touch; }
int editor_in_3d(void) { return mode == M_3D; }

int editor_update(u32 pad, u32 press, int lx, int ly, int rx, int ry)
{
    int k;
    ed_t++;
    want = ED_STAY;
    layout();
    ed_lx = lx; ed_ly = ly; ed_rx = rx; ed_ry = ry;
    tap_press = 0;
    pointer_events();
    press |= tap_press; pad |= tap_press | touch_hold;
    if (toast_t > 0) toast_t--;
    for (k = 0; k < nchars; k++) {   /* typed keys: the keyboard panel, or shortcuts */
        int c = chars[k];
        if (ov == OV_KEYS) { if (c == 13) { kb_finish(1); } else if (c == 27) kb_finish(0); else kb_type(c); press &= ~(PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_CROSS | PAD_SQUARE | PAD_CIRCLE | PAD_START); continue; }
        if (c == 26) do_undo(0);
        if (c == 25) do_undo(1);
        if (c == 19 && mode != M_LIST) { if (save_now()) ed_toast("SAVED"); }
    }
    nchars = 0;
    if (mode == M_LIST) list_update(press);
    else if (mode == M_MAP) map_update(pad, press);
    else {
        if (ov == OV_TILES) tiles_update(press);
        else if (ov == OV_MENU) { menu_update(press); if (ov == OV_NONE && mode == M_3D && want == ED_STAY) { } }
        else if (ov == OV_KEYS) keys_update(press);
        else if (ov == OV_HELP) { if (press) ov = OV_MENU; }
        else {
            ed3d_update(pad, press);
            if (press & PAD_START) { ov = OV_MENU; ov_sel = 0; ov_scroll = 0; }
            if (ed3d_closing()) { mode = M_MAP; level_debug_all_rooms(0); }
        }
    }
    ed_nptr = 0; ed_wheel = 0;
    return want;
}

void editor_draw(FrameBuf *fb, int t)
{
    eu_begin(fb);
    layout();
    if (mode == M_LIST) { list_draw(fb, t); draw_toast(); return; }
    if (mode == M_MAP) { map_draw(); return; }
    ed3d_draw(fb, t);
    draw_top("3D VIEW");
    if (ov == OV_MENU) list_panel("EDITOR", MN_COUNT, menu_label, menu_value, PX0 + 30);
    if (ov == OV_TILES) tiles_draw();
    if (ov == OV_KEYS) keys_draw();
    if (ov == OV_HELP) help_draw();
    if (ov == OV_MENU) { static const u32 b[2] = { PAD_CROSS, PAD_CIRCLE }; static const char *const l[2] = { "OK", "BACK" }; ed_prompts(b, l, 2); }
    if (ov == OV_TILES) { static const u32 b[2] = { PAD_CROSS, PAD_CIRCLE }; static const char *const l[2] = { "USE", "BACK" }; ed_prompts(b, l, 2); }
    draw_prompts();
    draw_toast();
}
#endif
