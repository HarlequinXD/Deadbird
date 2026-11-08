#!/usr/bin/env python3
"""
Builds Deadbird's level: the shinigami energy-extraction plant.

  Extraction Hall  - the giant tank holding a captured shinigami, smaller extraction pods,
                     a catwalk with stairs, consoles, pillars
  Reactor          - the soul reactor core, turbine generators, conduits
  Control Lab      - consoles, monitors, server racks, the vitals wall, an autopsy table,
                     specimen jars
  Containment      - caged shinigami, crates and soul canisters
  corridors        - pipes, cables, floor grates

Output (game/lab_data.h):
  * two 8bpp texture pages (TIM) of procedural sci-fi tiles with baked lighting and dither
  * a chunked mesh of textured quads with per-vertex colours: the lighting is baked here from
    coloured point lights, with shadow rays against every solid thing and ambient occlusion
    in corners and under props (the PS1 GPU then gouraud-shades and dithers it)
  * collision boxes, light halo sprites, spawn point, animated object anchors
Everything is sized for the PS1: 64x64 texels per tile, ~512-unit polygons, s16 coordinates.
"""
import os, struct, math
import numpy as np
import lab_textures as TX

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TA = {n: i for i, n in enumerate(TX.NAMES_A)}
TB = {n: i for i, n in enumerate(TX.NAMES_B)}

# face flags (must match game/lab.c)
F_QUAD, F_ADD, F_HALF, F_DOUBLE, F_PULSE, F_FLOOR, F_EMIT = 1, 2, 4, 8, 16, 32, 64

faces = []        # dict(p=[4 or 3 xyz], uv=[(u,v)], page, flags, anim, emit)
occluders = []    # (x0,y0,z0,x1,y1,z1) blocks light
colliders = []    # (x0,z0,x1,z1,top_h)  solid from the floor up to y = -top_h
lights = []       # (pos, rgb(0..), radius)
sprites = []      # (x,y,z,size,r,g,b,anim)
anchors = {}      # named positions for animated things drawn in C
fans = []         # (x,y,z, axis, radius, speed)
rings = []        # (x, z, r, y_top, y_bot)
ceilings = []     # (x0,z0,x1,z1,height) for the camera


def V(*a):
    return np.array(a, float)


def tile_uv(page, tile, u0=0, v0=0, u1=63, v1=63):
    tx, ty = (tile % 4) * 64, (tile // 4) * 64
    return [(tx + u0, ty + v0), (tx + u1, ty + v0), (tx + u0, ty + v1), (tx + u1, ty + v1)]


def tid(name):
    if name in TA:
        return 0, TA[name]
    return 1, TB[name]


def add_quad(o, U, V_, tex, flags=0, anim=0, emit=None, uvr=None, nd=None):
    """quad o, o+U, o+V, o+U+V (PS1 Z order). Visible side has normal V x U; if nd is given
    the quad is flipped to face that way."""
    o, U, V_ = np.asarray(o, float), np.asarray(U, float), np.asarray(V_, float)
    if nd is not None and np.dot(np.cross(V_, U), nd) < 0:
        o = o + U
        U = -U
    page, t = tid(tex)
    uv = tile_uv(page, t, *(uvr or (0, 0, 63, 63)))
    faces.append(dict(p=[o, o + U, o + V_, o + U + V_], uv=uv, page=page, flags=flags | F_QUAD,
                      anim=anim, emit=emit))


def add_tri(a, b, c, tex, flags=0, anim=0, emit=None, nd=None):
    a, b, c = (np.asarray(x, float) for x in (a, b, c))
    if nd is not None and np.dot(np.cross(c - a, b - a), nd) < 0:
        b, c = c, b
    page, t = tid(tex)
    uv = tile_uv(page, t)
    faces.append(dict(p=[a, b, c], uv=[(uv[0][0] + 32, uv[0][1]), uv[2], uv[3]], page=page, flags=flags & ~F_QUAD,
                      anim=anim, emit=emit))


def grid(o, U, V_, cell, tex, flags=0, anim=0, emit=None, nd=None, texf=None, maxn=16):
    """subdivide a rectangle into ~cell-sized quads, one texture repeat each"""
    o, U, V_ = np.asarray(o, float), np.asarray(U, float), np.asarray(V_, float)
    nu = max(1, min(maxn, int(round(np.linalg.norm(U) / cell))))
    nv = max(1, min(maxn, int(round(np.linalg.norm(V_) / cell))))
    for j in range(nv):
        for i in range(nu):
            t = texf(i, j, nu, nv) if texf else tex
            add_quad(o + U * i / nu + V_ * j / nv, U / nu, V_ / nv, t, flags, anim, emit, nd=nd)


def box(x0, y0, z0, x1, y1, z1, top='dark_metal', side='dark_metal', front=None, cell=600, nd_front=None,
        occl=True, collide=True, flags=0, emit_front=None, anim=0, sides=(1, 1, 1, 1), bottom=False):
    """solid block (y0 above y1 in Y-down space): top + four sides; `front` texture on the
    face whose outward normal is nd_front (one quad, not tiled - for screens, panels, signs)"""
    if top:
        grid(V(x0, y0, z0), V(x1 - x0, 0, 0), V(0, 0, z1 - z0), cell, top, flags, nd=V(0, -1, 0))
    if bottom:
        grid(V(x0, y1, z0), V(x1 - x0, 0, 0), V(0, 0, z1 - z0), cell, top or side, flags, nd=V(0, 1, 0))
    sd = [(V(x0, y0, z1), V(x1 - x0, 0, 0), V(0, 0, 1)),
          (V(x0, y0, z0), V(x1 - x0, 0, 0), V(0, 0, -1)),
          (V(x0, y0, z0), V(0, 0, z1 - z0), V(-1, 0, 0)),
          (V(x1, y0, z0), V(0, 0, z1 - z0), V(1, 0, 0))]
    for k, (o, U, n) in enumerate(sd):
        if not sides[k]:
            continue
        H = V(0, y1 - y0, 0)
        if front and nd_front is not None and np.dot(n, nd_front) > 0.9:
            add_quad(o, U, H, front, flags | (F_EMIT if emit_front else 0), anim, emit_front, nd=n)
        else:
            grid(o, U, H, cell, side, flags, nd=n)
    if occl:
        occluders.append((x0, y0, z0, x1, y1, z1))
    if collide:
        colliders.append((min(x0, x1), min(z0, z1), max(x0, x1), max(z0, z1), -y0))


def cylinder(c, r, y0, y1, n, tex, flags=0, anim=0, emit=None, inward=False, axis='y', cap=None, rep=1):
    """n-sided cylinder along an axis; c is the centre of the base on that axis (y0..y1 span)"""
    c = np.asarray(c, float)
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
        def ring(a, h):
            if axis == 'y':
                return c + V(math.cos(a) * r, h, math.sin(a) * r)
            if axis == 'x':
                return c + V(h, math.cos(a) * r, math.sin(a) * r)
            return c + V(math.cos(a) * r, math.sin(a) * r, h)
        p0, p1 = ring(a0, y0), ring(a1, y0)
        mid = (a0 + a1) / 2
        nrm = (ring(mid, 0) - c)
        nrm /= np.linalg.norm(nrm)
        if inward:
            nrm = -nrm
        U = p1 - p0
        Vv = ring(a0, y1) - p0
        page, t = tid(tex)
        uv = tile_uv(page, t)
        if rep > 1:
            for k in range(rep):
                add_quad(p0 + Vv * k / rep, U, Vv / rep, tex, flags, anim, emit, nd=nrm)
        else:
            add_quad(p0, U, Vv, tex, flags, anim, emit, nd=nrm)
    if cap and axis == 'y':
        ctr = c + V(0, y1, 0)
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            p0 = c + V(math.cos(a0) * r, y1, math.sin(a0) * r)
            p1 = c + V(math.cos(a1) * r, y1, math.sin(a1) * r)
            add_tri(ctr, p0, p1, cap, flags, nd=V(0, -1 if y1 < y0 else 1, 0))


def cyl_collide(x, z, r, h):
    """cylinder -> two crossed boxes (an octagon)"""
    k = r * 0.72
    colliders.append((x - r, z - k, x + r, z + k, h))
    colliders.append((x - k, z - r, x + k, z + r, h))
    occluders.append((x - k, -h, z - k, x + k, 0, z + k))


# ------------------------------------------------------------------ rooms
DOOR_H = 2600
WALL_T = 240


def room(x0, z0, x1, z1, H, doors=(), open_ends=(), floor='floor_plate', lights_on=True, name='',
         upper='wall_panel', trim=True):
    """interior: floor, ceiling, four walls (with door openings), wall colliders.
    doors: list of (side, centre, width) with side in 'N','S','E','W' (N = +z, E = +x)."""
    ceilings.append((x0, z0, x1, z1, H))
    # floor: plates, with grates along the walls
    def ftex(i, j, nu, nv):
        if floor != 'floor_plate':
            return floor
        return 'floor_grate' if (i in (0, nu - 1) or j in (0, nv - 1)) and (i + j) % 3 == 0 else 'floor_plate'
    grid(V(x0, 0, z0), V(x1 - x0, 0, 0), V(0, 0, z1 - z0), 512, floor, F_FLOOR, nd=V(0, -1, 0), texf=ftex, maxn=40)
    # ceiling: dark plates with light panels
    def ctex(i, j, nu, nv):
        return 'ceiling_light' if (i % 3 == 1 and j % 3 == 1) else 'dark_metal'
    nu = max(1, int((x1 - x0) / 1000)); nv = max(1, int((z1 - z0) / 1000))
    for j in range(nv):
        for i in range(nu):
            t = ctex(i, j, nu, nv)
            o = V(x0 + (x1 - x0) * i / nu, -H, z0 + (z1 - z0) * j / nv)
            add_quad(o, V((x1 - x0) / nu, 0, 0), V(0, 0, (z1 - z0) / nv), t,
                     F_EMIT if t == 'ceiling_light' else 0, 0, (150, 160, 170) if t == 'ceiling_light' else None, nd=V(0, 1, 0))
    # walls
    sides = {'S': (V(x0, 0, z0), V(1, 0, 0), x1 - x0, V(0, 0, 1)),
             'N': (V(x0, 0, z1), V(1, 0, 0), x1 - x0, V(0, 0, -1)),
             'W': (V(x0, 0, z0), V(0, 0, 1), z1 - z0, V(1, 0, 0)),
             'E': (V(x1, 0, z0), V(0, 0, 1), z1 - z0, V(-1, 0, 0))}
    for s, (o, d, L, n) in sides.items():
        if s in open_ends:
            continue
        holes = sorted([(c - w / 2, c + w / 2) for (ds, c, w) in doors if ds == s])
        # spans along the wall: solid between holes (full height), above holes (from DOOR_H up)
        cuts = [0]
        for a, b in holes:
            cuts += [a, b]
        cuts.append(L)
        for k in range(0, len(cuts), 2):
            a, b = cuts[k], cuts[k + 1]
            if b - a > 1:
                wall_strip(o + d * a, d * (b - a), 0, H, n, upper, trim)
                # collider behind this strip
                p0 = o + d * a - n * WALL_T
                p1 = o + d * b
                colliders.append((min(p0[0], p1[0]), min(p0[2], p1[2]), max(p0[0], p1[0]), max(p0[2], p1[2]), 8000))
                occluders.append((min(p0[0], p1[0]), -H - 10, min(p0[2], p1[2]), max(p0[0], p1[0]), 10, max(p0[2], p1[2])))
        for a, b in holes:
            if H > DOOR_H:
                wall_strip(o + d * a, d * (b - a), DOOR_H, H, n, upper, False)
                occluders.append((min((o + d * a - n * WALL_T)[0], (o + d * b)[0]), -H - 10, min((o + d * a - n * WALL_T)[2], (o + d * b)[2]),
                                  max((o + d * a - n * WALL_T)[0], (o + d * b)[0]), -DOOR_H, max((o + d * a - n * WALL_T)[2], (o + d * b)[2])))
            door_frame(o + d * a, d * (b - a), n)


def wall_strip(o, U, ya, yb, n, upper, trim):
    """wall from height ya to yb (positive heights, Y up) along U at base o, facing n"""
    lo = min(yb, 700)
    if ya < lo:
        grid(o + V(0, -lo, 0), U, V(0, lo - ya, 0), 512, 'wall_lower', nd=n)
    top = yb
    mid0 = max(ya, lo)
    if trim and top - mid0 > 900:
        # panels up to 3000, then circuitry / vents, then a beam under the ceiling
        p_top = min(top - 300, 3000)
        def wtex(i, j, nu, nv):
            return 'wall_vent' if (i % 4 == 2 and j == nv - 1) else upper
        grid(o + V(0, -p_top, 0), U, V(0, p_top - mid0, 0), 640, upper, nd=n, texf=wtex)
        if top - 300 > p_top:
            grid(o + V(0, -(top - 300), 0), U, V(0, top - 300 - p_top, 0), 640, 'circuit', nd=n)
        grid(o + V(0, -top, 0), U, V(0, 300, 0), 640, 'beam', nd=n)
    elif top > mid0:
        grid(o + V(0, -top, 0), U, V(0, top - mid0, 0), 640, upper, nd=n)


def door_frame(o, U, n):
    """bulkhead reveal through the wall thickness + hazard lintel"""
    L = np.linalg.norm(U)
    d = U / L
    back = -n * WALL_T
    # jambs
    add_quad(o + V(0, -DOOR_H, 0), back, V(0, DOOR_H, 0), 'bulkhead', nd=d)
    add_quad(o + U + V(0, -DOOR_H, 0), back, V(0, DOOR_H, 0), 'bulkhead', nd=-d)
    # soffit
    add_quad(o + V(0, -DOOR_H, 0), U, back, 'hazard', nd=V(0, 1, 0))
    # lintel stripe on the room side
    add_quad(o + V(0, -DOOR_H - 200, 0) + n * 4, U, V(0, 200, 0), 'hazard', nd=n)


def corridor(x0, z0, x1, z1, axis):
    """corridor with open ends; pipes along the walls, cables under the ceiling, grates"""
    H = DOOR_H
    room(x0, z0, x1, z1, H, open_ends=('W', 'E') if axis == 'x' else ('S', 'N'), floor='floor_plate', trim=False,
         upper='wall_panel')
    if axis == 'x':
        for zz, yy in ((z0 + 160, -1900), (z0 + 160, -2150), (z1 - 160, -1900)):
            cylinder(V(x0, yy, zz), 90, 0, x1 - x0, 6, 'pipe', axis='x')
        grid(V(x0, -H + 20, z0 + 600), V(x1 - x0, 0, 0), V(0, 0, 400), 640, 'cables', nd=V(0, 1, 0))
        # floor light strip down the middle
        grid(V(x0, -2, (z0 + z1) / 2 - 90), V(x1 - x0, 0, 0), V(0, 0, 180), 640, 'floor_light', F_EMIT | F_FLOOR,
             emit=(130, 140, 140), nd=V(0, -1, 0))
        lights.append((V((x0 + x1) / 2, -2300, (z0 + z1) / 2), V(0.7, 0.9, 0.95), 4500))
    else:
        for xx, yy in ((x0 + 160, -1900), (x0 + 160, -2150), (x1 - 160, -1900)):
            cylinder(V(xx, yy, z0), 90, 0, z1 - z0, 6, 'pipe', axis='z')
        grid(V(x0 + 600, -H + 20, z0), V(400, 0, 0), V(0, 0, z1 - z0), 640, 'cables', nd=V(0, 1, 0))
        grid(V((x0 + x1) / 2 - 90, -2, z0), V(180, 0, 0), V(0, 0, z1 - z0), 640, 'floor_light', F_EMIT | F_FLOOR,
             emit=(130, 140, 140), nd=V(0, -1, 0))
        lights.append((V((x0 + x1) / 2, -2300, (z0 + z1) / 2), V(0.7, 0.9, 0.95), 4500))


# ------------------------------------------------------------------ props
def captive(x, y, z, s=1.0, facing=0.0):
    """a low-poly caged / tanked shinigami: dark feathered body, bird skull, spread wings"""
    c, sn = math.cos(facing), math.sin(facing)
    def P(px, py, pz):
        return V(x + (px * c + pz * sn) * s, y + py * s, z + (-px * sn + pz * c) * s)
    def blk(a, b, tex, front=None):
        (ax, ay, az), (bx, by, bz) = a, b
        corners = [P(ax, ay, az), P(bx, ay, az), P(ax, by, az), P(bx, by, az), P(ax, ay, bz), P(bx, ay, bz), P(ax, by, bz), P(bx, by, bz)]
        # 6 faces, outward
        F = [(0, 1, 2, V(0, 0, -1)), (4, 5, 6, V(0, 0, 1)), (0, 4, 2, V(-1, 0, 0)), (1, 5, 3, V(1, 0, 0)), (0, 1, 4, V(0, -1, 0)), (2, 3, 6, V(0, 1, 0))]
        for (i0, i1, i2, n) in F:
            nn = V(n[0] * c + n[2] * sn, n[1], -n[0] * sn + n[2] * c)
            t = front if (front and n[2] > 0.5) else tex
            add_quad(corners[i0], corners[i1] - corners[i0], corners[i2] - corners[i0], t, nd=nn)
    blk((-160, -1100, -90), (160, -500, 90), 'shini_body')          # torso
    blk((-130, -1450, -130), (130, -1150, 130), 'shini_body', 'shini_skull')   # skull
    blk((-110, -500, -70), (-30, 0, 70), 'shini_body')              # legs
    blk((30, -500, -70), (110, 0, 70), 'shini_body')
    blk((-520, -1080, -40), (-160, -980, 40), 'shini_body')         # arms out
    blk((160, -1080, -40), (520, -980, 40), 'shini_body')
    # wings: double-sided feather sheets
    add_quad(P(-160, -1200, -100), P(-900, -1500, -300) - P(-160, -1200, -100), P(-160, -600, -100) - P(-160, -1200, -100),
             'shini_body', F_DOUBLE)
    add_quad(P(160, -1200, -100), P(900, -1500, -300) - P(160, -1200, -100), P(160, -600, -100) - P(160, -1200, -100),
             'shini_body', F_DOUBLE)


def extraction_pod(x, z, facing, anim):
    """wall pod: base, glass tube, captive, top machine, pipes to the ceiling, side console"""
    r = 420
    cylinder(V(x, 0, z), r + 120, 0, -380, 8, 'machine_base', cap='hazard')
    cylinder(V(x, -380, z), r, 0, -2300, 8, 'glass', F_ADD, anim, rep=2)
    cylinder(V(x, -2680, z), r + 120, 0, -600, 8, 'dark_metal', cap='dark_metal')
    for k in range(3):
        a = k * 2.1 + 0.3
        cylinder(V(x + math.cos(a) * 260, -3280, z + math.sin(a) * 260), 70, 0, -1200, 5, 'pipe')
    captive(x, -500, z, 0.95, facing)
    cyl_collide(x, z, r + 120, 3300)
    lights.append((V(x, -1500, z), V(0.25, 0.85, 1.0), 3200))
    sprites.append((x, -2650, z, 380, 60, 200, 230, anim))
    anchors.setdefault('pods', []).append((x, z, r))
    # console beside it
    cx, cz = x + math.cos(facing) * 900, z - math.sin(facing) * 900
    console(cx, cz, facing + math.pi, 'screen_vitals')


def console(x, z, facing, screen='screen_graph'):
    """desk + slanted keyboard + monitor; facing = direction the operator looks (radians, 0 = +z)"""
    c, s = math.cos(facing), math.sin(facing)
    fwd, right = V(s, 0, c), V(c, 0, -s)
    o = V(x, 0, z)
    w, d = 420, 260
    # body: four sides + top as oriented quads
    corners = [o - right * w - fwd * d, o + right * w - fwd * d, o + right * w + fwd * d, o - right * w + fwd * d]
    for k in range(4):
        a, b = corners[k], corners[(k + 1) % 4]
        n = np.cross(V(0, 1, 0), b - a); n /= np.linalg.norm(n)
        n = -n if np.dot(n, ((a + b) / 2 - o)) < 0 else n
        add_quad(a + V(0, -800, 0), b - a, V(0, 800, 0), 'machine_panel', nd=n)
    add_quad(corners[0] + V(0, -800, 0), corners[1] - corners[0], corners[3] - corners[0], 'keyboard', nd=V(0, -1, 0))
    scr = o + fwd * (d - 40) - right * 380 + V(0, -1500, 0)
    add_quad(scr, right * 760, V(0, 560, 0) + fwd * 0, screen, F_EMIT, 1, (150, 150, 150), nd=-fwd)
    add_quad(scr + fwd * 60, right * 760, V(0, 560, 0), 'dark_metal', nd=fwd)
    add_quad(scr + V(0, 0, 0), fwd * 60, V(0, 560, 0), 'dark_metal', nd=-right)
    add_quad(scr + right * 760, fwd * 60, V(0, 560, 0), 'dark_metal', nd=right)
    add_quad(scr, right * 760, fwd * 60, 'dark_metal', nd=V(0, -1, 0))
    xs = [p[0] for p in corners]; zs = [p[2] for p in corners]
    colliders.append((min(xs), min(zs), max(xs), max(zs), 800))
    occluders.append((min(xs), -800, min(zs), max(xs), 0, max(zs)))
    lights.append((scr + right * 380 - fwd * 300 + V(0, 280, 0), V(0.35, 0.5, 0.45) if screen == 'screen_graph' else V(0.6, 0.2, 0.15), 1300))


def rack(x, z, nd):
    """server rack against a wall; nd = facing direction (unit along x or z)"""
    nd = np.asarray(nd, float)
    if abs(nd[0]) > 0.5:
        box(x - 250, -2200, z - 350, x + 250, 0, z + 350, 'dark_metal', 'dark_metal', 'server_rack', nd_front=nd,
            emit_front=(120, 120, 120), cell=800)
    else:
        box(x - 350, -2200, z - 250, x + 350, 0, z + 250, 'dark_metal', 'dark_metal', 'server_rack', nd_front=nd,
            emit_front=(120, 120, 120), cell=800)


def crate(x, z, s=500, h=None, top='crate'):
    h = h or s
    box(x - s / 2, -h, z - s / 2, x + s / 2, 0, z + s / 2, top, 'crate', cell=s)


def canister(x, z):
    cylinder(V(x, 0, z), 200, 0, -700, 8, 'canister', cap='dark_metal')
    cyl_collide(x, z, 200, 700)


def pillar(x, z, H):
    box(x - 300, -H, z - 300, x + 300, 0, z + 300, None, 'beam', cell=700, occl=True)
    box(x - 340, -500, z - 340, x + 340, 0, z + 340, 'hazard', 'hazard', cell=700, collide=False, occl=False)


# ------------------------------------------------------------------ the plant
# Extraction Hall
HALL = (-4500, -4500, 4500, 4500, 4600)
room(*HALL[:4], HALL[4], doors=[('N', 4500, 2000), ('E', 4500, 2000), ('W', 6500, 2000)], name='hall')
# the great tank in the middle
TANK_R = 1000
cylinder(V(0, 0, 0), TANK_R + 300, 0, -600, 12, 'machine_base', cap='hazard')
cylinder(V(0, -600, 0), TANK_R, 0, -2800, 12, 'glass', F_ADD, 2, rep=2)
cylinder(V(0, -3400, 0), TANK_R + 260, 0, -700, 12, 'dark_metal', cap='dark_metal')
for k in range(6):                       # feed pipes up into the ceiling
    a = k * math.pi / 3 + 0.3
    cylinder(V(math.cos(a) * 700, -4100, math.sin(a) * 700), 130, 0, -500, 6, 'pipe')
for k in range(4):                       # cable trunks along the floor to the walls
    a = k * math.pi / 2 + math.pi / 4
    x0, z0 = math.cos(a) * (TANK_R + 300), math.sin(a) * (TANK_R + 300)
    x1, z1 = math.cos(a) * 3400, math.sin(a) * 3400
    d = V(x1 - x0, 0, z1 - z0)
    side = np.cross(d, V(0, 1, 0)); side = side / np.linalg.norm(side) * 160
    add_quad(V(x0, -40, z0) - side, d, side * 2, 'cables', F_FLOOR, nd=V(0, -1, 0))
# glowing ring on the floor round the base
for k in range(12):
    a0, a1 = 2 * math.pi * k / 12, 2 * math.pi * (k + 1) / 12
    r0, r1 = TANK_R + 320, TANK_R + 520
    p0 = V(math.cos(a0) * r0, -3, math.sin(a0) * r0)
    p1 = V(math.cos(a1) * r0, -3, math.sin(a1) * r0)
    p2 = V(math.cos(a0) * r1, -3, math.sin(a0) * r1)
    faces.append(dict(p=[p0, p1, p2, V(math.cos(a1) * r1, -3, math.sin(a1) * r1)],
                      uv=tile_uv(0, TA['floor_light']), page=0, flags=F_QUAD | F_EMIT | F_FLOOR | F_PULSE, anim=2, emit=(140, 150, 150)))
    if np.dot(np.cross(p2 - p0, p1 - p0), V(0, -1, 0)) < 0:
        f = faces[-1]; f['p'] = [f['p'][1], f['p'][0], f['p'][3], f['p'][2]]
cyl_collide(0, 0, TANK_R + 300, 4600)
anchors['tank'] = (0, -2000, 0)
rings.append((0, 0, TANK_R - 60, -3300, -700))
lights.append((V(0, -2000, 0), V(0.3, 1.0, 1.3), 9000))
lights.append((V(0, -500, 0), V(0.2, 0.7, 0.9), 3000))
sprites.append((0, -3450, 0, 900, 70, 210, 240, 2))
# pods along the west and south walls
extraction_pod(-3800, -1200, math.pi / 2, 3)
extraction_pod(-3800, 600, math.pi / 2, 4)
extraction_pod(-1800, -3800, 0.0, 3)
extraction_pod(1800, -3800, 0.0, 4)
# pillars
for (px, pz) in ((-2600, 2600), (2600, -2400)):
    pillar(px, pz, 4600)
# catwalk along the north-east corner with stairs up from the hall floor
CW_H = 1040
box(2400, -CW_H, 2600, 4500, 0, 4500, 'floor_grate', 'wall_lower', cell=520)
for i in range(8):
    sx0 = 600 + i * 220
    box(sx0, -(i + 1) * 130, 3300, sx0 + 220 if i < 7 else 2400, 0, 4500, 'stair_tread', 'dark_metal', cell=1200,
        occl=(i > 3))
for (a, b) in (((2400, 2600), (4500, 2600)), ((2400, 2600), (2400, 3300))):        # railings
    (ax, az), (bx, bz) = a, b
    if ax == bx:
        box(ax - 30, -CW_H - 900, az, ax + 30, -CW_H, bz, 'beam', 'beam', cell=900, occl=False, collide=False)
        colliders.append((ax - 40, az, ax + 40, bz, CW_H + 900))
    else:
        box(ax, -CW_H - 900, az - 30, bx, -CW_H, az + 30, 'beam', 'beam', cell=900, occl=False, collide=False)
        colliders.append((ax, az - 40, bx, az + 40, CW_H + 900))
console(3300, -1900, math.pi / 2, 'screen_vitals')
# warning signs on the walls
add_quad(V(-1000, -2600, 4500 - 3), V(1000, 0, 0) * -1, V(0, 1000, 0), 'sign', nd=V(0, 0, -1))
add_quad(V(4500 - 3, -2600, 2000), V(0, 0, 1000), V(0, 1000, 0), 'sign', nd=V(-1, 0, 0))
for (lx, lz) in ((-2500, -2500), (2500, -2500), (-2500, 2500), (2500, 2500)):
    lights.append((V(lx, -4300, lz), V(0.8, 0.84, 0.92), 8000))
lights.append((V(4300, -2900, 0), V(1.3, 0.15, 0.1), 2400))          # red alarm by the reactor door
sprites.append((4380, -2900, 0, 260, 255, 40, 30, 5))
crate(-4200, 4200, 600); crate(-3600, 4250, 500); crate(-4250, 3600, 500, 1000)
canister(-2900, 4280); canister(-2450, 4280)

# Corridor east -> Reactor
corridor(4500, -1000, 8000, 1000, 'x')
REACT = (8000, -4000, 15000, 4000, 5600)
room(*REACT[:4], REACT[4], doors=[('W', 4000, 2000)], name='reactor')
RC = (11800, 0)
box(RC[0] - 1600, -300, RC[1] - 1600, RC[0] + 1600, 0, RC[1] + 1600, 'hazard', 'wall_lower', cell=800)
cylinder(V(RC[0], -300, RC[1]), 950, 0, -3400, 12, 'coil', rep=3)
for k, yb in enumerate((-1000, -2000, -3000)):
    cylinder(V(RC[0], yb, RC[1]), 1010, 0, -260, 12, 'energy', F_EMIT | F_PULSE, 6 + k % 2, emit=(170, 170, 170))
cylinder(V(RC[0], -3700, RC[1]), 1250, 0, -500, 12, 'dark_metal', cap='dark_metal')
for k in range(4):                       # conduits to the walls
    a = k * math.pi / 2
    if k % 2 == 0:
        cylinder(V(RC[0] + (1200 if k == 0 else -3800), -3950, RC[1]), 200, 0, 2600, 8, 'pipe', axis='x')
    else:
        cylinder(V(RC[0], -3950, RC[1] + (1200 if k == 1 else -3800)), 200, 0, 2600, 8, 'pipe', axis='z')
cyl_collide(RC[0], RC[1], 1600, 300)
colliders.append((RC[0] - 1100, RC[1] - 1100, RC[0] + 1100, RC[1] + 1100, 5000))
occluders.append((RC[0] - 800, -3700, RC[1] - 800, RC[0] + 800, 0, RC[1] + 800))
lights.append((V(RC[0], -2000, RC[1]), V(1.5, 0.75, 0.3), 11000))
lights.append((V(RC[0], -4300, RC[1]), V(0.8, 0.5, 0.3), 5000))
sprites.append((RC[0], -2000, RC[1], 1700, 255, 150, 60, 6))
anchors['reactor'] = (RC[0], -2000, RC[1])
# turbine generators with fans on their faces
for gz in (-2900, 2900):
    box(9200, -2400, gz - 700, 11000, 0, gz + 700, 'dark_metal', 'machine_base', cell=700)
    fz = gz + 705 if gz < 0 else gz - 705
    fans.append((10100, -1200, fz, 'z', 600, 1 if gz < 0 else -1))
for (cx, cz, f) in ((14200, -2500, math.pi / 2), (14200, 2500, math.pi / 2), (14200, 0, math.pi / 2)):
    console(cx, cz, f, 'screen_graph')
for zz in (-3780, 3780):
    canister(8600, zz); canister(9000, zz)
for (lx, lz) in ((9500, -2600), (9500, 2600), (14000, -2600), (14000, 2600)):
    lights.append((V(lx, -5300, lz), V(0.7, 0.7, 0.76), 8500))
add_quad(V(8003, -2700, -1300), V(0, 0, -1000), V(0, 1000, 0), 'sign', nd=V(1, 0, 0))

# Corridor north -> Control Lab
corridor(-1000, 4500, 1000, 8000, 'z')
LAB = (-4000, 8000, 4000, 12500, 3400)
room(*LAB[:4], LAB[4], doors=[('S', 4000, 2000)], name='lab')
# vitals wall: big screens on the far wall
for k, scr in enumerate(('screen_graph', 'screen_vitals', 'screen_graph')):
    x0 = -2800 + k * 1900
    add_quad(V(x0 + 1600, -2900, 12500 - 6), V(-1600, 0, 0), V(0, 1200, 0), scr, F_EMIT, 1, (160, 160, 160), nd=V(0, 0, -1))
lights.append((V(0, -2300, 11800), V(0.4, 0.55, 0.5), 3600))
for xx in (-2600, -1200, 1200, 2600):
    console(xx, 10400, 0.0, 'screen_graph' if xx < 0 else 'screen_vitals')
for zz in (8800, 9600, 10400, 11200):
    rack(-3700, zz, V(1, 0, 0))
    rack(3700, zz, V(-1, 0, 0))
# autopsy table with a shinigami on it
box(-700, -800, 8700, 700, 0, 9500, 'dark_metal', 'machine_panel', cell=800)
# the body lies flat: build it rotated by hand (torso along x)
box(-500, -900, 8950, 300, -800, 9250, 'shini_body', 'shini_body', cell=900, collide=False, occl=False)
box(300, -980, 8960, 560, -800, 9240, 'shini_body', 'shini_body', front='shini_skull', nd_front=V(1, 0, 0), cell=900, collide=False, occl=False)
add_quad(V(-300, -905, 9250), V(400, 0, 0), V(-200, 0, 700), 'shini_body', F_DOUBLE)
add_quad(V(-300, -905, 8950), V(400, 0, 0), V(-200, 0, -700), 'shini_body', F_DOUBLE)
lights.append((V(0, -2600, 9100), V(1.0, 1.0, 0.95), 2600))
sprites.append((0, -3300, 9100, 400, 255, 250, 230, 0))
# specimen jars on a bench
box(-3200, -800, 11700, -1200, 0, 12300, 'dark_metal', 'machine_panel', cell=700)
for k in range(4):
    jx = -2950 + k * 480
    cylinder(V(jx, -800, 12000), 170, 0, -500, 6, 'glass', F_ADD, 2)
    box(jx - 70, -1100, 11930, jx + 70, -960, 12070, 'shini_skull', 'shini_skull', front='shini_skull', nd_front=V(0, 0, -1),
        cell=400, collide=False, occl=False)
for (lx, lz) in ((-2400, 9200), (2400, 9200), (0, 11600)):
    lights.append((V(lx, -3200, lz), V(0.8, 0.85, 0.9), 6500))

# Corridor west -> Containment
corridor(-8000, 1000, -4500, 3000, 'x')
CONT = (-15000, -2000, -8000, 6000, 4000)
room(*CONT[:4], CONT[4], doors=[('E', 4000, 2000)], name='containment', floor='floor_plate')
# cages along the north and south walls, each with a captive
for k in range(4):
    cx = -14200 + k * 1500
    for side in (-1, 1):
        zw = 6000 if side > 0 else -2000
        zf = zw - side * 1300
        x0, x1 = cx - 600, cx + 600
        # bars: front and two sides, double-sided see-through
        add_quad(V(x0, -2400, zf), V(1200, 0, 0), V(0, 2400, 0), 'cage_bars', F_DOUBLE)
        add_quad(V(x0, -2400, zf), V(0, 0, zw - zf), V(0, 2400, 0), 'cage_bars', F_DOUBLE)
        add_quad(V(x1, -2400, zf), V(0, 0, zw - zf), V(0, 2400, 0), 'cage_bars', F_DOUBLE)
        grid(V(x0, -2400, zf), V(1200, 0, 0), V(0, 0, zw - zf), 700, 'dark_metal', nd=V(0, 1, 0))
        grid(V(x0, -2400, zf), V(1200, 0, 0), V(0, 0, zw - zf), 700, 'beam', nd=V(0, -1, 0))
        colliders.append((x0, min(zf, zw), x1, max(zf, zw), 2400))
        if k != 2:
            captive(cx, -60, (zf + zw) / 2, 0.85, 0.0 if side < 0 else math.pi)
        lights.append((V(cx, -2200, (zf + zw) / 2), V(1.0, 0.15, 0.12), 2600))
# crates and canisters in the middle
for (x, z, s, h) in ((-12500, 1500, 700, 700), (-11800, 1600, 600, 1200), (-12300, 2400, 600, 600), (-10200, 1000, 700, 700),
                     (-10000, 2700, 600, 600)):
    crate(x, z, s, h)
for (x, z) in ((-13300, 2000), (-13300, 2500), (-9400, 1800), (-11000, 2200)):
    canister(x, z)
for (lx, lz) in ((-13000, 2000), (-10000, 2000)):
    lights.append((V(lx, -3700, lz), V(0.7, 0.6, 0.65), 7500))
add_quad(V(-8003, -2700, 3000 + 1100), V(0, 0, 1000), V(0, 1000, 0), 'sign', nd=V(1, 0, 0))

SPAWN = (0, -3000, 0)            # x, z, yaw: south of the tank, looking at it


# ------------------------------------------------------------------ lighting bake
AMBIENT = V(0.22, 0.23, 0.29)
OCC = np.array(occluders, float)                     # (M, 6): x0,y0,z0,x1,y1,z1
OCC_MIN = np.minimum(OCC[:, 0:3], OCC[:, 3:6])
OCC_MAX = np.maximum(OCC[:, 0:3], OCC[:, 3:6])


def shadowed(P, L):
    """for points P (N,3) toward light L (3,): is the segment blocked by any occluder box?"""
    D = L[None] - P                                   # (N,3)
    inv = 1.0 / np.where(np.abs(D) < 1e-6, 1e-6, D)
    t0 = (OCC_MIN[None] - P[:, None]) * inv[:, None]  # (N,M,3)
    t1 = (OCC_MAX[None] - P[:, None]) * inv[:, None]
    tmin = np.minimum(t0, t1).max(-1)
    tmax = np.maximum(t0, t1).min(-1)
    hit = (tmax >= tmin) & (tmax > 0.0) & (tmin < 0.98)
    return hit.any(-1)


def floor_ao(P):
    """contact darkness on floors near anything standing on them"""
    out = np.ones(len(P))
    C = np.array(colliders, float)
    for k in range(0, len(P), 4000):
        p = P[k:k + 4000]
        dx = np.maximum(np.maximum(C[None, :, 0] - p[:, None, 0], p[:, None, 0] - C[None, :, 2]), 0)
        dz = np.maximum(np.maximum(C[None, :, 1] - p[:, None, 2], p[:, None, 2] - C[None, :, 3]), 0)
        d = np.sqrt(dx * dx + dz * dz)
        tall = C[None, :, 4] > -p[:, None, 1] + 20      # only things that rise above this floor
        d = np.where(tall, d, 1e9).min(-1)
        out[k:k + 4000] = 0.45 + 0.55 * np.clip(d / 500, 0, 1)
    return out


def bake():
    pts, nrm, idx = [], [], []
    for fi, f in enumerate(faces):
        p = f['p']
        n = np.cross(p[2] - p[0], p[1] - p[0])
        ln = np.linalg.norm(n)
        n = n / ln if ln > 0 else V(0, -1, 0)
        f['n'] = n
        for ci, c in enumerate(p):
            pts.append(c); nrm.append(n); idx.append((fi, ci))
    P = np.array(pts); N = np.array(nrm)
    Pn = P + N * 12
    acc = np.tile(AMBIENT, (len(P), 1))
    # ambient occlusion: floors near props, walls near floor / ceiling
    up = N[:, 1] < -0.8
    ao = np.ones(len(P))
    ao[up] = floor_ao(P[up])
    wall = np.abs(N[:, 1]) < 0.3
    hgt = -P[:, 1]
    ao[wall] *= 0.55 + 0.45 * np.clip(hgt[wall] / 600, 0, 1)
    acc *= ao[:, None]
    for (L, col, R) in lights:
        D = L[None] - Pn
        d = np.linalg.norm(D, axis=1)
        sel = np.where(d < R)[0]
        if len(sel) == 0:
            continue
        l = D[sel] / d[sel, None]
        ndl = (N[sel] * l).sum(1)
        sel2 = ndl > 0
        s = sel[sel2]
        if len(s) == 0:
            continue
        att = np.clip(1 - (d[s] / R) ** 2, 0, 1) ** 1.5
        lit = ~shadowed(Pn[s], L)
        k = ndl[sel2] * att * np.where(lit, 1.0, 0.12) * (0.65 + 0.35 * ao[s])
        acc[s] += k[:, None] * col[None]
    for i, (fi, ci) in enumerate(idx):
        f = faces[fi]
        if f['emit'] is not None:
            c = np.array(f['emit'], float)
        elif f['flags'] & F_ADD:
            c = np.clip(acc[i] * 60 + 40, 0, 140)
        else:
            c = np.clip(acc[i] * 128 * 1.15, 0, 255)
        f.setdefault('rgb', [None] * len(f['p']))[ci] = [int(round(x)) for x in c]


# ------------------------------------------------------------------ export
def export():
    bake()
    # chunk by centroid on a 2048 grid
    chunks = {}
    for f in faces:
        c = np.mean(f['p'], 0)
        key = (int(math.floor(c[0] / 2048)), int(math.floor(c[2] / 2048)))
        chunks.setdefault(key, []).append(f)
    vout, fout, cout = [], [], []
    for key in sorted(chunks):
        fl = chunks[key]
        # split big chunks so vertex indices stay small
        for s in range(0, len(fl), 300):
            part = fl[s:s + 300]
            vmap = {}
            v0, f0 = len(vout), len(fout)
            for f in part:
                ids = []
                for p in f['p']:
                    k = tuple(int(round(x)) for x in p)
                    if k not in vmap:
                        vmap[k] = len(vout) - v0
                        vout.append(k)
                    ids.append(vmap[k])
                while len(ids) < 4:
                    ids.append(ids[-1])
                fout.append((ids, f))
            P = np.array([vout[v0 + i] for i in range(len(vout) - v0)], float)
            mn, mx = P.min(0), P.max(0)
            ctr = (mn + mx) / 2
            r = np.linalg.norm(mx - mn) / 2
            cout.append((ctr, r, v0, len(vout) - v0, f0, len(fout) - f0))
    for v in vout:
        assert all(-32768 <= x <= 32767 for x in v)
    return vout, fout, cout


def tim_bytes(idx, clut, vx, vy, cy):
    tim = struct.pack('<II', 0x10, 0x09)
    tim += struct.pack('<IHHHH', 12 + 512, 0, cy, 256, 1) + struct.pack('<256H', *clut)
    tim += struct.pack('<IHHHH', 12 + 256 * 256, vx, vy, 128, 256) + idx.tobytes()
    return tim


def main():
    from PIL import Image
    pages = []
    for k, funcs in enumerate((TX.PAGE_A, TX.PAGE_B)):
        p, m = TX.build_page(funcs)
        idx, clut, rgb = TX.quantise(p, m)
        pages.append(tim_bytes(idx, clut, 384 + k * 128, 0, 485 + k))
        Image.fromarray(rgb.astype(np.uint8)).save(os.path.join(ROOT, 'docs', f'lab_page_{"ab"[k]}.png'))
    vout, fout, cout = export()
    nq = len(fout)
    out = []
    w = out.append
    w('/* Deadbird lab level: generated by tools/build_lab.py - edit the script, not this file */')
    w('#ifndef LAB_DATA_H\n#define LAB_DATA_H')
    w(f'#define LAB_NVERTS {len(vout)}\n#define LAB_NFACES {nq}\n#define LAB_NCHUNKS {len(cout)}')
    w(f'#define LAB_NBOXES {len(colliders)}\n#define LAB_NSPRITES {len(sprites)}\n#define LAB_NFANS {len(fans)}')
    w(f'#define LAB_NRINGS {len(rings)}\n#define LAB_NPODS {len(anchors["pods"])}\n#define LAB_NCEIL {len(ceilings)}')
    w(f'#define LAB_SPAWN_X {SPAWN[0]}\n#define LAB_SPAWN_Z {SPAWN[1]}\n#define LAB_SPAWN_YAW {SPAWN[2]}')
    w('#define LAB_TANK_X %d\n#define LAB_TANK_Y %d\n#define LAB_TANK_Z %d' % anchors['tank'])
    w('#define LAB_REACTOR_X %d\n#define LAB_REACTOR_Y %d\n#define LAB_REACTOR_Z %d' % anchors['reactor'])
    w('static const LabVert lab_verts[LAB_NVERTS] = {')
    for i in range(0, len(vout), 6):
        w('  ' + ' '.join('{%d,%d,%d},' % v for v in vout[i:i + 6]))
    w('};')
    w('static const LabFace lab_faces[LAB_NFACES] = {')
    for ids, f in fout:
        uv = ','.join('%d,%d' % (u, v) for (u, v) in (f['uv'] + [f['uv'][-1]] * (4 - len(f['uv']))))
        rgb = f['rgb'] + [f['rgb'][-1]] * (4 - len(f['rgb']))
        cols = ','.join('%d,%d,%d' % tuple(c) for c in rgb)
        w('  {{%d,%d,%d,%d},{%s},{%s},%d,%d,%d},' % (*ids, uv, cols, f['page'], f['flags'], f['anim']))
    w('};')
    w('static const LabChunk lab_chunks[LAB_NCHUNKS] = {')
    for (c, r, v0, nv, f0, nf) in cout:
        w('  {%d,%d,%d,%d,%d,%d,%d,%d},' % (int(c[0]), int(c[1]), int(c[2]), int(r) + 1, v0, nv, f0, nf))
    w('};')
    w('static const LabBox lab_boxes[LAB_NBOXES] = {')
    for (x0, z0, x1, z1, h) in colliders:
        w('  {%d,%d,%d,%d,%d},' % (int(x0), int(z0), int(x1), int(z1), int(h)))
    w('};')
    w('static const LabSprite lab_sprites[LAB_NSPRITES] = {')
    for s in sprites:
        w('  {%d,%d,%d,%d,%d,%d,%d,%d},' % tuple(int(x) for x in s))
    w('};')
    w('static const LabFan lab_fans[LAB_NFANS] = {')
    for (x, y, z, ax, r, sp) in fans:
        w('  {%d,%d,%d,%d,%d},' % (x, y, z, int(r), int(sp)))
    w('};')
    w('static const LabRing lab_rings[LAB_NRINGS] = {')
    for (x, z, r, yt, yb) in rings:
        w('  {%d,%d,%d,%d,%d},' % (x, z, r, yt, yb))
    w('};')
    w('static const LabPod lab_pods[LAB_NPODS] = {')
    for (x, z, r) in anchors['pods']:
        w('  {%d,%d,%d},' % (int(x), int(z), int(r)))
    w('};')
    w('static const LabBox lab_ceil[LAB_NCEIL] = {')
    for (x0, z0, x1, z1, h) in ceilings:
        w('  {%d,%d,%d,%d,%d},' % (x0, z0, x1, z1, h))
    w('};')
    for k, t in enumerate(pages):
        w(f'#define LAB_TIM{k}_SIZE {len(t)}')
        w(f'static const unsigned char lab_tim{k}[{len(t)}] = {{')
        for i in range(0, len(t), 32):
            w('  ' + ','.join(str(b) for b in t[i:i + 32]) + ',')
        w('};')
    w('#endif')
    open(os.path.join(ROOT, 'game', 'lab_data.h'), 'w').write('\n'.join(out) + '\n')
    tris = sum(2 if (f['flags'] & F_QUAD) else 1 for _, f in fout)
    print(f'lab: {len(vout)} verts, {nq} quads (~{tris} triangles), {len(cout)} chunks, {len(colliders)} boxes, '
          f'{len(lights)} lights, {len(sprites)} halos')
    print(f'data: verts {len(vout) * 6} B, faces {nq * 32} B, textures {sum(len(t) for t in pages)} B')


if __name__ == '__main__':
    main()
