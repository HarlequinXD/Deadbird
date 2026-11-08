/*
 * Deadbird animation data: demon brawler moves authored as full-body IK targets.
 * Space: fighter root, Y down (negative = up), facing +Z, +X = her left side.
 * Rest landmarks: pelvis y -993, shoulders (+-174, -1516, 0), ankles on the floor at y -164.
 * K = keep the stance value.
 *
 * columns: frame | pelvis x y z | pelvis rx ry rz | torso rx ry rz | head rx ry |
 *          L wrist xyz | R wrist xyz | L ankle xyz | R ankle xyz
 * rotations: 4096 = 360 deg. rx < 0 leans forward, ry > 0 turns toward her left.
 */
#include "anim.h"

#define POSE(f, px,py,pz, prx,pry,prz, trx,try_,trz, hrx,hry, lhx,lhy,lhz, rhx,rhy,rhz, lfx,lfy,lfz, rfx,rfy,rfz) \
    { f, { px,py,pz, prx,pry,prz, trx,try_,trz, hrx,hry, lhx,lhy,lhz, rhx,rhy,rhz, lfx,lfy,lfz, rfx,rfy,rfz } }

/* the fighting stance: crouched, left side leading, fists up */
#define STANCE(f) POSE(f, 0,70,0, 0,-110,0, -70,-60,0, -30,150, 150,-1380,330, -140,-1410,250, 125,-164,200, -130,-164,-170)

/* ------------------------------------------------------------------ loops */
static AnimKey k_idle[] = {
    STANCE(0),
    POSE(30, K,92,K, K,-100,K, -80,-50,K, -40,K, 150,-1360,320, -140,-1392,240, K,K,K, K,K,K),
    STANCE(60),
};

/* walk: root moves 16 units/frame; planted feet slide back 256 per half cycle */
static AnimKey k_walk[] = {
    POSE(0,  K,70,K, K,K,K, K,K,K, K,K, K,K,K, K,K,K, 125,-164,200, -130,-164,-300),
    POSE(8,  K,52,K, K,-90,K, K,-70,K, K,K, 150,-1370,345, -140,-1400,235, 125,-164,72,  -130,-262,-172),
    POSE(16, K,70,K, K,K,K, K,K,K, K,K, K,K,K, K,K,K, 125,-164,-56, -130,-164,-44),
    POSE(24, K,52,K, K,-130,K, K,-50,K, K,K, 150,-1390,315, -140,-1420,265, 125,-262,72, -130,-164,-172),
    POSE(32, K,70,K, K,K,K, K,K,K, K,K, K,K,K, K,K,K, 125,-164,200, -130,-164,-300),
};

/* guard: forearms crossed in front of the skull */
#define GUARD(f, pz) POSE(f, 0,110,pz, 0,-60,0, -80,-30,0, -90,90, -30,-1520,330, 40,-1470,350, 125,-164,200, -130,-164,-170)
static AnimKey k_guard[] = { GUARD(0, 0), GUARD(1, 0) };
static AnimKey k_blockstun[] = { GUARD(0, 0), GUARD(3, -70), GUARD(12, 0) };

/* ------------------------------------------------------------------ attacks */
/* light 1: snapping left claw jab */
static AnimKey k_jab[] = {
    STANCE(0),
    POSE(3,  K,K,20, K,-150,K, -90,-120,K, K,K, 120,-1400,450, K,K,K, K,K,K, K,K,K),
    POSE(6,  K,K,40, K,-200,K, -110,-160,K, K,180, 60,-1420,700, -150,-1420,220, K,K,K, K,K,K),
    POSE(9,  K,K,40, K,-190,K, -100,-150,K, K,180, 70,-1410,660, K,K,K, K,K,K, K,K,K),
    STANCE(20),
};

/* light 2: wild right hook */
static AnimKey k_hook[] = {
    STANCE(0),
    POSE(4,  K,K,K, K,-20,K, -60,100,K, K,100, K,K,K, -340,-1420,110, K,K,K, K,K,K),
    POSE(9,  K,80,50, K,-300,K, -110,-220,30, K,250, 200,-1340,240, -30,-1440,660, K,K,K, K,K,K),
    POSE(12, K,80,50, K,-320,K, -100,-240,30, K,250, 200,-1340,240, 80,-1420,600, K,K,K, K,K,K),
    STANCE(24),
};

/* light 3: overhead claw rake, dragging down through the target */
static AnimKey k_rake[] = {
    STANCE(0),
    POSE(6,  K,40,K, K,-60,K, 60,70,K, 80,120, K,K,K, -170,-1920,200, K,K,K, K,K,K),
    POSE(11, K,130,60, K,-220,K, -230,-130,K, -90,200, 220,-1300,200, 0,-1140,640, K,K,K, K,K,K),
    POSE(15, K,140,60, K,-240,K, -250,-150,K, -90,200, 220,-1300,200, 70,-990,480, K,K,K, K,K,K),
    STANCE(30),
};

/* L,L,H finisher: both fists overhead, skull-hammer smash down */
static AnimKey k_hammer[] = {
    STANCE(0),
    POSE(10, K,30,-20, K,-30,K, 130,-20,K, 150,60, 90,-2010,160, -90,-2010,160, K,K,K, K,K,K),
    POSE(16, K,170,70, K,-60,K, -320,-40,K, -150,60, 60,-1060,620, -60,-1060,620, K,K,K, K,K,K),
    POSE(22, K,175,70, K,-60,K, -330,-40,K, -150,60, 60,-1030,610, -60,-1030,610, K,K,K, K,K,K),
    STANCE(44),
};

/* heavy: huge wind-up haymaker */
static AnimKey k_haymaker[] = {
    STANCE(0),
    POSE(10, K,90,-40, K,120,K, 30,300,K, 40,-150, 180,-1360,300, -430,-1490,-130, K,K,K, K,K,K),
    POSE(17, K,90,70, K,-210,K, -150,-330,40, -40,320, 220,-1320,160, 60,-1450,650, K,K,K, K,K,K),
    POSE(21, K,100,80, K,-260,K, -160,-360,40, -40,330, 220,-1320,160, 180,-1380,520, K,K,K, K,K,K),
    STANCE(40),
};

/* H,H: hoof stomp-kick to the gut */
static AnimKey k_stomp[] = {
    STANCE(0),
    POSE(8,  K,40,-30, K,-60,K, 80,-40,K, 30,100, K,K,K, K,K,K, K,K,K, -110,-650,120),
    POSE(14, K,30,-60, K,-40,K, 170,-30,K, 60,90, 220,-1440,260, -200,-1470,200, K,K,K, -80,-760,780),
    POSE(18, K,30,-60, K,-40,K, 170,-30,K, 60,90, 220,-1440,260, -200,-1470,200, K,K,K, -80,-740,760),
    POSE(28, K,60,K, K,K,K, K,K,K, K,K, K,K,K, K,K,K, K,K,K, -130,-210,0),
    STANCE(42),
};

/* forward + H: horn-gore lunge (root also moves forward in fighter.c) */
static AnimKey k_gore[] = {
    STANCE(0),
    POSE(8,  K,190,-30, K,-60,K, -260,-20,K, -160,60, 230,-1250,-150, -230,-1250,-150, K,K,K, K,K,K),
    POSE(16, K,130,130, K,-40,K, -430,-10,K, -260,40, 230,-1280,-40, -230,-1280,-40, 125,-164,330, K,K,K),
    POSE(22, K,130,130, K,-40,K, -440,-10,K, -260,40, 230,-1280,-40, -230,-1280,-40, 125,-164,330, K,K,K),
    STANCE(48),
};

/* ------------------------------------------------------------------ reactions */
static AnimKey k_hit_high[] = {
    STANCE(0),
    POSE(4,  K,90,-70, K,-80,K, 220,-30,K, 210,80, 270,-1350,160, -270,-1350,120, K,K,K, K,K,K),
    STANCE(18),
};

static AnimKey k_hit_body[] = {
    STANCE(0),
    POSE(4,  K,150,-40, K,-90,K, -270,-40,K, -110,120, 120,-1140,260, -120,-1140,260, K,K,K, K,K,K),
    STANCE(20),
};

/* knocked down: thrown on her back (body along -Z, legs toward +Z) */
#define LYING(f, py) POSE(f, 0,py,-350, 1024,0,0, 30,0,0, 40,0, 480,-150,-720, -480,-150,-720, 120,-120,470, -120,-120,480)
static AnimKey k_fall[] = {
    POSE(0,  K,90,-40, K,-80,K, 200,-30,K, 200,80, 270,-1350,160, -270,-1350,120, K,K,K, K,K,K),
    POSE(9,  0,320,-180, 420,0,0, 220,0,0, 120,0, 380,-1250,-200, -380,-1250,-200, 120,-164,230, -120,-200,140),
    LYING(22, 840),
    LYING(26, 830),
};
static AnimKey k_lie[] = { LYING(0, 840), LYING(30, 834), LYING(60, 840) };
static AnimKey k_getup[] = {
    LYING(0, 840),
    POSE(12, 0,600,-200, 300,0,0, -320,0,0, -60,0, 300,-130,-100, -300,-130,-100, 120,-164,260, -120,-164,240),
    POSE(24, 0,300,0, 0,-60,0, -220,-30,0, -60,120, 160,-1250,300, -150,-1280,240, 125,-164,200, -130,-164,-170),
    STANCE(36),
};

/* victory: arms up, roaring skull */
static AnimKey k_win[] = {
    STANCE(0),
    POSE(16, 0,10,0, 0,0,0, 150,0,0, 250,0, 260,-2050,100, -260,-2050,100, 130,-164,60, -130,-164,-60),
    POSE(28, 0,0,0, 0,30,0, 170,30,0, 280,-60, 270,-2070,80, -250,-2040,120, 130,-164,60, -130,-164,-60),
    POSE(40, 0,10,0, 0,0,0, 150,0,0, 250,0, 260,-2050,100, -260,-2050,100, 130,-164,60, -130,-164,-60),
};

/* pre-round taunt: beckoning claw */
static AnimKey k_taunt[] = {
    STANCE(0),
    POSE(14, 0,40,0, 0,-60,0, 60,-40,0, 120,120, 300,-1600,380, -140,-1420,240, K,K,K, K,K,K),
    POSE(22, 0,40,0, 0,-60,0, 60,-40,0, 120,120, 200,-1650,260, -140,-1420,240, K,K,K, K,K,K),
    POSE(30, 0,40,0, 0,-60,0, 60,-40,0, 120,120, 300,-1600,380, -140,-1420,240, K,K,K, K,K,K),
    STANCE(48),
};

#define CLIP(name, arr, len, loop) { name, arr, (u8)(sizeof(arr) / sizeof(arr[0])), len, loop }
Clip clips[CLIP_COUNT] = {
    CLIP("idle", k_idle, 60, 1),
    CLIP("walk", k_walk, 32, 1),
    CLIP("guard", k_guard, 1, 0),
    CLIP("blockstun", k_blockstun, 12, 0),
    CLIP("jab", k_jab, 20, 0),
    CLIP("hook", k_hook, 24, 0),
    CLIP("rake", k_rake, 30, 0),
    CLIP("hammer", k_hammer, 44, 0),
    CLIP("haymaker", k_haymaker, 40, 0),
    CLIP("stomp", k_stomp, 42, 0),
    CLIP("gore", k_gore, 48, 0),
    CLIP("hit_high", k_hit_high, 18, 0),
    CLIP("hit_body", k_hit_body, 20, 0),
    CLIP("fall", k_fall, 26, 0),
    CLIP("lie", k_lie, 60, 1),
    CLIP("getup", k_getup, 36, 0),
    CLIP("win", k_win, 40, 0),
    CLIP("taunt", k_taunt, 48, 0),
};
