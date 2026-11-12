/*
 * Deadbird Studio, driven headless: a Galaxy S23-sized screen, scripted taps and drags, pixel-art
 * screenshots (rendered by hr_soft, shown 2x), and checks on the project it edits.
 *   ENGINE_FILES=<dir> studiotest <out dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"
#include "hr_soft.h"
#include "gpu.h"
#include "snd.h"
#include "doc.h"
#include "edmap.h"
#include "geom.h"
#include "edui.h"
#include "editor.h"

Doc *studio_doc(void);
int  studio_screen(void);
int  studio_play_ok(void);
void studio_tab(int t);
int  studio_tab_now(void);
extern int screen_w;
extern EmMap ED;
extern s32 ed_cx, ed_cz;

#define SW 2340
#define SH 1080
#define DENS 263                          /* 420 dpi */
static const char *out;
static const HrList *last;
static int fails;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; }

static void frame(void)
{
    static s16 mix[736];
    eng_frame(0);
    snd_mix(mix, 368);
    last = game_app.hires_frame(SW, SH, DENS);
}
static void run(int n) { while (n-- > 0) frame(); }
static float PS(void) { return DENS / 100.0f; }
static void ptr(int ev, float x, float y) { game_app.hires_pointer(0, ev, (int)(x * PS()), (int)(y * PS()), 0); }
static void tap(float x, float y) { ptr(EV_DOWN, x, y); frame(); ptr(EV_UP, x, y); frame(); frame(); }
static void drag(float x0, float y0, float x1, float y1)
{
    int i;
    ptr(EV_DOWN, x0, y0); frame();
    for (i = 1; i <= 10; i++) { ptr(EV_MOVE, x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10); frame(); }
    ptr(EV_UP, x1, y1); frame(); frame();
}

static void shot(const char *name)
{
    static u32 px[1024 * 512];
    static unsigned char row[2048 * 3 + 4];
    unsigned char h[54] = { 'B', 'M' };
    char path[512];
    int vw = last->vw, vh = last->vh, W2 = vw * 2, H2 = vh * 2, pitch = (W2 * 3 + 3) & ~3, size = 54 + pitch * H2, x, y, dx, dy;
    HrFrameSrc fr;
    FILE *f;
    eng_display_area(&dx, &dy);
    fr.vram = gpu_vram(); fr.stride = VRAM_W; fr.x = dx; fr.y = dy; fr.w = SCREEN_W; fr.h = SCREEN_H;
    hr_soft_draw(last, px, vw, vh, &fr);
    snprintf(path, sizeof(path), "%s/%s.bmp", out, name);
    if (!(f = fopen(path, "wb"))) return;
    h[2] = size & 255; h[3] = (size >> 8) & 255; h[4] = (size >> 16) & 255; h[10] = 54; h[14] = 40;
    h[18] = W2 & 255; h[19] = W2 >> 8; h[22] = H2 & 255; h[23] = H2 >> 8; h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (y = H2 - 1; y >= 0; y--) {
        for (x = 0; x < W2; x++) {
            u32 c = px[(y / 2) * vw + x / 2];
            row[x * 3] = (unsigned char)(c >> 16); row[x * 3 + 1] = (unsigned char)(c >> 8); row[x * 3 + 2] = (unsigned char)c;
        }
        fwrite(row, 1, (size_t)pitch, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    Doc *d;
    int n0, i;
    out = argc > 1 ? argv[1] : ".";
    screen_w = 512;                      /* a 19.5:9 phone, as Android sets it */
    eng_init();
    run(2);
    printf("canvas %d x %d for a %d x %d screen\n", last->vw, last->vh, SW, SH);
    shot("st_01_projects");
    /* a new game from the first-person template */
    {
        float cw = (last->vw - 86 - 22 - 30) / 4.0f;
        tap(86 + cw / 2, 130);
    }
    d = studio_doc();
    expect(studio_screen() == 1, "a template opens the editor");
    expect(d->nfsms >= 2 && d->ntypes >= 3, "the template's machines and types are there");
    run(2);
    shot("st_02_behaviour");
    /* select the chase state */
    i = 0;
    {
        DocFsm *m = &d->fsms[0];
        int k = doc_state_index(m, "chase");
        float x = (m->s[k].x + 20) + 40, y = 45 + (m->s[k].y + 20) + 20;
        tap(x, y);
        run(1);
        shot("st_03_state");
        n0 = m->s[k].na;
        /* + Add action (under the three every-tick rows), the Move group, its first action */
        tap(last->vw - 150, 275);
        run(1);
        shot("st_03b_library");
        tap(60 + 60, 30 + 52 + 36 + 16);
        run(1);
        tap(last->vw / 2, 30 + 52 + 20);
        run(1);
        expect(m->s[k].na == n0 + 1, "the action library adds an action to the state");
        printf("      added %s\n", m->s[k].na > n0 ? m->s[k].a[m->s[k].na - 1].action : "-");
        shot("st_03c_added");
    }
    printf("states in imp_ai: %d\n", d->fsms[0].ns);
    /* drag a link from idle's handle to claw */
    {
        DocFsm *m = &d->fsms[0];
        int a = doc_state_index(m, "idle"), b = doc_state_index(m, "claw"), before = m->s[a].nt;
        float hx = (m->s[a].x + 20) + 128, hy = 45 + (m->s[a].y + 20) + 25;
        float tx = (m->s[b].x + 20) + 50, ty = 45 + (m->s[b].y + 20) + 25;
        drag(hx, hy, tx, ty);
        run(1);
        shot("st_04_event_picker");
        /* the picker's first event chip */
        tap(last->vw / 2 - 210 + 14 + 47, last->vh / 2 - 130 + 48 + 15);
        run(1);
        expect(m->s[a].nt == before + 1, "dragging from a handle to a state adds a transition");
        shot("st_05_linked");
    }
    /* the scene tab */
    tap(52 + 7 * 15 + 30 + 42, 22);
    run(1);
    shot("st_06_scene");
    (void)i;
    /* play-test */
    tap(last->vw - 46, 22);
    run(90);
    expect(studio_screen() == 2, "Play starts a play-test");
    expect(studio_play_ok(), "the edited project (saved as text) loads in the runtime");
    shot("st_07_play");
    tap(44, 20);
    run(2);
    expect(studio_screen() == 1, "Stop goes back to the editor");

    /* the Geometry tab: the project's own level (a copy of level 1) */
    studio_tab(1);
    run(3);
    shot("st_08_geometry");
    {   /* Deadbird's own level editor, on the project's level: the d-pad, the tool rail, X */
        float ix, iy, iw, ih;
        run(2);
        geom_view(&ix, &iy, &iw, &ih);                       /* canvas units, as the pointers are */
        printf("      frame %dx%d shown %.0fx%.0f at %.0f,%.0f (pixel aspect %.3f)\n", SCREEN_W, SCREEN_H, iw, ih, ix, iy, (iw / SCREEN_W) / (ih / SCREEN_H));
        expect(fabsf((iw / SCREEN_W) / (ih / SCREEN_H) - 1) < 0.01f, "the level editor's pixels are square");
#define FX(x) (ix + (x) * iw / SCREEN_W)
#define FY(y) (iy + (y) * ih / SCREEN_H)
        int ns = ED.nsecs, k;
        s32 cx0;
        expect(ns > 10, "the Geometry tab opens Deadbird's level editor on the project's level");
        tap(FX(30), FY(61));                                  /* the tool rail: RECT */
        cx0 = ed_cx;
        ptr(EV_DOWN, FX(60 + 32), FY(SCREEN_H - 34 - 17));    /* hold the d-pad's up arrow: off the lift, into the bay */
        run(40);
        ptr(EV_UP, FX(60 + 32), FY(SCREEN_H - 34 - 17)); frame();
        tap(FX(SCREEN_W - 38), FY(44));                       /* X: one corner */
        ptr(EV_DOWN, FX(60 + 32 - 17), FY(SCREEN_H - 34));    /* left */
        run(30);
        ptr(EV_UP, FX(60 + 32 - 17), FY(SCREEN_H - 34)); frame();
        expect(ed_cx < cx0, "the d-pad moves the cursor");
        ptr(EV_DOWN, FX(60 + 32), FY(SCREEN_H - 34 + 17));    /* down */
        run(15);
        ptr(EV_UP, FX(60 + 32), FY(SCREEN_H - 34 + 17)); frame();
        shot("st_09_rect");
        tap(FX(SCREEN_W - 38), FY(44));                       /* X: the other corner */
        run(2);
        for (k = 0; k < 3 && ED.nsecs == ns; k++) run(1);
        printf("      sectors %d -> %d, cursor %d,%d\n", ns, ED.nsecs, (int)ed_cx, (int)ed_cz);
        expect(ED.nsecs > ns, "RECT and X twice makes a sector, as in the game's editor");
        shot("st_10_after");
        tap(FX(314), FY(9)); run(3);                          /* MENU: the editor's panels in the Studio's look */
        shot("st_11_menu");
        expect(eu_ncmd > 40, "the editor's UI is drawn in high resolution");
        ptr(EV_DOWN, FX(SCREEN_W - 200), FY(180)); frame();   /* drag the menu's list up: later rows come in */
        for (k = 1; k <= 8; k++) { ptr(EV_MOVE, FX(SCREEN_W - 200), FY(180 - k * 12)); frame(); }
        ptr(EV_UP, FX(SCREEN_W - 200), FY(84)); run(2);
        printf("      menu scrolled to row %d\n", editor_panel_scroll());
        expect(editor_panel_scroll() > 0, "a drag scrolls the menu (DRAG FOR MORE)");
        shot("st_11b_menu_dragged");
        game_app.hires_back(); run(2);                        /* back closes the menu, the tab stays */
        expect(studio_tab_now() == 1, "back closes the editor's menu first");
        tap(FX(SCREEN_W - 38), FY(80)); run(3);               /* INSPECT */
        shot("st_12_inspect");
        game_app.hires_back(); run(2);
        tap(FX(351), FY(9)); run(20);                         /* 3D */
        shot("st_13_3d");
        for (k = 0; k < 4 && studio_tab_now() == 1; k++) { game_app.hires_back(); run(2); }
        expect(studio_tab_now() == 0, "back leaves the level editor for the Studio's tabs");
#undef FX
#undef FY
    }
    /* play-test the edited level */
    tap(last->vw - 46, 22);
    run(60);
    expect(studio_screen() == 2 && studio_play_ok(), "the project plays on its own level");
    shot("st_14_play_level");
    tap(44, 20);
    run(2);
    printf(fails ? "studiotest: %d FAILURES\n" : "studiotest: all passed\n", fails);
    return fails ? 1 : 0;
}
