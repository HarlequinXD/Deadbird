/*
 * The Studio's Geometry tab: Deadbird's own level editor (game/editor.c), embedded. The same
 * tools, d-pad cursor, tool rail, inspector, 3D view and controls as the editor in the game, on
 * the project's level file (<project>.map). It draws into the engine's frame (the PS1-style
 * 320..512 x 240 picture), which the tab shows scaled up; touches on it go to the editor.
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

static char mapfile[32];
static int loaded, last_kind = EK_TOUCH;          /* the Studio is touch first; a mouse switches it */
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
    ed_quiet = 1;
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
int geom_tick(void)
{
    const Input *in = eng_in();
    int r;
    if (!loaded) return GEOM_STAY;
    editor_style(in->pad_style, touch_now());
    r = editor_update(in->pad, in->press, in->lx, in->ly, in->rx, in->ry);
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
    fx = (int)((x - ix) * SCREEN_W / iw); fy = (int)((y - iy) * SCREEN_H / ih);
    editor_pointer(id, ev, fx, fy, kind);
    return 1;
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
    /* square pixels: the frame takes the area's shape (as wide as VRAM allows), fills its
       height, and sits in the middle; a screen wider than that gets thin dark sides */
    want_w = (int)(SCREEN_H * w / h) & ~1;
    if (want_w < 320) want_w = 320;
    if (want_w > SCREEN_W_MAX) want_w = SCREEN_W_MAX;
    ih = h; iw = h * SCREEN_W / SCREEN_H;
    if (iw > w) { iw = w; ih = w * SCREEN_H / SCREEN_W; }
    ix = x + (w - iw) / 2; iy = y + (h - ih) / 2;
    if (iw < w || ih < h) sui_rect(x, y, w, h, 0xff0a0606u);
    sui_frame_image(ix, iy, iw, ih);
}
int geom_frame_w(void) { return loaded ? want_w : 0; }
void geom_view(float *x, float *y, float *w, float *h) { *x = ix; *y = iy; *w = iw; *h = ih; }
