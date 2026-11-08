#!/usr/bin/env python3
"""Authors Deadbird's keyframed (FK, joint-angle) animation clips and writes game/fkclips.c.
Angles in degrees. Cycles are 64 frames: left foot contacts at 0, right at 32. The right
leg / arm is the left one shifted half a cycle, so the gait is always symmetric."""
import os

CH = ["PY","PRX","PRY","PRZ","TRX","TRY","TRZ","HRX","HRY",
      "LSD","LSS","LEL","RSD","RSS","REL",
      "LHP","LKN","LAN","LAB","RHP","RKN","RAN","RAB"]

def interp(curve, f, length):
    """curve: list of (frame, value) covering 0..length, looping"""
    pts = sorted(curve)
    if pts[-1][0] != length: pts = pts + [(length, pts[0][1])]
    f %= length
    for (a, va), (b, vb) in zip(pts, pts[1:]):
        if a <= f <= b:
            return va + (vb - va) * (f - a) / (b - a) if b > a else va
    return pts[0][1]

def cycle(name, length, base, leg, arm, body, frames, contact, liftoff):
    keys = []
    half = length // 2
    for f in frames:
        v = dict(base)
        for k, curve in body.items(): v[k] = interp(curve, f, length)
        for side, off in (("L", 0), ("R", half)):
            for k, curve in leg.items(): v[side + k] = interp(curve, f - off, length)
            for k, curve in arm.items():
                # arms swing opposite their own-side leg
                v[side + k] = interp(curve, f - off - half, length) if k == "SS" else interp(curve, f - off, length)
        keys.append((f, v))
    return dict(name=name, length=length, loop=1, keys=keys, contact=contact, liftoff=liftoff)

def oneshot(name, keys):
    return dict(name=name, length=keys[-1][0], loop=0, keys=keys, contact=(0, 0), liftoff=(0, 0))

STAND = dict(PY=0, PRX=0, PRY=0, PRZ=0, TRX=0, TRY=0, TRZ=0, HRX=0, HRY=0,
             LSD=74, LSS=4, LEL=16, RSD=74, RSS=4, REL=16,
             LHP=3, LKN=6, LAN=0, LAB=5, RHP=3, RKN=6, RAN=0, RAB=5)
def pose(**kw):
    v = dict(STAND); v.update(kw); return v

F8 = list(range(0, 65, 8))
F4 = list(range(0, 65, 4))

# --- walk: heel strike, roll over a slightly flexed leg, push off, high knee swing
walk = cycle("walk", 64, STAND,
    leg=dict(HP=[(0,24),(8,18),(16,6),(24,-6),(32,-16),(40,-20),(48,-2),(56,20)],
             KN=[(0,4),(8,16),(16,9),(24,5),(32,9),(40,30),(48,46),(56,22)],
             AN=[(0,-12),(8,0),(16,0),(24,4),(32,12),(40,24),(48,8),(56,-6)],
             AB=[(0,4)]),
    arm=dict(SD=[(0,76)], SS=[(0,26),(16,4),(32,-24),(48,4)], EL=[(0,14),(32,30)]),
    body=dict(PRX=[(0,-3)], TRX=[(0,-3)], HRX=[(0,2)],
              PRY=[(0,-7),(16,0),(32,7),(48,0)], TRY=[(0,11),(16,0),(32,-11),(48,0)],
              PRZ=[(0,0),(8,3),(24,0),(40,-3),(56,0)],
              PY=[(0,0)]),
    frames=F8, contact=(0, 32), liftoff=(40, 8))

# --- run: forward lean, short ground contact, flight, knee drives high, pumping bent arms
run = cycle("run", 64, STAND,
    leg=dict(HP=[(0,34),(6,22),(14,4),(22,-24),(30,-14),(40,22),(48,52),(56,46)],
             KN=[(0,16),(6,34),(14,24),(22,22),(30,70),(40,112),(48,82),(56,34)],
             AN=[(0,-6),(6,6),(14,6),(22,30),(30,36),(40,14),(48,0),(56,-8)],
             AB=[(0,3)]),
    arm=dict(SD=[(0,82)], SS=[(0,34),(16,4),(32,-30),(48,4)], EL=[(0,78),(32,100)]),
    body=dict(PRX=[(0,-9)], TRX=[(0,-12)], HRX=[(0,14)],
              PRY=[(0,-10),(16,0),(32,10),(48,0)], TRY=[(0,16),(16,0),(32,-16),(48,0)],
              PRZ=[(0,0),(8,4),(24,0),(40,-4),(56,0)],
              PY=[(0,30),(6,46),(14,20),(22,-30),(28,-40),(32,30),(38,46),(46,20),(54,-30),(60,-40)]),
    frames=F4, contact=(0, 32), liftoff=(22, 54))

# --- idle: weight on the right leg, slow breathing, a little sway
idle = dict(name="idle", length=96, loop=1, contact=(0,0), liftoff=(0,0), keys=[
    (0,  pose(TRX=-2, HRX=-2, PRZ=-3, TRZ=3, LHP=6, LKN=14, LAB=8, RHP=2, RKN=4, RAB=4, LSD=72, RSD=74, LEL=22, REL=18)),
    (48, pose(TRX=1, HRX=1, PRZ=-4, TRZ=4, HRY=-6, LHP=6, LKN=16, LAB=8, RHP=2, RKN=5, RAB=4, LSD=70, RSD=72, LSS=8, RSS=6, LEL=26, REL=22)),
    (96, pose(TRX=-2, HRX=-2, PRZ=-3, TRZ=3, LHP=6, LKN=14, LAB=8, RHP=2, RKN=4, RAB=4, LSD=72, RSD=74, LEL=22, REL=18)),
])

# --- air: rising tuck -> falling reach (sampled by vertical speed, 0 = rising fast, 32 = falling)
air = oneshot("air", [
    (0,  pose(PRX=-6, TRX=-10, HRX=10, LHP=58, LKN=96, LAN=20, RHP=-14, RKN=50, RAN=30,
              LSD=60, LSS=40, LEL=70, RSD=64, RSS=-24, REL=50)),
    (16, pose(PRX=-4, TRX=-6, HRX=6, LHP=50, LKN=80, LAN=14, RHP=10, RKN=60, RAN=20,
              LSD=52, LSS=24, LEL=60, RSD=54, RSS=10, REL=60)),
    (32, pose(PRX=2, TRX=-2, HRX=-6, LHP=30, LKN=34, LAN=-4, RHP=14, RKN=24, RAN=-4,
              LSD=44, LSS=24, LEL=36, RSD=44, RSS=24, REL=36, LAB=10, RAB=10)),
])

# --- land: crouch on impact (blended in by the landing spring)
land = oneshot("land", [
    (0, pose(PRX=-14, TRX=-18, HRX=18, LHP=56, LKN=96, LAN=-14, RHP=50, RKN=92, RAN=-14, LAB=10, RAB=10,
             LSD=50, LSS=30, LEL=50, RSD=50, RSS=30, REL=50)),
    (1, pose(PRX=-14, TRX=-18, HRX=18, LHP=56, LKN=96, LAN=-14, RHP=50, RKN=92, RAN=-14, LAB=10, RAB=10,
             LSD=50, LSS=30, LEL=50, RSD=50, RSS=30, REL=50)),
])

# --- bump: knocked back off a wall, arms fly up, head snaps back, stagger, recover
bump = oneshot("bump", [
    (0,  pose(PRX=0, TRX=0, LHP=10, RHP=-6)),
    (4,  pose(PRX=10, TRX=22, HRX=26, HRY=10, TRZ=-6, LHP=26, LKN=20, RHP=-12, RKN=12, RAN=10,
              LSD=10, LSS=40, LEL=70, RSD=0, RSS=56, REL=80)),
    (12, pose(PRX=6, TRX=8, HRX=10, HRY=6, LHP=-8, LKN=30, RHP=20, RKN=26,
              LSD=36, LSS=24, LEL=50, RSD=30, RSS=30, REL=60)),
    (22, pose(PRX=-6, TRX=-10, HRX=6, LHP=24, LKN=40, RHP=4, RKN=30,
              LSD=60, LSS=10, LEL=30, RSD=58, RSS=14, REL=34)),
    (32, pose()),
])

# --- push: leaning into a wall you are walking against
push = oneshot("push", [
    (0, pose(PRX=-10, TRX=-20, HRX=14, LHP=20, LKN=24, RHP=-22, RKN=10, RAN=20,
             LSD=84, LSS=74, LEL=34, RSD=84, RSS=74, REL=34)),
    (1, pose(PRX=-10, TRX=-20, HRX=14, LHP=20, LKN=24, RHP=-22, RKN=10, RAN=20,
             LSD=84, LSS=74, LEL=34, RSD=84, RSS=74, REL=34)),
])

clips = [walk, run, idle, air, land, bump, push]
out = ["/* generated by tools/make_fkclips.py - edit the script, not this file */",
       '#include "fk.h"', "#include \"fkclips.h\"", ""]
for c in clips:
    out.append("static const FKKey k_%s[] = {" % c["name"])
    for f, v in c["keys"]:
        out.append("    { %d, { %s } }," % (f, ", ".join(str(int(round(v[k]))) for k in CH)))
    out.append("};")
    out.append('const FKClip clip_%s = { "%s", k_%s, %d, %d, %d, { %d, %d }, { %d, %d } };' % (
        c["name"], c["name"], c["name"], len(c["keys"]), c["length"], c["loop"],
        c["contact"][0], c["contact"][1], c["liftoff"][0], c["liftoff"][1]))
    out.append("")
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
open(os.path.join(root, "game", "fkclips.c"), "w").write("\n".join(out))
open(os.path.join(root, "game", "fkclips.h"), "w").write(
    "#ifndef FKCLIPS_H\n#define FKCLIPS_H\n#include \"fk.h\"\n" +
    "".join("extern const FKClip clip_%s;\n" % c["name"] for c in clips) + "#endif\n")
print("wrote", len(clips), "clips")
