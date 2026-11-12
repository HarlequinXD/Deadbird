/*
 * The Studio's Geometry tab: Deadbird's own level editor (game/editor.c), embedded. The same
 * tools, d-pad cursor, tool rail, inspector, 3D view and controls as the editor in the game, on
 * the project's level file (<project>.map). The level is drawn into the engine's frame (the
 * PS1-style 320..512 x 240 picture), which the tab shows scaled up; the editor's bars, buttons,
 * panels and text are recorded instead (edui.h, eu_hires) and drawn here in the Studio's own
 * crisp UI over it. Touches go to the editor in frame coordinates.
 */
#include <stdio.h>
#include <string.h>
#include "geom.h"
#include "editor.h"
#include "edmap.h"
#include "engine.h"
#include "files.h"
#include "level.h"
#include "sui.h"
#include "edui.h"
#include "ui.h"

static char mapfile[32];
#ifdef _WIN32
static int loaded, last_kind = EK_MOUSE_L;        /* on a PC the editor starts in its mouse and keyboard layout */
#else
static int loaded, last_kind = EK_TOUCH;          /* the Studio is touch first; a mouse switches it */
#endif
static int hov_x = -1, hov_y = -1;               /* the mouse over the editor (frame pixels) */
static float ix, iy, iw, ih;                 /* where the frame is shown (canvas units) */
static int want_w;                           /* the frame width that keeps its pixels square */
static u32 ptr_mask;                         /* pointers that went down on the editor */
static void (*say_)(const char *);

void geom_set_toast(void (*f)(const char *)) { say_ = f; }
static void say(const char *s) { if (say_) say_(s); }
int  geom_loaded(void) { return loaded; }
const char *geom_file(void) { return mapfile; }

static int touch_now(void) { return last_kind == EK_TOUCH || (last_kind < 0 && eng_in()->touch_ui); }

int geom_open(const char *file)
{
    loaded = 0; ptr_mask = 0;
    snprintf(mapfile, sizeof(mapfile), "%s", file ? file : "");
    if (!mapfile[0]) return 0;
    ed_quiet = 1; eu_hires = 1;
    if (!editor_open_file(mapfile, eng_in()->pad_style, touch_now())) return 0;
    loaded = 1;
    return 1;
}
int geom_create(const char *file, int from_level1)
{
    static char buf[1 << 18];
    static EmMap m;
    int n;
    if (from_level1) { if (!em_parse(&m, em_src_e1)) return 0; }
    else em_new_level(&m, "LEVEL");
    n = em_write(&m, buf, (int)sizeof(buf));
    if (n < 0 || !plat_file_save(file, buf, n)) return 0;
    return geom_open(file);
}
void geom_save(void) { if (loaded && !editor_save()) say("COULDN'T SAVE THE LEVEL"); }

/* back from a play-test: the editor reloads its textures and 3D view */
void geom_resume(void) { if (loaded) editor_resume(); }

/* the play-test's level loader (sector_world): the project's .map, built; "" = level 1 */
int geom_load_level(const char *name)
{
    static char buf[1 << 18];
    static EmMap m;
    const MapDef *d;
    int n;
    if (!name || !name[0]) { level_load_map(0); return 1; }
    if (loaded && !strcmp(name, mapfile)) geom_save();
    n = plat_file_load(name, buf, (int)sizeof(buf) - 1);
    if (n <= 0) { level_load_map(0); say("LEVEL FILE MISSING - PLAYING LEVEL 1"); return 1; }
    buf[n] = 0;
    if (em_parse(&m, buf) && (d = em_compile(&m, 0)) != 0) level_load_map(d);
    else { char e[128]; snprintf(e, sizeof(e), "LEVEL: %s", em_error); say(e); level_load_map(0); }
    return 1;
}

/* each engine tick while the tab is up: the editor's update, with the pad / keys / sticks */
int geom_tick(int no_pad)
{
    const Input *in = eng_in();
    u32 pad = in->pad, press = in->press;
    int r, lx = in->lx, ly = in->ly, rx = in->rx, ry = in->ry;
    if (!loaded) return GEOM_STAY;
    if (no_pad) {                        /* the controller drives the Studio's pointer: keyboard only */
        int a, b, c, d;
        u32 dev = eng_pad_device();
        eng_pad_sticks(&a, &b, &c, &d);
        pad &= ~dev; press &= ~dev; lx -= a; ly -= b; rx -= c; ry -= d;
    }
    editor_style(in->pad_style, touch_now());
    r = editor_update(pad, press, lx, ly, rx, ry);
    if (r == ED_PLAYTEST) return GEOM_PLAY;
    if (r == ED_EXIT) return GEOM_EXIT;
    return GEOM_STAY;
}
void geom_draw(FrameBuf *fb, int t) { if (loaded) editor_draw(fb, t); }
/* Android's back: the editor's O (close / cancel); with nothing open, 1: leave the editor */
int geom_back(void) { if (!loaded) return 1; if (editor_idle()) { geom_save(); return 1; } editor_back(); return 0; }

/* a pointer from the Studio (canvas units): onto the editor's picture */
int geom_pointer(int id, int ev, float x, float y, int kind)
{
    int fx, fy;
    u32 bit = id >= 0 && id < 32 ? 1u << id : 0;
    if (!loaded || iw < 1 || ih < 1) return 0;
    if (ev == EP_DOWN) {
        if (x < ix || y < iy || x >= ix + iw || y >= iy + ih) return 0;
        ptr_mask |= bit;
    } else if (ev != EP_HOVER && !(ptr_mask & bit)) return 0;
    if (ev == EP_UP) ptr_mask &= ~bit;
    last_kind = kind;
    fx = (int)((x - ix) * EU_W / iw); fy = (int)((y - iy) * EU_H / ih);   /* the editor's units */
    if (kind != EK_TOUCH) { hov_x = fx; hov_y = fy; }
    if (ev == EP_HOVER && (x < ix || y < iy || x >= ix + iw || y >= iy + ih)) { hov_x = hov_y = -1; return 0; }
    editor_pointer(id, ev, fx, fy, kind);
    return 1;
}
void geom_wheel(int steps) { if (loaded) editor_wheel(steps); }
void geom_char(int ch) { if (loaded) editor_char(ch); }
int  geom_hot(void) { return loaded && hov_x >= 0 && eu_hit_at(hov_x, hov_y) >= 0; }

/* ---- the editor's UI, recorded in frame coordinates (edui.h), drawn crisp over the frame */
static u32 col(u32 rgb, int semi)
{
    rgb &= 0xffffffu;
    if (semi < 0) return 0xff000000u | rgb;
    if (semi == SEMI_SUB) {                   /* darkens by the colour: black, that strong */
        u32 m = ((rgb >> 16 & 255) + (rgb >> 8 & 255) + (rgb & 255)) / 3;
        return ((m * 3 > 255 ? 255 : m * 3) << 24);
    }
    if (semi == SEMI_ADD) return 0xa0000000u | rgb;
    if (semi == SEMI_QADD) return 0x50000000u | rgb;
    return 0x80000000u | rgb;
}
static int sk_of(int cell)
{
    switch (cell) {
    case KIT_PANEL: return SK_PANEL;
    case KIT_BTN: return SK_BUTTON;
    case KIT_BTN_LIT: return SK_BUTTON_LIT;
    case KIT_BTN_HOT: return SK_BUTTON_HOT;
    case KIT_BAR: return SK_BAR;
    case KIT_ROW: return SK_ROW;
    case KIT_FIELD: return SK_FIELD;
    case KIT_SEL: return SK_SELECT;
    default: return SK_PLAQUE;
    }
}
static void hires_ui(void)
{
    static short order[16000];
    float s = iw / EU_W;                          /* dp per UI unit */
    int n = eu_ncmd, i, k, hi = sui_fullres() && eu_w;
    float em = 9.6f * s;                          /* the PS1 font's 7-pixel capitals, as outlines */
#define X(v) (ix + (v) * s)
#define Y(v) (iy + (v) * s)
    if (n > 16000) n = 16000;
    /* the PS1's order: the deepest first; at one depth the last added first (it was prepended) */
    for (i = 0; i < n; i++) order[i] = (short)(n - 1 - i);
    for (i = 1; i < n; i++) {                     /* stable insertion sort, deepest first */
        short t = order[i];
        for (k = i; k > 0 && eu_cmd[order[k - 1]].ot < eu_cmd[t].ot; k--) order[k] = order[k - 1];
        order[k] = t;
    }
    for (i = 0; i < n; i++) {
        const EuCmd *c = &eu_cmd[order[i]];
        switch (c->kind) {
        case EUC_RECT:
            sui_rect(X(c->x[0]), Y(c->y[0]), (c->x[1] - c->x[0]) * s, (c->y[1] - c->y[0]) * s, col(c->c, c->semi));
            break;
        case EUC_RECT_G:
            sui_gradient(X(c->x[0]), Y(c->y[0]), (c->x[1] - c->x[0]) * s, (c->y[1] - c->y[0]) * s, col(c->c, -1), col(c->c2, -1));
            break;
        case EUC_TRI:          /* full res: sector fills are meshes, so no soft rims (no seams) */
            if (hi) sui_tri_flat(X(c->x[0]), Y(c->y[0]), X(c->x[1]), Y(c->y[1]), X(c->x[2]), Y(c->y[2]), col(c->c, c->semi));
            else sui_tri(X(c->x[0]), Y(c->y[0]), X(c->x[1]), Y(c->y[1]), X(c->x[2]), Y(c->y[2]), col(c->c, c->semi));
            break;
        case EUC_QUAD:
            sui_tri_flat(X(c->x[0]), Y(c->y[0]), X(c->x[1]), Y(c->y[1]), X(c->x[2]), Y(c->y[2]), col(c->c, c->semi));
            sui_tri_flat(X(c->x[1]), Y(c->y[1]), X(c->x[3]), Y(c->y[3]), X(c->x[2]), Y(c->y[2]), col(c->c, c->semi));
            break;
        case EUC_DISC:
            sui_circle(X(c->x[0] + 0.5f), Y(c->y[0] + 0.5f), (c->x[1] + 0.5f) * s, col(c->c, c->semi));
            break;
        case EUC_RING: {
            float r = (c->x[1] + 0.5f) * s, t = s < 1.5f ? 1.5f : s;
            sui_rrect_line(X(c->x[0] + 0.5f) - r, Y(c->y[0] + 0.5f) - r, r * 2, r * 2, r, t, col(c->c, -1));
            break;
        }
        case EUC_TEXT:
            if (sui_fullres()) sui_text_base(FONT_SEMIBOLD, em, X(c->x[0]), Y(c->y[0] + 7), eu_hi_text + c->str, col(c->c, -1));
            else sui_text(FONT_REGULAR, 12, X(c->x[0]), Y(c->y[0] + 4) - 5, eu_hi_text + c->str, col(c->c, -1));
            break;
        case EUC_TEXT_C:
            if (sui_fullres()) {
                const char *t = eu_hi_text + c->str;
                sui_text_base(FONT_SEMIBOLD, em, X(c->x[0]) - sui_text_base_w(FONT_SEMIBOLD, em, t) / 2, Y(c->y[0] + 7), t, col(c->c, -1));
            } else sui_text_c(FONT_REGULAR, 12, X(c->x[0]), Y(c->y[0] + 4) - 5, eu_hi_text + c->str, col(c->c, -1));
            break;
        case EUC_KIT:
            sui_nine(sk_of((int)c->c), X(c->x[0]), Y(c->y[0]), (c->x[1] - c->x[0]) * s, (c->y[1] - c->y[0]) * s);
            break;
        case EUC_TEX:
            sui_frame_part(X(c->x[0]), Y(c->y[0]), (c->x[1] - c->x[0]) * s, (c->y[1] - c->y[0]) * s,
                           (float)c->x[2] / SCREEN_W, (float)c->y[2] / SCREEN_H, (float)c->x[3] / SCREEN_W, (float)c->y[3] / SCREEN_H);
            break;
        }
    }
#undef X
#undef Y
}

/* the tab: the editor's picture filling the area under the Studio's bar; or, with no level
   yet, a way to make one */
void geom_tab(float x, float y, float w, float h, int *wants_create)
{
    *wants_create = 0;
    if (!loaded) {
        float cx = x + w / 2;
        iw = ih = 0;
        sui_nine(SK_PANEL, cx - 260, y + 70, 520, 170);
        sui_text_c(FONT_FIRE, 20, cx, y + 92, "This game has no level yet", C_TEXT);
        sui_text_c(FONT_REGULAR, 12, cx, y + 126, "Start from Deadbird's level 1 or from one empty room.", C_MUTED);
        if (sui_button("mk1", cx - 220, y + 160, 210, 40, "Copy level 1", BTN_PRIMARY)) *wants_create = 1;
        if (sui_button("mk0", cx + 10, y + 160, 210, 40, "Empty room", BTN_PLAIN)) *wants_create = 2;
        return;
    }
    /* the frame takes the area's shape (as wide as VRAM allows) */
    want_w = (int)(SCREEN_H * w / h) & ~1;
    if (want_w < 320) want_w = 320;
    if (want_w > SCREEN_W_MAX) want_w = SCREEN_W_MAX;
    if (sui_fullres()) {
        /* full resolution: the editor is laid out on the whole area at 1.5 dp a unit (its UI and
           2D map drawn crisp); only the 3D view is the PS1 frame, stretched over the area */
        eu_w = (int)(w / 1.5f); eu_h = (int)(h / 1.5f);
        if (eu_h < SCREEN_H) { eu_h = SCREEN_H; eu_w = (int)(SCREEN_H * w / h); }
        ix = x; iy = y; iw = w; ih = h;
        if (editor_in_3d()) sui_frame_image(ix, iy, iw, ih);
        else sui_rect(ix, iy, iw, ih, 0xff070304u);
    } else {
        /* square pixels: fills the height, sits in the middle; a wider screen gets dark sides */
        eu_w = eu_h = 0;
        ih = h; iw = h * SCREEN_W / SCREEN_H;
        if (iw > w) { iw = w; ih = w * SCREEN_H / SCREEN_W; }
        ix = x + (w - iw) / 2; iy = y + (h - ih) / 2;
        if (iw < w || ih < h) sui_rect(x, y, w, h, 0xff0a0606u);
        sui_frame_image(ix, iy, iw, ih);
    }
    sui_clip(ix, iy, iw, ih);
    hires_ui();
    sui_unclip();
}
int geom_frame_w(void) { return loaded ? want_w : 0; }
void geom_view(float *x, float *y, float *w, float *h) { *x = ix; *y = iy; *w = iw; *h = ih; }
