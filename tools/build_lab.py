#!/usr/bin/env python3
"""
Builds Deadbird's level: the shinigami energy-extraction plant - a full level, dock to reactor.

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

# face flags (must match game/lab.c)
F_QUAD, F_ADD, F_HALF, F_DOUBLE, F_PULSE, F_FLOOR, F_EMIT, F_ANIMUV = 1, 2, 4, 8, 16, 32, 64, 128

faces = []        # dict(p=[4 or 3 xyz], uv=[(u,v)], page, flags, anim, emit)
occluders = []    # (x0,y0,z0,x1,y1,z1) blocks light
colliders = []    # (x0,z0,x1,z1,top_h)  solid from the floor up to y = -top_h
lights = []       # (pos, rgb(0..), radius)
sprites = []      # (x,y,z,size,r,g,b,anim,area)
anchors = {}      # named positions for animated things drawn in C
fans = []         # (x,y,z, axis, radius, speed)
rings = []        # (x, z, r, y_top, y_bot)
ceilings = []     # (x0,z0,x1,z1,height) for the camera
areas = []        # dict(name, rect, H, kind, links)
CUR = [0]         # area that new faces belong to


def V(*a):
    return np.array(a, float)


def tile_uv(page, tile, u0=0, v0=0, u1=63, v1=63):
    tx, ty = (tile % 4) * 64, (tile // 4) * 64
    return [(tx + u0, ty + v0), (tx + u1, ty + v0), (tx + u0, ty + v1), (tx + u1, ty + v1)]


def tid(name):
    return TX.INDEX[name]


def add_quad(o, U, V_, tex, flags=0, anim=0, emit=None, uvr=None, nd=None):
    """quad o, o+U, o+V, o+U+V (PS1 Z order). Visible side has normal V x U; if nd is given
    the quad is flipped to face that way."""
    o, U, V_ = np.asarray(o, float), np.asarray(U, float), np.asarray(V_, float)
    if nd is not None and np.dot(np.cross(V_, U), nd) < 0:
        o = o + U
        U = -U
    page, t = tid(tex)
    u0, v0, u1, v1 = uvr or (0, 0, 63, 63)
    if tex in TX.ANIMATED:
        flags |= F_ANIMUV
    # no polygon edge over ~2 m: long polygons sort and cull by their middle and pop in and out
    nu = max(1, int(math.ceil(np.linalg.norm(U) / 2100.0)))
    nv = max(1, int(math.ceil(np.linalg.norm(V_) / 2100.0)))
    for j in range(nv):
        for i in range(nu):
            a0, a1, b0, b1 = i / nu, (i + 1) / nu, j / nv, (j + 1) / nv
            uv = tile_uv(page, t, int(round(u0 + (u1 - u0) * a0)), int(round(v0 + (v1 - v0) * b0)),
                         int(round(u0 + (u1 - u0) * a1)), int(round(v0 + (v1 - v0) * b1)))
            q = o + U * a0 + V_ * b0
            faces.append(dict(p=[q, q + U / nu, q + V_ / nv, q + U / nu + V_ / nv], uv=uv, page=page, flags=flags | F_QUAD,
                              anim=anim, emit=emit, area=CUR[0]))


def add_tri(a, b, c, tex, flags=0, anim=0, emit=None, nd=None):
    a, b, c = (np.asarray(x, float) for x in (a, b, c))
    if nd is not None and np.dot(np.cross(c - a, b - a), nd) < 0:
        b, c = c, b
    page, t = tid(tex)
    uv = tile_uv(page, t)
    faces.append(dict(p=[a, b, c], uv=[(uv[0][0] + 32, uv[0][1]), uv[2], uv[3]], page=page, flags=flags & ~F_QUAD,
                      anim=anim, emit=emit, area=CUR[0]))


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
    """n-sided cylinder along an axis; c is the centre of the base on that axis (y0..y1 span).
    Long cylinders are cut every 2 m along the axis (no huge polygons: they sort and cull badly)."""
    c = np.asarray(c, float)
    rep = max(rep, int(math.ceil(abs(y1 - y0) / 2000.0)))
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




# ------------------------------------------------------------------ rooms and halls
DOOR_H = 4000            # halls are exactly door height, so doorways line up
WALL_T = 240
TEX_M = 2000             # one 64-texel tile covers 2 m (32 texels per metre)
SUB = 1000               # polygons are 1 m, so the baked lighting has 1 m resolution

def area(name, x0, z0, x1, z1, H, kind):
    areas.append(dict(name=name, rect=(x0, z0, x1, z1), H=H, kind=kind, links=set()))
    CUR[0] = len(areas) - 1
    ceilings.append((x0, z0, x1, z1, H))
    return CUR[0]


# The world is laid out on a 2 m tile grid. Most wall tiles repeat a room's pattern; special
# tiles are placed by hand, one tile each: (room, wall side) -> [(position along the wall, tile)]
# (x for N/S walls, z for E/W walls; the tile whose 2 m column contains it is replaced).
PLACED = {
    ('Security Checkpoint', 'S'): [(-2000, 'wall_graph')],
    ('Extraction Hall', 'N'):     [(1000, 'wall_vitals')],       # the great tank's captive, seen from the catwalk
    ('Research Lab', 'N'):        [(-23000, 'wall_vitals'), (-19000, 'wall_graph')],
    ('Reactor Core', 'S'):        [(25000, 'wall_graph')],
}

ROOM_STYLE = {
    'Loading Dock':        (['base_plain', 'base_pipes', 'base_plain', 'base_hazard'],
                            ['dock_plate', 'dock_number', 'dock_plate', 'mid_poster_danger', 'dock_plate', 'mid_arrow']),
    'Security Checkpoint': (['base_plain', 'base_vent'],
                            ['checkpoint_panel', 'mid_panel', 'mid_authorized', 'mid_panel']),
    'Processing Hall':     (['base_pipes', 'base_plain', 'base_vent'],
                            ['processing_panel', 'mid_vpipes', 'mid_panel', 'mid_poster_power']),
    'Extraction Hall':     (['base_plain', 'base_pipes', 'base_vent', 'base_plain'],
                            ['extraction_panel', 'mid_panel', 'mid_poster_power', 'mid_vpipes', 'mid_poster_danger']),
    'Containment Block':   (['cell_wall'], ['cell_wall', 'cell_warning', 'cell_wall', 'mid_poster_danger']),
    'Research Lab':        (['lab_panel'], ['lab_panel', 'lab_poster', 'lab_panel']),
    'Server Vault':        (['base_plain', 'base_vent'], ['vault_panel', 'vault_panel', 'mid_authorized']),
    'Pipe Gallery':        (['gallery_pipes'], ['gallery_pipes', 'mid_vpipes', 'gallery_pipes', 'mid_poster_danger']),
    'Turbine Hall':        (['base_pipes', 'base_hazard', 'base_plain'], ['turbine_panel', 'mid_vpipes', 'turbine_panel', 'mid_poster_danger']),
    'Reactor Core':        (['reactor_plate', 'base_hazard'], ['reactor_plate', 'reactor_warning', 'reactor_plate', 'mid_poster_danger']),
}


def tex_cell(o, R, D, a, b, h0, h1, tu0, hv0, tile, flags=0, emit=None, sub=SUB):
    """one textured patch of a vertical or horizontal surface, cut into <= sub-sized quads.
    o: origin; R: unit 'right' (u direction); D: unit 'down' (v direction); patch covers
    u in [a,b] and v in [h0,h1] (distances along R and D). The texture's top-left sits at
    (tu0, hv0), so texel density is always TEX_M per tile."""
    nu = max(1, int(math.ceil((b - a) / sub - 1e-6)))
    nv = max(1, int(math.ceil((h1 - h0) / sub - 1e-6)))
    page, t = tid(tile)
    tx, ty = (t % 4) * 64, (t // 4) * 64
    if tile in TX.ANIMATED:
        flags |= F_ANIMUV
    def U_(p): return int(round(min(63, max(0, (p - tu0) / TEX_M * 63))))
    def V_(p): return int(round(min(63, max(0, (p - hv0) / TEX_M * 63))))
    for j in range(nv):
        v0 = h0 + (h1 - h0) * j / nv; v1 = h0 + (h1 - h0) * (j + 1) / nv
        for i in range(nu):
            u0 = a + (b - a) * i / nu; u1 = a + (b - a) * (i + 1) / nu
            p0 = o + R * u0 + D * v0
            uv = [(tx + U_(u0), ty + V_(v0)), (tx + U_(u1), ty + V_(v0)), (tx + U_(u0), ty + V_(v1)), (tx + U_(u1), ty + V_(v1))]
            faces.append(dict(p=[p0, p0 + R * (u1 - u0), p0 + D * (v1 - v0), p0 + R * (u1 - u0) + D * (v1 - v0)],
                              uv=uv, page=page, flags=flags | F_QUAD, anim=0, emit=emit, area=CUR[0]))


def columns(L, anchor):
    """split a span of length L into texture columns of TEX_M; anchor 'start', 'end' or 'centre'
    decides where the partial column goes. Returns (a, b, texture_left_edge) triples."""
    if anchor == 'end':
        out, e = [], L
        while e > 1:
            out.append((max(0, e - TEX_M), e, e - TEX_M)); e -= TEX_M
        return out[::-1]
    off = 0 if anchor == 'start' else -((TEX_M - (L % TEX_M)) / 2 if L % TEX_M else 0)
    out, s = [], off
    while s < L - 1:
        out.append((max(0, s), min(L, s + TEX_M), s)); s += TEX_M
    return out


def floor_rect(x0, z0, x1, z1, y, texf, up=True, sub=SUB, flags=F_FLOOR, emit_tiles=()):
    """floor (up=True) or ceiling at height y, tiled 2 m, centred, cut into sub-sized quads"""
    R = V(1, 0, 0)
    D = V(0, 0, 1) if not up else V(0, 0, -1)
    o = V(x0, y, z0 if not up else z1)
    W, Dd = x1 - x0, z1 - z0
    cu, cv = columns(W, 'centre'), columns(Dd, 'centre')
    for j, (c, d_, tv0) in enumerate(cv):
        for i, (a, b, tu0) in enumerate(cu):
            tile = texf(i, j, len(cu), len(cv))
            em = (150, 160, 170) if tile in emit_tiles else None
            tex_cell(o, R, D, a, b, c, d_, tu0, tv0, tile, (flags | F_EMIT) if em else flags, em, sub)


def build_shell(ai, floor_tex=None, ceil_tex=None):
    """floor, ceiling and walls of area ai - every wall detail is in the textures"""
    a = areas[ai]
    x0, z0, x1, z1 = a['rect']
    H = a['H']
    CUR[0] = ai
    hall = a['kind'] == 'hall'
    if hall:
        mid_col = lambda i, j, nu, nv: (i if a['axis'] == 'z' else j) == ((nu if a['axis'] == 'z' else nv) // 2)
        floor_rect(x0, z0, x1, z1, 0, floor_tex or (lambda i, j, nu, nv: 'floor_strip' if mid_col(i, j, nu, nv) and a['axis'] == 'z'
                                                     else 'floor_plate'))
        floor_rect(x0, z0, x1, z1, -H, ceil_tex or (lambda i, j, nu, nv: ('ceiling_light' if (i + j) % 3 == 0 else 'ceiling_cables')
                                                    if mid_col(i, j, nu, nv) else 'ceiling_panel'),
                   up=False, sub=TEX_M, flags=0, emit_tiles=('ceiling_light',))
        if a['axis'] == 'x':          # the light strip runs along the hall
            pass
    else:
        floor_rect(x0, z0, x1, z1, 0, floor_tex or (lambda i, j, nu, nv: 'floor_grate' if (i in (0, nu - 1) or j in (0, nv - 1)) and (i + j) % 3 == 0
                                                     else 'floor_plate'))
        floor_rect(x0, z0, x1, z1, -H, ceil_tex or (lambda i, j, nu, nv: 'ceiling_light' if (i % 3 == 1 and j % 3 == 1) else 'ceiling_panel'),
                   up=False, sub=TEX_M, flags=0, emit_tiles=('ceiling_light',))
    sides = {'S': (V(x0, 0, z0), V(1, 0, 0), x1 - x0, V(0, 0, 1)),
             'N': (V(x0, 0, z1), V(1, 0, 0), x1 - x0, V(0, 0, -1)),
             'W': (V(x0, 0, z0), V(0, 0, 1), z1 - z0, V(1, 0, 0)),
             'E': (V(x1, 0, z0), V(0, 0, 1), z1 - z0, V(-1, 0, 0))}
    style = ROOM_STYLE.get(a['name'], (['base_plain'], ['mid_panel']))
    for s, (o, d, L, n) in sides.items():
        if s in a.get('open', ()):
            continue
        place = PLACED.get((a['name'], s), [])
        holes = sorted(a.get('doors', {}).get(s, []))
        cuts = [0]
        for c0, c1 in holes:
            cuts += [c0, c1]
        cuts.append(L)
        for k in range(0, len(cuts), 2):
            p, q = cuts[k], cuts[k + 1]
            if q - p > 1:
                wall_section(o, d, p, q, H, n, style, hall, door_p=(k > 0), door_q=(k + 2 < len(cuts)), seed=k * 3 + len(s), place=place)
                w0 = o + d * p - n * WALL_T
                w1 = o + d * q
                colliders.append((min(w0[0], w1[0]), min(w0[2], w1[2]), max(w0[0], w1[0]), max(w0[2], w1[2]), 9000))
                occluders.append((min(w0[0], w1[0]), -H - 10, min(w0[2], w1[2]), max(w0[0], w1[0]), 10, max(w0[2], w1[2])))
        for c0, c1 in holes:
            w0 = o + d * c0 - n * WALL_T
            w1 = o + d * c1
            if H > DOOR_H:
                wall_section(o, d, c0, c1, H, n, style, hall, lintel=True)
                occluders.append((min(w0[0], w1[0]), -H - 10, min(w0[2], w1[2]), max(w0[0], w1[0]), -DOOR_H, max(w0[2], w1[2])))
            door_frame(o + d * c0, d * (c1 - c0), n)


def wall_section(o, d, p, q, H, n, style, hall, door_p=False, door_q=False, lintel=False, seed=0, place=()):
    """wall from p to q along d (base point o), facing n, in 2 m texture bands:
         0-2 m base (kickplate, painted pipes, vents, hazard paint)
         2-4 m middle (panels, posters, signs, monitors, room stencils)
         4 m+  top (circuitry, I-beam and cable tray under the ceiling)
       Columns touching a doorway get the painted door surround; above a door, the lintel."""
    right = np.cross(-n, V(0, -1, 0))
    flip = np.dot(right, d) < 0
    # in 'right' coordinates the segment runs from s0 to s1
    base = o + d * (q if flip else p)
    L = q - p
    door_left, door_right = (door_q, door_p) if flip else (door_p, door_q)
    anchor = 'end' if door_right and not door_left else 'start'
    cols = columns(L, anchor)
    D = V(0, 1, 0)
    top_o = base + V(0, -H, 0)                   # top-left corner of the wall (v measured down from the ceiling)
    def row(h0, h1, tile, anch, a, b, tu0):
        # heights h measured up from the floor -> v measured down from the ceiling
        v0, v1 = H - h1, H - h0
        hv0 = (H - h0 - TEX_M) if anch == 'bottom' else (H - h1)
        tex_cell(top_o, right, D, a, b, v0, v1, tu0, hv0, tile)
    for ci, (a, b, tu0) in enumerate(cols):
        if lintel:
            rows = []
        elif hall:
            rows = [(0, 2000, 'hall_wall', 'bottom'), (2000, min(H, 4000), 'hall_wall_top', 'bottom')]
        else:
            k = ci + seed
            bt = style[0][k % len(style[0])]
            mt = style[1][k % len(style[1])]
            ctr = base + right * ((a + b) / 2)                 # world centre of this 2 m column
            along = ctr[0] if abs(right[0]) > 0.5 else ctr[2]
            for (pos, tile) in place:
                if -TEX_M / 2 <= (pos - along) * (1 if (right[0] + right[2]) > 0 else -1) < TEX_M / 2 and b - a > TEX_M * 0.9:
                    mt = tile
            if door_left and ci == 0:
                bt = mt = 'door_jamb_r'
            if door_right and ci == len(cols) - 1:
                bt = mt = 'door_jamb_l'
            rows = [(0, 2000, bt, 'bottom'), (2000, 4000, mt, 'bottom')]
        if not hall:
            if H <= 6000:
                rows.append((4000, H, 'door_lintel' if lintel else 'top_beam', 'bottom' if lintel else 'top'))
            else:
                rows.append((4000, 6000, 'door_lintel' if lintel else 'top_circuit', 'bottom'))
                if H - 6000 > TEX_M:
                    rows.append((6000, H - TEX_M, 'top_circuit', 'bottom'))
                    rows.append((H - TEX_M, H, 'top_beam', 'top'))
                else:
                    rows.append((6000, H, 'top_beam', 'top'))
        for (h0, h1, tile, anch) in rows:
            if h1 - h0 > 1:
                row(h0, h1, tile, anch, a, b, tu0)


def door_frame(o, U, n):
    """the opening's reveal through the wall thickness (bulkhead plates) and its soffit"""
    L = np.linalg.norm(U)
    d = U / L
    back = -n * WALL_T
    add_quad(o + V(0, -DOOR_H, 0), back, V(0, DOOR_H, 0), 'bulkhead', nd=d)
    add_quad(o + U + V(0, -DOOR_H, 0), back, V(0, DOOR_H, 0), 'bulkhead', nd=-d)
    add_quad(o + V(0, -DOOR_H, 0), U, back, 'hazard', nd=V(0, 1, 0))


def hall_between(name, ra, rb, centre, width=3000):
    """a straight hall linking two rooms that face each other; doorways are cut automatically"""
    A, B = areas[ra]['rect'], areas[rb]['rect']
    if A[2] <= B[0] or B[2] <= A[0]:          # side by side in x -> hall runs along x
        lo, hi = (ra, rb) if A[2] <= B[0] else (rb, ra)
        L, Hh = areas[lo]['rect'], areas[hi]['rect']
        x0, x1 = L[2], Hh[0]
        z0, z1 = centre - width / 2, centre + width / 2
        hi_ = area(name, x0, z0, x1, z1, DOOR_H, 'hall')
        areas[hi_]['open'] = ('W', 'E'); areas[hi_]['axis'] = 'x'
        areas[lo].setdefault('doors', {}).setdefault('E', []).append((z0 - L[1], z1 - L[1]))
        areas[hi].setdefault('doors', {}).setdefault('W', []).append((z0 - Hh[1], z1 - Hh[1]))
    else:                                       # one above the other in z -> hall runs along z
        lo, hi = (ra, rb) if A[3] <= B[1] else (rb, ra)
        L, Hh = areas[lo]['rect'], areas[hi]['rect']
        z0, z1 = L[3], Hh[1]
        x0, x1 = centre - width / 2, centre + width / 2
        hi_ = area(name, x0, z0, x1, z1, DOOR_H, 'hall')
        areas[hi_]['open'] = ('S', 'N'); areas[hi_]['axis'] = 'z'
        areas[lo].setdefault('doors', {}).setdefault('N', []).append((x0 - L[0], x1 - L[0]))
        areas[hi].setdefault('doors', {}).setdefault('S', []).append((x0 - Hh[0], x1 - Hh[0]))
    areas[hi_]['links'] |= {ra, rb}
    areas[ra]['links'].add(hi_); areas[rb]['links'].add(hi_)
    return hi_


def dress_hall(hi):
    """halls are plain boxes - the pipes, cable trays and guide lights are painted on; just light them"""
    a = areas[hi]
    CUR[0] = hi
    x0, z0, x1, z1 = a['rect']
    L = (x1 - x0) if a['axis'] == 'x' else (z1 - z0)
    n = max(1, int(L / 3000))
    for k in range(n):
        if a['axis'] == 'x':
            p = V(x0 + (k + 0.5) * L / n, -3600, (z0 + z1) / 2)
        else:
            p = V((x0 + x1) / 2, -3600, z0 + (k + 0.5) * L / n)
        lights.append((p, V(0.75, 0.9, 0.95), 4800))


def ceiling_lights(ai, col, R, step=4000, h=None):
    x0, z0, x1, z1 = areas[ai]['rect']
    H = h or areas[ai]['H']
    nx = max(1, int(round((x1 - x0) / step))); nz = max(1, int(round((z1 - z0) / step)))
    for j in range(nz):
        for i in range(nx):
            lights.append((V(x0 + (i + 0.5) * (x1 - x0) / nx, -H + 300, z0 + (j + 0.5) * (z1 - z0) / nz), V(*col), R))


def stairs(x0, z0, x1, z1, top, axis, up_positive=True):
    """flight of 130-high steps from the floor to `top`, rising along axis"""
    n = int(round(top / 130))
    for i in range(n):
        if axis == 'x':
            L = (x1 - x0) / n
            a = x0 + i * L if up_positive else x1 - (i + 1) * L
            box(a, -(i + 1) * 130, z0, a + L, 0, z1, 'stair_tread', 'dark_metal', cell=1600, occl=(i > n // 2))
        else:
            L = (z1 - z0) / n
            a = z0 + i * L if up_positive else z1 - (i + 1) * L
            box(x0, -(i + 1) * 130, a, x1, 0, a + L, 'stair_tread', 'dark_metal', cell=1600, occl=(i > n // 2))


def rail_x(x0, x1, z, base):
    x0, x1 = min(x0, x1), max(x0, x1)
    box(x0, -base - 900, z - 30, x1, -base, z + 30, 'beam', 'beam', cell=1200, occl=False, collide=False)
    colliders.append((x0, z - 40, x1, z + 40, base + 900))


def rail_z(z0, z1, x, base):
    z0, z1 = min(z0, z1), max(z0, z1)
    box(x - 30, -base - 900, z0, x + 30, -base, z1, 'beam', 'beam', cell=1200, occl=False, collide=False)
    colliders.append((x - 40, z0, x + 40, z1, base + 900))


def machine(x0, z0, x1, z1, h, face):
    """processing machine block: vented base sides, control panel on the face toward the room"""
    box(x0, -h, z0, x1, 0, z1, 'dark_metal', 'machine_base', 'machine_panel', nd_front=V(*face), cell=900)
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
    sprites.append((x, -2650, z, 380, 60, 200, 230, anim, CUR[0]))
    anchors.setdefault('pods', []).append((x, z, r))
    # console beside it
    cx, cz = x + math.cos(facing) * 900, z - math.sin(facing) * 900
    console(cx, cz, facing + math.pi, 'scr_vitals')


def console(x, z, facing, screen='scr_graph', y=0):
    """desk + slanted keyboard + monitor; facing = direction the operator looks (radians, 0 = +z)"""
    c, s = math.cos(facing), math.sin(facing)
    fwd, right = V(s, 0, c), V(c, 0, -s)
    o = V(x, y, z)
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
    add_quad(scr, right * 760, V(0, 560, 0), screen, F_EMIT, 1, (150, 150, 150), nd=-fwd)
    add_quad(scr + fwd * 60, right * 760, V(0, 560, 0), 'dark_metal', nd=fwd)
    add_quad(scr + V(0, 0, 0), fwd * 60, V(0, 560, 0), 'dark_metal', nd=-right)
    add_quad(scr + right * 760, fwd * 60, V(0, 560, 0), 'dark_metal', nd=right)
    add_quad(scr, right * 760, fwd * 60, 'dark_metal', nd=V(0, -1, 0))
    xs = [p[0] for p in corners]; zs = [p[2] for p in corners]
    colliders.append((min(xs), min(zs), max(xs), max(zs), 800 - y))
    occluders.append((min(xs), y - 800, min(zs), max(xs), y, max(zs)))
    lights.append((scr + right * 380 - fwd * 300 + V(0, 280, 0), V(0.35, 0.5, 0.45) if screen == 'scr_graph' else V(0.6, 0.2, 0.15), 1300))


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
# Route: Loading Dock -> Security Checkpoint -> Processing Hall -> Extraction Hall ->
#        Containment Block -> Research Lab -> Server Vault -> Pipe Gallery -> Turbine Hall ->
#        Reactor Core. The Extraction Hall also opens straight into the Pipe Gallery (a loop).
# Rooms keep their middles open (room to fight); machines and props stand along the walls.
DOCK = area('Loading Dock',        -26000, -30000, -12000, -20000, 6500, 'room')
CHECK = area('Security Checkpoint',  -6000, -28000,   2000, -22000, 4500, 'room')
PROC = area('Processing Hall',     -10000, -16000,   6000,  -8000, 5500, 'room')
EXTR = area('Extraction Hall',      -9000,  -4000,   5000,  10000, 6500, 'room')
CONT = area('Containment Block',   -28000,  -2000, -15000,   8000, 4800, 'room')
LAB = area('Research Lab',         -27000,  14000, -16000,  22000, 4200, 'room')
VAULT = area('Server Vault',       -10000,  13000,      0,  23000, 4500, 'room')
GALL = area('Pipe Gallery',          6000,   2000,  11000,  26000, 6000, 'room')
TURB = area('Turbine Hall',          6300, -18000,  22500,  -4000, 7500, 'room')
CORE = area('Reactor Core',         14000,      0,  29000,  16000, 9000, 'room')

H1 = hall_between('Dock Hall', DOCK, CHECK, -25000)
H2 = hall_between('Checkpoint Hall', CHECK, PROC, -2000)
H3 = hall_between('Processing Hall North', PROC, EXTR, -2000)
H4 = hall_between('Containment Hall', CONT, EXTR, 3000)
H5 = hall_between('Lab Hall', CONT, LAB, -21600)
H6 = hall_between('Vault Hall', LAB, VAULT, 18000)
H7 = hall_between('Gallery Hall', VAULT, GALL, 18000)
H8 = hall_between('Shortcut Hall', EXTR, GALL, 4000)
H9 = hall_between('Turbine Hall Stair', TURB, GALL, 7900)
H10 = hall_between('Core Hall', TURB, CORE, 21000)

SPAWN = (-19000, -26500, 0)
ROUTE_HALLS = [H1, H2, H3, H4, H5, H6, H7, H9, H10]      # dock -> reactor (H8 is the shortcut)
GOAL = (18500, 5000)


def route_points():
    """walkable path for the route test: spawn, then for every hall a point 2000 into the room
    before its doorway, the doorway, the far doorway and a point 2000 into the next room"""
    pts = [SPAWN[:2]]
    for h in ROUTE_HALLS:
        x0, z0, x1, z1 = areas[h]['rect']
        cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
        if areas[h]['axis'] == 'x':
            ends = [(x0, cz), (x1, cz)]
            d = (1, 0)
        else:
            ends = [(cx, z0), (cx, z1)]
            d = (0, 1)
        # walk the hall in whichever direction is nearer to where we are
        px, pz = pts[-1]
        if (ends[1][0] - px) ** 2 + (ends[1][1] - pz) ** 2 < (ends[0][0] - px) ** 2 + (ends[0][1] - pz) ** 2:
            ends = ends[::-1]
            d = (-d[0], -d[1])
        a, b = ends
        pts += [(a[0] - d[0] * 2000, a[1] - d[1] * 2000), a, b, (b[0] + d[0] * 2000, b[1] + d[1] * 2000)]
    pts.append(GOAL)
    return [(int(x), int(z)) for x, z in pts]


def dock_floor(i, j, nu, nv):
    if j == 1 and 1 <= i <= nu - 2:
        return 'floor_hazard'
    if i == nu - 1 and j in (2, 3):
        return 'floor_arrow'
    return 'floor_edge' if j == nv - 1 else 'floor_dock'


build_shell(DOCK, dock_floor)
build_shell(CHECK)
build_shell(PROC, lambda i, j, nu, nv: 'floor_grate' if j == nv // 2 else ('floor_grate' if (i in (0, nu - 1) or j in (0, nv - 1)) and (i + j) % 3 == 0 else 'floor_plate'))
build_shell(EXTR)
build_shell(CONT, lambda i, j, nu, nv: 'floor_drain' if (i * 7 + j * 3) % 11 == 0 else 'floor_cell')
build_shell(LAB, lambda i, j, nu, nv: 'floor_lab')
build_shell(VAULT, lambda i, j, nu, nv: 'floor_grate' if i in (0, 2, 3) and j != 2 else ('floor_arrow' if j == 2 and i == 1 else 'floor_plate'))
build_shell(GALL, lambda i, j, nu, nv: 'floor_grate' if i == 2 else ('floor_strip' if i == 1 else 'floor_plate'))
build_shell(TURB, lambda i, j, nu, nv: 'floor_hazard' if j == nv - 1 else ('floor_grate' if i % 3 == 1 else 'floor_plate'))
build_shell(CORE, lambda i, j, nu, nv: 'floor_reactor' if 1 <= i <= nu - 2 and 1 <= j <= nv - 2 and abs(i - nu / 2 + 0.5) < 3 and abs(j - nv / 2 + 0.5) < 3 else ('floor_hazard' if (i in (0, nu - 1) or j in (0, nv - 1)) else 'floor_plate'))
for h in (H1, H2, H3, H4, H5, H6, H7, H8, H9, H10):
    build_shell(h)
    dress_hall(h)

# ---------------- Loading Dock: arrival bay, cargo containers, a loading platform with stairs
CUR[0] = DOCK
box(-25800, -2600, -22800, -20000, 0, -20200, 'dark_metal', 'crate', cell=1300)          # container
box(-25800, -5200, -22800, -21500, -2600, -20200, 'dark_metal', 'crate', cell=1300, collide=False)   # stacked
box(-17400, -2600, -22800, -13200, 0, -20200, 'dark_metal', 'crate', cell=1300)
box(-26000, -520, -29000, -23200, 0, -24000, 'floor_grate', 'plate_side', cell=900)       # loading platform
stairs(-23200, -27500, -22100, -25500, 520, 'x', up_positive=False)
rail_z(-29000, -27500, -23200, 520)
rail_z(-25500, -24000, -23200, 520)
for (x, z) in ((-14500, -29400), (-13700, -29400), (-14200, -28700), (-12400, -29600)):
    crate(x, z, 700, 700)
canister(-25200, -23400); canister(-24600, -23400)
ceiling_lights(DOCK, (0.8, 0.82, 0.88), 8500, 4600)
lights.append((V(-12300, -3300, -25000), V(1.3, 0.15, 0.1), 2600))
sprites.append((-12350, -3300, -25000, 260, 255, 40, 30, 5, DOCK))

# ---------------- Security Checkpoint: scanner arch, guard consoles
CUR[0] = CHECK
for px in (-3800, -200):
    box(px - 400, -3400, -24000, px + 400, 0, -22800, 'dark_metal', 'hazard', cell=900)
box(-3800, -3800, -23800, -200, -3400, -23000, 'beam', 'beam', cell=1200, collide=False)
add_quad(V(-3400, -3420, -23800), V(2800, 0, 0), V(0, 0, 800), 'floor_light', F_EMIT | F_PULSE, 5, (180, 60, 50), nd=V(0, 1, 0))
lights.append((V(-2000, -3200, -23400), V(1.2, 0.2, 0.15), 3200))
console(-5000, -27200, math.pi, 'scr_graph')
console(1000, -27200, math.pi, 'scr_vitals')
console(1200, -22900, 0.0, 'scr_graph')
ceiling_lights(CHECK, (0.75, 0.8, 0.85), 7000, 4000)

# ---------------- Processing Hall: machines down both long walls, overhead pipes
CUR[0] = PROC
for x in (-7800, -5200, 1200, 3800):
    machine(x - 1000, -16000, x + 1000, -14400, 2600, (0, 0, 1))
    machine(x - 1000, -9600, x + 1000, -8000, 2600, (0, 0, -1))
    lights.append((V(x, -1200, -14000), V(1.1, 0.5, 0.2), 2400))
    lights.append((V(x, -1200, -10000), V(1.1, 0.5, 0.2), 2400))
console(4500, -12000, math.pi / 2, 'scr_graph')
ceiling_lights(PROC, (0.75, 0.78, 0.85), 7500, 4000)

# ---------------- Extraction Hall: the great tank, four pods, catwalk
CUR[0] = EXTR
TX0, TZ0, TANK_R = -2000, 3000, 1300
cylinder(V(TX0, 0, TZ0), TANK_R + 300, 0, -600, 12, 'machine_base', cap='hazard')
cylinder(V(TX0, -600, TZ0), TANK_R, 0, -3200, 12, 'glass', F_ADD, 2, rep=2)
cylinder(V(TX0, -3800, TZ0), TANK_R + 260, 0, -800, 12, 'dark_metal', cap='dark_metal')
for k in range(6):
    a = k * math.pi / 3 + 0.3
    cylinder(V(TX0 + math.cos(a) * 900, -4600, TZ0 + math.sin(a) * 900), 150, 0, -1900, 6, 'pipe')
for k in range(12):
    a0, a1 = 2 * math.pi * k / 12, 2 * math.pi * (k + 1) / 12
    r0, r1 = TANK_R + 320, TANK_R + 560
    p0 = V(TX0 + math.cos(a0) * r0, -3, TZ0 + math.sin(a0) * r0)
    p1 = V(TX0 + math.cos(a1) * r0, -3, TZ0 + math.sin(a1) * r0)
    p2 = V(TX0 + math.cos(a0) * r1, -3, TZ0 + math.sin(a0) * r1)
    p3 = V(TX0 + math.cos(a1) * r1, -3, TZ0 + math.sin(a1) * r1)
    f = dict(p=[p0, p1, p2, p3], uv=tile_uv(*tid('floor_light')), page=tid('floor_light')[0], flags=F_QUAD | F_EMIT | F_FLOOR | F_PULSE,
             anim=2, emit=(140, 150, 150), area=EXTR)
    if np.dot(np.cross(p2 - p0, p1 - p0), V(0, -1, 0)) < 0:
        f['p'] = [p1, p0, p3, p2]
    faces.append(f)
cyl_collide(TX0, TZ0, TANK_R + 300, 6500)
anchors['tank'] = (TX0, -2300, TZ0)
rings.append((TX0, TZ0, TANK_R - 60, -3700, -800))
lights.append((V(TX0, -2300, TZ0), V(0.3, 1.0, 1.3), 10000))
lights.append((V(TX0, -500, TZ0), V(0.2, 0.7, 0.9), 3600))
sprites.append((TX0, -3850, TZ0, 1100, 70, 210, 240, 2, EXTR))
extraction_pod(-8300, -1500, math.pi / 2, 3)
extraction_pod(-8300, 7500, math.pi / 2, 4)
extraction_pod(4300, -1500, -math.pi / 2, 3)
extraction_pod(4300, 7500, -math.pi / 2, 4)
CW_H = 1040
box(-6000, -CW_H, 8000, 2000, 0, 10000, 'floor_grate', 'plate_side', cell=900)
stairs(-8200, 8400, -6000, 10000, CW_H, 'x')
rail_x(-6000, 2000, 8000, CW_H)
rail_z(8000, 10000, 2000, CW_H)
console(-2000, 9500, 0.0, 'scr_vitals', y=-CW_H)
for (x, z) in ((-4500, -3800), (500, -3800)):
    pillar(x, z + 400, 6500)
ceiling_lights(EXTR, (0.8, 0.84, 0.92), 8500, 4700)

# ---------------- Containment Block: barred cells under red light along both long walls
CUR[0] = CONT
for cx in (-26300, -24200, -19000, -16700):
    for side in (-1, 1):
        zw = 8000 if side > 0 else -2000
        zf = zw - side * 1600
        x0, x1 = cx - 900, cx + 900
        add_quad(V(x0, -2600, zf), V(1800, 0, 0), V(0, 2600, 0), 'cage_bars', F_DOUBLE)
        add_quad(V(x0, -2600, zf), V(0, 0, zw - zf), V(0, 2600, 0), 'cage_bars', F_DOUBLE)
        add_quad(V(x1, -2600, zf), V(0, 0, zw - zf), V(0, 2600, 0), 'cage_bars', F_DOUBLE)
        grid(V(x0, -2600, zf), V(1800, 0, 0), V(0, 0, zw - zf), 900, 'dark_metal', nd=V(0, 1, 0))
        grid(V(x0, -2600, zf), V(1800, 0, 0), V(0, 0, zw - zf), 900, 'beam', nd=V(0, -1, 0))
        colliders.append((x0, min(zf, zw), x1, max(zf, zw), 2600))
        captive(cx, -60, (zf + zw) / 2, 0.85, 0.0 if side < 0 else math.pi)
        lights.append((V(cx, -2300, (zf + zw) / 2), V(1.0, 0.15, 0.12), 2800))
for (x, z) in ((-27600, 2600), (-27600, 3300)):
    canister(x, z)
crate(-27500, 4300, 700, 1200)
for (lx, lz) in ((-25000, 3000), (-19000, 3000)):
    lights.append((V(lx, -4400, lz), V(0.7, 0.6, 0.65), 7500))

# ---------------- Research Lab: monitor wall, operator desks, autopsy table, specimen bench
CUR[0] = LAB
lights.append((V(-21500, -2600, 21300), V(0.4, 0.55, 0.5), 5000))
for xx in (-25000, -22800, -20600, -18400):
    console(xx, 20600, 0.0, 'scr_graph' if xx < -21500 else 'scr_vitals')
TBX = -18800
box(TBX - 700, -800, 15600, TBX + 700, 0, 16400, 'dark_metal', 'machine_panel', cell=800)
box(TBX - 500, -900, 15850, TBX + 300, -800, 16150, 'shini_body', 'shini_body', cell=900, collide=False, occl=False)
box(TBX + 300, -980, 15860, TBX + 560, -800, 16140, 'shini_body', 'shini_body', front='shini_skull', nd_front=V(1, 0, 0), cell=900,
    collide=False, occl=False)
add_quad(V(TBX - 300, -905, 16150), V(400, 0, 0), V(-200, 0, 700), 'shini_body', F_DOUBLE)
add_quad(V(TBX - 300, -905, 15850), V(400, 0, 0), V(-200, 0, -700), 'shini_body', F_DOUBLE)
lights.append((V(TBX, -3000, 16000), V(1.0, 1.0, 0.95), 3200))
sprites.append((TBX, -3900, 16000, 450, 255, 250, 230, 0, LAB))
box(-27000, -800, 15000, -26400, 0, 19000, 'dark_metal', 'machine_panel', cell=800)
for k in range(5):
    jz = 15400 + k * 750
    cylinder(V(-26700, -800, jz), 190, 0, -560, 6, 'glass', F_ADD, 2)
    box(-26780, -1140, jz - 80, -26620, -980, jz + 80, 'shini_skull', 'shini_skull', front='shini_skull', nd_front=V(1, 0, 0),
        cell=400, collide=False, occl=False)
for zz in (14600, 15400):
    rack(-16300, zz, V(-1, 0, 0))
ceiling_lights(LAB, (0.8, 0.85, 0.9), 7000, 3800)

# ---------------- Server Vault: rows of racks with a cross aisle, cold blue light
CUR[0] = VAULT
for rx in (-7500, -5000, -2500):
    for rz in (14200, 15000, 15800, 20200, 21000, 21800):
        box(rx - 350, -2600, rz - 380, rx + 350, 0, rz + 380, 'dark_metal', 'server_rack', cell=800)
    lights.append((V(rx + 1250, -3600, 15000), V(0.35, 0.55, 1.0), 4000))
    lights.append((V(rx + 1250, -3600, 21000), V(0.35, 0.55, 1.0), 4000))
ceiling_lights(VAULT, (0.5, 0.6, 0.8), 6500, 4500)

# ---------------- Pipe Gallery: giant pipes along the east wall, high pipes on the west
CUR[0] = GALL
for zz in range(4000, 26000, 4000):
    box(9100, -5600, zz - 120, 11000, -5300, zz + 120, 'beam', 'beam', cell=1200, collide=False, occl=False)
for k in range(6):
    lights.append((V(8000, -5000, 3500 + k * 4200), V(0.75, 0.8, 0.7), 6500))
    lights.append((V(10000, -300, 3500 + k * 4200), V(0.2, 0.9, 0.7), 2200))

# ---------------- Turbine Hall: three generators with fans, control platform, big pipes
CUR[0] = TURB
for gx in (10500, 15500):
    box(gx - 1600, -3400, -18000, gx + 1600, 0, -14600, 'dark_metal', 'machine_base', cell=1000)
    fans.append((gx, -1700, -14595, 'z', 1100, 1 if gx < 14000 else -1))
    cylinder(V(gx, -3400, -16300), 450, 0, -4100, 8, 'pipe')
    lights.append((V(gx, -1700, -13600), V(1.1, 0.55, 0.25), 3600))
box(18000, -1040, -18000, 22500, 0, -12000, 'floor_grate', 'plate_side', cell=900)       # control platform
stairs(18000, -12000, 20000, -10400, 1040, 'z', up_positive=False)
rail_x(20000, 22500, -12000, 1040)
rail_z(-18000, -12000, 18000, 1040)
for zz in (-17000, -15000, -13200):
    console(21700, zz, math.pi / 2, 'scr_graph', y=-1040)
ceiling_lights(TURB, (0.72, 0.72, 0.78), 9000, 5000)

# ---------------- Reactor Core: the soul reactor, conduits, ring of consoles
CUR[0] = CORE
RC = (21500, 8500)
box(RC[0] - 2200, -300, RC[1] - 2200, RC[0] + 2200, 0, RC[1] + 2200, 'hazard', 'plate_side', cell=1100)
cylinder(V(RC[0], -300, RC[1]), 1300, 0, -5200, 12, 'coil', rep=4)
for k, yb in enumerate((-1300, -2600, -3900)):
    cylinder(V(RC[0], yb, RC[1]), 1370, 0, -320, 12, 'energy', F_EMIT | F_PULSE, 6 + k % 2, emit=(170, 170, 170))
cylinder(V(RC[0], -5500, RC[1]), 1700, 0, -700, 12, 'dark_metal', cap='dark_metal')
cylinder(V(RC[0], -6200, RC[1]), 600, 0, -2800, 8, 'pipe')
cylinder(V(RC[0] + 1650, -6000, RC[1]), 320, 0, 29000 - RC[0] - 1650, 8, 'pipe', axis='x')
cylinder(V(14000, -6000, RC[1]), 320, 0, RC[0] - 1650 - 14000, 8, 'pipe', axis='x')
cylinder(V(RC[0], -6000, RC[1] + 1650), 320, 0, 16000 - RC[1] - 1650, 8, 'pipe', axis='z')
cyl_collide(RC[0], RC[1], 2200, 300)
colliders.append((RC[0] - 1400, RC[1] - 1400, RC[0] + 1400, RC[1] + 1400, 9000))
occluders.append((RC[0] - 1000, -5500, RC[1] - 1000, RC[0] + 1000, 0, RC[1] + 1000))
lights.append((V(RC[0], -2600, RC[1]), V(1.5, 0.75, 0.3), 14000))
lights.append((V(RC[0], -7000, RC[1]), V(0.8, 0.5, 0.3), 8000))
sprites.append((RC[0], -2600, RC[1], 2300, 255, 150, 60, 6, CORE))
anchors['reactor'] = (RC[0], -2600, RC[1])
for k in range(6):
    a = k * math.pi / 3 + math.pi / 6
    console(RC[0] + math.sin(a) * 5200, RC[1] + math.cos(a) * 5200, a + math.pi, 'scr_graph' if k % 2 else 'scr_vitals')
for (lx, lz) in ((16000, 2000), (27000, 2000), (16000, 14000), (27000, 14000)):
    lights.append((V(lx, -8500, lz), V(0.7, 0.7, 0.76), 10000))
# ------------------------------------------------------------------ lighting bake
AMBIENT = V(0.22, 0.23, 0.29)
OCC = OCC_MIN = OCC_MAX = None


def prep_occluders():
    global OCC, OCC_MIN, OCC_MAX
    OCC = np.array(occluders, float)
    OCC_MIN = np.minimum(OCC[:, 0:3], OCC[:, 3:6])
    OCC_MAX = np.maximum(OCC[:, 0:3], OCC[:, 3:6])



def shadowed(P, L, MN=None, MX=None):
    """for points P (N,3) toward light L (3,): is the segment blocked by any occluder box?"""
    if MN is None:
        MN, MX = OCC_MIN, OCC_MAX
    if len(MN) == 0:
        return np.zeros(len(P), bool)
    if len(P) > 1500:
        return np.concatenate([shadowed(P[k:k + 1500], L, MN, MX) for k in range(0, len(P), 1500)])
    D = L[None] - P                                   # (N,3)
    inv = 1.0 / np.where(np.abs(D) < 1e-6, 1e-6, D)
    t0 = (MN[None] - P[:, None]) * inv[:, None]  # (N,M,3)
    t1 = (MX[None] - P[:, None]) * inv[:, None]
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
    prep_occluders()
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
        near = np.all((OCC_MIN <= L + R) & (OCC_MAX >= L - R), axis=1)
        lit = ~shadowed(Pn[s], L, OCC_MIN[near], OCC_MAX[near])
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
def pvs():
    """what you can see from each area: itself, its halls, and the rooms at the far end of them
    (halls are straight, so you see down them into the next room) - 2 steps on the area graph"""
    out = []
    for i in range(len(areas)):
        seen = {i}
        frontier = {i}
        for _ in range(2):
            nxt = set()
            for a in frontier:
                nxt |= areas[a]['links']
            seen |= nxt
            frontier = nxt
        out.append(sum(1 << k for k in seen))
    return out


def views():
    """a camera spot per room for the tour renderer: back corner, looking across the room"""
    v = []
    for a in areas:
        if a['kind'] != 'room':
            continue
        x0, z0, x1, z1 = a['rect']
        cx, cz = x0 + (x1 - x0) * 0.12, z0 + (z1 - z0) * 0.12
        yaw = int(round(math.atan2((x0 + x1) / 2 - cx, (z0 + z1) / 2 - cz) / (2 * math.pi) * 4096)) & 4095
        v.append((a['name'], int(cx), int(-min(a['H'] - 600, 3200)), int(cz), yaw, 170))
    return v


def export():
    bake()
    chunks = {}
    for f in faces:
        c = np.mean(f['p'], 0)
        key = (f['area'], int(math.floor(c[0] / 3072)), int(math.floor(c[2] / 3072)))
        chunks.setdefault(key, []).append(f)
    vout, fout, cout = [], [], []
    for key in sorted(chunks):
        fl = chunks[key]
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
            cout.append((ctr, r, v0, len(vout) - v0, f0, len(fout) - f0, key[0]))
    for v in vout:
        assert all(-32000 <= x <= 32000 for x in v), v
    assert len(vout) < 65536 and len(areas) <= 32
    return vout, fout, cout


def tim_bytes(idx, clut, vx, vy, cy):
    tim = struct.pack('<II', 0x10, 0x09)
    tim += struct.pack('<IHHHH', 12 + 512, 0, cy, 256, 1) + struct.pack('<256H', *clut)
    tim += struct.pack('<IHHHH', 12 + 256 * 256, vx, vy, 128, 256) + idx.tobytes()
    return tim


def level_map(rp):
    """top-down plan of the level with the route (docs/level_map.png)"""
    from PIL import Image, ImageDraw
    S, OX, OZ = 1 / 50, 30000, 32000
    W, Hh = int(60000 * S), int(60000 * S)
    im = Image.new('RGB', (W, Hh), (12, 12, 18))
    d = ImageDraw.Draw(im)
    P = lambda x, z: ((x + OX) * S, Hh - (z + OZ) * S)
    for a in areas:
        x0, z0, x1, z1 = a['rect']
        col = (60, 70, 86) if a['kind'] == 'room' else (44, 50, 60)
        (ax, ay), (bx, by) = P(x0, z1), P(x1, z0)
        d.rectangle([ax, ay, bx, by], fill=col, outline=(120, 140, 160))
    for (x0, z0, x1, z1, h) in colliders:
        if h < 8000:
            (ax, ay), (bx, by) = P(x0, z1), P(x1, z0)
            d.rectangle([ax, ay, bx, by], fill=(150, 110, 60) if h < 1100 else (110, 90, 90))
    d.line([P(*p) for p in rp], fill=(80, 220, 230), width=2)
    sx, sy = P(*SPAWN[:2]); d.ellipse([sx - 5, sy - 5, sx + 5, sy + 5], fill=(80, 255, 120))
    gx, gy = P(*GOAL); d.ellipse([gx - 5, gy - 5, gx + 5, gy + 5], fill=(255, 120, 40))
    for a in areas:
        if a['kind'] == 'room':
            x0, z0, x1, z1 = a['rect']
            tx, ty = P(x0 + 300, z1 - 300)
            d.text((tx, ty), a['name'], fill=(230, 230, 240))
    im.save(os.path.join(ROOT, 'docs', 'level_map.png'))


def main():
    from PIL import Image
    pages = []
    sheet = Image.new('RGB', (256 * 3, 256 * 2))
    for k, funcs in enumerate(TX.PAGES):
        p, m = TX.build_page(funcs)
        idx, clut, rgb = TX.quantise(p, m)
        vx, vy = TX.PAGE_VRAM[k]
        pages.append(tim_bytes(idx, clut, vx, vy, 485 + k))
        sheet.paste(Image.fromarray(rgb.astype(np.uint8)), ((k % 3) * 256, (k // 3) * 256))
    sheet.save(os.path.join(ROOT, 'docs', 'lab_textures.png'))
    vout, fout, cout = export()
    vis = pvs()
    vw = views()
    nq = len(fout)
    out = []
    w = out.append
    w('/* Deadbird level: generated by tools/build_lab.py - edit the script, not this file */')
    w('#ifndef LAB_DATA_H\n#define LAB_DATA_H')
    w(f'#define LAB_NVERTS {len(vout)}\n#define LAB_NFACES {nq}\n#define LAB_NCHUNKS {len(cout)}')
    w(f'#define LAB_NBOXES {len(colliders)}\n#define LAB_NSPRITES {len(sprites)}\n#define LAB_NFANS {len(fans)}')
    w(f'#define LAB_NRINGS {len(rings)}\n#define LAB_NPODS {len(anchors["pods"])}\n#define LAB_NCEIL {len(ceilings)}')
    rp = route_points()
    w(f'#define LAB_NAREAS {len(areas)}\n#define LAB_NVIEWS {len(vw)}\n#define LAB_NROUTE {len(rp)}')
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
    for (c, r, v0, nv, f0, nf, ar) in cout:
        w('  {%d,%d,%d,%d,%d,%d,%d,%d,%d},' % (int(c[0]), int(c[1]), int(c[2]), int(r) + 1, v0, nv, f0, nf, ar))
    w('};')
    w('static const LabBox lab_boxes[LAB_NBOXES] = {')
    for (x0, z0, x1, z1, h) in colliders:
        w('  {%d,%d,%d,%d,%d},' % (int(x0), int(z0), int(x1), int(z1), int(min(h, 32000))))
    w('};')
    w('static const LabSprite lab_sprites[LAB_NSPRITES] = {')
    for s in sprites:
        w('  {%d,%d,%d,%d,%d,%d,%d,%d,%d},' % tuple(int(x) for x in s))
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
        w('  {%d,%d,%d,%d,%d},' % (int(x0), int(z0), int(x1), int(z1), int(h)))
    w('};')
    w('static const LabArea lab_areas[LAB_NAREAS] = {')
    for a, m in zip(areas, vis):
        x0, z0, x1, z1 = a['rect']
        w('  {%d,%d,%d,%d,0x%08xu,"%s"},' % (int(x0), int(z0), int(x1), int(z1), m, a['name']))
    w('};')
    w('/* waypoints from the spawn to the reactor (used by the route test) */')
    w('static const s32 lab_route[LAB_NROUTE][2] = {')
    w('  ' + ' '.join('{%d,%d},' % p for p in rp))
    w('};')
    w('static const LabView lab_views[LAB_NVIEWS] = {')
    for (n, x, y, z, yaw, pitch) in vw:
        w('  {"%s",%d,%d,%d,%d,%d},' % (n, x, y, z, yaw, pitch))
    w('};')
    w(f'#define LAB_NPAGES {len(pages)}')
    w('static const s16 lab_page_xy[LAB_NPAGES][2] = { ' + ' '.join('{%d,%d},' % xy for xy in TX.PAGE_VRAM) + ' };')
    for k, t in enumerate(pages):
        w(f'#define LAB_TIM{k}_SIZE {len(t)}')
        w(f'static const unsigned char lab_tim{k}[{len(t)}] = {{')
        for i in range(0, len(t), 32):
            w('  ' + ','.join(str(b) for b in t[i:i + 32]) + ',')
        w('};')
    w('#endif')
    open(os.path.join(ROOT, 'game', 'lab_data.h'), 'w').write('\n'.join(out) + '\n')
    level_map(rp)
    tris = sum(2 if (f['flags'] & F_QUAD) else 1 for _, f in fout)
    rooms = [a for a in areas if a['kind'] == 'room']
    floor = sum((a['rect'][2] - a['rect'][0]) * (a['rect'][3] - a['rect'][1]) for a in areas) / 1e6
    print(f'level: {len(rooms)} rooms + {len(areas) - len(rooms)} halls, floor area {floor:.0f} M sq units')
    print(f'mesh: {len(vout)} verts, {nq} quads (~{tris} triangles), {len(cout)} chunks, {len(colliders)} boxes, '
          f'{len(lights)} lights, {len(sprites)} halos')
    print(f'data: verts {len(vout) * 6} B, faces {nq * 32} B, textures {sum(len(t) for t in pages)} B')


if __name__ == '__main__':
    main()
