#!/usr/bin/env python3
"""Writes maps/e1.map: Deadbird level 1, the Extraction Plant, laid out on a 2 m grid.

    python3 tools/levels/build_e1.py && python3 tools/make_map.py maps/e1.map

Rules the layout keeps (so every texture sits whole):
  - room corners on the 2 m grid, floors and ceilings 2 m apart (rows of whole 2 m tiles)
  - rooms are separated by 2 m of solid wall; a doorway runs through it: a 0.625 m alcove in
    each room, a 0.25 m recess, the 0.25 m door, a 0.25 m recess (the door sits in the middle)
  - doors are 2 or 4 m wide and 4 m tall, on the grid
  - wall rows use plain tiles only; signs, screens and posters go on single whole tiles
    (`wall I cell C,R`) and only on walls whose length is a whole number of tiles
  - secret doors are flush with the wall they hide in and wear that wall's rows
The map stays editable in the level editor afterwards; this script is how it was first made.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "maps", "e1.map")

SIGNS = {"MID_POSTER_POWER", "MID_POSTER_DANGER", "MID_AUTHORIZED", "MID_MONITOR", "MID_ARROW", "DOOR_LINTEL",
         "SCREEN_GRAPH", "SCREEN_VITALS", "DOCK_NUMBER", "CHECKPOINT_PANEL", "PROCESSING_PANEL", "EXTRACTION_PANEL",
         "CELL_WARNING", "LAB_POSTER", "TURBINE_PANEL", "REACTOR_WARNING", "WALL_GRAPH", "WALL_VITALS", "CRATE",
         "CANISTER", "KEYBOARD", "DOOR_JAMB_L", "DOOR_JAMB_R"}
A, R, D = 0.625, 0.25, 0.25          # alcove depth, recess depth, door thickness (A + R + D + R + A = 2 m)
DOOR_H = 4

lines = []
def out(s=""): lines.append(s)
def f(v):
    s = ("%.4f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s
def P(pts): return "  ".join("%s,%s" % (f(x), f(z)) for x, z in pts)
def dedupe(pts):
    o = []
    for p in pts:
        if not o or (abs(o[-1][0] - p[0]) > 1e-9 or abs(o[-1][1] - p[1]) > 1e-9): o.append(p)
    if len(o) > 1 and abs(o[0][0] - o[-1][0]) < 1e-9 and abs(o[0][1] - o[-1][1]) < 1e-9: o.pop()
    return o
def is_grid(v): return abs(v / 2 - round(v / 2)) < 1e-9
def seg_len(a, b): return abs(a[0] - b[0]) + abs(a[1] - b[1])

# ------------------------------------------------------------------ doorway pieces
def notch(axis, line, u1, u2, out_sign, margin):
    """outline points for a doorway alcove on a wall at `axis` = line (axis 'z': the wall runs
    along x at z = line), traversed from u1 toward u2 (the opening), going out of the room by
    out_sign; margin: how far the alcove runs past the opening on each side (0 or 2 m)"""
    dirn = 1 if u2 > u1 else -1
    a, b = u1 - margin * dirn, u2 + margin * dirn
    d = line + out_sign * A
    if axis == "z":
        return [(a, line), (a, d), (u1, d), (u2, d), (b, d), (b, line)]
    return [(line, a), (d, a), (d, u1), (d, u2), (d, b), (line, b)]

def door_sectors(name, room_a, room_b, axis, line, lo, hi, out_sign, floor, lock=None):
    """the recess / door / recess through the wall band that starts at `line` in room_a and runs
    out_sign-ward 2 m to room_b: a 0.625 m alcove, recess, door, recess, alcove"""
    z = [line + out_sign * t for t in (A, A + R, A + R + D, A + R + D + R)]
    pieces = [("%s_ra" % name, room_a, z[0], z[1], "recess"), ("%s" % name, room_a if out_sign > 0 else room_b, z[1], z[2], "door"),
              ("%s_rb" % name, room_b, z[2], z[3], "recess")]
    for nm, room, p, q, kind in pieces:
        p, q = min(p, q), max(p, q)
        if axis == "z": pts = [(lo, p), (hi, p), (hi, q), (lo, q)]           # walls: 0 south, 1 east, 2 north, 3 west
        else: pts = [(p, lo), (q, lo), (q, hi), (p, hi)]                    # walls: 0 south (the band's), ...
        portals = "0,2" if axis == "z" else "1,3"
        sides = "1,3" if axis == "z" else "0,2"
        if kind == "recess":
            out("sector %s room %s floor %s ceil %s" % (nm, room, f(floor), f(floor + DOOR_H)))
            out("    pts " + P(pts))
            out("    ftex FLOOR_HAZARD")
            out("    ctex DARK_METAL")
            out("    walls DARK_METAL")
        else:
            out("sector %s room %s floor %s door %s%s" % (nm, room, f(floor), f(DOOR_H), (" lock " + lock) if lock else ""))
            out("    pts " + P(pts))
            out("    ftex FLOOR_HAZARD")
            out("    walls DARK_METAL")
            out("    wall %s BULKHEAD" % portals)
        out()

def secret_door(name, room, axis, line, lo, hi, out_sign, floor, rows):
    """a door flush with the wall at `line` (0.25 m into the band), wearing the wall's own rows"""
    p, q = sorted((line, line + out_sign * D))
    pts = [(lo, p), (hi, p), (hi, q), (lo, q)] if axis == "z" else [(p, lo), (q, lo), (q, hi), (p, hi)]
    portals = "0,2" if axis == "z" else "1,3"
    out("sector %s room %s floor %s door %s secret" % (name, room, f(floor), f(DOOR_H)))
    out("    pts " + P(pts))
    out("    walls DARK_METAL")
    out("    wall %s %s" % (portals, rows))
    out()

# ------------------------------------------------------------------ rooms
class Room:
    def __init__(self, sec, room, floor, ceil, pts):
        self.sec, self.room, self.floor, self.ceil = sec, room, floor, ceil
        self.pts = dedupe(pts)
        self.extra = []
        self.cells = []            # (wall index, col, row, tile)
    def wall_at(self, a, b):
        """the wall that holds the whole 2 m tile from a to b (world points on one wall)"""
        n = len(self.pts)
        for i in range(n):
            p, q = self.pts[i], self.pts[(i + 1) % n]
            if abs(p[0] - q[0]) < 1e-9 and abs(p[0] - a[0]) < 1e-9 and abs(a[0] - b[0]) < 1e-9:
                lo, hi = sorted((p[1], q[1]))
                if lo - 1e-9 <= min(a[1], b[1]) and max(a[1], b[1]) <= hi + 1e-9: return i, p, q
            if abs(p[1] - q[1]) < 1e-9 and abs(p[1] - a[1]) < 1e-9 and abs(a[1] - b[1]) < 1e-9:
                lo, hi = sorted((p[0], q[0]))
                if lo - 1e-9 <= min(a[0], b[0]) and max(a[0], b[0]) <= hi + 1e-9: return i, p, q
        raise SystemExit("%s: no wall holds %s-%s" % (self.sec, a, b))
    def sign(self, a, b, row, tile):
        """one tile on the whole 2 m span a..b of a wall, `row` 2 m rows up from the floor"""
        i, p, q = self.wall_at(a, b)
        if not (is_grid(p[0]) and is_grid(p[1]) and is_grid(q[0]) and is_grid(q[1])):
            raise SystemExit("%s: sign %s on wall %d, which isn't on the grid (it would be cut)" % (self.sec, tile, i))
        if abs(seg_len(a, b) - 2) > 1e-9 or not all(is_grid(v) for v in a + b):
            raise SystemExit("%s: sign %s isn't one whole grid tile" % (self.sec, tile))
        # columns count from the wall's end point (point i+1), 2 m each
        col = int(round(min(seg_len(q, a), seg_len(q, b)) / 2))
        if row * 2 + 2 > self.ceil - self.floor + 1e-9:
            raise SystemExit("%s: sign %s above the ceiling" % (self.sec, tile))
        self.cells.append((i, col, row, tile))
    def side_walls(self, depth=A):
        """walls exactly `depth` long (alcove sides): dark metal, not cut room tiles"""
        n = len(self.pts)
        return [i for i in range(n) if abs(seg_len(self.pts[i], self.pts[(i + 1) % n]) - depth) < 1e-9]
    def emit(self, *spec, kind=""):
        out("sector %s room %s floor %s ceil %s%s" % (self.sec, self.room, f(self.floor), f(self.ceil), kind))
        out("    pts " + P(self.pts))
        for s in spec: out("    " + s)
        sides = self.side_walls()
        if sides: out("    wall %s DARK_METAL" % ",".join(map(str, sides)))
        for i, c, r, t in self.cells: out("    wall %d cell %d,%d %s" % (i, c, r, t))
        for s in self.extra: out("    " + s)
        out()

def check_rows(spec):
    for s in spec:
        k = s.split()
        if k[0] in ("walls", "lows", "ups") or (k[0] == "wall" and len(k) > 2 and k[2] not in ("cell", "glass", "bars", "shift")):
            for t in k[1:]:
                if t in SIGNS: raise SystemExit("sign tile %s in a repeating row: %s" % (t, s))

# ================================================================== the plan
out("# Deadbird - level 1: the Extraction Plant (written by tools/levels/build_e1.py).")
out("#")
out("# Arrival Bay (the freight lift lowers her in) -> Security Checkpoint -> the Pump Hall, the hub")
out("# around the great extraction tank. West: stairs down to the Sump (cyan key); the cyan door opens")
out("# the Cell Block, whose lift climbs to the Overseer's Office (amber key) and a ledge to drop back")
out("# into the hall. East: the amber door opens the Cooling Hall; stairs climb to its gantry (crimson")
out("# key). North: the crimson door, a corridor and stairs down to the Generator. Three secret doors.")
out("#")
out("# Built on a 2 m grid; rooms 2 m apart, doorways through the 2 m walls (alcove, recess, door,")
out("# recess, alcove). Metres; x east, z north; heights up from the hall floor.")
out()
for rid, nm in [("bay", "Arrival Bay"), ("checkpoint", "Security Checkpoint"), ("hall", "Pump Hall"), ("sump", "The Sump"),
                ("cells", "Cell Block"), ("office", "Overseer's Office"), ("cooling", "Cooling Hall"),
                ("corridor", "Generator Corridor"), ("generator", "Generator")]:
    out("room %s %s" % (rid, nm))
out()

# ------------------------------------------------------------------ ARRIVAL BAY  x -8..8, z -30..-20
BAY_ROWS = "BASE_PLAIN BASE_PIPES BASE_PLAIN BASE_VENT / DOCK_PLATE / DOCK_PLATE / top TOP_BEAM"
out("# ================================================================== ARRIVAL BAY (start)")
bay = Room("bay", "bay", 0, 8,
           [(-8, -30), (4, -30), (4, -26), (8, -26), (8, -20)] +
           notch("z", -20, 2, -2, +1, 2) +
           [(-8, -20), (-8, -24), (-8, -26)])
bay.sign((-6, -30), (-4, -30), 1, "DOCK_NUMBER")
bay.sign((8, -24), (8, -22), 1, "MID_POSTER_DANGER")
bay.sign((-8, -22), (-8, -20), 1, "MID_AUTHORIZED")
for a, b in (((-8, -22), (-8, -24)), ((-8, -28), (-8, -30))):    # the secret door's wall: one plain row, so it hides
    bay.extra.append("wall %d BASE_PLAIN / DOCK_PLATE / DOCK_PLATE / top TOP_BEAM" % bay.wall_at(a, b)[0])
spec = ["ftex FLOOR_DOCK", "frule FLOOR_HAZARD 99,1 0,-10", "ctex CEILING_PANEL", "crule CEILING_LIGHT 2,2 0,0",
        "walls " + BAY_ROWS, "lows PLATE_SIDE", "ups TOP_BEAM / DOCK_PLATE / DOCK_PLATE"]
check_rows(spec); bay.emit(*spec)
out("# the freight lift that lowers her in from the shaft above the bay")
out("sector bay_lift room bay floor 8 ceil 16 lift 0 8")
out("    pts 4,-30  8,-30  8,-26  4,-26")
out("    ftex FLOOR_HAZARD")
out("    walls BASE_HAZARD / DARK_METAL / DARK_METAL / HAZARD / top BEAM")
out("    lows HAZARD / DARK_METAL / DARK_METAL / DARK_METAL")
out()
out("# secret: a plain panel in the west wall is a door; a store room behind it")
secret_door("bay_secret", "bay", "x", -8, -26, -24, -1, 0, "BASE_PLAIN / DOCK_PLATE")
out("sector bay_store room bay floor 0 ceil 4")
out("    pts " + P([(-12, -28), (-8.25, -28), (-8.25, -26), (-8.25, -24), (-8.25, -22), (-12, -22)]))
out("    ftex FLOOR_GRATE")
out("    ctex DARK_METAL")
out("    walls DARK_METAL / top BEAM")
out()

# D1: bay -> checkpoint, 4 m, through z -20..-18
out("# the bay door")
door_sectors("bay_door", "bay", "checkpoint", "z", -20, -2, 2, +1, 0)

# ------------------------------------------------------------------ SECURITY CHECKPOINT  x -6..6, z -18..-10
CP_ROWS = "BASE_PLAIN BASE_VENT / MID_VPIPES MID_PANEL / top TOP_BEAM"
out("# ================================================================== SECURITY CHECKPOINT")
cp = Room("checkpoint", "checkpoint", 0, 6,
          notch("z", -18, -2, 2, -1, 2) +
          [(6, -18), (6, -10)] +
          notch("z", -10, 2, 0, +1, 2) +
          [(-6, -10), (-6, -18)])
cp.sign((-6, -16), (-6, -14), 1, "CHECKPOINT_PANEL")
cp.sign((6, -14), (6, -12), 1, "WALL_GRAPH")
cp.sign((6, -18), (6, -16), 1, "MID_AUTHORIZED")
spec = ["ftex FLOOR_PLATE", "frule FLOOR_STRIP 99,1 0,-7", "ctex CEILING_PANEL", "crule CEILING_LIGHT 2,2 0,0",
        "walls " + CP_ROWS, "lows PLATE_SIDE", "ups TOP_BEAM"]
check_rows(spec); cp.emit(*spec)

# D2: checkpoint -> hall, 2 m, through z -10..-8
out("# the hall door")
door_sectors("hall_door", "checkpoint", "hall", "z", -10, 0, 2, +1, 0)

# ------------------------------------------------------------------ PUMP HALL  x -10..10, z -8..12, 12 m tall
HALL_ROWS = "BASE_PLAIN BASE_PIPES BASE_VENT BASE_PLAIN / MID_VPIPES MID_PANEL / TOP_CIRCUIT / HALL_WALL / HALL_WALL_TOP / top TOP_BEAM"
out("# ================================================================== PUMP HALL (the hub)")
hall = Room("hall", "hall", 0, 12,
            notch("z", -8, 0, 2, -1, 2) +
            [(10, -8)] +
            notch("x", 10, -2, 2, +1, 2) +
            [(10, 12)] +
            notch("z", 12, 2, -2, +1, 2) +
            [(-6, 12), (-10, 12)] +
            notch("x", -10, 8, 4, -1, 2) +
            [(-10, 0), (-10, -4), (-10, -8)])
hall.sign((-8, -8), (-6, -8), 1, "EXTRACTION_PANEL")
hall.sign((6, -8), (8, -8), 1, "EXTRACTION_PANEL")
hall.sign((10, -8), (10, -6), 1, "WALL_VITALS")
hall.sign((10, 6), (10, 8), 1, "MID_POSTER_POWER")
hall.sign((6, 12), (8, 12), 1, "MID_POSTER_DANGER")
hall.sign((-10, -8), (-10, -6), 1, "WALL_GRAPH")
spec = ["ftex FLOOR_PLATE", "frule FLOOR_GRATE 2,2 0,0", "ctex CEILING_CABLES", "crule CEILING_LIGHT 3,3 0,0",
        "walls " + HALL_ROWS, "lows BASE_PLAIN / MID_VPIPES", "ups TOP_BEAM / HALL_WALL_TOP / HALL_WALL / TOP_CIRCUIT"]
check_rows(spec); hall.emit(*spec)

out("# the ledge from the office: 4 m over the hall floor, a rail with a gap at its west end to drop through")
out("sector ledge room office floor 4 ceil 8")
out("    pts -10,12  -6,12  -6,18  -10,18")
out("    ftex FLOOR_GRATE")
out("    ctex CEILING_PANEL")
out("    walls LAB_PANEL / top TOP_BEAM")
out("    lows PLATE_SIDE / PLATE_SIDE")
out("    ups TOP_BEAM / HALL_WALL_TOP")
out()

# ------------------------------------------------------------------ STAIRS DOWN and THE SUMP  (cyan key)
out("# stairs down to the sump, off the hall's west wall")
out("stairs sump_stairs room sump -10,-4 -16,0 w 0 -4 ceil 4 steps 20")
out("    ftex STAIR_TREAD")
out("    walls HALL_WALL / HALL_WALL / HALL_WALL_TOP / top TOP_BEAM")
out("    lows PLATE_SIDE")
out("    ups TOP_BEAM / HALL_WALL")
out()
SUMP_ROWS = "BASE_PIPES BASE_PLAIN BASE_VENT / MID_VPIPES / top TOP_BEAM"
out("# ================================================================== THE SUMP (cyan key)")
sump = Room("sump", "sump", -4, 2,
            [(-28, -16), (-24, -16), (-22, -16), (-16, -16), (-16, -4), (-16, 0), (-28, 0)])
sump.sign((-28, -8), (-28, -10), 1, "PROCESSING_PANEL")
sump.sign((-16, -12), (-16, -10), 1, "MID_POSTER_DANGER")
sump.sign((-22, 0), (-20, 0), 1, "WALL_VITALS")
sump.extra.append("wall %d BASE_PLAIN / MID_VPIPES / top TOP_BEAM" % sump.wall_at((-28, -16), (-26, -16))[0])
sump.extra.append("wall %d BASE_PLAIN / MID_VPIPES / top TOP_BEAM" % sump.wall_at((-22, -16), (-20, -16))[0])
spec = ["ftex FLOOR_PLATE", "frule FLOOR_GRATE 1,99 0,-5", "ctex CEILING_PANEL", "crule CEILING_LIGHT 3,3 1,1",
        "walls " + SUMP_ROWS, "lows PLATE_SIDE", "ups TOP_BEAM"]
check_rows(spec); sump.emit(*spec)
out("# secret: the south wall's middle panel opens on a drain room")
secret_door("sump_secret", "sump", "z", -16, -24, -22, -1, -4, "BASE_PLAIN / MID_VPIPES")
out("sector sump_drain room sump floor -4 ceil 0")
out("    pts " + P([(-26, -20), (-20, -20), (-20, -16.25), (-22, -16.25), (-24, -16.25), (-26, -16.25)]))
out("    ftex FLOOR_DRAIN")
out("    ctex DARK_METAL")
out("    walls DARK_METAL / top BEAM")
out()

# ------------------------------------------------------------------ CELL BLOCK  x -24..-12, z 2..14
out("# the cyan door: hall -> cell block, through x -12..-10")
door_sectors("cyan_door", "cells", "hall", "x", -12, 4, 8, +1, 0, "cyan")
CELL_ROWS = "CELL_WALL / CELL_WALL / top TOP_BEAM"
out("# ================================================================== CELL BLOCK")
cells = Room("cells", "cells", 0, 6,
             [(-24, 2)] + notch("x", -12, 4, 8, +1, 2) + [(-12, 14), (-20, 14), (-24, 14)])
cells.sign((-24, 6), (-24, 8), 1, "CELL_WARNING")
cells.sign((-14, 14), (-16, 14), 1, "CELL_WARNING")
spec = ["ftex FLOOR_CELL", "frule FLOOR_DRAIN 5,3 2,1", "ctex CEILING_PANEL", "crule CEILING_LIGHT 3,3 1,1",
        "walls " + CELL_ROWS, "lows PLATE_SIDE", "ups TOP_BEAM / CELL_WALL"]
check_rows(spec); cells.emit(*spec)
out("# the lift up to the office")
out("sector cells_lift room cells floor 0 ceil 10 lift 0 4")
out("    pts -24,14  -20,14  -20,18  -24,18")
out("    ftex FLOOR_HAZARD")
out("    walls BASE_HAZARD / DARK_METAL / DARK_METAL / top BEAM")
out("    lows HAZARD / DARK_METAL")
out()

# ------------------------------------------------------------------ OVERSEER'S OFFICE  x -24..-6, z 18..24  (amber key)
out("# ================================================================== OVERSEER'S OFFICE (amber key)")
office = Room("office", "office", 4, 8,
              [(-24, 18), (-20, 18), (-10, 18), (-6, 18), (-6, 24), (-24, 24)])
office.sign((-16, 24), (-18, 24), 0, "LAB_POSTER")
office.sign((-10, 24), (-12, 24), 0, "WALL_VITALS")
office.sign((-12, 18), (-14, 18), 0, "WALL_GRAPH")
spec = ["ftex FLOOR_LAB", "ctex CEILING_PANEL", "crule CEILING_LIGHT 2,2 0,0",
        "walls LAB_PANEL / top TOP_BEAM", "lows PLATE_SIDE", "ups TOP_BEAM"]
check_rows(spec); office.emit(*spec)

# ------------------------------------------------------------------ COOLING HALL  x 12..28, z -12..6  (crimson key on the gantry)
out("# the amber door: hall -> cooling hall, through x 10..12")
door_sectors("amber_door", "hall", "cooling", "x", 10, -2, 2, +1, 0, "amber")
COOL_ROWS = "BASE_PIPES BASE_HAZARD BASE_PLAIN / GALLERY_PIPES / GALLERY_PIPES / TOP_CIRCUIT / top TOP_BEAM"
out("# ================================================================== COOLING HALL (crimson key)")
cool = Room("cooling", "cooling", 0, 10,
            [(12, -12), (14, -12), (16, -12), (18, -12), (18, -8), (18, -6), (24, -6), (24, 6), (12, 6)] +
            notch("x", 12, 2, -2, -1, 2) + [(12, -12)])
cool.sign((12, 6), (14, 6), 1, "TURBINE_PANEL")
cool.sign((22, 6), (24, 6), 1, "TURBINE_PANEL")
cool.sign((12, -10), (12, -8), 1, "MID_POSTER_DANGER")
cool.extra.append("wall %d BASE_PLAIN / GALLERY_PIPES / GALLERY_PIPES / TOP_CIRCUIT / top TOP_BEAM" % cool.wall_at((12, -12), (14, -12))[0])
cool.extra.append("wall %d BASE_PLAIN / GALLERY_PIPES / GALLERY_PIPES / TOP_CIRCUIT / top TOP_BEAM" % cool.wall_at((16, -12), (18, -12))[0])
spec = ["ftex FLOOR_PLATE", "frule FLOOR_GRATE 3,1 1,0", "ctex CEILING_CABLES", "crule CEILING_LIGHT 3,3 1,1",
        "walls " + COOL_ROWS, "lows HAZARD / PLATE_SIDE", "ups TOP_BEAM / TOP_CIRCUIT / GALLERY_PIPES / GALLERY_PIPES"]
check_rows(spec); cool.emit(*spec)
out("# stairs up to the gantry along the south wall")
out("stairs cool_stairs room cooling 18,-12 24,-8 e 0 4 ceil 10 steps 20")
out("    ftex STAIR_TREAD")
out("    walls BASE_PLAIN / GALLERY_PIPES / GALLERY_PIPES / TOP_CIRCUIT / top TOP_BEAM")
out("    lows PLATE_SIDE")
out()
out("sector gantry room cooling floor 4 ceil 10")
out("    pts 24,-12  28,-12  28,6  24,6  24,-6  24,-8")
out("    ftex FLOOR_GRATE")
out("    ctex CEILING_CABLES")
out("    walls GALLERY_PIPES / TOP_CIRCUIT / top TOP_BEAM")
out("    wall 2 cell 0,0 TURBINE_PANEL")
out("    lows HAZARD / PLATE_SIDE")
out("    ups TOP_BEAM")
out()
out("# secret: a pipe panel in the south wall")
secret_door("cool_secret", "cooling", "z", -12, 14, 16, -1, 0, "BASE_PLAIN / GALLERY_PIPES")
out("sector cool_store room cooling floor 0 ceil 4")
out("    pts " + P([(12, -16), (18, -16), (18, -12.25), (16, -12.25), (14, -12.25), (12, -12.25)]))
out("    ftex FLOOR_GRATE")
out("    ctex DARK_METAL")
out("    walls DARK_METAL / top BEAM")
out()

# ------------------------------------------------------------------ GENERATOR CORRIDOR and the GENERATOR
out("# the crimson door: hall -> the generator corridor, through z 12..14 (the corridor is as wide")
out("# as the door, so its side has no alcove)")
door_sectors("crimson_door", "hall", "corridor", "z", 12, -2, 2, +1, 0, "crimson")
out("# ================================================================== GENERATOR CORRIDOR and GENERATOR (the end)")
corr = Room("corridor", "corridor", 0, 6, [(-2, 13.375), (2, 13.375), (2, 16), (-2, 16)])
spec = ["ftex FLOOR_HAZARD", "ctex CEILING_CABLES", "walls HALL_WALL / HALL_WALL / top TOP_BEAM", "ups TOP_BEAM"]
check_rows(spec); corr.emit(*spec)
out("stairs gen_stairs room corridor -2,16 2,20 n 0 -2 ceil 6 steps 10")
out("    ftex STAIR_TREAD")
out("    walls HALL_WALL / HALL_WALL / top TOP_BEAM")
out("    ups TOP_BEAM")
out()
GEN_ROWS = "REACTOR_PLATE BASE_HAZARD / REACTOR_PLATE / TOP_CIRCUIT / HALL_WALL / HALL_WALL_TOP / top TOP_BEAM"
gen = Room("generator", "generator", -2, 10, [(-4, 20), (-2, 20), (2, 20), (16, 20), (16, 30), (-4, 30)])
gen.sign((-4, 24), (-4, 26), 1, "REACTOR_WARNING")
gen.sign((16, 26), (16, 24), 1, "REACTOR_WARNING")
gen.sign((4, 30), (6, 30), 1, "WALL_VITALS")
gen.sign((6, 30), (8, 30), 1, "WALL_GRAPH")
spec = ["ftex FLOOR_REACTOR", "frule FLOOR_HAZARD 99,1 0,10", "ctex CEILING_CABLES", "crule CEILING_LIGHT 2,2 0,0",
        "walls " + GEN_ROWS, "lows REACTOR_PLATE", "ups TOP_BEAM / HALL_WALL_TOP / HALL_WALL / TOP_CIRCUIT"]
check_rows([s.replace("REACTOR_PLATE", "X") for s in spec]); gen.emit(*spec)

# ================================================================== LIGHTS
out("# ================================================================== LIGHTS")
out("lightgrid -8,-30 8,-20 5.5 4 1.0 1.05 1.15 8")
out("light 6,-26.5,6 1.3 0.15 0.1 4")
out("sprite 6,-26.2,6.2 0.26 255 40 30 5")
out("light -10,-25,3 0.5 0.35 0.6 3.5")
out("light 0,-18.5,4.4 0.9 0.3 0.15 3")
out("lightgrid -6,-18 6,-10 4.5 4 0.8 0.85 0.9 5.5")
out("light 1,-8.5,4.4 0.9 0.3 0.15 3")
out("# the hall: the tank's cyan, work lights high on the walls, a ring of lights over the floor")
out("light 0,2,4 0.35 1.1 1.4 15")
out("light 0,2,0.6 0.25 0.8 1.0 6")
out("lightgrid -10,-8 10,12 7 5 0.8 0.82 0.9 10")
out("light -8,-6,9 0.8 0.75 0.6 12")
out("light 8,-6,9 0.8 0.75 0.6 12")
out("light -8,10,9 0.7 0.7 0.75 12")
out("light 8,10,9 0.7 0.7 0.75 12")
out("light -10.5,6,4.4 0.2 0.8 1.0 3.5")
out("light 10.5,0,4.4 1.0 0.6 0.1 3.5")
out("light 0,12.5,4.4 1.1 0.15 0.1 3.5")
out("lightgrid -28,-16 -16,0 0.5 4 0.9 0.78 0.6 7")
out("light -13,-2,3 0.6 0.6 0.7 5")
out("light -23,-18,-1 0.5 0.35 0.7 3.5")
out("lightgrid -24,2 -12,14 4.5 4 0.6 0.65 0.75 6.5")
out("light -22,16,6 0.9 0.6 0.2 4")
out("lightgrid -24,18 -6,24 7 4 0.85 0.9 0.95 6.5")
out("light -8,15,7 0.7 0.75 0.8 5")
out("lightgrid 12,-12 28,6 7 4 0.9 0.86 0.72 8.5")
out("light 16,4,0.3 0.2 0.9 0.7 4")
out("light 21,4,0.3 0.2 0.9 0.7 4")
out("light 26,0,8 0.6 0.6 0.55 7")
out("light 15,-14,2.5 0.5 0.35 0.6 3.5")
out("light 0,15,4.5 1.0 0.5 0.25 5")
out("light 0,19,3 0.8 0.5 0.3 5")
out("lightgrid -4,20 16,30 7 5 0.7 0.6 0.5 9")
out("light 6,25,0 1.6 0.8 0.3 17")
out("light 6,25,7 0.9 0.55 0.3 11")
out("light -2,22,4 0.8 0.55 0.4 7")
out("light 14,22,4 0.8 0.55 0.4 7")
out("light 14,28,4 0.8 0.55 0.4 7")
out("light -2,28,4 0.8 0.55 0.4 7")
out()

# ================================================================== PROPS
out("# ================================================================== PROPS")
out("# arrival bay")
for p in ["crate -6.5,-28.5 0.8 0.8", "crate -5.6,-28.6 0.8 0.8", "crate -6.1,-28.4 0.8 1.6", "crate 6.6,-21.5 0.7 0.7",
          "crate -6.8,-21.4 0.8 0.8", "can 2.6,-29.2", "can 3.2,-29.4", "can -3.5,-21",
          "machine 6.8,-22 8,-20 2.2 w"]:
    out("prop " + p)
out("# checkpoint")
for p in ["console 4,-16.6 w graph", "console -4,-11 s vitals", "box -6,-12 -5.2,-10 0 3.4 DARK_METAL HAZARD cell 0.9",
          "box 4.4,-12 6,-11.2 0 1.1 DARK_METAL MACHINE_PANEL cell 0.9"]:
    out("prop " + p)
out("# the pump hall")
for p in ["tank 0,2", "pillar -6,-4 12", "pillar 6,-4 12", "pillar -6,8 12", "pillar 6,8 12",
          "console -7,-7.2 n graph", "console 7,-7.2 n vitals", "rail -9,12 -6,12 4"]:
    out("prop " + p)
out("# the sump: extraction pods in two rows, a machine down the middle")
for p in ["pod -25,-12 e 3", "pod -25,-6 e 4", "pod -19,-12 w 3", "pod -19,-6 w 4",
          "console -22,-1 s vitals", "can -17,-15", "can -17.6,-15.3"]:
    out("prop " + p)
out("# the cell block: barred cells along both long walls")
for p in ["cell -22,2.8 n", "cell -19.5,2.8 n", "cell -17,2.8 n", "cell -17,13.2 s", "cell -14.5,13.2 s", "can -13,2.6",
          "crate -23.3,12.5 0.7 0.7"]:
    out("prop " + p)
out("# the office")
for p in ["box -15,22.2 -13,23.6 4 4.8 DARK_METAL MACHINE_PANEL cell 0.8", "box -14.8,22.6 -13.9,23.1 4.8 4.95 SHINI_BODY SHINI_BODY cell 0.9",
          "console -20,23.4 s graph", "console -22.5,23.4 s vitals", "rack -6.6,22 w", "rack -6.6,20.5 w",
          "cyl -9.4,23,4.8 0.19 0.56 6 GLASS add anim 2 rep 1", "cyl -8.6,23,4.8 0.19 0.56 6 GLASS add anim 2 rep 1",
          "box -9.8,22.2 -8.2,23.6 4 4.8 DARK_METAL MACHINE_PANEL cell 0.8"]:
    out("prop " + p)
out("# the cooling hall: two turbines with spinning fans, a rail along the gantry")
for p in ["box 14,2 18,5.5 0 3.4 DARK_METAL MACHINE_BASE cell 1", "fan 16,5.51,1.7 1.1 1",
          "box 19,2 23,5.5 0 3.4 DARK_METAL MACHINE_BASE cell 1", "fan 21,5.51,1.7 1.1 -1",
          "rail 24,-6 24,-2 4", "console 27.4,-4 w graph", "crate 13,-15 0.7 0.7"]:
    out("prop " + p)
out("# the generator")
for p in ["core 6,25", "console -3.2,22 e graph", "console -3.2,28 e vitals", "console 15.2,22 w vitals", "console 15.2,28 w graph"]:
    out("prop " + p)
out()

# ================================================================== THINGS
out("# ================================================================== THINGS")
out("thing start 6,-28 yaw 0")
out()
for t in ["-4,-24 yaw 90", "2,-22 yaw 180",
          "-4,-12 yaw 180", "4,-14 yaw 270",
          "-6,6 yaw 135", "4,-2 yaw 270", "6,10 yaw 200",
          "-20,-6 yaw 90", "-24,-12 yaw 45", "-26,-4 yaw 120",
          "-16,10 yaw 90", "-20,6 yaw 120", "-14,4 yaw 60",
          "-12,22 yaw 270", "-18,20 yaw 90",
          "16,0 yaw 270", "20,-2 yaw 200", "14,-8 yaw 0", "26,-8 yaw 0",
          "0,24 yaw 160", "12,23 yaw 200", "1,28 yaw 135", "12,28 yaw 225"]:
    out("thing trooper " + t)
out()
out("thing key cyan -22,-14")
out("thing key amber -20,22")
out("thing key crimson 26,4")
out("thing shard -8,16")
out("thing shard 27,-10")
out("thing shard 13,4")
out("thing shard -10.5,-25 secret")
out("thing shard -23,-18.5 secret")
out("thing shard 15,-14.5 secret")
out("thing exit 6,25                    # the generator: A beside it ends the level")
out()
out("# route test: the whole level in key order (stairs, lifts, every door; the first point is skipped)")
for w in [(6, -28), (6, -24), (0, -22), (0, -19), (0, -14), (1, -11), (1, -9), (1, -6), (-6, -2), (-13, -2), (-18, -2),
          (-22, -4), (-22, -14), (-22, -4), (-18, -2), (-13, -2), (-6, -2), (-8, 6), (-11, 6), (-14, 6), (-18, 10),
          (-22, 12), (-22, 16), (-22, 20), (-20, 22), (-12, 20), (-9.3, 20), (-9.3, 15), (-9.3, 13), (-9.3, 10),
          (-6, 2), (3, -4), (8, 0), (11, 0), (14, 0), (15, -4), (15, -10), (21, -10), (26, -10), (26, 4),
          (25, 0), (22, 0), (16, 0), (11, 0), (6, 4), (0, 10), (0, 13), (0, 15), (0, 18), (0, 22), (2, 24)]:
    out("thing way %s,%s" % (f(w[0]), f(w[1])))

open(OUT, "w").write("\n".join(lines) + "\n")
print("wrote", OUT, len(lines), "lines")
