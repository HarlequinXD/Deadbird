# Deadbird

A **third-person action game** built on a **true PS1-style engine**. You play Deadbird, a shinigami,
through the plant where her kind are drained for power. The engine is written so it can run on a
real PlayStation later. For now it ships on **Android** and **Windows PC** (with full Xbox
controller support), using a fast software model of the PS1's graphics chip.

## The game

- **Title**: hell sky, embers, DEADBIRD logo and menu (PLAY / CONTROLS) over the live level,
  with Deadbird roaming on autopilot.
- **Movement ported from Slayer from Hell** (`game/player.c`): Quake-style ground physics with
  friction, acceleration toward the stick direction, a stop speed, limited air control, gravity
  and jump. Floating analog stick on the left of the touch screen: half push walks, push past 70%
  (or hold R) runs. Directions are relative to where the camera looks now, so holding the stick
  to one side keeps her curving round as the camera follows. Walls slide you along them (she
  turns and walks along the wall); push straight into one and she just stops (no hit reaction). Ledges you step up are
  smoothed, and landings dip by how hard you hit.
- **Facing, lean and bank** (from Slayer's `Hero.java`): the body turns toward where she is
  actually moving, leans into acceleration (back when braking) and banks into turns.
- **Foot IK, as in Slayer from Hell** (`game/player.c`, two-bone solver in `engine/ik.c`): each
  foot stays planted on the ground while it is in stance and swings through on an arc (toe-off,
  heel strike, heel kick when running) on a gait clock driven by distance, so feet don't slide.
  Every foot finds the floor under itself, so on stairs each foot lands on its own step; the
  pelvis drops just enough for both legs to reach, and the knees bend forward to put the hooves
  on their targets. Turning on the spot makes little shuffle steps; jumping tucks the feet.
- **Keyframed upper body** (`game/fk.c`, clips in `game/fkclips.c`, authored by
  `tools/make_fkclips.py`): torso, head and arms play idle / walk / run clips in step with the
  legs, blended by speed, with air and landing poses on top.
- **The level: a shinigami energy-extraction plant** (`tools/build_lab.py` → `game/lab_data.h`,
  drawn by `game/level.c`; plan in `docs/level_map.png`). Where they drain shinigami - what
  Deadbird is - for power. Ten big rooms with open floors (machines along the walls, room to
  fight) joined by long halls, played start to finish:
  1. *Loading Dock* - arrival bay, cargo containers, loading platform with stairs
  2. *Security Checkpoint* - red scanner arch, guard consoles
  3. *Processing Hall* - processing machines down both walls, overhead pipes
  4. *Extraction Hall* - the great tank with a captured shinigami, four extraction pods,
     a catwalk with stairs (a side hall shortcuts to the Pipe Gallery)
  5. *Containment Block* - barred cells with captives under red light
  6. *Research Lab* - monitor wall, operator desks, autopsy table, skull specimen jars
  7. *Server Vault* - rows of server racks with a cross aisle, cold blue light
  8. *Pipe Gallery* - a 24,000-unit-long gallery of giant pipes
  9. *Turbine Hall* - generators with spinning fans, raised control platform
  10. *Reactor Core* - the soul reactor (copper coil, pulsing plasma), ring of consoles
  The route test walks it end to end: about 35 s sprinting, 1.5 min walking - before enemies.
- **Textures with the detail painted in** (`tools/lab_textures.py`, sheet in
  `docs/lab_textures.png`): 74 procedural 64x64 tiles on five 8bpp CLUT pages at a fixed
  **32 texels per metre** - one tile covers 2 x 2 m. Walls are built in 2 m bands: a base band
  (kickplates, painted pipes and valves, vents, hazard paint), a middle band (panels, posters,
  "AUTHORIZED PERSONNEL ONLY", wall monitors, room stencils like DOCK 07 / CHECKPOINT /
  REACTOR CORE 5, cell warnings, claw marks) and a top band (circuitry, I-beam + cable tray).
  Every doorway gets painted chevron jambs and an EXIT lintel. Each room has its own wall set;
  floors and ceilings have hazard lines, arrows, grates, drains, guide-light strips and painted
  cable trays. No floating decal polygons - details are part of the wall texture. Baked
  bevels, grime and occlusion in the paint; PS1 4x4 ordered dither.
- **PS1-style level rendering**: about 8,300 textured quads (1 m polygons) with **baked
  vertex lighting**: 139 coloured lights, shadow rays against every solid object, ambient
  occlusion under props and in corners; the GPU gouraud-shades and dithers it. At run time:
  only the rooms visible from the camera's room (its halls and the rooms beyond them), chunk
  frustum culling, near-plane + guard-band polygon clipping, distance fog, pulsing
  screens / plasma / alarm lights, additive light halos, rising energy rings, spinning fans.
  Typically 550-1,100 level polygons on screen; level data about 350 KB + 330 KB of textures (VRAM). Tour: `docs/lab_tour.png`.
- **Camera**: third-person follow camera that drifts back behind you as you move; swing it with
  L1/R1 (< >) or by dragging a finger on the right side of the screen. Indoors it pulls in in
  front of walls, machines and the ceiling.

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
game/              Deadbird itself (platform independent): flow, player movement + keyframed animation, level, HUD, fx, title
assets/            demon.tim (256x256 8bpp CLUT), demon_mesh.h (488 tris, 16 parts)
platform/android/  NativeActivity + GLES2 present + touch pad (Gradle project)
platform/windows/  Win32 + GDI present, XInput (Xbox controller) + keyboard
platform/headless/ PC test build that renders frames to BMP (used by CI)
tools/             asset generators (build_demon.py, build_lab.py + lab_textures.py), viewer, bin2c
docs/PS1_PORT.md   plan for the real PS1 build (PSn00bSDK)
```

## Controls

**Xbox controller (Windows)**: left stick move (push far to run) · right stick camera ·
**A** jump / confirm · **B**, **X** or **RT** sprint · **LB / RB** swing camera · **START** pause ·
**BACK** quit to title from pause · D-pad / left stick for menus · rumbles on hard landings.
Any of the four controller slots, plugged in before or after starting.

**Keyboard (Windows)**: WASD / arrows move · SHIFT sprint · SPACE jump · Q / E camera ·
ENTER / ESC pause · F11 or ALT+ENTER fullscreen.

**Touch (Android)** - on-screen PS1 pad:

- **D-pad**: move relative to the camera (menus: up/down; tap a menu row to pick it)
- **Left thumb**: floating analog stick · **R** (○ / □) sprint · **J** (✕) jump · **< >** (L1 / R1) swing camera · **II** (START) pause

## Building

**Android APK**: GitHub Actions builds it on every push (`Actions → build → Deadbird-debug-apk`).
To build locally: `gradle -p platform/android assembleDebug` with the Android SDK + NDK installed.

**Windows EXE**: GitHub Actions builds it on every push (`Actions → build → Deadbird-windows`,
a single `Deadbird.exe`, no installer or DLLs needed). To build yourself: MinGW-w64
(`make -C platform/windows`, natively in MSYS2 or cross-compiled from Linux) or Visual Studio
(`platform/windows/build_msvc.bat` from an x64 Native Tools prompt).

**PC headless test**:
```
make -C platform/headless
./platform/headless/deadbird_headless out_dir   # scripted play-through: frames, timing, RAM use
make -C platform/headless gaittest && ./platform/headless/gaittest gait.bmp   # side-on walk/run sheet
```

## Assets

The skull demon is generated by `tools/build_demon.py`: box-modelled mesh, baked lighting,
PS1 dither and an 8bpp TIM. It writes `demon.tim` and `demon_mesh.h`. Then
`python3 tools/bin2c.py assets/demon.tim demon_tim > assets/demon_tim.h` embeds the texture.
