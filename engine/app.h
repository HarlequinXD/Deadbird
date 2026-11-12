/*
 * The contract between the engine and a game.
 *
 * A game is a set of callbacks in one `App` named `game_app`. The engine owns the main loop,
 * the frame buffers and all input; every platform (engine/platform/...) drives the same engine,
 * so a game written against this header runs on Android, Windows, the PS1 and the headless
 * test runner without platform code of its own.
 *
 *   init   once, after the GPU and VRAM are up: upload textures, build models, snd_init(...)
 *   tick   one fixed 60 Hz step of game logic; read the controls with eng_in()
 *   draw   build this frame's display list into fb (the engine clears the OT before and draws
 *          and presents it after); tick has already run for this frame
 *
 * Everything else is optional: leave it NULL / 0.
 */
#ifndef APP_H
#define APP_H

#include "gpu.h"
#include "hires.h"

/* pointer events (touch, mouse) in frame pixels (SCREEN_W x SCREEN_H) */
enum { EV_DOWN, EV_MOVE, EV_UP, EV_HOVER };
enum { PK_TOUCH, PK_MOUSE_L, PK_MOUSE_R };

/* keyboard -> pad. Keys are upper-case ASCII ('A'..'Z', '0'..'9', ' ', '-', '+') or KEY_*. */
enum {
    KEY_ENTER = 13, KEY_ESC = 27, KEY_BACKSPACE = 8, KEY_TAB = 9,
    KEY_UP = 128, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SHIFT, KEY_CTRL, KEY_PGUP, KEY_PGDN,
    KEY_F1, KEY_F10, KEY_MOUSE_L, KEY_COUNT = 160
};
enum { KB_LX_POS = 1, KB_LX_NEG, KB_LY_POS, KB_LY_NEG };   /* a key that pushes the left stick */
typedef struct {
    u8  key;
    u8  axis;          /* KB_* or 0 */
    u8  no_ctrl;       /* ignored while CTRL is held (so CTRL+Z is not also Z) */
    u32 bits;          /* PAD_* pressed while the key is down */
} KeyBind;

typedef struct App {
    const char *name;                    /* window title, log tag */

    void (*init)(void);
    void (*tick)(void);
    void (*draw)(FrameBuf *fb);

    /* ---- optional ---- */
    /* touch controls (when eng touch UI is on): pad bits of the on-screen buttons under a point,
       a finger landing anywhere (menus), and a finger dragged on the look half of the screen */
    u32  (*touch_buttons)(int x, int y);
    void (*touch_tap)(int x, int y);
    void (*touch_look)(int dx);
    int  stick_split;                    /* % of the screen width that starts the floating stick
                                            (0: no touch stick); right of it is the look area */
    /* raw pointer mode: while it returns 1, touches and the mouse go to pointer() instead of
       the touch controls (editors, custom UIs); wheel and typed text come along */
    int  (*pointer_mode)(void);
    void (*pointer)(int id, int ev, int x, int y, int kind);
    void (*wheel)(int steps);
    void (*text)(int ch);                /* 8 backspace, 13 enter, 27 esc, 26 ctrl-Z, 25 ctrl-Y, 19 ctrl-S */
    /* menus: 1 while the left stick should also press the d-pad */
    int  (*stick_as_dpad)(void);
    /* keyboard layouts (Windows): normal and while pointer_mode() is on; end with key 0 */
    const KeyBind *keys, *keys_pointer;
    /* user files (Windows next to the exe, Android in the app's files folder, headless ./):
       folder name and the extension listed by plat_file_list */
    const char *files_dir, *files_ext;
    /* static RAM the game uses, checked against the PS1's 2 MB */
    int  (*ram_used)(void);
    /* full-resolution UI (editors, tools; not on the PS1): while hires() returns 1 the platform
       shows the HrList hires_frame builds for its screen size (density: pixels per 160 dpi,
       x 100) instead of the scaled PS1 frame, and sends pointers / the back key in screen pixels */
    int  (*hires)(void);
    const HrList *(*hires_frame)(int w, int h, int density);
    void (*hires_pointer)(int id, int ev, int x, int y, int kind);
    void (*hires_back)(void);
} App;

extern const App game_app;               /* every game defines exactly one */

#endif
