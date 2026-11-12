/*
 * Deadbird Studio - a game editor for Android (then Windows), built on the engine.
 *
 * Projects -> the editor (Scene, Geometry, Behaviour, Assets) -> Play-test. The whole UI is drawn
 * at the screen's real resolution with sui (engine/studio); play-testing runs the project in the
 * framework runtime (engine/fw) and shows the engine's PS1 frame inside the Studio.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "engine.h"
#include "files.h"
#include "sui.h"
#include "fwrt.h"
#include "fwproj.h"
#include "fsm.h"
#include "doc.h"
#include "hellgate_game.h"
#include "studio_atlas.h"
#include "geom.h"

extern const FwWorld sector_world;
void sector_world_init(void);
void sector_world_set_loader(int (*load)(const char *name));

static const char EMPTY_GAME[] =
    "game \"NEW GAME\"\ncamera first\nplayer walk 44 run 80 jump 72 gravity 5 eye 1450 hp 100 turn 70\nweapon hitscan 20000 damage 10 rate 12\n\n"
    "type enemy\n  color 200 60 40\n  shape pillar\n  size 600 radius 300 height 1600 hp 30 speed 30\n  flags solid gravity visible\n  tags enemy\n  thing trooper\n  fsm enemy_ai\nend\n\n"
    "type exit\n  color 60 255 120\n  shape orb\n  size 900 radius 600 height 1500\n  flags visible pickup\n  thing exit\n  fsm exit_logic\nend\n\n"
    "fsm enemy_ai\n  state idle at 40 40\n    every IfSeePlayer 8000 SEEN\n    on SEEN chase\n    on DIED dead\n"
    "  state chase at 260 40\n    every ChasePlayer 30\n    on DIED dead\n  state dead at 260 170\n    do AddScore 100\n    do Wait 120\n    do Destroy\nend\n\n"
    "fsm exit_logic\n  state waiting at 40 40\n    every IfPlayerNear 900 REACHED\n    on REACHED done\n  state done at 260 40\n    do WinGame\nend\n";

enum { SCR_PROJECTS, SCR_EDIT, SCR_PLAY };
enum { TAB_SCENE, TAB_GEOMETRY, TAB_BEHAVIOUR, TAB_ASSETS };

static Doc doc;
static int screen, tab = TAB_BEHAVIOUR, back_tab;
static char files[64][32]; static int nfiles;
static char textbuf[65536];
static char toast[96]; static int toast_t;
static int play_ok, paused;
static float play_x, play_y, play_w, play_h;    /* where the game view sits (dp) */
static float W, H;

/* behaviour editor */
static int cur_fsm, sel_state = -1, sel_trans = -1, sel_act = -1, sel_arg = -1;
static float pan_x = 20, pan_y = 20, zoom = 1;
static int drag_state = -1, linking = -1, panning;
static float link_x, link_y;
static int pick_open, pick_group, pick_every = 1;
static int ev_pick = -1, ev_pick_to = -1;       /* choosing the event for a new link */
/* scene editor */
static int sel_type;
static float insp_scroll, insp_content;     /* the behaviour inspector's scroll */

static void say(const char *s) { doc_copy(toast, s, sizeof(toast)); toast_t = 150; }

/* ------------------------------------------------------------------ files */
static void refresh_files(void) { nfiles = plat_file_list(files, 64); }
static void save_doc(void)
{
    int n;
    if (!doc.file[0]) return;
    n = doc_write(&doc, textbuf, (int)sizeof(textbuf));
    if (!plat_file_save(doc.file, textbuf, n)) say("COULDN'T SAVE THE PROJECT");
    doc.dirty = 0;
    geom_save();
}
/* the project's level: <project>.map beside it */
static void level_name(char *out, int cap)
{
    int l;
    snprintf(out, (size_t)cap, "%s", doc.file);
    l = (int)strlen(out);
    if (l > 5 && !strcmp(out + l - 5, ".game")) out[l - 5] = 0;
    l = (int)strlen(out);
    if (l > cap - 5) out[cap - 5] = 0;
    strcat(out, ".map");
}
static void make_level(int from_level1)
{
    char name[32];
    level_name(name, (int)sizeof(name));
    if (!geom_create(name, from_level1)) { say("COULDN'T CREATE THE LEVEL"); return; }
    doc_head_set(&doc, "level", name);
    save_doc();
}
static int open_doc(const char *file)
{
    int n = plat_file_load(file, textbuf, (int)sizeof(textbuf) - 1);
    if (n <= 0) return 0;
    doc_copy(doc.file, file, sizeof(doc.file));
    if (!doc_parse(&doc, textbuf, n)) return 0;
    cur_fsm = 0; sel_state = sel_trans = sel_act = sel_arg = -1; sel_type = 0;
    geom_open(doc_head_word(&doc, "level", 1));
    pan_x = 20; pan_y = 20; zoom = 1;
    screen = SCR_EDIT; tab = TAB_BEHAVIOUR;
    return 1;
}
static void new_project(const char *text, int len, const char *base, int level)
{
    char name[32];
    int i, k = 1;
    for (;;) {
        snprintf(name, sizeof(name), "%s%d.game", base, k);
        for (i = 0; i < nfiles; i++) if (!strcmp(files[i], name)) break;
        if (i == nfiles) break;
        k++;
    }
    if (!plat_file_save(name, text, len)) { say("COULDN'T CREATE THE PROJECT"); return; }
    refresh_files();
    if (open_doc(name) && level) make_level(level == 1);
}

/* ------------------------------------------------------------------ play */
static void play_start(void)
{
    int n = doc_write(&doc, textbuf, (int)sizeof(textbuf));
    save_doc();
    play_ok = fw_runtime_load(textbuf, n);
    paused = 0;
    screen = SCR_PLAY;
    eng_set_touch_ui(1);
}
static void play_stop(void) { screen = SCR_EDIT; }

/* ------------------------------------------------------------------ shared chrome */
static void header(const char *where)
{
    static const char *const T[4] = { "Scene", "Geometry", "Behaviour", "Assets" };
    float x;
    int i;
    sui_nine(SK_BAR, 0, 0, W, 45);
    if (sui_button("back", 6, 4, 40, 36, 0, BTN_TAB)) { save_doc(); refresh_files(); screen = SCR_PROJECTS; }
    sui_line(30, 14, 22, 22, 2, C_TEXT); sui_line(22, 22, 30, 30, 2, C_TEXT);
    x = 52 + sui_text(FONT_BOLD, 13, 52, 14, doc.title[0] ? doc.title : doc.file, C_TEXT);
    x += 6 + sui_text(FONT_REGULAR, 12, x + 6, 15, where, C_MUTED);
    x += 18;
    sui_nine(SK_FIELD, x, 6, 4 * 84 + 6, 32);
    for (i = 0; i < 4; i++)
        if (sui_button(T[i], x + 3 + i * 84, 10, 84, 24, T[i], tab == i ? BTN_TAB_ON : BTN_TAB)) { tab = i; sel_arg = -1; }
    if (sui_button("play", W - 84, 6, 76, 32, "Play", BTN_PRIMARY)) play_start();
    sui_tri(W - 76, 16, W - 76, 28, W - 66, 22, C_ONACC);
}

static void draw_toast(void)
{
    float w;
    if (!toast_t) return;
    toast_t--;
    w = sui_text_w(FONT_SEMIBOLD, 12, toast) + 28;
    sui_nine(SK_PLAQUE, W / 2 - w / 2, H - 52, w, 34);
    sui_text_c(FONT_SEMIBOLD, 12, W / 2, H - 40, toast, C_TEXT);
}

/* ------------------------------------------------------------------ projects */
static void projects(void)
{
    static const char *const TPL[4][2] = { { "First-person shooter", "Gun, imps, keys, exit" }, { "Third-person action", "Orbit camera, same enemies" },
                                           { "Exploration", "No enemies: find the exit" }, { "Empty", "One enemy type, an exit" } };
    float x = 86, y, cw;
    int i;
    sui_nine(SK_BAR, -6, -6, 70, H + 12);
    sui_nine(SK_BUTTON_HOT, 10, 10, 44, 44);
    sui_text_c(FONT_FIRE, 20, 32, 22, "DB", C_TEXT);
    sui_text(FONT_FIRE, 20, x, 14, "Your games", C_TEXT);
    sui_text(FONT_REGULAR, 12, x, 42, "Projects live on this device - open one to edit and play it", C_MUTED);
    sui_label(x, 70, "START FROM A TEMPLATE");
    cw = (W - x - 22 - 30) / 4;
    for (i = 0; i < 4; i++) {
        float cx = x + i * (cw + 10), cy = 88;
        int tap = sui_tap(TPL[i][0], cx, cy, cw, 104);
        sui_nine(i == 0 ? SK_PANEL : SK_PLAQUE, cx, cy, cw, 104);
        sui_gradient(cx + 10, cy + 10, cw - 20, 44, i == 0 ? 0x3A2A22 : i == 1 ? 0x22283A : i == 2 ? 0x1F2A24 : 0x121418, 0x101216);
        if (i == 0) sui_rect(cx + cw / 2 - 6, cy + 28, 12, 22, 0x8A8F99);
        if (i == 1) sui_rect(cx + cw / 2 - 5, cy + 24, 10, 26, 0xC9502D);
        if (i == 3) sui_text_c(FONT_REGULAR, 20, cx + cw / 2, cy + 20, "+", C_MUTED);
        sui_text(FONT_SEMIBOLD, 13, cx + 12, cy + 62, TPL[i][0], C_TEXT);
        sui_text(FONT_REGULAR, 11, cx + 12, cy + 82, TPL[i][1], C_MUTED);
        if (tap) {
            if (i == 0) new_project(HELLGATE, (int)sizeof(HELLGATE) - 1, "shooter", 1);
            else if (i == 3) new_project(EMPTY_GAME, (int)sizeof(EMPTY_GAME) - 1, "game", 2);
            else {
                Doc *d = &doc;
                doc.file[0] = 0;
                doc_parse(d, HELLGATE, (int)sizeof(HELLGATE) - 1);
                if (i == 1) { doc_head_set(d, "camera", "third"); doc_head_set(d, "game", "\"THIRD PERSON\""); }
                else { int k; for (k = 0; k < d->ntypes; k++) if (!strcmp(d->types[k].name, "imp")) doc_type_set_line(&d->types[k], "thing", "none"); doc_head_set(d, "game", "\"EXPLORATION\""); }
                { int n = doc_write(d, textbuf, (int)sizeof(textbuf)); static char t2[65536]; memcpy(t2, textbuf, (size_t)n); new_project(t2, n, i == 1 ? "action" : "explore", 1); }
            }
        }
    }
    sui_label(x, 210, nfiles ? "RECENT" : "NO PROJECTS YET - PICK A TEMPLATE");
    y = 228;
    for (i = 0; i < nfiles && y < H - 20; i++, y += 50) {
        sui_nine(SK_PLAQUE, x, y, W - x - 22, 44);
        sui_gradient(x + 12, y + 8, 44, 28, 0x4A1B10, 0x1C1F28);
        sui_text(FONT_SEMIBOLD, 13, x + 66, y + 7, files[i], C_TEXT);
        sui_text(FONT_REGULAR, 11, x + 66, y + 25, "a game project", C_MUTED);
        if (sui_button(files[i] + 1, W - 22 - 150, y + 6, 66, 32, "Play", BTN_PLAIN) && open_doc(files[i])) play_start();
        if (sui_button(files[i], W - 22 - 78, y + 6, 70, 32, "Edit", BTN_PRIMARY) && !open_doc(files[i])) say("THAT PROJECT COULDN'T BE READ");
    }
}

/* ------------------------------------------------------------------ behaviour: the graph */
#define NODE_W 120
#define NODE_H 50
static float nx_(const DocState *s) { return (s->x + pan_x) * zoom; }
static float ny_(const DocState *s, float top) { return top + (s->y + pan_y) * zoom; }
static int in_node(float px, float py, float x, float y) { return px >= x && px < x + NODE_W * zoom && py >= y && py < y + NODE_H * zoom; }

static const char *group_of(const char *action)
{
    int id = fw_action_find(action);
    return id >= 0 ? fw_actions[id].group : "?";
}
static u32 group_col(const char *g, int bg)
{
    if (!strcmp(g, "Move")) return bg ? 0x24324A : 0x9CC4FF;
    if (!strcmp(g, "Sense")) return bg ? 0x33284A : 0xC9B2FF;
    if (!strcmp(g, "Flow")) return bg ? 0x2E3A24 : 0xB9E39A;
    if (!strcmp(g, "Combat")) return bg ? 0x4A2424 : 0xFFA8A8;
    if (!strcmp(g, "Effect")) return bg ? 0x3A3424 : 0xF2D48A;
    if (!strcmp(g, "Game")) return bg ? 0x2A1B12 : 0xFFB08F;
    return bg ? 0x262A33 : 0xC8CDD6;
}

/* a link's curve: bowed sideways so A->B and B->A don't overlap; clipped to the boxes */
static void link_curve(float ax, float ay, float bx, float by, float *c)
{
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy), nx, ny, bend = 26 * zoom;
    if (l < 1) l = 1;
    nx = -dy / l; ny = dx / l;
    c[0] = ax; c[1] = ay;
    c[2] = ax + dx / 3 + nx * bend; c[3] = ay + dy / 3 + ny * bend;
    c[4] = ax + dx * 2 / 3 + nx * bend; c[5] = ay + dy * 2 / 3 + ny * bend;
    c[6] = bx; c[7] = by;
}
static int outside(float x, float y, float rx, float ry) { return x < rx - 2 || x > rx + NODE_W * zoom + 2 || y < ry - 2 || y > ry + NODE_H * zoom + 2; }

static void graph(float gx, float gy, float gw, float gh)
{
    DocFsm *m = cur_fsm < doc.nfsms ? &doc.fsms[cur_fsm] : 0;
    const SuiPtr *p = sui_ptr();
    int i, j, hit_node = -1;
    float top = gy;
    sui_gradient(gx, gy, gw, gh, 0x120807, 0x070303);
    sui_clip(gx, gy, gw, gh);
    {   /* dot grid */
        float step = 16 * zoom, ox = fmodf(pan_x * zoom, step), oy = fmodf(pan_y * zoom, step), x, y;
        if (step > 6) for (y = gy + oy; y < gy + gh; y += step) for (x = gx + ox; x < gx + gw; x += step) sui_rect(floorf(x), floorf(y), 1, 1, 0x2E1610);
    }
    if (!m) { sui_text(FONT_REGULAR, 13, gx + 20, gy + 20, "This project has no state machines yet.", C_MUTED); sui_unclip(); return; }
    /* links */
    for (i = 0; i < m->ns; i++) {
        DocState *s = &m->s[i];
        for (j = 0; j < s->nt; j++) {
            int to = doc_state_index(m, s->t[j].to), k, n = 0, sel = sel_state == i && sel_trans == j;
            float c[8], pts[2 * 40], ax, ay, bx, by, lx, ly, w;
            u32 col = sel ? 0xFFD070 : (sel_state == i ? C_ACCENT : 0x8A6656);
            if (to < 0) continue;
            ax = nx_(s) + NODE_W * zoom / 2; ay = ny_(s, top) + NODE_H * zoom / 2;
            if (to == i) {                                /* a loop back into the same state */
                float x0 = nx_(s) + NODE_W * zoom * 0.7f, y0 = ny_(s, top);
                sui_bezier(x0, y0, x0 + 10 * zoom, y0 - 40 * zoom, x0 + 50 * zoom, y0 - 30 * zoom, x0 + 30 * zoom, y0, 2, col);
                lx = x0 + 22 * zoom; ly = y0 - 34 * zoom;
            } else {
                bx = nx_(&m->s[to]) + NODE_W * zoom / 2; by = ny_(&m->s[to], top) + NODE_H * zoom / 2;
                link_curve(ax, ay, bx, by, c);
                for (k = 0; k <= 36; k++) {
                    float x, y;
                    sui_bezier_point(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], k / 36.0f, &x, &y);
                    if (outside(x, y, nx_(s), ny_(s, top)) && outside(x, y, nx_(&m->s[to]), ny_(&m->s[to], top))) { pts[n * 2] = x; pts[n * 2 + 1] = y; n++; }
                }
                if (n >= 2) {
                    float ex = pts[n * 2 - 2], ey = pts[n * 2 - 1], dx = ex - pts[n * 2 - 4], dy = ey - pts[n * 2 - 3], l = sqrtf(dx * dx + dy * dy);
                    sui_polyline(pts, n, 2, col);
                    if (l > 0.01f) { dx /= l; dy /= l; sui_tri(ex + dx * 3, ey + dy * 3, ex - dx * 8 - dy * 5, ey - dy * 8 + dx * 5, ex - dx * 8 + dy * 5, ey - dy * 8 - dx * 5, col); }
                }
                sui_bezier_point(c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], 0.5f, &lx, &ly);
            }
            w = sui_text_w(FONT_MONO, 10, s->t[j].event) + 10;
            sui_nine(sel ? SK_BUTTON_HOT : SK_FIELD, lx - w / 2, ly - 8, w, 16);
            sui_text(FONT_MONO, 10, lx - w / 2 + 5, ly - 5, s->t[j].event, sel ? 0xFFF0D2 : C_ACCENT2);
            if (sui_tap(&s->t[j], lx - w / 2 - 4, ly - 13, w + 8, 26)) { sel_state = i; sel_trans = j; sel_act = sel_arg = -1; }
        }
    }
    /* nodes */
    for (i = 0; i < m->ns; i++) {
        DocState *s = &m->s[i];
        float x = nx_(s), y = ny_(s, top), w = NODE_W * zoom, h = NODE_H * zoom;
        int sel = sel_state == i && sel_trans < 0;
        char sum[64];
        int k, o = 0;
        sui_nine(sel ? SK_PANEL : SK_PLAQUE, x, y, w, h);
        if (sel) sui_nine(SK_SELECT, x - 3, y - 3, w + 6, h + 6);
        if (i == 0) sui_text(FONT_BOLD, 9 * zoom, x + 10 * zoom, y + 6 * zoom, "START", C_GREEN);
        sui_text(FONT_BOLD, 13 * zoom, x + 10 * zoom, y + (i == 0 ? 17 : 10) * zoom, s->name, C_TEXT);
        sum[0] = 0;
        for (k = 0; k < s->na && o < 40; k++) { int q; if (k) sum[o++] = ' '; for (q = 0; s->a[k].action[q] && o < 40; q++) sum[o++] = s->a[k].action[q]; sum[o] = 0; }
        if (o >= 40) { sum[37] = '.'; sum[38] = '.'; sum[39] = 0; }
        sui_clip(x, y, w - 14 * zoom, h);
        sui_text(FONT_REGULAR, 10 * zoom, x + 10 * zoom, y + (i == 0 ? 33 : 30) * zoom, sum, sel ? C_ACCENT2 : C_MUTED);
        sui_unclip();
        /* the link handle */
        sui_circle(x + w, y + h / 2, 6 * zoom, sel ? C_ACCENT : C_LINE2);
        sui_circle(x + w, y + h / 2, 2.5f * zoom, C_BG);
        if (p->pressed && sui_in(gx, gy, gw, gh) && !sui_captured()) {
            float dx = p->x - (x + w), dy = p->y - (y + h / 2);
            if (dx * dx + dy * dy < 16 * 16) { insp_scroll = 0; linking = i; link_x = p->x; link_y = p->y; sui_capture(&linking); }
            else if (in_node(p->x, p->y, x, y)) hit_node = i;
        }
    }
    /* the link being drawn */
    if (linking >= 0 && linking < m->ns) {
        DocState *s = &m->s[linking];
        float ax = nx_(s) + NODE_W * zoom, ay = ny_(s, top) + NODE_H * zoom / 2;
        link_x = p->x; link_y = p->y;
        sui_bezier(ax, ay, ax + 60 * zoom, ay, link_x - 60 * zoom, link_y, link_x, link_y, 2, C_ACCENT);
        sui_circle(link_x, link_y, 5, C_ACCENT);
        if (p->released) {
            for (i = 0; i < m->ns; i++) if (in_node(p->x, p->y, nx_(&m->s[i]), ny_(&m->s[i], top))) break;
            if (i < m->ns) { ev_pick = linking; ev_pick_to = i; }
            linking = -1;
        }
    }
    /* pressing a node selects it; dragging moves it; empty space pans; two fingers zoom */
    if (hit_node >= 0) { drag_state = hit_node; insp_scroll = 0; sel_state = hit_node; sel_trans = -1; sel_act = sel_arg = -1; sui_capture(&drag_state); }
    else if (p->pressed && sui_in(gx, gy, gw, gh) && !sui_captured() && linking < 0) { panning = 1; sui_capture(&panning); }
    if (p->down && sui_captured() == &drag_state && drag_state >= 0 && drag_state < m->ns && p->moved && p->fingers < 2) {
        m->s[drag_state].x += p->dx / zoom; m->s[drag_state].y += p->dy / zoom; doc.dirty = 1;
    }
    if (p->down && sui_captured() == &panning && p->fingers < 2) { pan_x += p->dx / zoom; pan_y += p->dy / zoom; }
    if (p->fingers >= 2 && p->pinch != 1) {
        float cx = gx + gw / 2, cy = gy + gh / 2, nz = zoom * p->pinch;
        if (nz < 0.5f) nz = 0.5f;
        if (nz > 2.2f) nz = 2.2f;
        pan_x += (cx - gx) / nz - (cx - gx) / zoom; pan_y += (cy - gy) / nz - (cy - gy) / zoom;
        zoom = nz;
    }
    if (p->released) {
        if (sui_captured() == &panning && !p->moved) { sel_state = -1; sel_trans = -1; sel_act = sel_arg = -1; }
        drag_state = -1; panning = 0;
    }
    sui_nine(SK_PLAQUE, gx + 8, gy + gh - 30, 300, 24);
    sui_text(FONT_MONO, 9.5f, gx + 18, gy + gh - 23, "DRAG A STATE'S HANDLE TO ANOTHER TO LINK", C_MUTED);
    sui_unclip();
}

/* ------------------------------------------------------------------ behaviour: arguments */
static int arg_options(const Doc *d, int type, char (*out)[DOC_STR], int max)
{
    static const char *const BTN[] = { "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "L1", "R1", "L2", "R2", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT" };
    static const char *const SND[] = { "SHOOT", "HIT", "DIE", "JUMP", "PICKUP", "LAND", "ALERT", "EXPLODE", "CLICK", "HUM" };
    static const char *const OPS[] = { ">=", ">", "<=", "<", "==", "!=" };
    static const char *const TGT[] = { "self", "player", "all", "enemies", "owner" };
    int n = 0, i;
    switch (type) {
    case ARG_EVENT: return doc_events(d, out, max);
    case ARG_PROTO: for (i = 0; i < d->ntypes && n < max; i++) doc_copy(out[n++], d->types[i].name, DOC_STR); return n;
    case ARG_STATE: if (cur_fsm < d->nfsms) for (i = 0; i < d->fsms[cur_fsm].ns && n < max; i++) doc_copy(out[n++], d->fsms[cur_fsm].s[i].name, DOC_STR); return n;
    case ARG_BUTTON: for (i = 0; i < 14 && n < max; i++) doc_copy(out[n++], BTN[i], DOC_STR); return n;
    case ARG_SOUND: for (i = 0; i < 10 && n < max; i++) doc_copy(out[n++], SND[i], DOC_STR); return n;
    case ARG_OP: for (i = 0; i < 6 && n < max; i++) doc_copy(out[n++], OPS[i], DOC_STR); return n;
    case ARG_TARGET: for (i = 0; i < 5 && n < max; i++) doc_copy(out[n++], TGT[i], DOC_STR); return n;
    case ARG_VAR: for (i = 0; i < 8 && n < max; i++) snprintf(out[n++], DOC_STR, "v%d", i); return n;
    case ARG_GLOBAL: for (i = 0; i < 16 && n < max; i++) snprintf(out[n++], DOC_STR, "g%d", i); return n;
    }
    return 0;
}
static void arg_name(const FwActionDef *a, int k, char *out)
{
    const char *s = a->arg_names;
    int i = 0, o = 0;
    while (*s && i < k) { if (*s == ' ') i++; s++; }
    while (*s && *s != ' ' && o < 15) out[o++] = *s++;
    out[o] = 0;
}
static int default_num(const char *nm)
{
    if (!strcmp(nm, "speed")) return 30;
    if (!strcmp(nm, "dist") || !strcmp(nm, "range")) return 3000;
    if (!strcmp(nm, "ticks") || !strcmp(nm, "rate") || !strcmp(nm, "max")) return 60;
    if (!strcmp(nm, "min")) return 30;
    if (!strcmp(nm, "percent")) return 10;
    if (!strcmp(nm, "damage") || !strcmp(nm, "amount") || !strcmp(nm, "points") || !strcmp(nm, "value")) return 10;
    if (!strcmp(nm, "volume")) return 3000;
    if (!strcmp(nm, "on")) return 1;
    if (!strcmp(nm, "r") || !strcmp(nm, "g") || !strcmp(nm, "b")) return 200;
    if (!strcmp(nm, "angle")) return 400;
    return 0;
}
static void fill_defaults(DocAct *x)
{
    int id = fw_action_find(x->action), k;
    char opt[32][DOC_STR], nm[16];
    if (id < 0) return;
    for (k = 0; k < fw_actions[id].nargs && k < 4; k++) {
        int t = fw_actions[id].type[k], n;
        arg_name(&fw_actions[id], k, nm);
        if (t == ARG_NUM) { snprintf(x->arg[k], DOC_STR, "%d", default_num(nm)); continue; }
        if (t == ARG_TEXT) { doc_copy(x->arg[k], "\"HELLO\"", DOC_STR); continue; }
        if (t == ARG_EVENT) { doc_copy(x->arg[k], "EVENT1", DOC_STR); continue; }
        n = arg_options(&doc, t, opt, 32);
        doc_copy(x->arg[k], n ? opt[0] : "0", DOC_STR);
    }
}
static int arg_step(const char *nm)
{
    if (!strcmp(nm, "dist") || !strcmp(nm, "range") || !strcmp(nm, "volume")) return 100;
    if (!strcmp(nm, "ticks") || !strcmp(nm, "rate") || !strcmp(nm, "angle")) return 5;
    return 1;
}

/* EVENT1, EVENT2 ...: the first one not in use */
static void new_event_name(char (*ev)[DOC_STR], int n, char *out)
{
    int k = 1, q, used;
    do {
        snprintf(out, DOC_STR, "EVENT%d", k++);
        for (used = 0, q = 0; q < n; q++) if (!strcmp(ev[q], out)) used = 1;
    } while (used);
}

/* the argument being edited, at the bottom of the inspector */
static void arg_editor(DocAct *x, float px, float py, float pw)
{
    int id = fw_action_find(x->action), t, n, i;
    char nm[16], opt[40][DOC_STR];
    if (id < 0 || sel_arg < 0 || sel_arg >= fw_actions[id].nargs) return;
    t = fw_actions[id].type[sel_arg];
    arg_name(&fw_actions[id], sel_arg, nm);
    sui_nine(SK_PANEL, px, py, pw, 76);
    sui_text(FONT_SEMIBOLD, 11, px + 10, py + 8, nm, C_ACCENT2);
    sui_text(FONT_MONO, 15, px + 10, py + 26, x->arg[sel_arg], C_TEXT);
    if (t == ARG_NUM) {
        int v = atoi(x->arg[sel_arg]), st = arg_step(nm);
        static const char *const L[4] = { "-10", "-1", "+1", "+10" };
        static const int M[4] = { -10, -1, 1, 10 };
        for (i = 0; i < 4; i++)
            if (sui_button(&L[i], px + 8 + i * (pw - 16) / 4, py + 46, (pw - 16) / 4 - 4, 24, L[i], BTN_PLAIN)) {
                v += M[i] * st; snprintf(x->arg[sel_arg], DOC_STR, "%d", v); doc.dirty = 1;
            }
        return;
    }
    if (t == ARG_TEXT) { sui_text(FONT_REGULAR, 11, px + 10, py + 52, "Text editing comes with the keyboard support", C_MUTED); return; }
    n = arg_options(&doc, t, opt, 40);
    if (t == ARG_EVENT && n < 40) { new_event_name(opt, n, opt[n]); n++; }
    for (i = 0; i < n; i++) if (!strcmp(opt[i], x->arg[sel_arg])) break;
    if (sui_button("argprev", px + 8, py + 46, 60, 24, "<", BTN_PLAIN) && n) { i = i >= n ? 0 : (i + n - 1) % n; doc_copy(x->arg[sel_arg], opt[i], DOC_STR); doc.dirty = 1; }
    if (sui_button("argnext", px + pw - 68, py + 46, 60, 24, ">", BTN_PLAIN) && n) { i = i >= n ? 0 : (i + 1) % n; doc_copy(x->arg[sel_arg], opt[i], DOC_STR); doc.dirty = 1; }
    sui_text_c(FONT_REGULAR, 11, px + pw / 2, py + 51, t == ARG_EVENT ? "events" : "choices", C_MUTED);
}

static float action_row(DocState *s, int k, float x, float y, float w)
{
    DocAct *a = &s->a[k];
    int id = fw_action_find(a->action), j, sel = sel_act == k;
    const char *g = group_of(a->action);
    float cx, h = 40;
    char tag[12];
    int q;
    for (q = 0; g[q] && q < 10; q++) tag[q] = (char)(g[q] >= 'a' && g[q] <= 'z' ? g[q] - 32 : g[q]);
    tag[q] = 0;
    sui_nine(sel ? SK_PANEL : SK_PLAQUE, x, y, w, h);
    if (sui_tap(a, x, y, w, h)) { sel_act = sel_act == k ? -1 : k; sel_arg = -1; }
    sui_rrect(x + 8, y + 12, sui_text_w(FONT_SEMIBOLD, 9, tag) + 10, 16, 4, group_col(g, 1));
    sui_text(FONT_SEMIBOLD, 9, x + 13, y + 14, tag, group_col(g, 0));
    sui_text(FONT_SEMIBOLD, 12, x + sui_text_w(FONT_SEMIBOLD, 9, tag) + 24, y + 12, a->action, id < 0 ? C_RED : C_TEXT);
    cx = x + w - 6;
    for (j = (id >= 0 ? fw_actions[id].nargs : 0) - 1; j >= 0; j--) {
        float cw = sui_text_w(FONT_MONO, 10, a->arg[j][0] ? a->arg[j] : "?") + 12;
        cx -= cw + 4;
        if (cx < x + 90) break;
        sui_nine(sel && sel_arg == j ? SK_BUTTON_HOT : SK_FIELD, cx, y + 9, cw, 22);
        sui_text(FONT_MONO, 10, cx + 6, y + 15, a->arg[j][0] ? a->arg[j] : "?", sel && sel_arg == j ? 0xFFF0D2 : C_TEXT);
        if (sui_tap(a->arg[j], cx, y, cw, h)) { sel_act = k; sel_arg = j; }
    }
    return h + 5;
}

static void picker(void);
static void inspector(float x, float y, float w, float h)
{
    DocFsm *m = cur_fsm < doc.nfsms ? &doc.fsms[cur_fsm] : 0;
    DocState *s;
    const SuiPtr *p = sui_ptr();
    float cy, top = y + 10;
    /* drag to scroll when it's taller than the screen */
    if (p->down && p->moved && p->fingers < 2 && !sui_captured() && sui_in(x, y, w, h)) insp_scroll -= p->dy;
    if (insp_scroll > insp_content - (h - 50)) insp_scroll = insp_content - (h - 50);
    if (insp_scroll < 0) insp_scroll = 0;
    cy = top - insp_scroll;
    int k, pass;
    sui_nine(SK_PANEL, x, y - 2, w + 6, h + 6);
    sui_clip(x, y, w, h);
    if (!m || sel_state < 0 || sel_state >= m->ns) {
        sui_text(FONT_BOLD, 14, x + 12, cy, m ? m->name : "No machine", C_TEXT);
        sui_text(FONT_REGULAR, 11.5f, x + 12, cy + 24, "Tap a state to edit what it does.", C_MUTED);
        sui_text(FONT_REGULAR, 11.5f, x + 12, cy + 42, "Drag from a state's handle to another", C_MUTED);
        sui_text(FONT_REGULAR, 11.5f, x + 12, cy + 58, "state to add a transition.", C_MUTED);
        if (m) {
            char b[64];
            snprintf(b, sizeof(b), "%d states", m->ns);
            sui_text(FONT_MONO, 11, x + 12, cy + 86, b, C_MUTED);
        }
        sui_unclip();
        return;
    }
    s = &m->s[sel_state];
    if (sel_trans >= 0 && sel_trans < s->nt) {      /* a link */
        DocTrans *t = &s->t[sel_trans];
        char ev[40][DOC_STR];
        int n = doc_events(&doc, ev, 40), i;
        sui_text(FONT_BOLD, 14, x + 12, cy, "Transition", C_TEXT);
        sui_text(FONT_REGULAR, 12, x + 12, cy + 26, s->name, C_MUTED);
        sui_text(FONT_REGULAR, 12, x + 12 + sui_text_w(FONT_REGULAR, 12, s->name) + 6, cy + 26, "->", C_MUTED);
        sui_text(FONT_SEMIBOLD, 12, x + 12 + sui_text_w(FONT_REGULAR, 12, s->name) + 26, cy + 26, t->to, C_TEXT);
        sui_label(x + 12, cy + 56, "WHEN THIS EVENT HAPPENS");
        for (i = 0; i < n; i++) if (!strcmp(ev[i], t->event)) break;
        if (sui_button("tprev", x + 12, cy + 74, 44, 36, "<", BTN_PLAIN) && n) { i = i >= n ? 0 : (i + n - 1) % n; doc_copy(t->event, ev[i], DOC_STR); doc.dirty = 1; }
        sui_nine(SK_FIELD, x + 62, cy + 74, w - 136, 36);
        sui_text_c(FONT_MONO, 13, x + 62 + (w - 136) / 2, cy + 84, t->event, C_ACCENT2);
        if (sui_button("tnext", x + w - 68, cy + 74, 44, 36, ">", BTN_PLAIN) && n) { i = i >= n ? 0 : (i + 1) % n; doc_copy(t->event, ev[i], DOC_STR); doc.dirty = 1; }
        if (sui_button("tdel", x + 12, cy + 124, w - 36, 36, "Delete transition", BTN_DANGER)) { doc_del_trans(s, sel_trans); sel_trans = -1; doc.dirty = 1; }
        sui_unclip();
        return;
    }
    sui_text(FONT_FIRE, 20, x + 12, cy - 2, s->name, C_TEXT);
    if (sel_state == 0) sui_text(FONT_SEMIBOLD, 10, x + w - 54, cy + 3, "START", C_GREEN);
    else if (sui_button("mkstart", x + w - 92, cy - 4, 80, 26, "Make start", BTN_PLAIN)) {
        DocState t = m->s[sel_state]; memmove(&m->s[1], &m->s[0], sizeof(DocState) * (size_t)sel_state); m->s[0] = t; sel_state = 0; doc.dirty = 1;
    }
    cy += 28;
    for (pass = 1; pass >= 0; pass--) {
        sui_label(x + 12, cy, pass ? "EVERY TICK" : "ON ENTER, IN ORDER");
        cy += 17;
        for (k = 0; k < s->na; k++) if (s->a[k].every == pass) cy += action_row(s, k, x + 10, cy, w - 20);
        cy += 3;
    }
    if (sel_act >= 0 && sel_act < s->na) {           /* the selected action's tools */
        DocAct *a = &s->a[sel_act];
        float bw = (w - 20 - 12) / 4;
        if (sui_button("aup", x + 10, cy, bw, 30, "Up", BTN_PLAIN)) { doc_move_act(s, sel_act, -1); if (sel_act > 0) sel_act--; doc.dirty = 1; }
        if (sui_button("adn", x + 14 + bw, cy, bw, 30, "Down", BTN_PLAIN)) { doc_move_act(s, sel_act, 1); if (sel_act < s->na - 1) sel_act++; doc.dirty = 1; }
        if (sui_button("aev", x + 18 + bw * 2, cy, bw, 30, a->every ? "Once" : "Every", BTN_PLAIN)) { a->every ^= 1; doc.dirty = 1; }
        if (sui_button("adel", x + 22 + bw * 3, cy, bw, 30, "Delete", BTN_DANGER)) { doc_del_act(s, sel_act); sel_act = sel_arg = -1; doc.dirty = 1; }
        cy += 36;
        if (sel_act >= 0 && sel_arg >= 0) { arg_editor(&s->a[sel_act], x + 10, cy, w - 20); cy += 82; }
    } else {
        if (sui_button("addact", x + 10, cy, w - 20, 34, "+ Add action", BTN_GHOST)) { pick_open = 1; pick_group = 0; }
        cy += 40;
    }
    sui_label(x + 12, cy, "TRANSITIONS");
    cy += 17;
    if (!s->nt) { sui_text(FONT_REGULAR, 11, x + 12, cy, "None: drag from the handle to add one", C_MUTED); cy += 18; }
    for (k = 0; k < s->nt; k++) {
        sui_text(FONT_MONO, 11, x + 12, cy + 4, s->t[k].event, C_ACCENT2);
        sui_text(FONT_REGULAR, 12, x + 110, cy + 3, "->", C_MUTED);
        sui_text(FONT_SEMIBOLD, 12, x + 130, cy + 3, s->t[k].to, C_TEXT);
        if (sui_tap(&s->t[k].to, x + 8, cy, w - 16, 22)) sel_trans = k;
        cy += 24;
    }
    cy += 8;
    if (sui_button("delstate", x + 10, cy, w - 20, 32, "Delete state", BTN_DANGER)) { doc_del_state(m, sel_state); sel_state = -1; doc.dirty = 1; }
    cy += 40;
    insp_content = cy + insp_scroll - top;
    sui_unclip();
}

/* add an action: the library by group */
static void picker(void)
{
    static const char *const G[8] = { "Flow", "Move", "Sense", "Combat", "Actor", "Data", "Effect", "Game" };
    DocFsm *m = &doc.fsms[cur_fsm];
    float pw = W - 120, ph = H - 60, px = 60, py = 30, y;
    int i;
    sui_rect(0, 0, W, H, 0xB0000000);
    sui_nine(SK_PANEL, px, py, pw, ph);
    sui_text(FONT_FIRE, 15, px + 16, py + 16, "Add an action", C_TEXT);
    if (sui_button("pk_every", px + 180, py + 10, 100, 28, "Every tick", pick_every ? BTN_CHIP_ON : BTN_CHIP)) pick_every = 1;
    if (sui_button("pk_once", px + 286, py + 10, 110, 28, "Once on enter", pick_every ? BTN_CHIP : BTN_CHIP_ON)) pick_every = 0;
    if (sui_button("pk_close", px + pw - 96, py + 10, 84, 28, "Cancel", BTN_PLAIN)) pick_open = 0;
    for (i = 0; i < 8; i++)
        if (sui_button(G[i], px + 12, py + 52 + i * 36, 110, 32, G[i], pick_group == i ? BTN_TAB_ON : BTN_TAB)) pick_group = i;
    sui_rect(px + 130, py + 50, 1, ph - 62, C_LINE);
    sui_clip(px + 136, py + 48, pw - 148, ph - 56);
    y = py + 52;
    for (i = 0; i < fw_action_count; i++) {
        const FwActionDef *a = &fw_actions[i];
        int col = 0;
        if (strcmp(a->group, G[pick_group])) continue;
        col = (int)((y - py - 52) / 46) >= 6 ? 1 : 0;
        (void)col;
        if (sui_tap(a, px + 140, y, pw - 160, 42) && sel_state >= 0 && sel_state < m->ns) {
            DocState *s = &m->s[sel_state];
            int k = doc_add_act(s, a->name, pick_every);
            if (k >= 0) { fill_defaults(&s->a[k]); sel_act = k; sel_arg = fw_actions[i].nargs ? 0 : -1; doc.dirty = 1; }
            pick_open = 0;
        }
        sui_nine(sui_in(px + 140, y, pw - 160, 42) && sui_ptr()->down ? SK_BUTTON_LIT : SK_PLAQUE, px + 140, y, pw - 160, 42);
        sui_text(FONT_SEMIBOLD, 12.5f, px + 152, y + 6, a->name, C_TEXT);
        sui_text(FONT_MONO, 10, px + 152 + sui_text_w(FONT_SEMIBOLD, 12.5f, a->name) + 10, y + 8, a->arg_names, C_ACCENT2);
        sui_text(FONT_REGULAR, 11, px + 152, y + 24, a->help, C_MUTED);
        y += 46;
    }
    sui_unclip();
}

/* choosing the event for a link that was just drawn */
static void event_picker(void)
{
    DocFsm *m = &doc.fsms[cur_fsm];
    char ev[40][DOC_STR];
    int n = doc_events(&doc, ev, 39), i;
    float pw = 420, ph = 260, px = W / 2 - 210, py = H / 2 - 130;
    new_event_name(ev, n, ev[n]); n++;
    sui_rect(0, 0, W, H, 0xB0000000);
    sui_nine(SK_PANEL, px, py, pw, ph);
    sui_text(FONT_FIRE, 15, px + 16, py + 16, "Which event takes this link?", C_TEXT);
    for (i = 0; i < n; i++) {
        float bx = px + 14 + (i % 4) * 99, by = py + 48 + (i / 4) * 36;
        if (by > py + ph - 80) break;
        if (sui_button(ev[i], bx, by, 94, 30, ev[i], i == n - 1 ? BTN_GHOST : BTN_CHIP)) {
            DocState *s = &m->s[ev_pick];
            if (doc_add_trans(s, ev[i], m->s[ev_pick_to].name) >= 0) { sel_state = ev_pick; sel_trans = s->nt - 1; doc.dirty = 1; }
            ev_pick = -1;
        }
    }
    if (sui_button("ev_cancel", px + pw - 96, py + ph - 44, 84, 32, "Cancel", BTN_PLAIN)) ev_pick = -1;
}

static void behaviour(void)
{
    float gx = 0, gy = 45, iw = 300;
    int i;
    graph(gx, gy, W - iw, H - gy);
    {   /* the machines, as chips over the graph */
        float x = 10;
        for (i = 0; i < doc.nfsms; i++) {
            float w = sui_text_w(FONT_SEMIBOLD, 11, doc.fsms[i].name) + 22;
            if (sui_button(&doc.fsms[i], x, gy + 8, w, 28, doc.fsms[i].name, i == cur_fsm ? BTN_CHIP_ON : BTN_CHIP)) { cur_fsm = i; sel_state = sel_trans = sel_act = sel_arg = -1; }
            x += w + 6;
        }
        if (cur_fsm < doc.nfsms && sui_button("addstate", W - iw - 96, gy + 8, 86, 28, "+ State", BTN_GHOST)) {
            DocFsm *m = &doc.fsms[cur_fsm];
            int k = doc_add_state(m, "state", 60 - pan_x + 40, 140 - pan_y);
            if (k >= 0) { sel_state = k; sel_trans = sel_act = sel_arg = -1; doc.dirty = 1; }
        }
    }
    inspector(W - iw, gy, iw, H - gy);
}

/* ------------------------------------------------------------------ scene: actor types and the game */
static void stepper(const void *id, float x, float y, float w, const char *label, DocType *t, const char *key, int step)
{
    int v = 0;
    char b[24];
    doc_type_num(t, key, &v);
    sui_text(FONT_REGULAR, 12, x, y + 8, label, C_MUTED);
    if (sui_button(id, x + w - 150, y, 40, 30, "-", BTN_PLAIN)) { doc_type_set_num(t, key, v - step); doc.dirty = 1; }
    snprintf(b, sizeof(b), "%d", v);
    sui_nine(SK_FIELD, x + w - 106, y, 62, 30);
    sui_text_c(FONT_MONO, 12, x + w - 75, y + 7, b, C_TEXT);
    if (sui_button((const char *)id + 1, x + w - 40, y, 40, 30, "+", BTN_PLAIN)) { doc_type_set_num(t, key, v + step); doc.dirty = 1; }
}

static void scene(void)
{
    static const u32 SW[8] = { 0xD23C1E, 0xFF6A2B, 0xE0B23A, 0x5BD16A, 0x3CC8C8, 0x4C9BFF, 0xB070FF, 0xE7E9EE };
    static const char *const SHAPES[4] = { "box", "pillar", "orb", "none" };
    float lx = 0, ly = 45, lw = 220, x, y;
    int i;
    sui_nine(SK_PANEL, lx - 4, ly - 2, lw + 4, H - ly + 6);
    sui_label(lx + 12, ly + 10, "ACTOR TYPES");
    y = ly + 30;
    for (i = 0; i < doc.ntypes; i++, y += 36) {
        DocType *t = &doc.types[i];
        int r = 200, g = 200, b = 200;
        const char *c = doc_type_word(t, "color");
        if (c[0]) { int k; sscanf(c, "%d", &r); for (k = 0; k < t->np; k++) if (!strncmp(t->p[k].line, "color", 5)) sscanf(t->p[k].line + 5, "%d %d %d", &r, &g, &b); }
        if (i == sel_type) sui_nine(SK_ROW, lx + 4, y, lw - 10, 34);
        sui_rrect(lx + 12, y + 8, 18, 18, 4, (u32)(r << 16 | g << 8 | b));
        sui_text(FONT_SEMIBOLD, 12.5f, lx + 40, y + 12, t->name, i == sel_type ? 0xFFF0D2 : C_TEXT);
        if (sui_tap(t, lx, y, lw, 34)) sel_type = i;
    }
    sui_label(lx + 12, y + 10, "GAME");
    {   /* camera */
        const char *cam = doc_head_word(&doc, "camera", 1);
        int third = !strcmp(cam, "third");
        if (sui_button("cam1", lx + 12, y + 28, 96, 30, "First person", third ? BTN_CHIP : BTN_CHIP_ON)) { doc_head_set(&doc, "camera", "first"); doc.dirty = 1; }
        if (sui_button("cam3", lx + 112, y + 28, 98, 30, "Third person", third ? BTN_CHIP_ON : BTN_CHIP)) { doc_head_set(&doc, "camera", "third"); doc.dirty = 1; }
    }
    /* the selected type */
    x = lw + 20; y = ly + 14;
    if (sel_type >= doc.ntypes) sel_type = 0;
    if (!doc.ntypes) { sui_text(FONT_REGULAR, 13, x, y, "No actor types.", C_MUTED); return; }
    {
        DocType *t = &doc.types[sel_type];
        float cw = W - x - 20, col2 = x + cw / 2 + 10, hw = cw / 2 - 10;
        const char *shape = doc_type_word(t, "shape"), *fsm = doc_type_word(t, "fsm");
        char cur_fsm_name[DOC_STR];
        sui_text(FONT_FIRE, 20, x, y - 2, t->name, C_TEXT);
        doc_copy(cur_fsm_name, fsm, DOC_STR);
        sui_label(x, y + 34, "LOOK");
        for (i = 0; i < 4; i++)
            if (sui_button(SHAPES[i], x + i * 66, y + 52, 62, 30, SHAPES[i], !strcmp(shape, SHAPES[i]) ? BTN_CHIP_ON : BTN_CHIP)) { doc_type_set_line(t, "shape", SHAPES[i]); doc.dirty = 1; }
        for (i = 0; i < 8; i++) {
            char rgb[24];
            sui_circle(x + 14 + i * 32, y + 104, 12, SW[i]);
            if (sui_tap(&SW[i], x + 2 + i * 32, y + 90, 28, 28)) { snprintf(rgb, sizeof(rgb), "%d %d %d", (int)(SW[i] >> 16), (int)(SW[i] >> 8 & 255), (int)(SW[i] & 255)); doc_type_set_line(t, "color", rgb); doc.dirty = 1; }
        }
        sui_label(x, y + 132, "BEHAVIOUR");
        sui_nine(SK_PLAQUE, x, y + 150, hw, 36);
        sui_text(FONT_SEMIBOLD, 12.5f, x + 12, y + 160, cur_fsm_name[0] ? cur_fsm_name : "none", cur_fsm_name[0] ? C_TEXT : C_MUTED);
        if (cur_fsm_name[0] && sui_button("editfsm", x + hw - 70, y + 154, 64, 28, "Edit", BTN_PRIMARY)) {
            for (i = 0; i < doc.nfsms; i++) if (!strcmp(doc.fsms[i].name, cur_fsm_name)) { cur_fsm = i; sel_state = sel_trans = sel_act = sel_arg = -1; tab = TAB_BEHAVIOUR; }
        }
        if (sui_button("nextfsm", x, y + 194, hw, 30, "Use another machine", BTN_GHOST) && doc.nfsms) {
            for (i = 0; i < doc.nfsms; i++) if (!strcmp(doc.fsms[i].name, cur_fsm_name)) break;
            i = i >= doc.nfsms ? 0 : (i + 1) % doc.nfsms;
            doc_type_set_line(t, "fsm", doc.fsms[i].name); doc.dirty = 1;
        }
        sui_label(col2, y + 34, "NUMBERS");
        stepper("s_hp", col2, y + 52, hw, "Health", t, "hp", 5);
        stepper("s_sp", col2, y + 88, hw, "Speed", t, "speed", 2);
        stepper("s_sz", col2, y + 124, hw, "Size", t, "size", 50);
        stepper("s_rd", col2, y + 160, hw, "Radius", t, "radius", 25);
        stepper("s_dm", col2, y + 196, hw, "Damage", t, "damage", 1);
        sui_text(FONT_REGULAR, 11, x, H - 26, "The 3D scene view and placing actors in the level arrive with the sector world (milestone 2).", C_MUTED);
    }
}

static void placeholder(const char *title, const char *line1, const char *line2)
{
    float cx = W / 2;
    sui_nine(SK_PANEL, cx - 260, 110, 520, 150);
    sui_text_c(FONT_FIRE, 20, cx, 132, title, C_TEXT);
    sui_text_c(FONT_REGULAR, 12, cx, 170, line1, C_MUTED);
    sui_text_c(FONT_REGULAR, 12, cx, 190, line2, C_MUTED);
}

/* ------------------------------------------------------------------ play-test */
static void play_view(void)
{
    const SuiPtr *p = sui_ptr();
    float fw = (float)SCREEN_W, fh = (float)SCREEN_H, sc = W / fw > H / fh ? W / fw : H / fh;
    play_w = fw * sc; play_h = fh * sc; play_x = (W - play_w) / 2; play_y = (H - play_h) / 2;
    sui_frame_image(play_x, play_y, play_w, play_h);
    sui_nine(SK_BAR, -6, -6, W + 12, 46);
    if (sui_button("stop", 8, 4, 72, 32, "Stop", BTN_DANGER)) play_stop();
    if (sui_button("pause", 86, 4, 72, 32, paused ? "Resume" : "Pause", BTN_PLAIN)) paused ^= 1;
    sui_text(FONT_REGULAR, 12, 170, 13, play_ok ? "Play-testing" : "The project has an error", play_ok ? C_MUTED : C_RED);
    if (play_ok) sui_text(FONT_SEMIBOLD, 12, 260, 13, doc.title, C_TEXT);
    (void)p;
}

/* ------------------------------------------------------------------ the App */
static int frame_w, frame_h, frame_d;
static const HrList *hires_frame(int w, int h, int density)
{
    frame_w = w; frame_h = h; frame_d = density;
    sui_begin(w, h, density);
    W = sui_w(); H = sui_h();
    sui_frame_input();
    if (screen == SCR_PROJECTS) projects();
    else if (screen == SCR_PLAY) play_view();
    else {
        static const char *const WH[4] = { "/ Scene", "/ Geometry", "/ Behaviour", "/ Assets" };
        int modal = tab == TAB_BEHAVIOUR && (pick_open || ev_pick >= 0);
        sui_mute(modal);
        if (tab == TAB_BEHAVIOUR) behaviour();
        else if (tab == TAB_SCENE) scene();
        else if (tab == TAB_GEOMETRY) { int wc; geom_tab(0, 45, W, H - 45, &wc); if (wc) make_level(wc == 1); }
        else placeholder("Assets", "Model import (FBX, glTF, OBJ) with animation clips comes in", "milestone 4, along with textures and sounds.");
        header(WH[tab]);
        sui_mute(0);
        if (pick_open && tab == TAB_BEHAVIOUR) picker();
        if (ev_pick >= 0 && tab == TAB_BEHAVIOUR) event_picker();
        if (doc.dirty && (eng_in()->ticks % 300) == 0) save_doc();
    }
    draw_toast();
    return sui_end(C_BG);
}

/* pointers: to the UI, and during play also to the game's touch controls (under the top bar) */
static void hires_pointer(int id, int ev, int x, int y, int kind)
{
    (void)kind;
    sui_pointer(id, ev, x, y);
    if (screen == SCR_PLAY && frame_d > 0) {
        float dx = x * 100.0f / frame_d, dy = y * 100.0f / frame_d;
        int fx = (int)((dx - play_x) * SCREEN_W / (play_w > 1 ? play_w : 1)), fy = (int)((dy - play_y) * SCREEN_H / (play_h > 1 ? play_h : 1));
        if (dy > 40 || ev != 0) eng_touch(id, ev, fx, fy);
    }
}
static void hires_back(void)
{
    if (screen == SCR_PLAY) play_stop();
    else if (pick_open) pick_open = 0;
    else if (ev_pick >= 0) ev_pick = -1;
    else if (screen == SCR_EDIT) { save_doc(); refresh_files(); screen = SCR_PROJECTS; }
}
static int hires(void) { return 1; }

/* the pixel-art atlas: Deadbird's fire font and UI kit (tools/make_studio_atlas.py) */
static u32 atlas[SA_W * SA_H];
static const SuiCell SKIN[SK_COUNT] = {
    { 0, 96, 32, 32, 10, 0x22090A, 0x0C0305, 0 },        /* panel: iron, bone corners, molten seam */
    { 32, 96, 24, 24, 6, 0x2A0F0D, 0x140708, 0 },        /* button */
    { 56, 96, 24, 24, 6, 0xFA822C, 0xA8280A, 0 },        /* button, pressed: the face burns */
    { 80, 96, 24, 24, 9, 0xBE3410, 0x641006, 0 },        /* button, primary / selected */
    { 104, 96, 24, 24, 6, 0x2C110D, 0x180808, 0 },       /* bar plate */
    { 128, 96, 24, 16, 6, 0xC83C10, 0x781808, 0 },       /* selected row */
    { 152, 96, 16, 16, 5, 0x070203, 0x0F0506, 0 },       /* inset field */
    { 168, 96, 16, 16, 5, 0, 0, 1 },                     /* selection frame (hollow) */
    { 184, 96, 24, 24, 7, 0x180808, 0x090303, 0 },       /* plaque */
};
static void atlas_init(void)
{
    int i = 0, o = 0;
    while (i + 4 < (int)sizeof(sa_rle) && o < SA_W * SA_H) {
        int n = sa_rle[i];
        u32 px = (u32)sa_rle[i + 1] | (u32)sa_rle[i + 2] << 8 | (u32)sa_rle[i + 3] << 16 | (u32)sa_rle[i + 4] << 24;
        while (n-- > 0 && o < SA_W * SA_H) atlas[o++] = px;
        i += 5;
    }
    sui_set_atlas(atlas, SA_W, SA_H);
    sui_set_font(sa_chars, SA_FONT_BASE, 8, 10, 7, 136, 0, 32, -128);
    sui_set_skin(SKIN);
}

static void init(void)
{
    atlas_init();
    sector_world_init();
    sector_world_set_loader(geom_load_level);
    geom_set_toast(say);
    fw_set_world(&sector_world);
    fw_init();
    refresh_files();
    (void)back_tab;
}
static void tick(void) { if (screen == SCR_PLAY && play_ok && !paused) fw_rt_tick(); }
static void draw(FrameBuf *fb)
{
    if (screen == SCR_PLAY) fw_rt_draw(fb);
    else if (screen == SCR_EDIT && tab == TAB_GEOMETRY && geom_in3d()) geom_draw3d(fb, (int)eng_in()->ticks);
}
static int ram(void) { return (int)sizeof(doc) + fw_rt_ram(); }

const App game_app = {
    "DEADBIRD STUDIO",
    init, tick, draw,
    fw_rt_touch_buttons, fw_rt_tap, fw_rt_look, 45,
    0, 0, 0, 0,
    0,
    fw_runtime_keys, 0,
    "projects", ".game",
    ram,
    hires, hires_frame, hires_pointer, hires_back,
};

/* tests */
Doc *studio_doc(void) { return &doc; }
int  studio_screen(void) { return screen; }
int  studio_play_ok(void) { return play_ok; }
void studio_tab(int t) { tab = t; }
