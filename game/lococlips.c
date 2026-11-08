/*
 * Authored locomotion cycles (body + arms), played back by the gait phase.
 * Clip time 0..64 = one full gait cycle (two steps). Left foot touches down at 0, right at 32.
 * Feet are NOT keyed here: planted feet are locked in the world and swinging feet follow
 * the swing profile in player.c, so the cycle can never slide. Hand Y is relative to the
 * pelvis (the pelvis bob is added on top).
 *
 * columns: frame | pelvis x y z | pelvis rx ry rz | torso rx ry rz | head rx ry |
 *          L wrist xyz | R wrist xyz | (feet unused)
 * pelvis ry < 0 brings the left hip forward; rz > 0 tilts toward her left.
 */
#include "anim.h"

#define P(f, py, pry, prz, trx, try_, hrx, lx, ly, lz, rx, ry, rz) \
    { f, { 0, py, 0,  0, pry, prz,  trx, try_, 0,  hrx, 0,  lx, ly, lz,  rx, ry, rz,  0, 0, 0,  0, 0, 0 } }

/* walk: heel strike low, rising over the planted leg (highest at mid-stance), hips swing with
   the leading leg, chest counter-rotates, arms hang and swing opposite the legs */
static const AnimKey k_walk[] = {
    /*      py  pry  prz  trx  try  hrx    L wrist            R wrist         */
    P( 0,   72, -70,  20, -40,  56,  20,   228, -990, -130,  -222,-1030,  190),   /* L heel strike */
    P( 8,   50, -36,  34, -46,  28,  24,   232,-1000,  -60,  -226,-1015,  120),
    P(16,   30,   0,  40, -52,   0,  28,   235,-1010,   40,  -235,-1010,   40),   /* L mid-stance (high) */
    P(24,   52,  36,  22, -46, -28,  24,   226,-1015,  120,  -232,-1000,  -60),
    P(32,   72,  70, -20, -40, -56,  20,   222,-1030,  190,  -228, -990, -130),   /* R heel strike */
    P(40,   50,  36, -34, -46, -28,  24,   226,-1015,  120,  -232,-1000,  -60),
    P(48,   30,   0, -40, -52,   0,  28,   235,-1010,   40,  -235,-1010,   40),   /* R mid-stance */
    P(56,   52, -36, -22, -46,  28,  24,   232,-1000,  -60,  -226,-1015,  120),
    P(64,   72, -70,  20, -40,  56,  20,   228, -990, -130,  -222,-1030,  190),
};

/* run: strong forward lean, compression on landing, flight between steps, big hip/chest
   counter-twist, elbows bent ~90 deg with fists pumping from hip to chin */
static const AnimKey k_run[] = {
    /*      py   pry  prz   trx  try  hrx   L wrist            R wrist         */
    P( 0,  120, -120,  30, -230,  95, 140,  176,-1190, -170,  -140,-1450,  330),   /* L lands, R fist forward */
    P( 6,  150,  -70,  44, -250,  55, 150,  178,-1220,  -90,  -150,-1420,  260),   /* compress */
    P(12,  110,    0,  36, -240,   0, 145,  170,-1300,   60,  -170,-1300,   60),   /* push off */
    P(18,   40,   70,  10, -220, -55, 135,  155,-1400,  250,  -178,-1215, -110),   /* flight */
    P(24,   60,  110, -10, -225, -90, 135,  145,-1440,  320,  -176,-1195, -165),
    P(32,  120,  120, -30, -230, -95, 140,  140,-1450,  330,  -176,-1190, -170),   /* R lands, L fist forward */
    P(38,  150,   70, -44, -250, -55, 150,  150,-1420,  260,  -178,-1220,  -90),
    P(44,  110,    0, -36, -240,   0, 145,  170,-1300,   60,  -170,-1300,   60),
    P(50,   40,  -70, -10, -220,  55, 135,  178,-1215, -110,  -155,-1400,  250),
    P(56,   60, -110,  10, -225,  90, 135,  176,-1195, -165,  -145,-1440,  320),
    P(64,  120, -120,  30, -230,  95, 140,  176,-1190, -170,  -140,-1450,  330),
};

/* idle: weight settles, slow breathing, arms hang loose */
static const AnimKey k_idle[] = {
    P( 0,  55,   0,   0, -30,   0,  10,   235,-1000,  40,  -235,-1000,  40),
    P(32,  70,   0,   6, -38,   0,  16,   238, -988,  48,  -232,-1006,  34),
    P(64,  55,   0,   0, -30,   0,  10,   235,-1000,  40,  -235,-1000,  40),
};

const Clip clip_walk = { "walk", k_walk, (u8)(sizeof(k_walk) / sizeof(k_walk[0])), 64, 1 };
const Clip clip_run  = { "run",  k_run,  (u8)(sizeof(k_run)  / sizeof(k_run[0])),  64, 1 };
const Clip clip_idle = { "idle", k_idle, (u8)(sizeof(k_idle) / sizeof(k_idle[0])), 64, 1 };
