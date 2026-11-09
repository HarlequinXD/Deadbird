#!/usr/bin/env python3
"""Compiles a Deadbird sector map (maps/<name>.map) into gen/map_<name>.c, and draws a top-down
preview to docs/maps/<name>.png.

    python3 tools/make_map.py maps/e1.map [more.map ...]

The map format (metres; heights go up; x east, z north; '#' starts a comment):

  room ID Display Name              declare a room (an area: rooms are what visibility and the
                                    level viewer work with); sectors name theirs by ID with `room`
  sector NAME [room R] floor H [ceil H] [fslope WALL H] [cslope WALL H]
              [door OPEN [lock cyan|amber|crimson]] [lift LOW HIGH]
      pts x,z x,z ...               the floor plan; any winding, concave is fine; no holes
      ftex TILE / ctex TILE         default floor / ceiling tile
      frule TILE SX,SZ OX,OZ        floor tile on world cells where cx % SX == OX and cz % SZ == OZ
      crule ...                     (the same for the ceiling)
      fcell CX,CZ TILE / ccell ...  one world cell (2 m grid, cell = floor(metres / 2))
      walls ROWS                    default rows for every solid wall part of this sector
      lows ROWS / ups ROWS          default rows for lower (step) / upper (soffit) walls
      wall I[,J,K-L] ROWS           rows for walls I.. (wall i runs from point i to point i+1)
      wall I low ROWS / wall I up ROWS
      wall I cell COL,ROW TILE      one tile on the solid / mid part (column along, row up)
      wall I glass | bars | end | mid | noshadow
  ROWS = tiles of the bottom row / the next row up / ... [/ top TILE]; a row's tiles repeat along
  the wall; `top TILE` pins one row under the ceiling. Tile names are gen_lab.h's without LT_.

  stairs NAME [room R] x0,z0 x1,z1 n|s|e|w FROM TO [ceil H] [steps N]
      a flight of step sectors filling the rectangle, climbing (or falling) from height FROM at the
      side it starts from to TO at the side it heads (n|s|e|w); 0.2 m a step unless N is given.
      Indented lines after it (ftex, walls, lows...) apply to every step.

  light x,z,h R G B RADIUS          a baked point light (colour 0..2)
  lightgrid x0,z0 x1,z1 h STEP R G B RADIUS   lights every STEP metres across a rectangle
  sprite x,z,h SIZE R G B [ANIM]    a light halo (colour 0..255)
  thing start|trooper|shard|exit|way x,z [yaw DEG] [patrol x,z]   (way: route-test waypoints, in order;
                                    a shard can add `secret`: it counts toward the secrets found)     thing key cyan|amber|crimson x,z
  prop box x0,z0 x1,z1 h0 h1 TOP SIDE [cell C] [noshadow] [nocoll]       (TOP may be -)
  prop boxf ... FRONT n|e|s|w [emit V]
  prop crate x,z SIZE H        prop can x,z           prop pillar x,z H
  prop console x,z n|e|s|w graph|vitals [h]        prop machine x0,z0 x1,z1 H n|e|s|w
  (props stand on the floor under them; box, cyl, rail and fan heights are absolute)
  prop pod x,z n|e|s|w ANIM    prop rack x,z n|e|s|w  prop fan x,z,h R SPEED
  prop rail x0,z0 x1,z1 BASE   prop cyl x,z,h R LEN N TILE [axis x|y|z] [add] [anim A] [emit V] [cap TILE] [rep N]
  prop tank x,z                prop core x,z          prop cell x,z n|e|s|w
"""
import math, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
U = 1024                                # world units per metre
TILE = 2048

def tiles_from_header():
    src = open(os.path.join(ROOT, "gen", "gen_lab.h")).read()
    body = src[src.index("enum {"):src.index("LT_COUNT")]
    names = re.findall(r"LT_(\w+)", body)
    t = {n: i for i, n in enumerate(names)}
    for a, b in re.findall(r"#define LT_(\w+)\s+LT_(\w+)", src):
        t[a] = t[b]
    return t

TILES = tiles_from_header()
OP = {n: i for i, n in enumerate("END CUR BOX BOXF STAIRS RAILX RAILZ CRATE CAN CEIL LIGHT SPRITE CONSOLE MACHINE CYL QUAD POD PILLAR RACK FAN SPECIAL BASE".split())}
ND = {"n": 5, "s": 6, "e": 1, "w": 2}          # wall/face normal: ND_PZ, ND_NZ, ND_PX, ND_NX
FACING = {"n": 0, "e": 1, "s": 2, "w": 3}
KEYS = {"cyan": 1, "amber": 2, "crimson": 3}
THINGS = {"start": 0, "trooper": 1, "key": 2, "shard": 3, "exit": 4, "way": 5}
F_ADD, F_EMIT, F_PULSE = 2, 64, 16

class MapError(Exception):
    pass

def m(v):
    return int(round(float(v) * U))

def pt(tok):
    a = tok.split(",")
    if len(a) != 2:
        raise MapError("expected x,z: %r" % tok)
    return (m(a[0]), m(a[1]))

def tile(name):
    if name == "-":
        return 255
    n = name.upper()
    if n.startswith("LT_"):
        n = n[3:]
    if n not in TILES:
        raise MapError("unknown tile %r" % name)
    return TILES[n]

def rows_spec(toks):
    rows, top, cur = [], 255, []
    i = 0
    while i < len(toks):
        t = toks[i]
        if t == "/":
            if cur: rows.append(cur)
            cur = []
        elif t == "top":
            top = tile(toks[i + 1]); i += 1
        else:
            cur.append(tile(t))
        i += 1
    if cur: rows.append(cur)
    return {"rows": rows, "top": top, "cells": []}

def wall_list(tok, n):
    out = []
    for part in tok.split(","):
        if part == "*":
            out += range(n)
        elif "-" in part:
            a, b = part.split("-"); out += range(int(a), int(b) + 1)
        else:
            out.append(int(part))
    for w in out:
        if not 0 <= w < n:
            raise MapError("wall %d out of range (sector has %d)" % (w, n))
    return out

class Sector:
    def __init__(self, name, line):
        self.name, self.line = name, line
        self.room, self.floor, self.ceil = None, 0, None
        self.fslope = self.cslope = None
        self.kind, self.lock, self.ka, self.kb = 0, 0, 0, 0
        self.pts = []
        self.flat = {"f": {"def": TILES["FLOOR_PLATE"], "rules": [], "cells": []},
                     "c": {"def": TILES["CEILING_PANEL"], "rules": [], "cells": []}}
        self.walls_def = None; self.lows_def = None; self.ups_def = None
        self.wall = {}           # i -> dict(mid, low, up, flags, cells)

    def w(self, i):
        return self.wall.setdefault(i, {"mid": None, "low": None, "up": None, "flags": 0, "cells": []})

room_names = {}

def parse(path):
    rooms, secs, lights, sprites, things, props = [], [], [], [], [], []
    cur = None
    for ln, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        toks = line.split()
        k = toks[0]
        try:
            if k == "room":
                rooms.append(toks[1]); room_names[toks[1]] = " ".join(toks[2:]) or toks[1]
            elif k == "sector":
                cur = Sector(toks[1], ln); secs.append(cur)
                i = 2
                while i < len(toks):
                    a = toks[i]
                    if a == "room": cur.room = toks[i + 1]; i += 2
                    elif a == "floor": cur.floor = m(toks[i + 1]); i += 2
                    elif a == "ceil": cur.ceil = m(toks[i + 1]); i += 2
                    elif a == "fslope": cur.fslope = (int(toks[i + 1]), m(toks[i + 2])); i += 3
                    elif a == "cslope": cur.cslope = (int(toks[i + 1]), m(toks[i + 2])); i += 3
                    elif a == "door": cur.kind = 1; cur.ka = m(toks[i + 1]); i += 2
                    elif a == "lock": cur.lock = KEYS[toks[i + 1]]; i += 2
                    elif a == "lift": cur.kind = 2; cur.ka = m(toks[i + 1]); cur.kb = m(toks[i + 2]); i += 3
                    else: raise MapError("unknown sector key %r" % a)
            elif k == "stairs":
                name, i, room, ceil, steps = toks[1], 2, None, None, None
                if toks[i] == "room": room = toks[i + 1]; i += 2
                (x0, z0), (x1, z1) = pt(toks[i]), pt(toks[i + 1]); d = toks[i + 2]; h0, h1 = m(toks[i + 3]), m(toks[i + 4]); i += 5
                while i < len(toks):
                    if toks[i] == "ceil": ceil = m(toks[i + 1]); i += 2
                    elif toks[i] == "steps": steps = int(toks[i + 1]); i += 2
                    else: raise MapError("unknown stairs key %r" % toks[i])
                x0, x1 = sorted((x0, x1)); z0, z1 = sorted((z0, z1))
                n = steps or max(1, int(math.ceil(abs(h1 - h0) / (0.2 * U) - 1e-6)))
                cur = Sector(name, ln); cur.room = room; cur.stairs = (x0, z0, x1, z1, d, h0, h1, n, ceil)
                secs.append(cur)
            elif k == "pts":
                cur.pts += [pt(t) for t in toks[1:]]
            elif k in ("ftex", "ctex"):
                cur.flat[k[0]]["def"] = tile(toks[1])
            elif k in ("frule", "crule"):
                sx, sz = map(int, toks[2].split(",")); ox, oz = map(int, toks[3].split(","))
                cur.flat[k[0]]["rules"].append((tile(toks[1]), sx, sz, ox % sx, oz % sz))
            elif k in ("fcell", "ccell"):
                cx, cz = map(int, toks[1].split(","))
                cur.flat[k[0]]["cells"].append((cx, cz, tile(toks[2])))
            elif k == "walls":
                cur.walls_def = rows_spec(toks[1:])
            elif k == "lows":
                cur.lows_def = rows_spec(toks[1:])
            elif k == "ups":
                cur.ups_def = rows_spec(toks[1:])
            elif k == "wall":
                ws = wall_list(toks[1], len(cur.pts))
                a = toks[2] if len(toks) > 2 else ""
                for wi in ws:
                    w = cur.w(wi)
                    if a in ("low", "up"):
                        w[a] = rows_spec(toks[3:])
                    elif a == "cell":
                        c, r = map(int, toks[3].split(",")); w["cells"].append((c, r, tile(toks[4])))
                    elif a in ("glass", "bars", "end", "mid", "noshadow"):
                        w["flags"] |= {"glass": 1, "bars": 2, "end": 4, "mid": 8, "noshadow": 16}[a]
                    else:
                        w["mid"] = rows_spec(toks[2:])
            elif k == "light":
                x, z, h = toks[1].split(",")
                lights.append((m(x), m(z), m(h), [int(round(float(c) * 100)) for c in toks[2:5]], m(toks[5])))
            elif k == "lightgrid":
                (x0, z0), (x1, z1) = pt(toks[1]), pt(toks[2]); h, step = m(toks[3]), m(toks[4])
                c = [int(round(float(v) * 100)) for v in toks[5:8]]; rad = m(toks[8])
                nx, nz = max(1, round(abs(x1 - x0) / step)), max(1, round(abs(z1 - z0) / step))
                for j in range(nz):
                    for i in range(nx):
                        lights.append((x0 + (x1 - x0) * (2 * i + 1) // (2 * nx), z0 + (z1 - z0) * (2 * j + 1) // (2 * nz), h, c, rad))
            elif k == "sprite":
                x, z, h = toks[1].split(",")
                sprites.append((m(x), m(z), m(h), m(toks[2]), [int(c) for c in toks[3:6]], int(toks[6]) if len(toks) > 6 else 0))
            elif k == "thing":
                t = {"type": THINGS[toks[1]], "x": 0, "z": 0, "yaw": 0, "arg": 0, "ax": 0, "az": 0}
                rest = toks[2:]
                if rest and rest[0] in KEYS:
                    t["arg"] = KEYS[rest[0]]; rest = rest[1:]
                toks = toks[:2] + rest
                t["x"], t["z"] = pt(toks[2]); t["ax"], t["az"] = t["x"], t["z"]
                i = 3
                while i < len(toks):
                    if toks[i] == "yaw": t["yaw"] = int(round(float(toks[i + 1]) * 4096 / 360)) & 4095; i += 2
                    elif toks[i] == "patrol": t["ax"], t["az"] = pt(toks[i + 1]); i += 2
                    elif toks[i] in KEYS: t["arg"] = KEYS[toks[i]]; i += 1
                    elif toks[i] == "secret": t["arg"] = 1; i += 1
                    else: raise MapError("unknown thing key %r" % toks[i])
                things.append(t)
            elif k == "prop":
                props.append((ln, toks[1:]))
            else:
                raise MapError("unknown line %r" % k)
        except (MapError, IndexError, ValueError, KeyError) as e:
            raise SystemExit("%s:%d: %s" % (path, ln, e))
    return rooms, secs, lights, sprites, things, props

# ------------------------------------------------------------------ geometry checks
def area2(p):
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))

def inside(p, x, z):
    c = False
    for i in range(len(p)):
        (x0, z0), (x1, z1) = p[i], p[(i + 1) % len(p)]
        if (z0 > z) != (z1 > z) and x < x0 + (z - z0) * (x1 - x0) / (z1 - z0):
            c = not c
    return c

def expand_stairs(secs):
    out = []
    for s in secs:
        st = getattr(s, "stairs", None)
        if not st:
            out.append(s); continue
        x0, z0, x1, z1, d, h0, h1, n, ceil = st
        for k in range(n):
            a, b = k / n, (k + 1) / n
            def lerp(p, q, t): return int(round(p + (q - p) * t))
            if d == "n": r = (x0, lerp(z0, z1, a), x1, lerp(z0, z1, b))
            elif d == "s": r = (x0, lerp(z1, z0, b), x1, lerp(z1, z0, a))
            elif d == "e": r = (lerp(x0, x1, a), z0, lerp(x0, x1, b), z1)
            else: r = (lerp(x1, x0, b), z0, lerp(x1, x0, a), z1)
            c = Sector("%s_%d" % (s.name, k), s.line)
            c.room = s.room
            c.floor = int(round(h0 + (h1 - h0) * (k + 1) / n))
            c.ceil = ceil if ceil is not None else max(h0, h1) + 4 * U
            c.pts = [(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])]
            c.flat = s.flat; c.walls_def = s.walls_def; c.lows_def = s.lows_def; c.ups_def = s.ups_def
            out.append(c)
    return out

def finish(path, rooms, secs):
    names = {}
    for s in secs:
        if s.name in names:
            raise SystemExit("%s:%d: sector %s defined twice" % (path, s.line, s.name))
        names[s.name] = s
        if len(s.pts) < 3:
            raise SystemExit("%s:%d: sector %s needs 3 points" % (path, s.line, s.name))
        if s.room is None:
            raise SystemExit("%s:%d: sector %s has no room" % (path, s.line, s.name))
        if s.room not in rooms:
            raise SystemExit("%s:%d: unknown room %r" % (path, s.line, s.room))
        if s.ceil is None:
            s.ceil = s.floor if s.kind == 1 else s.floor + 4 * U
        if area2(s.pts) < 0:                         # make every loop counter-clockwise (x east, z north)
            n = len(s.pts)
            s.pts.reverse()
            # wall i (p[i] -> p[i+1]) becomes wall n-2-i; remap per-wall settings
            s.wall = {(n - 2 - i) % n: w for i, w in s.wall.items()}
            for key in ("fslope", "cslope"):
                v = getattr(s, key)
                if v: setattr(s, key, ((n - 2 - v[0]) % n, v[1]))
        for i in range(len(s.pts)):
            if s.pts[i] == s.pts[(i + 1) % len(s.pts)]:
                raise SystemExit("%s:%d: sector %s repeats a point" % (path, s.line, s.name))
            for v in s.pts[i]:
                if not -32000 <= v <= 32000:
                    raise SystemExit("%s:%d: sector %s point outside +-31.25 m" % (path, s.line, s.name))
    # portals: an edge matched by the same edge reversed in another sector
    edges = {}
    for si, s in enumerate(secs):
        for i in range(len(s.pts)):
            edges[(s.pts[i], s.pts[(i + 1) % len(s.pts)])] = (si, i)
    for si, s in enumerate(secs):
        s.next = []
        for i in range(len(s.pts)):
            a, b = s.pts[i], s.pts[(i + 1) % len(s.pts)]
            o = edges.get((b, a))
            if o and o[0] == si:
                raise SystemExit("%s:%d: sector %s touches itself" % (path, s.line, s.name))
            s.next.append(o[0] if o else None)
    # unmatched edges that overlap another sector's edge are split edges (T-junctions): not allowed
    segs = [(si, i, s.pts[i], s.pts[(i + 1) % len(s.pts)]) for si, s in enumerate(secs) for i in range(len(s.pts)) if s.next[i] is None]
    for ai in range(len(segs)):
        sa, ia, a0, a1 = segs[ai]
        for bi in range(ai + 1, len(segs)):
            sb, ib, b0, b1 = segs[bi]
            if sa == sb:
                continue
            if overlap(a0, a1, b0, b1):
                raise SystemExit("%s: wall %d of %s and wall %d of %s overlap but don't share both points - split them"
                                 % (path, ia, secs[sa].name, ib, secs[sb].name))

def overlap(a0, a1, b0, b1):
    dx, dz = a1[0] - a0[0], a1[1] - a0[1]
    def cr(p): return dx * (p[1] - a0[1]) - dz * (p[0] - a0[0])
    if cr(b0) != 0 or cr(b1) != 0:
        return False
    L = dx * dx + dz * dz
    def t(p): return (dx * (p[0] - a0[0]) + dz * (p[1] - a0[1])) / L
    lo, hi = sorted((t(b0), t(b1)))
    return min(hi, 1) - max(lo, 0) > 1e-6

# ------------------------------------------------------------------ props -> opcode stream
def compile_props(path, rooms, secs, lights, sprites, props):
    out = []
    sec_of = lambda x, z: next((s for s in secs if inside(s.pts, x, z)), None)
    cur, base = [-1], [0]
    def at(x, z, absolute=False, h=None):
        s = sec_of(x, z)
        r = rooms.index(s.room) if s else 0
        if r != cur[0]:
            out.extend([OP["CUR"], r]); cur[0] = r
        # props stand on the floor of the sector they're in (sloped floors: the hinge height);
        # props given in absolute heights (boxes, cylinders, rails, fans, lights) don't move
        b = 0 if absolute else (h if h is not None else (s.floor if s else 0))
        if b != base[0]:
            out.extend([OP["BASE"], b]); base[0] = b
    for (x, z, h, c, rad) in lights:
        at(x, z, True); out += [OP["LIGHT"], x, -h, z] + c + [rad]
    for (x, z, h, size, c, anim) in sprites:
        at(x, z, True); out += [OP["SPRITE"], x, -h, z, size] + c + [anim]
    for ln, t in props:
        try:
            k = t[0]
            if k in ("box", "boxf"):
                (x0, z0), (x1, z1) = pt(t[1]), pt(t[2]); h0, h1 = m(t[3]), m(t[4])
                top, side = tile(t[5]), tile(t[6])
                top = -1 if top == 255 else top
                cell, flags, i = 1000, 0, 7
                front, nd, emit = None, 0, 0
                if k == "boxf":
                    front, nd = tile(t[7]), ND[t[8]]; i = 9
                while i < len(t):
                    if t[i] == "cell": cell = m(t[i + 1]); i += 2
                    elif t[i] == "noshadow": flags |= 1; i += 1
                    elif t[i] == "nocoll": flags |= 2; i += 1
                    elif t[i] == "emit": emit = int(t[i + 1]); i += 2
                    else: raise MapError("unknown box key %r" % t[i])
                at((x0 + x1) // 2, (z0 + z1) // 2, True)
                out += [OP["BOX"] if k == "box" else OP["BOXF"], min(x0, x1), -h1, min(z0, z1), max(x0, x1), -h0, max(z0, z1), top, side, cell, flags]
                if k == "boxf": out += [front, nd, emit]
            elif k == "crate":
                x, z = pt(t[1]); at(x, z); out += [OP["CRATE"], x, z, m(t[2]), m(t[3])]
            elif k == "can":
                x, z = pt(t[1]); at(x, z); out += [OP["CAN"], x, z]
            elif k == "pillar":
                x, z = pt(t[1]); at(x, z); out += [OP["PILLAR"], x, z, m(t[2])]
            elif k == "console":
                x, z = pt(t[1]); at(x, z, h=m(t[4]) if len(t) > 4 else None)
                scr = {"graph": TILES["SCR_GRAPH"], "vitals": TILES["SCR_VITALS"]}[t[3]]
                out += [OP["CONSOLE"], x, z, FACING[t[2]], scr, 0]
            elif k == "machine":
                (x0, z0), (x1, z1) = pt(t[1]), pt(t[2]); at((x0 + x1) // 2, (z0 + z1) // 2)
                out += [OP["MACHINE"], min(x0, x1), min(z0, z1), max(x0, x1), max(z0, z1), m(t[3]), ND[t[4]]]
            elif k == "pod":
                x, z = pt(t[1]); at(x, z); out += [OP["POD"], x, z, FACING[t[2]], int(t[3])]
            elif k == "rack":
                x, z = pt(t[1]); at(x, z); out += [OP["RACK"], x, z, ND[t[2]]]
            elif k == "fan":
                x, z, h = t[1].split(","); x, z, h = m(x), m(z), m(h); at(x, z, True)
                out += [OP["FAN"], x, -h, z, m(t[2]), int(t[3])]
            elif k == "rail":
                (x0, z0), (x1, z1) = pt(t[1]), pt(t[2]); rb = m(t[3]); at((x0 + x1) // 2, (z0 + z1) // 2, True)
                base_ = rb
                if z0 == z1: out += [OP["RAILX"], x0, x1, z0, base_]
                elif x0 == x1: out += [OP["RAILZ"], z0, z1, x0, base_]
                else: raise MapError("rails run along x or z")
            elif k == "cyl":
                x, z, h = t[1].split(","); x, z, h = m(x), m(z), m(h); at(x, z, True)
                r, ln_, n, tl = m(t[2]), m(t[3]), int(t[4]), tile(t[5])
                axis, flags, anim, emit, cap, rep, i = 1, 0, 0, 0, -1, 1, 6
                while i < len(t):
                    if t[i] == "axis": axis = "xyz".index(t[i + 1]); i += 2
                    elif t[i] == "add": flags |= F_ADD; i += 1
                    elif t[i] == "anim": anim = int(t[i + 1]); i += 2
                    elif t[i] == "emit": emit = int(t[i + 1]); flags |= F_EMIT; i += 2
                    elif t[i] == "pulse": flags |= F_PULSE; i += 1
                    elif t[i] == "cap": cap = tile(t[i + 1]); i += 2
                    elif t[i] == "rep": rep = int(t[i + 1]); i += 2
                    else: raise MapError("unknown cyl key %r" % t[i])
                # y-axis cylinders grow upward from h (length up = negative y)
                out += [OP["CYL"], x, -h, z, r, 0, -ln_ if axis == 1 else ln_, n, tl, flags, anim, emit, axis, cap, rep]
            elif k in ("tank", "core"):
                x, z = pt(t[1]); at(x, z)
                out += [OP["SPECIAL"], 0 if k == "tank" else 2, x, z, 1]
            elif k == "cell":
                x, z = pt(t[1]); at(x, z); out += [OP["SPECIAL"], 3, x, z, FACING[t[2]]]
            else:
                raise MapError("unknown prop %r" % k)
        except (MapError, IndexError, ValueError, KeyError) as e:
            raise SystemExit("%s:%d: %s" % (path, ln, e))
    out.append(OP["END"])
    for v in out:
        if not -32768 <= v <= 32767:
            raise SystemExit("%s: prop value %d out of range" % (path, v))
    return out

# ------------------------------------------------------------------ emit C
def spec_wall(d):
    if d is None:
        return None
    b = [len(d["rows"]), d["top"]]
    for r in d["rows"]:
        b += [len(r)] + r
    b += [len(d["cells"])]
    for c, r, t in d["cells"]:
        b += [c, r, t]
    return b

def spec_flat(f):
    b = [f["def"], len(f["rules"])]
    for r in f["rules"]:
        b += list(r)
    b += [len(f["cells"])]
    for cx, cz, t in f["cells"]:
        b += [cx & 255, cz & 255, t]
    return b

def emit_c(name, rooms, secs, things, props):
    blob, cache = [], {}
    def add(b):
        if b is None:
            return 0xffff
        k = tuple(b)
        if k not in cache:
            cache[k] = len(blob); blob.extend(b)
        return cache[k]
    pts, walls, sdefs = [], [], []
    for si, s in enumerate(secs):
        p0 = len(pts)
        for i, p in enumerate(s.pts):
            pts.append(p)
            w = s.wall.get(i, {"mid": None, "low": None, "up": None, "flags": 0, "cells": []})
            mid = w["mid"] or s.walls_def or {"rows": [], "top": 255, "cells": []}
            if w["cells"]:
                mid = dict(mid); mid["cells"] = w["cells"]
            low = w["low"] or s.lows_def
            up = w["up"] or s.ups_def
            nxt = s.next[i]
            walls.append((0 if nxt is None else nxt + 1, add(spec_wall(mid)), add(spec_wall(low)), add(spec_wall(up)), w["flags"]))
        fs = s.fslope or (-1, 0); cs = s.cslope or (-1, 0)
        sdefs.append((p0, len(s.pts), s.floor, s.ceil, fs[0], cs[0], fs[1], cs[1],
                      add(spec_flat(s.flat["f"])), add(spec_flat(s.flat["c"])), rooms.index(s.room), s.kind, s.lock, s.ka, s.kb, s.name))
    if len(blob) >= 0xffff:
        raise SystemExit("map too big: spec blob")
    o = ["/* generated by tools/make_map.py from maps/%s.map - edit the map, not this file */" % name,
         '#include "map.h"', '#include "gen_lab.h"', ""]
    o.append("static const MapPt pts[] = {")
    for i in range(0, len(pts), 8):
        o.append("    " + " ".join("{ %d, %d }," % p for p in pts[i:i + 8]))
    o.append("};")
    o.append("static const MapWall walls[] = {")
    for i in range(0, len(walls), 4):
        o.append("    " + " ".join("{ %d, %d, %d, %d, %d }," % w for w in walls[i:i + 4]))
    o.append("};")
    o.append("static const MapSector secs[] = {")
    for d in sdefs:
        o.append("    { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, 0, %d, %d },   /* %s */" % d)
    o.append("};")
    o.append("static const u8 spec[] = {")
    for i in range(0, len(blob), 24):
        o.append("    " + ", ".join(str(v) for v in blob[i:i + 24]) + ",")
    o.append("};")
    o.append("static const s16 props[] = {")
    for i in range(0, len(props), 16):
        o.append("    " + ", ".join(str(v) for v in props[i:i + 16]) + ",")
    o.append("};")
    o.append("static const MapThing things[] = {")
    for t in things:
        o.append("    { %d, %d, %d, %d, %d, %d, %d }," % (t["type"], t["arg"], t["x"], t["z"], t["yaw"], t["ax"], t["az"]))
    if not things:
        o.append("    { 0, 0, 0, 0, 0, 0, 0 },")
    o.append("};")
    o.append("static const char *const rooms[] = { %s };" % ", ".join('"%s"' % room_names.get(r, r) for r in rooms))
    o.append("const MapDef map_%s = { \"%s\", pts, %d, walls, secs, %d, spec, props, things, %d, rooms, %d };"
             % (name, name, len(pts), len(secs), len(things), len(rooms)))
    return "\n".join(o) + "\n", len(blob)

# ------------------------------------------------------------------ preview
def preview(name, rooms, secs, lights, things, out_png):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("  (no Pillow: preview skipped)")
        return
    xs = [p[0] for s in secs for p in s.pts]; zs = [p[1] for s in secs for p in s.pts]
    x0, x1, z0, z1 = min(xs) - 3 * U, max(xs) + 3 * U, min(zs) - 3 * U, max(zs) + 3 * U
    S = 14.0 / U                                  # 14 px a metre
    W, H = int((x1 - x0) * S), int((z1 - z0) * S)
    img = Image.new("RGB", (W, H), (24, 22, 26)); d = ImageDraw.Draw(img)
    P = lambda x, z: ((x - x0) * S, (z1 - z) * S)
    fl = [s.floor for s in secs]; lo, hi = min(fl), max(fl)
    for gx in range((x0 // TILE) * TILE, x1, TILE):
        d.line([P(gx, z0), P(gx, z1)], fill=(34, 32, 38))
    for gz in range((z0 // TILE) * TILE, z1, TILE):
        d.line([P(x0, gz), P(x1, gz)], fill=(34, 32, 38))
    for s in secs:
        k = (s.floor - lo) / (hi - lo) if hi > lo else 0.5
        c = (int(60 + 120 * k), int(70 + 90 * k), int(90 + 40 * k))
        if s.kind == 1: c = (150, 110, 60) if not s.lock else {1: (40, 170, 190), 2: (210, 150, 40), 3: (190, 40, 50)}[s.lock]
        if s.kind == 2: c = (110, 140, 70)
        if s.ceil <= s.floor and s.kind != 1: c = (50, 48, 52)
        d.polygon([P(*p) for p in s.pts], fill=c)
    for s in secs:
        for i in range(len(s.pts)):
            a, b = s.pts[i], s.pts[(i + 1) % len(s.pts)]
            if s.next[i] is None:
                d.line([P(*a), P(*b)], fill=(235, 230, 220), width=3)
            else:
                o = secs[s.next[i]]
                step = abs(o.floor - s.floor) > 220
                d.line([P(*a), P(*b)], fill=(200, 120, 90) if step else (110, 110, 120), width=1)
    for s in secs:
        cx = sum(p[0] for p in s.pts) / len(s.pts); cz = sum(p[1] for p in s.pts) / len(s.pts)
        if abs(area2(s.pts)) / 2 > 8 * U * U:
            d.text(P(cx, cz), "%g" % (s.floor / U), fill=(20, 20, 20), anchor="mm")
    for (x, z, h, c, rad) in lights:
        px, pz = P(x, z); d.ellipse([px - 2, pz - 2, px + 2, pz + 2], fill=(255, 230, 120))
    col = {0: (60, 240, 90), 1: (240, 60, 60), 2: (255, 255, 255), 3: (180, 120, 255), 4: (255, 200, 0)}
    way = [P(t["x"], t["z"]) for t in things if t["type"] in (0, 5)]
    if len(way) > 1:
        d.line(way, fill=(90, 200, 120), width=1)
    for t in things:
        if t["type"] == 5:
            continue
        px, pz = P(t["x"], t["z"]); c = col[t["type"]]
        if t["type"] == 2: c = {1: (40, 220, 240), 2: (250, 180, 40), 3: (230, 40, 60)}[t["arg"]]
        d.ellipse([px - 5, pz - 5, px + 5, pz + 5], fill=c, outline=(0, 0, 0))
        a = t["yaw"] * 2 * math.pi / 4096
        d.line([(px, pz), (px + math.sin(a) * 10, pz - math.cos(a) * 10)], fill=c, width=2)
    for r in rooms:
        ss = [s for s in secs if s.room == r]
        if not ss: continue
        bx = [p[0] for s in ss for p in s.pts]; bz = [p[1] for s in ss for p in s.pts]
        d.text(P((min(bx) + max(bx)) / 2, max(bz)), room_names.get(r, r), fill=(255, 255, 255), anchor="md")
    d.text((8, 8), "%s - floor height shades dark (low) to light (high); red edges are steps; dots: lights, things" % name, fill=(200, 200, 200))
    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    img.save(out_png)

def main():
    for path in sys.argv[1:] or [os.path.join(ROOT, "maps", "e1.map")]:
        name = os.path.splitext(os.path.basename(path))[0]
        rooms, secs, lights, sprites, things, props = parse(path)
        secs = expand_stairs(secs)
        finish(path, rooms, secs)
        ops = compile_props(path, rooms, secs, lights, sprites, props)
        c, nspec = emit_c(name, rooms, secs, things, ops)
        open(os.path.join(ROOT, "gen", "map_%s.c" % name), "w").write(c)
        preview(name, rooms, secs, lights, things, os.path.join(ROOT, "docs", "maps", "%s.png" % name))
        np_ = sum(len(s.pts) for s in secs)
        size = np_ * (4 + 9) + len(secs) * 28 + nspec + len(ops) * 2 + len(things) * 12
        print("%s: %d rooms, %d sectors, %d walls, %d portals, %d things, ~%d bytes" %
              (name, len(rooms), len(secs), np_, sum(1 for s in secs for n in s.next if n is not None), len(things), size))

if __name__ == "__main__":
    main()
