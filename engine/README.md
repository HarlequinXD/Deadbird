# The engine

A small, portable PlayStation-style engine in C99. One codebase runs on **Android**, **Windows**,
the **original PlayStation** and a **headless** runner for CI. Deadbird is built on it, and so is
the sample game in `examples/hello`.

It follows the PS1's rules everywhere, so whatever runs on a phone also runs on the console:

- no floating point and no `malloc` in engine or game code; every buffer is static and counted
  against the PS1's 2 MB
- fixed point: 4096 = 1.0, angles 4096 = 360°, the same as the GTE
- primitives go into a depth-sorted ordering table (no Z-buffer), with affine textures,
  4- and 8-bit palettes, semi-transparency modes and dithering
- on PC and Android a software GPU rasterises into an emulated 1 MB VRAM; on the PS1 the same
  ordering table goes to the real GPU
- the picture is always 240 lines tall; on PC and phones it is 320–512 pixels wide to match the
  screen's shape (the PS1 build is 320×240)

## Making a game

A game is one `App` (see `app.h`) named `game_app`, plus whatever `.c` files it needs. The engine
owns the main loop, the frame buffers and all input.

```c
#include "engine.h"
#include "draw2d.h"

static void init(void) { /* upload textures, build models, snd_init(bank, n) */ }

static void tick(void)                      /* fixed 60 Hz */
{
    const Input *in = eng_in();
    if (in->press & PAD_CROSS) { /* ... */ }
}

static void draw(FrameBuf *fb)              /* the OT is cleared; the engine draws it after */
{
    quad_g(fb, 0, 0, SCREEN_W, SCREEN_H, RGB(20, 24, 60), RGB(0, 0, 0), NO_SEMI, OT_LEN - 1);
}

const App game_app = { "MY GAME", init, tick, draw };
```

That builds and runs everywhere. `examples/hello/hello.c` is a complete version: a spinning
gouraud cube, a synthesised sound, a touch button, the floating touch stick and keyboard keys.

The optional parts of `App`:

| field | what it's for |
|---|---|
| `touch_buttons(x, y)` | which pad bits your on-screen buttons give at a point. The engine tracks every finger and holds those bits while a finger stays on a button. |
| `stick_split` | the percentage of the screen width, from the left, where a thumb starts the **floating stick**. It shows up in `Input.stick_*`. 0 means no stick. |
| `touch_look(dx)` | a finger dragging on the right of the screen (camera look) |
| `touch_tap(x, y)` | any finger landing on the screen (menus); `eng_tap(bits)` presses buttons on the next tick |
| `pointer_mode()` + `pointer` / `wheel` / `text` | while `pointer_mode()` returns 1, touches and the mouse come to you as raw pointer events instead (editors, custom UIs), along with the wheel and typed text |
| `stick_as_dpad()` | menus: the left stick also presses the d-pad |
| `keys`, `keys_pointer` | keyboard layouts for Windows (`KeyBind`: key → pad bits or a left-stick direction) |
| `files_dir`, `files_ext` | the user-files folder and file extension that `files.h` lists (saves, levels) |
| `ram_used()` | your static RAM, added to the engine's for the 2 MB check |

### Input

`eng_in()` returns the controls for the current tick. Every platform maps its input onto the
PS1 pad layout, so a game never has to check which device is in use:

- `pad` holds the buttons that are down and `press` holds the ones that went down this tick.
  The buttons are `PAD_CROSS`, `PAD_CIRCLE`, `PAD_SQUARE`, `PAD_TRIANGLE`, `PAD_L1`/`R1`/`L2`/`R2`,
  `PAD_START`, `PAD_SELECT` and the d-pad.
- `lx`, `ly`, `rx`, `ry` are the analog sticks, from -4096 to 4096. Dead zones are already applied.
- `stick_on` and `stick_ox/oy/x/y` describe the floating touch stick.
- `touch_ui` is 1 on touch screens, so you know to draw your on-screen controls.
- `pad_style` is 0 for PlayStation button names and 1 for Xbox or keyboard names.
- `fps` is greater than 0 when the player has turned on the frame-rate readout.

Each platform feeds that `Input` from its own devices:

| device | what the platform passes in |
|---|---|
| Xbox and Bluetooth gamepads, the DualShock | buttons and both sticks |
| Touch screen | raw fingers |
| Keyboard | keys, through your `KeyBind` tables |
| Mouse | pointer events, and the left button as `KEY_MOUSE_L` |

`eng_rumble(0..255)` makes the controller rumble.

### Engine modules

| file | what |
|---|---|
| `psx.h` | types, the PS1 memory model, `SCREEN_W` / `SCREEN_H`, pad bits |
| `gpu.h` | primitives (`POLY`), ordering tables (`FrameBuf`, `ot_add`), VRAM upload, texture pages and CLUTs |
| `gpu_soft.c` | the software PS1 GPU (PC and Android) |
| `gte.[ch]` | GTE maths: `MATRIX`, `SVECTOR`, rotate-translate-perspective, `NCLIP`, `isin` / `icos` / `isqrt` |
| `model.[ch]` | rigid hierarchical models (one GTE matrix per bone) |
| `ik.[ch]` | two-bone IK (legs and arms) |
| `draw2d.[ch]` | untextured 2D: gradient quads and triangles |
| `snd.h`, `snd_mix.c` | sound: `snd_init(bank, n)`, `snd_play`, `snd_loop`. A software mixer on PC and Android, the SPU on the PS1 |
| `files.h` | user files (save, load, list, delete) |
| `app.h`, `engine.[ch]` | the App contract and the runtime: the frame loop, input and touch controls |
| `platform/android` | NativeActivity, GLES2 presentation, OpenSL ES audio, touch, gamepads, files |
| `platform/windows` | Win32 and GDI presentation, XInput, keyboard and mouse, waveOut, files; `tinycrt.c` for MSVC builds with no C runtime |
| `platform/psx` | PSn00bSDK: the GPU packet output (`gpu_psx.c`), the SPU (`snd_psx.c`), the DualShock |
| `platform/headless` | `headless_main.c`, a scripted runner that writes BMP frames for any game, and `files_posix.c` |

The engine's `.c` files include only engine headers. `make -C platform/headless hello_headless`
builds the engine with nothing but the sample game, and CI does that on every push.

## Building a game for each platform

Deadbird's build projects in `platform/` show the pattern. Each one compiles
`engine/platform/<name>`, then `engine/*.c`, then the game's own sources:

- **Android**: `platform/android` is a Gradle project. Its `CMakeLists.txt` lists the sources. Set
  the package name and label in `build.gradle` and `AndroidManifest.xml`.
- **Windows**: `platform/windows/Makefile` (MinGW) or `build_msvc.bat`. The game provides its icon
  in a `.rc` file as resource 1.
- **PS1**: `platform/psx/CMakeLists.txt` (PSn00bSDK). PC and Android generate assets at start-up;
  the PS1 has no FPU, so Deadbird bakes them on the host first (`tools/gen_dump.c`).
- **Headless**: `engine/platform/headless/headless_main.c` + `engine/*.c` + your game. Run it as
  `./game <out dir> [script]`. The script commands are `hold`, `tap`, `stick`, `touch` and `shot`.

For a new game, copy those four folders, change the source lists and names, and keep `engine/`
shared.
