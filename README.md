# Deadbird

A **third-person action game** built on a **true PS1-style engine**. You play Deadbird in a hellish test
level. The engine is written so it can run on a real PlayStation later. For now it ships on
**Android**, using a fast software model of the PS1's graphics chip.

## The game

- **Title**: hell sky, embers, DEADBIRD logo and menu (PLAY / CONTROLS) over the live level,
  with Deadbird roaming on autopilot.
- **Camera-relative controls** (MediEvil-style) with a **floating analog stick** on the left of
  the touch screen: push a little to walk, push far to run (speed scales smoothly in between);
  R sprints, J jumps. The direction you hold is locked to the camera angle when you started
  pushing, so the follow camera (which drifts back behind her, or swings with L1/R1 / < >)
  never steers you. She turns fast, and skids round when you reverse.
- **Test level**: floor with lava seams, boundary walls, a low platform, stairs to a high
  platform, a ramp, pillars and a wall to run into, and skull totems.
- **Authored walk / run / idle cycles** (`game/lococlips.c`): pelvis bob and twist, torso
  drive and lean, head, arm swing (walk) and bent-arm pump (run), played through the IK rig.
  The cycles are driven by a **gait phase that advances with distance travelled**, and walk
  and run blend by speed.
- **Full-body IK locomotion** (`game/player.c`, rig in `game/rig.c`, solver in `engine/ik.c`):
  - each foot is **locked to the ground in world space** while planted; a foot only steps when
    the body has moved or turned away from it, so **feet never slide** (the headless test measures
    planted-foot drift every frame and fails CI if it is not 0)
  - stride length, cadence and stance time come from the **actual velocity**; running has a
    flight phase, heel kick and knee drive; standing / turning on the spot uses a step planner
  - **foot IK** onto stairs, platforms and the ramp: each foot samples the ground; the pelvis drops so both feet reach
  - **lean** into speed and acceleration (back when braking), **bank** into turns, hip/chest
    counter-twist with the stride, arms counter-swing (pump when running), gait bob
  - **jump** with tucked legs and a landing dip; **bump reaction** (head snaps back, arms brace,
    recoil off the wall, camera shake) when you walk into something
- **Camera**: third-person follow camera.

## What "true PS1" means here

| PS1 hardware | Deadbird engine |
|---|---|
| MIPS R3000, **no floating point** | engine + game are pure fixed-point C (4096 = 1.0, angles 4096 = 360°) |
| **2 MB main RAM** | no `malloc`; all buffers are static, compile-time checked against a 2 MB budget (currently ~550 KB) |
| **1 MB VRAM** (1024×512, 16-bit) | emulated 1 MB VRAM; framebuffers, textures and palettes (CLUTs) live in it exactly where a PS1 would put them |
| GTE geometry coprocessor | `engine/gte.*`: `MATRIX`/`SVECTOR`, rotate-translate-perspective, `NCLIP` backface test |
| GPU + ordering tables | `engine/gpu.h`: primitives linked into a depth-sorted ordering table (OT), drawn far → near, no Z-buffer |
| GPU rasteriser | `engine/gpu_soft.c`: integer vertices (wobble), **affine** texturing, 4/8/15-bit textures, palette lookup, texel 0 = transparent, texture × colour / 128, 15-bit output, PS1 4×4 dither, 4 semi-transparency modes |
| TIM textures | `engine/tim.*` loads `.tim` files straight into VRAM |
| 320×240 display | double-buffered at VRAM (0,0)/(0,240); Android scales it up with nearest filtering, 4:3 letterbox |

A full game frame (level, player, HUD) takes **~1.3 ms** on a PC, so phones have lots of headroom.

## Layout

```
engine/            portable PS1 engine (C99, fixed point, no malloc)
  psx.h            types, PS1 memory constants, pad bits
  gte.[ch]         GTE-style matrix/vector maths + perspective
  gpu.h            primitives, ordering table, VRAM/GPU API
  gpu_soft.c       software PS1 GPU (Android/PC only; PS1 uses the real GPU)
  tim.[ch]         TIM loader -> VRAM
  model.[ch]       rigid hierarchical models (one GTE matrix per body part)
  ik.[ch]          fixed-point two-bone IK
game/              Deadbird itself (platform independent): flow, player + IK locomotion, rig, level, HUD, fx, title
assets/            demon.tim (256x256 8bpp CLUT), demon_mesh.h (488 tris, 16 parts)
platform/android/  NativeActivity + GLES2 present + touch pad (Gradle project)
platform/headless/ PC test build that renders frames to BMP (used by CI)
tools/             asset generator (build_demon.py), viewer, bin2c
docs/PS1_PORT.md   plan for the real PS1 build (PSn00bSDK)
```

## Controls

On-screen PS1 pad (Android) or a PS1 pad:

- **D-pad**: move relative to the camera (menus: up/down; tap a menu row to pick it)
- **Left thumb**: floating analog stick · **R** (○ / □) sprint · **J** (✕) jump · **< >** (L1 / R1) swing camera · **II** (START) pause

## Building

**Android APK**: GitHub Actions builds it on every push (`Actions → build → Deadbird-debug-apk`).
To build locally: `gradle -p platform/android assembleDebug` with the Android SDK + NDK installed.

**PC headless test**:
```
make -C platform/headless
./platform/headless/deadbird_headless out_dir   # scripted play-through: frames, timing, RAM use, foot-slide check
make -C platform/headless gaittest && ./platform/headless/gaittest gait.bmp   # side-on walk/run sheet
```

## Assets

The skull demon is generated by `tools/build_demon.py`: box-modelled mesh, baked lighting,
PS1 dither and an 8bpp TIM. It writes `demon.tim` and `demon_mesh.h`. Then
`python3 tools/bin2c.py assets/demon.tim demon_tim > assets/demon_tim.h` embeds the texture.
