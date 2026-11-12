/*
 * Deadbird engine - PS1 hardware model shared by every platform.
 *
 * Rules that keep this code PS1-portable (MIPS R3000, no FPU, 2 MB RAM, 1 MB VRAM):
 *   - no floating point anywhere in engine/ or game/
 *   - no malloc: every buffer is static and counted against the 2 MB RAM budget
 *   - fixed point: 4096 = 1.0 (same as the GTE), angles 4096 = 360 degrees
 *   - 64-bit maths is only used inside the software GPU (gpu_soft.c), which the
 *     real PS1 replaces with its hardware GPU
 */
#ifndef PSX_H
#define PSX_H

#include <stdint.h>
#include <string.h>

typedef int8_t   s8;
typedef uint8_t  u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;

#ifndef ONE
#define ONE         4096            /* fixed point 1.0 (4.12) */
#endif

/* PS1 memory model */
#define RAM_BUDGET  (2 * 1024 * 1024)
#define SCREEN_H    240
#define SCREEN_W_MAX 512

#ifdef GEN_BAKED
/* the PS1 build: 320x240, double buffered at the left of VRAM */
#define VRAM_W      512             /* 16-bit pixels (game/vram.h maps it) */
#define VRAM_H      512
#define SCREEN_W    320
#define FB_X        0
#else
/* Android / PC: still 240 lines of PS1-sized pixels, but as wide as the screen's shape
   (320..512: a phone's 19.5:9 is 512, a 16:9 monitor 427), so the picture fills the screen
   instead of sitting in 4:3 with black bars. The frame buffers move right of the texture pages
   into the other half of a full PS1-sized VRAM (1024 x 512, 1 MB). Set by the platform. */
#define VRAM_W      1024
#define VRAM_H      512
extern int screen_w;
#define SCREEN_W    screen_w
#define FB_X        512
#endif

/* Pad buttons (same bit layout as a PS1 digital pad, active high here) */
#define PAD_SELECT   (1u << 0)
#define PAD_L3       (1u << 1)       /* stick clicks (an analog pad / an Xbox controller) */
#define PAD_R3       (1u << 2)
#define PAD_START    (1u << 3)
#define PAD_UP       (1u << 4)
#define PAD_RIGHT    (1u << 5)
#define PAD_DOWN     (1u << 6)
#define PAD_LEFT     (1u << 7)
#define PAD_L2       (1u << 8)
#define PAD_R2       (1u << 9)
#define PAD_L1       (1u << 10)
#define PAD_R1       (1u << 11)
#define PAD_TRIANGLE (1u << 12)
#define PAD_CIRCLE   (1u << 13)
#define PAD_CROSS    (1u << 14)
#define PAD_SQUARE   (1u << 15)

/* compile-time check */
#define PSX_STATIC_ASSERT(name, cond) typedef char psx_assert_##name[(cond) ? 1 : -1]

#endif
