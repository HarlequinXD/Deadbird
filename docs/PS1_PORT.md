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

## Next steps

1. Add `platform/psx/` with a PSn00bSDK `CMakeLists.txt` and `main.c` (display/draw environments, pad, VSync loop).
2. Write `gpu_psx.c`: translate `POLY` (flags `PF_QUAD|PF_TEX|PF_GOURAUD|PF_SEMI`) into packed libgpu primitives in the packet buffer.
3. Optionally rewrite `model.c`'s vertex loop with GTE inline macros (`gte_ldv0`, `gte_rtps`, `gte_stsxy`) and keep the scratch arrays in the 1 KB scratchpad.
4. Test in DuckStation or PCSX-Redux, then on hardware.
