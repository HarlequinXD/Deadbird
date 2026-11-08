# Deadbird

A **third-person action game** built on a **true PS1-style engine**. You play Deadbird in a hellish test
level. The engine is written so it can run on a real PlayStation later. For now it ships on
**Android**, using a fast software model of the PS1's graphics chip.

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
  drawn by `game/level.c`). This is where they drain shinigami - what Deadbird is - for power:
  - *Extraction Hall*: the great tank with a captured shinigami hanging in it, energy rings
    rising through the glass, four extraction pods with caged beings, consoles showing their
    failing vitals, a catwalk with stairs, pillars, cable trunks, a glowing floor ring
  - *Reactor*: the soul-reactor core (copper coil, pulsing plasma bands), conduits to the
    walls, turbine generators with spinning fans, control consoles
  - *Control Lab*: wall of monitors, operator consoles, server racks, an autopsy table with a
    shinigami on it, specimen jars of skulls
  - *Containment*: barred cells with captives under red light, crates and soul canisters
  - corridors with pipes, cable trays and floor light strips
- **PS1-style level rendering**: 32 procedural 64x64 sci-fi texture tiles on two 8bpp CLUT
  pages (panels, rivets, hazard stripes, grates, screens, coils, glass, plasma...) painted with
  baked bevels, grime and corner occlusion, quantised with 4x4 ordered dither
  (`docs/lab_page_a.png`, `docs/lab_page_b.png`). About 4,700 textured quads with **baked
  vertex lighting**: 47 coloured lights, shadow rays against every solid object, ambient
  occlusion under props and in corners; the GPU gouraud-shades and dithers it. At run time:
  chunk frustum culling, near-plane + guard-band polygon clipping, distance fog, pulsing
  screens / plasma / alarm lights, additive light halos, rising energy rings, spinning fans.
  Typically 500-1,800 level polygons on screen. Tour: `docs/lab_tour.png`.
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
platform/headless/ PC test build that renders frames to BMP (used by CI)
tools/             asset generators (build_demon.py, build_lab.py + lab_textures.py), viewer, bin2c
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
./platform/headless/deadbird_headless out_dir   # scripted play-through: frames, timing, RAM use
make -C platform/headless gaittest && ./platform/headless/gaittest gait.bmp   # side-on walk/run sheet
```

## Assets

The skull demon is generated by `tools/build_demon.py`: box-modelled mesh, baked lighting,
PS1 dither and an 8bpp TIM. It writes `demon.tim` and `demon_mesh.h`. Then
`python3 tools/bin2c.py assets/demon.tim demon_tim > assets/demon_tim.h` embeds the texture.
