# Porting Deadbird to a real PS1

## Language: C, compiled to MIPS

PS1 games were written in C (Sony's Psy-Q SDK) and compiled to the console's MIPS R3000
CPU, with hand-written assembly only for a few hot loops. Deadbird follows the same
approach. The engine and game are plain C99 that a MIPS GCC toolchain compiles directly.
The open-source **PSn00bSDK** (`mipsel-none-elf-gcc`) is the planned toolchain.

The code already respects the console's limits:

- **No floats.** All maths is 32-bit fixed point (4.12), like the GTE.
- **No `malloc`.** Every buffer is static and checked against the 2 MB RAM budget at compile time.
- **Hardware layouts.** Texture pages, CLUT and ordering tables use the PS1's own encodings (`get_tpage`, `get_clut`).
- **Big data stays out of the stack.** Models and textures are `const` data, ready for ROM/CD loading.
- **64-bit maths only in the software GPU** (`gpu_soft.c`), which the PS1 build doesn't use.

## What changes on PS1

| Engine piece | Android/PC | PS1 build |
|---|---|---|
| `gte.c` | C fixed point | same C, or swap in GTE macros (`gte_rtps`, `gte_nclip`) for speed |
| `gpu_soft.c` | software rasteriser into emulated VRAM | **removed**: replaced by `gpu_psx.c`, which converts `POLY` into libgpu packets (`POLY_FT3`/`POLY_FT4`/`POLY_G4`) and calls `DrawOTag` |
| `tim.c` | copies into emulated VRAM | `LoadImage` into real VRAM (same coordinates) |
| VRAM layout | framebuffers (0,0)/(0,240), demon texture (640,0), CLUT (0,480) | identical |
| `platform/android` | NativeActivity, GLES2, touch | `platform/psx/main.c`: `ResetGraph`, `PutDispEnv`, pad via `InitPAD`, `VSync` |

`game/` and `engine/model.c` stay unchanged: they only build primitives and link them into the OT.

## Status

Done:

1. `platform/psx/`: PSn00bSDK `CMakeLists.txt` + presets, `main.c` (display/draw environments,
   BIOS pad with DualShock analog sticks, VSync loop), `iso.xml` + `system.cnf` for the CD image.
2. `gpu_psx.c`: translates each engine `POLY` into a libgpu packet (`POLY_GT4/GT3/FT4/FT3/G4/G3/F4/F3`,
   plus a `DR_TPAGE` for untextured semi-transparent prims), links them in OT order and calls `DrawOTag`.
3. Frame pacing: the game logic stays at 60 ticks per second. If a frame takes longer than one
   vblank, `main.c` runs extra `game_step` ticks (up to 3) so the game keeps real-time speed at a
   lower frame rate.
4. CI builds `deadbird.exe`, `deadbird.bin` and `deadbird.cue` (`Actions → build → Deadbird-ps1`).

Still to do:

- Test in DuckStation / PCSX-Redux, then on hardware.
- Speed, done so far: level and character vertices go through the real GTE (`engine/gte_hw.h`,
  `PSX_GTE`); the GPU draws one frame while the CPU builds the next; no 64-bit maths or divides in
  the per-face loop; PS1 fog is 7-14.5 m. SELECT shows the frame rate. Next if needed: keep
  per-chunk vertex buffers in the 1 KB scratchpad, and write GPU packets directly instead of
  converting engine `POLY`s.
- The level and textures are linked into the EXE (~0.9 MB). Streaming them from CD would free RAM.
