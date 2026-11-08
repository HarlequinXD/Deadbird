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

def cycle(name, length, base, leg, arm, body, frames, contact, liftoff, arm_r=None):
    keys = []
    half = length // 2
    for f in frames:
        v = dict(base)
        for k, curve in body.items(): v[k] = interp(curve, f, length)
        for side, off in (("L", 0), ("R", half)):
            for k, curve in leg.items(): v[side + k] = interp(curve, f - off, length)
            for k, curve in arm.items():
                if side == "R" and arm_r is not None:
                    continue
                # arms swing opposite their own-side leg
                v[side + k] = interp(curve, f - off - half, length) if k == "SS" else interp(curve, f - off, length)
        if arm_r is not None:          # the chainsaw arm has its own curves (absolute frames)
            for k, curve in arm_r.items(): v["R" + k] = interp(curve, f, length)
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

# She is a heavy creature with a chainsaw strapped to her right fist:
#  * short, slow arm swings; the free left arm swings a little, the saw arm barely moves
#    (held low and a touch forward, elbow bent, it just sways with her body)
#  * the torso leans away from the saw and forward, twists little, and the pelvis rolls
#    side to side as her weight moves from foot to foot (the pelvis bob / stride / foot lift
#    that go with it are in player.c)

# --- walk: heavy plod - flat heel strike, long loaded stance, low swing, weight rolling over each foot
walk = cycle("walk", 64, STAND,
    leg=dict(HP=[(0,25),(10,20),(20,8),(30,-5),(38,-13),(46,-2),(52,14),(58,24)],
             KN=[(0,3),(8,15),(18,6),(28,4),(36,24),(44,52),(52,38),(58,10)],
             AN=[(0,-10),(6,0),(20,-4),(32,6),(40,20),(48,2),(56,-6)],
             AB=[(0,4)]),
    arm=dict(SD=[(0,76)], SS=[(0,10),(16,3),(32,-8),(48,3)], EL=[(0,18),(32,24)]),
    arm_r=dict(SD=[(0,72),(8,73),(24,71),(40,73),(56,71)],
               SS=[(0,18),(16,21),(32,18),(48,15)],
               EL=[(0,36),(8,40),(24,35),(40,40),(56,35)]),
    body=dict(PRX=[(0,-3)], TRX=[(0,-6),(6,-8),(20,-5),(38,-8),(52,-5)], HRX=[(0,2),(6,-1),(20,3),(38,-1),(52,3)],
              PRY=[(0,-5),(16,0),(32,5),(48,0)], TRY=[(0,5),(16,0),(32,-5),(48,0)],
              PRZ=[(0,0),(8,5),(24,1),(40,-5),(56,-1)],
              TRZ=[(0,4),(8,2),(24,4),(40,6),(56,4)],
              PY=[(0,0)]),
    frames=list(range(0, 65, 4)), contact=(0, 32), liftoff=(40, 8))

# --- run: lumbering charge - big forward lean, heavy landings, free arm pumps short, saw held forward
run = cycle("run", 64, STAND,
    leg=dict(HP=[(0,30),(8,15),(16,-5),(22,-20),(28,-10),(36,20),(44,48),(52,52),(58,42)],
             KN=[(0,20),(6,36),(14,24),(22,22),(30,72),(38,112),(46,96),(54,50),(60,26)],
             AN=[(0,0),(8,-4),(18,10),(22,28),(30,30),(40,10),(52,-4)],
             AB=[(0,3)]),
    arm=dict(SD=[(0,74)], SS=[(0,22),(16,4),(32,-14),(48,4)], EL=[(0,66),(32,80)]),
    arm_r=dict(SD=[(0,66),(6,69),(22,64),(38,69),(54,64)],
               SS=[(0,30),(16,34),(32,30),(48,26)],
               EL=[(0,52),(6,58),(22,50),(38,58),(54,50)]),
    body=dict(PRX=[(0,-9)], TRX=[(0,-10),(6,-14),(20,-9),(38,-14),(52,-9)], HRX=[(0,10),(6,6),(20,11),(38,6),(52,11)],
              PRY=[(0,-7),(16,0),(32,7),(48,0)], TRY=[(0,7),(16,0),(32,-7),(48,0)],
              PRZ=[(0,0),(8,6),(24,1),(40,-6),(56,-1)],
              TRZ=[(0,4),(8,1),(24,4),(40,7),(56,4)],
              PY=[(0,30),(6,46),(14,20),(22,-30),(28,-40),(32,30),(38,46),(46,20),(54,-30),(60,-40)]),
    frames=F4, contact=(0, 32), liftoff=(22, 54))

# --- idle: weight on the right leg, the saw arm hanging heavy, slow breathing
idle = dict(name="idle", length=96, loop=1, contact=(0,0), liftoff=(0,0), keys=[
    (0,  pose(TRX=-4, HRX=-3, PRZ=-3, TRZ=5, LHP=6, LKN=14, LAB=8, RHP=2, RKN=4, RAB=4, LSD=72, RSD=78, LEL=22, REL=14, RSS=8)),
    (48, pose(TRX=-1, HRX=0, PRZ=-4, TRZ=6, HRY=-6, LHP=6, LKN=16, LAB=8, RHP=2, RKN=5, RAB=4, LSD=70, RSD=77, LSS=8, RSS=9, LEL=26, REL=16)),
    (96, pose(TRX=-4, HRX=-3, PRZ=-3, TRZ=5, LHP=6, LKN=14, LAB=8, RHP=2, RKN=4, RAB=4, LSD=72, RSD=78, LEL=22, REL=14, RSS=8)),
])

# --- air: rising tuck -> falling reach (sampled by vertical speed, 0 = rising fast, 32 = falling)
air = oneshot("air", [
    (0,  pose(PRX=-6, TRX=-10, HRX=10, LHP=58, LKN=96, LAN=20, RHP=-14, RKN=50, RAN=30,
              LSD=62, LSS=24, LEL=60, RSD=66, RSS=16, REL=44)),
    (16, pose(PRX=-4, TRX=-6, HRX=6, LHP=50, LKN=80, LAN=14, RHP=10, RKN=60, RAN=20,
              LSD=56, LSS=18, LEL=52, RSD=62, RSS=20, REL=44)),
    (32, pose(PRX=2, TRX=-2, HRX=-6, LHP=30, LKN=34, LAN=-4, RHP=14, RKN=24, RAN=-4,
              LSD=50, LSS=16, LEL=34, RSD=58, RSS=24, REL=40, LAB=10, RAB=10)),
])

# --- land: crouch on impact (blended in by the landing spring)
land = oneshot("land", [
    (0, pose(PRX=-14, TRX=-18, HRX=18, LHP=56, LKN=96, LAN=-14, RHP=50, RKN=92, RAN=-14, LAB=10, RAB=10,
             LSD=56, LSS=24, LEL=46, RSD=62, RSS=32, REL=44, TRZ=5)),
    (1, pose(PRX=-14, TRX=-18, HRX=18, LHP=56, LKN=96, LAN=-14, RHP=50, RKN=92, RAN=-14, LAB=10, RAB=10,
             LSD=56, LSS=24, LEL=46, RSD=62, RSS=32, REL=44, TRZ=5)),
])

clips = [walk, run, idle, air, land]
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
