#!/usr/bin/env python3
"""PS1-style arm chainsaw for Deadbird's right hand.

Box/prism-modelled mesh (~140 faces + a 36-face chain), one 256x192 8bpp CLUT texture with
everything painted and lit in it:
  * unique texel area per face (true planar UVs, so the PS1's affine mapping is exact)
  * painted details: hazard stripes, stencils, rivets, cooling fins, spark plug lead, pull-start
    vents, leather straps with stitching, a brushed bar with nose sprocket, chips, rust, soot, oil
  * baked lighting: key + fill + rim, metal highlights, shadow rays and ambient occlusion
    against the saw's own parts
  * the chain is a 4-frame strip (cutters with transparent gaps, texel 0); the game shows one
    frame per tick so the chain runs around the bar
  * 15-bit colour + PS1 4x4 ordered dither -> 255-colour CLUT, TIM at VRAM (768,32), CLUT (0,491)

Outputs: assets/chainsaw_mesh.h, assets/chainsaw.tim (+ assets/chainsaw_tim.h via bin2c),
         docs/chainsaw_texture.png, tools/out/chainsaw_viewer.json (for the 3D viewer)
"""
import math, os, json, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tools', 'out')
os.makedirs(OUT, exist_ok=True)

TW, TH = 256, 192                  # texture size (8bpp: 128 VRAM halfwords wide)
VRAM_X, VRAM_Y = 768, 32           # inside the 8bpp texture page at (768,0)
CLUT_Y = 491
CH_ROWS = 12                       # rows per chain frame
CH_FRAMES = 4
ATLAS_Y0 = CH_ROWS * CH_FRAMES     # rows 48.. hold the per-face atlas
FX = 1024                          # 1 model metre = 1024 engine units (same as the demon)
rng = np.random.default_rng(7)


def nrm(v):
    v = np.asarray(v, float)
    n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.where(n < 1e-12, 1, n)


# ---------------------------------------------------------------- the mount frame
# Model space: Y up, +Z = her front, right arm along -X (T-pose). The mount sits in the middle of
# her right fist; A points down the arm past the knuckles. Everything is built as (a, up, sd)
# around the mount and converted to model space.
D = nrm([-4094, 0, 141])          # DEMON_MOUNT_DX/-DY/DZ
Y = np.array([0., 1., 0.])
S = nrm(np.cross(Y, D))            # ~ +Z (her front)


def M(a, up, sd):
    return a * D + up * Y + sd * S


# ---------------------------------------------------------------- geometry
FACES = []          # dict(p=[3|4 model pts, cyclic], n=[corner normals], mat, part, box)
BOXES = []          # occluders for baked shadows / AO: (min, max) in (a, up, sd)
CUR = {'box': -1}


def occluder(lo, hi):
    BOXES.append((np.array(lo, float), np.array(hi, float)))
    CUR['box'] = len(BOXES) - 1


def face(pts, mat, outward, normals=None, part='body', tag=None, ids=None, ring=0):
    pts = [np.asarray(p, float) for p in pts]
    flipped = False
    n = np.cross(pts[1] - pts[0], pts[2] - pts[0])
    if len(pts) == 4:
        n = n + np.cross(pts[2] - pts[0], pts[3] - pts[0])
    if np.dot(n, outward) < 0:
        pts = pts[::-1]; flipped = True
        if normals is not None:
            normals = normals[::-1]
    # edges inside a fan (between faces of one flat cap) get no chips / catch-light
    inner = set()
    if ids is not None:
        o = ids[::-1] if flipped else ids
        for k in range(len(o)):
            i, j = o[k], o[(k + 1) % len(o)]
            if i is None or j is None or (ring and (j - i) % ring not in (1, ring - 1)):
                inner.add(k)
    fn = nrm(np.cross(pts[1] - pts[0], pts[2] - pts[0]) + (np.cross(pts[2] - pts[0], pts[3] - pts[0]) if len(pts) == 4 else 0))
    if normals is None:
        normals = [fn] * len(pts)
    FACES.append(dict(p=pts, n=[nrm(x) for x in normals], fn=fn, mat=mat, part=part, box=CUR['box'], tag=tag, inner=inner))


def box(lo, hi, mat, skip=(), part='body'):
    """axis-aligned box in (a, up, sd); skip any of 'a-','a+','u-','u+','s-','s+'."""
    (a0, u0, s0), (a1, u1, s1) = lo, hi
    occluder(lo, hi)
    c = M((a0 + a1) / 2, (u0 + u1) / 2, (s0 + s1) / 2)
    sides = {
        'a-': [(a0, u0, s0), (a0, u1, s0), (a0, u1, s1), (a0, u0, s1)],
        'a+': [(a1, u0, s0), (a1, u1, s0), (a1, u1, s1), (a1, u0, s1)],
        'u-': [(a0, u0, s0), (a1, u0, s0), (a1, u0, s1), (a0, u0, s1)],
        'u+': [(a0, u1, s0), (a1, u1, s0), (a1, u1, s1), (a0, u1, s1)],
        's-': [(a0, u0, s0), (a1, u0, s0), (a1, u1, s0), (a0, u1, s0)],
        's+': [(a0, u0, s1), (a1, u0, s1), (a1, u1, s1), (a0, u1, s1)],
    }
    for k, q in sides.items():
        if k in skip:
            continue
        P = [M(*v) for v in q]
        face(P, mat, sum(P) / 4 - c, part=part, tag=k)


def prism(axis, center, profile, t0, t1, mat, cap0=True, cap1=True, smooth=True, part='body', capmat=None, outward_in=False):
    """prism along axis 'a'|'u'|'s'; profile = [(p, q)] around the axis (cyclic, convex)."""
    def P(t, p, q):
        a, u, s = center
        if axis == 'a': return M(t, u + p, s + q)
        if axis == 'u': return M(a + p, t, s + q)
        return M(a + p, u + q, t)
    lo = np.array(center, float); hi = np.array(center, float)
    ext = max(max(abs(p), abs(q)) for p, q in profile)
    i = 'aus'.index(axis)
    lo -= ext; hi += ext; lo[i] = min(t0, t1); hi[i] = max(t0, t1)
    occluder(lo, hi)
    n = len(profile)
    axc = (P(t0, 0, 0) + P(t1, 0, 0)) / 2
    for k in range(n):
        p0, p1 = profile[k], profile[(k + 1) % n]
        Q = [P(t0, *p0), P(t0, *p1), P(t1, *p1), P(t1, *p0)]
        if smooth:
            nn = [P(t0, *p0) - P(t0, 0, 0), P(t0, *p1) - P(t0, 0, 0), P(t1, *p1) - P(t1, 0, 0), P(t1, *p0) - P(t1, 0, 0)]
        else:
            nn = None
        out = sum(Q) / 4 - axc
        if outward_in:
            out = -out
        face(Q, mat, out, normals=nn, part=part, tag='side')
    for t, do in ((t0, cap0), (t1, cap1)):
        if not do:
            continue
        ring = [P(t, *pq) for pq in profile]
        out = P(t, 0, 0) - axc
        if n == 4:
            face(ring, capmat or mat, out, part=part, tag='cap')
        else:   # fan of quads: (0,1,2,3), (3,4,5,6)... closing with the first vertex
            k = 0
            while k + 2 < n:
                idx = [0, k + 1, k + 2, k + 3] if k + 3 < n else [0, k + 1, k + 2]
                face([ring[j % n] for j in idx], capmat or mat, out, part=part, tag='cap', ids=[j % n for j in idx], ring=n)
                k += 2


def ngon(r, n, rot=0.0, rx=None, ry=None):
    rx = rx or r; ry = ry or r
    return [(rx * math.cos(rot + 2 * math.pi * k / n), ry * math.sin(rot + 2 * math.pi * k / n)) for k in range(n)]


def octo(hu, hs, c):
    return [(hu, hs - c), (hu - c, hs), (-hu + c, hs), (-hu, hs - c), (-hu, -hs + c), (-hu + c, -hs), (hu - c, -hs), (hu, -hs + c)]


# housing around the fist: octagonal can, forearm enters at the back
HA0, HA1 = -0.13, 0.072
prism('a', (0, 0, 0), octo(0.062, 0.058, 0.021), HA0, HA1, 'housing', smooth=False)
# bar clamp plate at the front
box((0.072, -0.024, -0.052), (0.098, 0.024, 0.052), 'clamp', skip=('a-',))
# engine: block, finned cylinder, spark plug
box((-0.118, 0.060, -0.047), (0.022, 0.112, 0.047), 'engine', skip=('u-',))
prism('u', (-0.048, 0, 0), ngon(0.033, 6, math.pi / 6), 0.112, 0.172, 'cylinder', cap0=False, capmat='cylhead')
box((-0.056, 0.172, -0.008), (-0.040, 0.192, 0.008), 'plug', skip=('u-',))
# fuel tank over the forearm, with its cap
box((-0.205, 0.030, -0.043), (-0.118, 0.104, 0.043), 'tank', skip=('u-',))
prism('u', (-0.170, 0, 0.0), ngon(0.014, 6), 0.104, 0.117, 'fuelcap', cap0=False, smooth=False)
# muffler on her front side, exhaust pipe pointing down the bar
box((-0.100, 0.066, 0.047), (-0.020, 0.104, 0.074), 'muffler', skip=('s-',))
prism('a', (0, 0.085, 0.063), ngon(0.009, 4, math.pi / 4), -0.020, 0.030, 'pipe', cap0=False, capmat='pipeend', smooth=False)
# pull-start cover + grip on the back side (the side the camera sees)
prism('s', (-0.050, 0.086, 0), ngon(0.031, 8, math.pi / 8), -0.047, -0.060, 'pullcover', cap0=False, capmat='pullface', smooth=False)
box((-0.072, 0.080, -0.068), (-0.028, 0.092, -0.060), 'grip', skip=('s+',))
# forearm straps + side rails tying them to the housing
for a0 in (-0.215, -0.285):
    prism('a', (0, 0, 0), octo(0.053, 0.052, 0.018), a0, a0 + 0.024, 'strap', cap0=False, cap1=False, smooth=False)
for sd in (-1, 1):
    box((-0.285, -0.009, sd * 0.052 - 0.004), (HA0, 0.011, sd * 0.052 + 0.004), 'rail', skip=('a-', 'a+'))

# ---- guide bar (flat in the a/sd plane), chain runs around its edge
BA0, BNOSE, HW, BT = 0.060, 0.452, 0.045, 0.0055
CIN, COUT = HW - 0.010, HW + 0.020           # chain band radial extent
CA0 = 0.093                                  # chain visible from the clamp outwards
CUR['box'] = -1
occluder((BA0, -BT, -HW), (BNOSE + HW, BT, HW))
bar_box = CUR['box']
NOSE_SEG = 6


def bar_outline(r):
    """points (a, sd) of the bar outline at radius r: straight +sd edge out, nose, straight -sd edge"""
    return [(BA0, r), (BNOSE, r)] + [(BNOSE + r * math.sin(math.pi * k / NOSE_SEG), r * math.cos(math.pi * k / NOSE_SEG)) for k in range(1, NOSE_SEG)] + [(BNOSE, -r), (BA0, -r)]


for side in (1, -1):
    up = side * BT
    out = Y * side
    # straight plate (two quads along the bar) + nose fan
    for a0, a1 in ((BA0, (BA0 + BNOSE) / 2), ((BA0 + BNOSE) / 2, BNOSE)):
        face([M(a0, up, CIN), M(a1, up, CIN), M(a1, up, -CIN), M(a0, up, -CIN)], 'bar', out, part='body', tag='plate',
)
        FACES[-1]['box'] = bar_box
    for k in range(NOSE_SEG):
        t0, t1 = math.pi * k / NOSE_SEG, math.pi * (k + 1) / NOSE_SEG
        face([M(BNOSE, up, 0), M(BNOSE + CIN * math.sin(t0), up, CIN * math.cos(t0)), M(BNOSE + CIN * math.sin(t1), up, CIN * math.cos(t1))],
             'bar', out, part='body', tag='nose')
        FACES[-1]['box'] = bar_box

# ---- edges shared by two coplanar faces of the same material are not real edges (no chips)
def _k(p):
    return tuple(np.round(p, 5))


_edges = {}
for i, f in enumerate(FACES):
    for k in range(len(f['p'])):
        e = frozenset((_k(f['p'][k]), _k(f['p'][(k + 1) % len(f['p'])])))
        _edges.setdefault(e, []).append((i, k))
for e, lst in _edges.items():
    for i, k in lst:
        for j, _ in lst:
            if j != i and FACES[j]['mat'] == FACES[i]['mat'] and FACES[j]['fn'] @ FACES[i]['fn'] > 0.999:
                FACES[i]['inner'].add(k)

# ---- chain path: arc-length parameter along the centre radius HW
L_STRAIGHT = BNOSE - CA0
L = 2 * L_STRAIGHT + math.pi * HW


def chain_pt(s, r):
    """(a, sd) at path length s, radius r"""
    if s <= L_STRAIGHT:
        return CA0 + s, r
    s -= L_STRAIGHT
    if s <= math.pi * HW:
        t = s / HW
        return BNOSE + r * math.sin(t), r * math.cos(t)
    s -= math.pi * HW
    return BNOSE - s, -r


def chain_dir_out(s):
    if s <= L_STRAIGHT: return np.array([0, 1.0])
    if s <= L_STRAIGHT + math.pi * HW:
        t = (s - L_STRAIGHT) / HW
        return np.array([math.sin(t), math.cos(t)])
    return np.array([0, -1.0])


CH_S = sorted(set([0, L_STRAIGHT / 3, 2 * L_STRAIGHT / 3, L_STRAIGHT]
                  + [L_STRAIGHT + math.pi * HW * k / NOSE_SEG for k in range(1, NOSE_SEG)]
                  + [L_STRAIGHT + math.pi * HW + L_STRAIGHT * k / 3 for k in range(4)]))
CHAIN = []   # (pts, normal, uv) for frame 0 - frames shift v by CH_ROWS
US = 255.0 / L


def chain_uv(s, row):
    return (int(round(s * US)), int(row))


for side in (1, -1):
    up = side * (BT + 0.0016)
    for s0, s1 in zip(CH_S[:-1], CH_S[1:]):
        p = [chain_pt(s0, CIN), chain_pt(s1, CIN), chain_pt(s1, COUT), chain_pt(s0, COUT)]
        P = [M(a, up, sd) for a, sd in p]
        uv = [chain_uv(s0, 0), chain_uv(s1, 0), chain_uv(s1, CH_ROWS - 1), chain_uv(s0, CH_ROWS - 1)]
        n = np.cross(P[1] - P[0], P[2] - P[0])
        if np.dot(n, Y * side) < 0:
            P = P[::-1]; uv = uv[::-1]
        CHAIN.append(dict(p=P, uv=uv, n=Y * side))
# edge ribbon (seen edge-on): the link rows
RE = HW + 0.005
for s0, s1 in zip(CH_S[:-1], CH_S[1:]):
    a0, d0 = chain_pt(s0, RE); a1, d1 = chain_pt(s1, RE)
    P = [M(a0, 0.0085, d0), M(a1, 0.0085, d1), M(a1, -0.0085, d1), M(a0, -0.0085, d0)]
    o = chain_dir_out((s0 + s1) / 2)
    out = M(0, 0, 0) + o[0] * D + o[1] * S
    uv = [chain_uv(s0, 1), chain_uv(s1, 1), chain_uv(s1, 6), chain_uv(s0, 6)]
    n = np.cross(P[1] - P[0], P[2] - P[0])
    if np.dot(n, out) < 0:
        P = P[::-1]; uv = uv[::-1]
    CHAIN.append(dict(p=P, uv=uv, n=nrm(out)))


# ---------------------------------------------------------------- UV atlas (true planar shapes)
DENS = {'housing': 330, 'clamp': 300, 'engine': 330, 'cylinder': 380, 'cylhead': 330, 'plug': 420, 'tank': 300,
        'fuelcap': 380, 'muffler': 320, 'pipe': 330, 'pipeend': 400, 'pullcover': 360, 'pullface': 400,
        'grip': 380, 'strap': 300, 'rail': 260, 'bar': 300}
for f in FACES:
    P = f['p']
    e1 = nrm(P[1] - P[0]); e2 = nrm(np.cross(f['fn'], e1))
    q = np.array([[np.dot(p - P[0], e1), np.dot(p - P[0], e2)] for p in P])
    # keep "up" of the texture pointing up where it can (stencils read the right way)
    f['e1'], f['e2'], f['q'] = e1, e2, q
    d = DENS[f['mat']] * 1.0
    lo = q.min(0)
    f['lo'], f['dens'] = lo, d
    f['w'] = int(math.ceil((q[:, 0].max() - lo[0]) * d)) + 1
    f['h'] = int(math.ceil((q[:, 1].max() - lo[1]) * d)) + 1


def pack(scale):
    x = y = rowh = 0
    pos = {}
    for i in sorted(range(len(FACES)), key=lambda i: -FACES[i]['h']):
        w = max(2, int(FACES[i]['w'] * scale)) + 2
        h = max(2, int(FACES[i]['h'] * scale)) + 2
        if x + w > TW:
            x = 0; y += rowh; rowh = 0
        pos[i] = (x, y)
        x += w; rowh = max(rowh, h)
    return pos, y + rowh


scale = 1.0
while True:
    pos, used_h = pack(scale)
    if used_h <= TH - ATLAS_Y0:
        break
    scale *= 0.97
print(f'atlas scale {scale:.3f}, {used_h} rows used of {TH - ATLAS_Y0}')
for i, f in enumerate(FACES):
    f['dens'] *= scale
    x, y = pos[i]
    f['ox'], f['oy'] = x + 1, y + 1 + ATLAS_Y0
    f['uv'] = [(int(round(f['ox'] + (qq[0] - f['lo'][0]) * f['dens'])), int(round(f['oy'] + (qq[1] - f['lo'][1]) * f['dens']))) for qq in f['q']]

# ---------------------------------------------------------------- noise helpers


def hash3(ix, iy, iz):
    h = (ix * 374761393 + iy * 668265263 + iz * 2147483647) & 0xffffffff
    h = ((h ^ (h >> 13)) * 1274126177) & 0xffffffff
    return (h & 0xffff) / 65535.0


def vnoise(p, freq):
    p = p * freq
    i = np.floor(p).astype(np.int64); f = p - i
    f = f * f * (3 - 2 * f)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1]) * (f[:, 2] if dz else 1 - f[:, 2])
                out = out + w * hash3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
    return out


def fbm(p, freq, oct=3):
    s, a, tot = 0, 1.0, 0
    for _ in range(oct):
        s = s + a * vnoise(p, freq); tot += a; a *= 0.5; freq *= 2.03
    return s / tot


def text_mask(txt, w, h):
    f = ImageFont.load_default()
    im = Image.new('L', (len(txt) * 7 + 4, 14))
    ImageDraw.Draw(im).text((1, 0), txt, font=f, fill=255)
    bb = im.getbbox(); im = im.crop(bb)
    return np.array(im.resize((w, h), Image.BILINEAR), float) / 255


MASKS = {'HELLSAW': text_mask('HELLSAW', 140, 28), 'DEADBIRD': text_mask('DEADBIRD', 160, 24), 'FUEL': text_mask('FUEL', 60, 18),
         '666': text_mask('666cc', 60, 18)}


def stencil(name, x, y, x0, x1, y0, y1):
    """sample a text mask placed over [x0,x1]x[y0,y1] (metres in some face-local 2D frame)"""
    m = MASKS[name]
    u = (x - x0) / (x1 - x0); v = 1 - (y - y0) / (y1 - y0)
    inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1)
    iu = np.clip((u * m.shape[1]).astype(int), 0, m.shape[1] - 1); iv = np.clip((v * m.shape[0]).astype(int), 0, m.shape[0] - 1)
    return np.where(inside, m[iv, iu], 0.0)


def smooth01(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- materials (per texel)
C = lambda *c: np.array(c, float) / 255


def paint(mat, P, loc, n, edge, f):
    """P: model points (N,3); loc: (a, up, sd) (N,3); n normals; edge: distance to face edge (m).
    returns base colour (N,3), metalness (N,), roughness exponent (N,)"""
    N = len(P)
    a, up, sd = loc[:, 0], loc[:, 1], loc[:, 2]
    g = fbm(P * 1.0 + 3.1, 60)
    g2 = fbm(P + 7.7, 180, 2)
    col = np.zeros((N, 3)); metal = np.zeros(N); shin = np.full(N, 12.0)
    worn = smooth01(0.006, 0.0, edge) * smooth01(0.35, 0.75, g2 + g * 0.3)       # chipped paint on edges

    def rivets(mask_pts, rad=0.0035):
        """mask_pts: list of (a, up, sd) rivet centres -> bump shading term"""
        out = np.zeros(N); hl = np.zeros(N)
        for c in mask_pts:
            d = np.linalg.norm(loc - np.array(c), axis=1)
            k = d < rad
            out[k] = 1
            hl = np.maximum(hl, np.where(k, 1 - d / rad, 0))
        return out, hl

    if mat in ('housing', 'clamp', 'engine', 'tank'):
        base = {'housing': C(118, 16, 14), 'clamp': C(40, 40, 44), 'engine': C(30, 30, 33), 'tank': C(150, 64, 18)}[mat]
        col[:] = base
        col *= (0.82 + 0.3 * g)[:, None]
        steel = C(150, 150, 155) * (0.7 + 0.4 * g2)[:, None]
        if mat == 'housing':
            # hazard band at the front
            hz = (a > 0.040) & (a < 0.064)
            stripe = ((a * 160 + (up + sd) * 160) % 2) < 1
            col[hz & stripe] = C(226, 170, 20)
            col[hz & ~stripe] = C(20, 18, 16)
            col[(np.abs(a - 0.040) < 0.002) | (np.abs(a - 0.064) < 0.002)] = C(16, 14, 12)
            # panel seam + seam rivets
            col[np.abs(a - 0.030) < 0.0015] *= 0.35
            pts = [(aa, uu * 0.062, ss) for aa in (-0.118, -0.045, 0.024) for uu, ss in ((1, 0.022), (1, -0.022))]
            pts += [(aa, 0.026, ss * 0.058) for aa in (-0.118, 0.024) for ss in (1, -1)]
            pts += [(aa, -0.026, ss * 0.058) for aa in (-0.118, 0.024) for ss in (1, -1)]
            rv, hl = rivets(pts, 0.0042)
            col[rv > 0] = (C(90, 88, 84) + hl[rv > 0, None] * C(120, 120, 110))
            # stencils on both flat sides (read along the arm)
            for s in (1, -1):
                k = (sd * s > 0.04)
                xx = a if s < 0 else -a
                x0, x1 = (-0.108, 0.020) if s < 0 else (-0.020, 0.108)
                m = stencil('HELLSAW', xx, up, x0, x1, -0.014, 0.016) * k
                col = col * (1 - m[:, None] * 0.95) + m[:, None] * C(225, 210, 190) * (0.8 + 0.2 * g)[:, None]
            # a skull-ish warning roundel on the underside
            dr = np.hypot(a + 0.05, sd)
            k = (up < -0.055) & (dr < 0.018)
            col[k] = C(220, 200, 40)
            col[k & (dr < 0.013)] = C(25, 22, 18)
            eyes = k & ((np.hypot(a + 0.054, sd - 0.005) < 0.003) | (np.hypot(a + 0.054, sd + 0.005) < 0.003))
            col[eyes] = C(220, 200, 40)
        if mat == 'engine':
            # crinkle finish + cooling slots on the sides, bolts at the corners
            slot = (np.abs(sd) > 0.045) & (((a * 220) % 1) < 0.35) & (up > 0.072) & (up < 0.100) & (a > -0.105) & (a < 0.010)
            col[slot] = C(6, 6, 8)
            pts = [(aa, uu, ss) for aa in (-0.110, 0.014) for uu in (0.068, 0.106) for ss in (0.047, -0.047)]
            pts += [(aa, 0.112, ss) for aa in (-0.110, 0.014) for ss in (0.040, -0.040)]
            rv, hl = rivets(pts, 0.0045)
            col[rv > 0] = C(70, 70, 72) + hl[rv > 0, None] * C(150, 150, 140)
            m = stencil('666', -a if True else a, up, -0.012, 0.040, 0.070, 0.084) * (sd < -0.04)
            col = col * (1 - m[:, None]) + m[:, None] * C(200, 40, 24)
        if mat == 'tank':
            m = stencil('FUEL', a, up, -0.196, -0.128, 0.052, 0.074) * (sd < -0.04)
            m2 = stencil('FUEL', -a, up, 0.128, 0.196, 0.052, 0.074) * (sd > 0.04)
            col = col * (1 - (m + m2)[:, None]) + (m + m2)[:, None] * C(20, 16, 12)
            # fuel stains running down from the cap
            drip = smooth01(0.6, 0.9, fbm(np.stack([a * 3, np.zeros(N), sd * 3], 1), 40, 2)) * smooth01(0.03, 0.104, up)
            col *= (1 - 0.45 * drip)[:, None]
        if mat == 'clamp':
            pts = [(0.098, uu, ss) for uu in (0.014, -0.014) for ss in (0.030, -0.030)]
            rv, hl = rivets(pts, 0.006)
            hexb = rv > 0
            col[hexb] = C(120, 118, 110) * (0.6 + 0.6 * hl[hexb, None])
            col[hexb & (hl > 0.55)] = C(30, 30, 30)
            metal[:] = 0.5
        col = col * (1 - worn[:, None]) + steel * worn[:, None]
        metal = np.maximum(metal, worn * 0.8)
        # grime collecting low down and in corners
        col *= (1 - 0.35 * smooth01(0.0, -0.06, up) * g)[:, None]
        shin[:] = 10
    elif mat in ('cylinder', 'cylhead'):
        alu = C(170, 172, 168) * (0.8 + 0.3 * g2)[:, None]
        if mat == 'cylinder':
            fin = ((up - 0.112) % 0.0075) / 0.0075
            groove = fin > 0.62
            col[:] = alu
            col[groove] = C(28, 26, 24)
            col[(fin > 0.50) & ~groove] *= 0.6          # underside of each fin
            col[fin < 0.12] *= 1.25                    # fin top edge catches the light
        else:
            col[:] = alu * 0.85
            r = np.hypot(a + 0.048, sd)
            col[(r > 0.010) & (r < 0.013)] = C(40, 40, 40)
            for k in range(6):
                t = math.pi / 3 * k
                col[np.hypot(a + 0.048 - 0.024 * math.cos(t), sd - 0.024 * math.sin(t)) < 0.0028] = C(60, 60, 58)
        col *= (0.75 + 0.35 * g)[:, None]
        metal[:] = 0.8; shin[:] = 30
        col = np.clip(col, 0, 1)
    elif mat == 'plug':
        col[:] = np.where((up > 0.183)[:, None], C(225, 222, 210), C(20, 20, 22))
        col[(up > 0.176) & (up < 0.181)] = C(150, 20, 16)                 # red HT lead
        metal[:] = 0.2; shin[:] = 25
    elif mat == 'fuelcap':
        col[:] = C(22, 22, 24) * (0.8 + 0.4 * g2)[:, None]
        col[((np.arctan2(sd, a + 0.170) * 6) % 1) < 0.3] *= 1.8          # knurling
    elif mat in ('muffler', 'pipe', 'pipeend'):
        rust = smooth01(0.4, 0.75, g + g2 * 0.4)
        col[:] = C(110, 102, 96) * (1 - rust)[:, None] + C(120, 52, 20) * rust[:, None]
        blue = smooth01(0.5, 0.0, np.abs(a + 0.06) * 10) * (mat == 'muffler')
        col = col * (1 - 0.35 * blue[:, None]) + C(70, 60, 120) * 0.35 * blue[:, None]
        if mat == 'muffler':
            holes = (sd > 0.072) & (((a * 180) % 1) < 0.4) & (((up * 180) % 1) < 0.4) & (a < -0.03) & (a > -0.09) & (up > 0.072) & (up < 0.098)
            col[holes] = C(10, 8, 6)
        if mat == 'pipeend':
            col[np.hypot(up - 0.085, sd - 0.063) < 0.0055] = C(5, 4, 3)
        soot = smooth01(-0.03, 0.03, a) * (mat == 'pipe')
        col *= (1 - 0.6 * soot)[:, None]
        metal[:] = 0.6; shin[:] = 14
    elif mat in ('pullcover', 'pullface'):
        col[:] = C(26, 26, 28) * (0.85 + 0.3 * g2)[:, None]
        if mat == 'pullface':
            r = np.hypot(a + 0.050, up - 0.086)
            ang = np.arctan2(up - 0.086, a + 0.050)
            vents = (r > 0.012) & (r < 0.026) & (((ang / (2 * np.pi) * 12) % 1) < 0.45)
            col[vents] = C(4, 4, 5)
            col[(r > 0.0275) & (r < 0.031)] = C(150, 24, 18)          # red rim
            col[r < 0.006] = C(150, 150, 150)                          # centre bolt
            col[r < 0.0025] = C(40, 40, 40)
        metal[:] = 0.1; shin[:] = 20
    elif mat == 'grip':
        col[:] = C(150, 24, 18) * (0.8 + 0.3 * g2)[:, None]
        col[((a * 300) % 1) < 0.3] *= 0.55                         # grip ribs
        shin[:] = 8
    elif mat == 'strap':
        col[:] = C(84, 52, 30) * (0.7 + 0.5 * g)[:, None]
        e = np.minimum(np.abs(a - np.round(a / 0.035) * 0) , 1)
        # stitching near both edges of the strap
        for a0 in (-0.215, -0.285):
            for off in (0.004, 0.020):
                k = (np.abs(a - (a0 + off)) < 0.0013) & ((((up + sd) * 220) % 1) < 0.5)
                col[k] = C(190, 170, 120)
        # buckle on the underside
        bk = (up < -0.045) & (np.abs(sd) < 0.016)
        col[bk] = C(160, 156, 140)
        col[bk & (np.abs(sd) < 0.010) & ((((a + 0.3) * 1000) % 35) > 6) & ((((a + 0.3) * 1000) % 35) < 18)] = C(30, 24, 18)
        metal[bk] = 0.9
        col *= (1 - 0.35 * smooth01(0.6, 0.85, g2))[:, None]        # scuffs
        shin[:] = 6
    elif mat == 'rail':
        col[:] = C(60, 60, 64) * (0.8 + 0.4 * g)[:, None]
        rv, hl = rivets([(aa, 0.001, s * 0.056) for aa in (-0.27, -0.20, -0.145) for s in (1, -1)], 0.004)
        col[rv > 0] = C(120, 118, 110) * (0.6 + 0.6 * hl[rv > 0, None])
        metal[:] = 0.7; shin[:] = 18
    elif mat == 'bar':
        streak = fbm(np.stack([a * 2, sd * 60, up * 5], 1), 12, 2)
        col[:] = C(132, 134, 138) * (0.75 + 0.45 * streak)[:, None]
        # oily darkening towards the chain, nose sprocket, rivets, engraving, blood
        col *= (1 - 0.45 * smooth01(CIN - 0.010, CIN, np.abs(np.where(a > BNOSE, np.hypot(a - BNOSE, sd), sd))))[:, None]
        rn = np.hypot(a - BNOSE, sd)
        spro = rn < 0.026
        col[spro] = C(96, 96, 100)
        col[spro & (rn > 0.0235)] = C(30, 30, 30)
        for k in range(8):
            t = math.pi / 4 * k
            col[np.hypot(a - BNOSE - 0.015 * math.cos(t), sd - 0.015 * math.sin(t)) < 0.0035] = C(18, 18, 18)
        col[rn < 0.004] = C(170, 170, 170)
        for aa in (0.120, 0.300):
            col[np.hypot(a - aa, sd) < 0.004] = C(175, 175, 172)
        m = stencil('DEADBIRD', a if True else -a, sd * np.sign(up + 1e-9), 0.150, 0.270, -0.010, 0.010)
        col *= (1 - 0.6 * m)[:, None]
        blood = smooth01(0.62, 0.78, fbm(P + 1.3, 70, 3)) * smooth01(0.25, 0.45, a)
        col = col * (1 - blood[:, None]) + C(70, 6, 6) * blood[:, None]
        metal[:] = 0.9 * (1 - blood); shin[:] = 40
    col = np.clip(col, 0, 1)
    return col, metal, shin


# ---------------------------------------------------------------- baked lighting
KEY = nrm(M(0.15, 0.80, -0.55) - M(0, 0, 0))     # from above, behind her (camera side)
FILL = nrm(M(-0.3, -0.4, 0.8) - M(0, 0, 0))
RIM = nrm(M(0.7, 0.2, 0.5) - M(0, 0, 0))
VIEW = nrm(M(0.2, 0.3, -1.0) - M(0, 0, 0))
LOCAL = np.stack([D, Y, S])                       # model -> (a, up, sd)


def to_local(P):
    return P @ LOCAL.T


def ray_hits(o, d, skip, tmax):
    """o,d in local (a,up,sd); any box hit within tmax (excluding box index skip)"""
    hit = np.zeros(len(o), bool)
    for bi, (lo, hi) in enumerate(BOXES):
        if bi == skip:
            continue
        with np.errstate(divide='ignore', invalid='ignore'):
            inv = 1.0 / np.where(np.abs(d) < 1e-9, 1e-9, d)
            t0 = (lo - o) * inv; t1 = (hi - o) * inv
        tmin = np.minimum(t0, t1).max(1); tmx = np.maximum(t0, t1).min(1)
        hit |= (tmx >= np.maximum(tmin, 1e-4)) & (tmin < tmax)
    return hit


HEMI = nrm(rng.normal(size=(10, 3)))
img = np.zeros((TH, TW, 3)); used = np.zeros((TH, TW), bool); alpha = np.ones((TH, TW), bool)

for fi, f in enumerate(FACES):
    w = int(math.ceil((f['q'][:, 0].max() - f['lo'][0]) * f['dens'])) + 2
    h = int(math.ceil((f['q'][:, 1].max() - f['lo'][1]) * f['dens'])) + 2
    xs, ys = np.meshgrid(np.arange(-1, w), np.arange(-1, h))
    tx = (f['ox'] + xs).ravel(); ty = (f['oy'] + ys).ravel()
    ok = (tx >= 0) & (tx < TW) & (ty >= ATLAS_Y0) & (ty < TH)
    tx, ty = tx[ok], ty[ok]
    # texel centre -> face plane (exact inverse of the planar UVs)
    qx = (tx - f['ox'] + 0.5) / f['dens'] + f['lo'][0]
    qy = (ty - f['oy'] + 0.5) / f['dens'] + f['lo'][1]
    P = f['p'][0] + qx[:, None] * f['e1'] + qy[:, None] * f['e2']
    # corner-normal interpolation (inverse-distance in the face plane)
    q = f['q']
    dd = np.stack([np.hypot(qx - q[k, 0], qy - q[k, 1]) for k in range(len(q))], 1) + 1e-4
    wts = 1 / dd ** 2; wts /= wts.sum(1, keepdims=True)
    nn = nrm(sum(wts[:, k:k + 1] * f['n'][k] for k in range(len(q))))
    nn = nrm(nn * 0.6 + f['fn'] * 0.4)
    # distance to the face outline (for chips / edge highlights)
    edge = np.full(len(qx), 9.0)
    for k in range(len(q)):
        if k in f['inner']:
            continue
        A, B = q[k], q[(k + 1) % len(q)]
        ab = B - A; t = np.clip(((qx - A[0]) * ab[0] + (qy - A[1]) * ab[1]) / max(ab @ ab, 1e-12), 0, 1)
        edge = np.minimum(edge, np.hypot(qx - A[0] - t * ab[0], qy - A[1] - t * ab[1]))
    loc = to_local(P)
    col, metal, shin = paint(f['mat'], P, loc, nn, edge, f)
    # lighting
    ln = to_local(nn)
    o = loc + ln * 0.0015
    sh = ray_hits(o, np.tile(to_local(KEY), (len(o), 1)), f['box'], 0.6)
    ao = np.zeros(len(o))
    for hd in HEMI:
        d = hd if True else hd
        dl = np.tile(to_local(d), (len(o), 1))
        flip = (dl * ln).sum(1) < 0
        dl[flip] *= -1
        ao += ray_hits(o, dl, f['box'], 0.045)
    ao = 1 - 0.6 * ao / len(HEMI)
    ndl = np.clip(nn @ KEY, 0, 1) * np.where(sh, 0.2, 1.0)
    diff = 0.30 * ao + 0.95 * ndl + 0.28 * np.clip(nn @ FILL, 0, 1) * ao + 0.35 * np.clip(nn @ RIM, 0, 1) ** 2
    H = nrm(KEY + VIEW)
    spec = np.clip(nn @ H, 0, 1) ** shin * np.where(sh, 0, 1) * (0.25 + 0.9 * metal)
    edge_hl = smooth01(0.0025, 0.0, edge) * 0.18 * (0.3 + metal)           # bevel catch-light
    c = col * diff[:, None] + (spec + edge_hl)[:, None] * (0.6 * col + 0.4)
    img[ty, tx] = np.clip(c, 0, 1)
    used[ty, tx] = True

# ---------------------------------------------------------------- chain strip (4 frames)
PER = 16
for k in range(CH_FRAMES):
    for row in range(CH_ROWS):
        for u in range(TW):
            p = (u + k * PER // CH_FRAMES) % PER
            y = k * CH_ROWS + row
            c = None
            # link plates (rows 0..6): dark side straps, bright rivets at p = 1 and p = 9
            if row <= 6:
                plate = C(64, 64, 70) if (p % 8) not in (0, 7) else C(18, 18, 22)
                if row in (0, 6):
                    plate = plate * 0.6
                if row == 1:
                    plate = plate * 1.5
                if row in (2, 3, 4) and p in (1, 9):
                    plate = C(225, 225, 220) if row == 2 else C(170, 170, 168)
                c = plate
            # cutters (rows 6..11): chrome hook rising towards the cutting corner, then a sharp drop
            prof = 6 + (p - 2) * 5 // 7 if 2 <= p <= 9 else (6 if p in (0, 1) else -1)
            if row >= 6 and prof >= 6:
                if prof >= row:
                    c = C(176, 178, 186) if prof > row else C(250, 250, 240)
                    if p == 9 and row < prof:
                        c = C(90, 92, 98)
                    if p in (0, 1):
                        c = C(120, 120, 126)
                elif row >= 7:
                    c = None
            elif row >= 7:
                c = None
            if c is None:
                alpha[y, u] = False
                continue
            img[y, u] = np.clip(c * (0.9 + 0.1 * ((u * 7 + row * 3) % 5) / 4), 0, 1)
            used[y, u] = True

# ---------------------------------------------------------------- PS1 colour: 15-bit + dither -> CLUT
DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float)
keep = used & alpha
c8 = img * 255
src = Image.fromarray(np.clip(c8, 0, 255).astype(np.uint8)[keep][None])
pal_img = src.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
pal = np.array(pal_img.getpalette()[:255 * 3], float).reshape(-1, 3)
pal = np.clip(np.round(pal / 8), 0, 31)
pal8 = pal * 8 + pal // 4
yy_, xx_ = np.mgrid[0:TH, 0:TW]
target = c8 + (DM[yy_ % 4, xx_ % 4] * 2.2)[..., None]
flat = target[keep]
idx = np.zeros((TH, TW), np.uint8)
best = np.zeros(len(flat), int)
for s0 in range(0, len(flat), 8192):
    d2 = ((flat[s0:s0 + 8192, None, :] - pal8[None]) ** 2).sum(2)
    best[s0:s0 + 8192] = d2.argmin(1)
idx[keep] = best.astype(np.uint8) + 1
clut = np.zeros((256, 3), int); clut[1:256] = pal[:255].astype(int)
words = []
for i, (r, g, b) in enumerate(clut):
    wv = int(r) | (int(g) << 5) | (int(b) << 10)
    if i > 0 and wv == 0:
        wv = 0x8000
    words.append(wv)
with open(os.path.join(ROOT, 'assets', 'chainsaw.tim'), 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09))
    fh.write(struct.pack('<IHHHH', 12 + 512, 0, CLUT_Y, 256, 1))
    fh.write(struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TW * TH, VRAM_X, VRAM_Y, TW // 2, TH))
    fh.write(idx.tobytes())
rgb = np.array([[(w & 31), (w >> 5) & 31, (w >> 10) & 31] for w in words])
rgb8 = (rgb * 8 + rgb // 4).astype(np.uint8)
tex_rgba = np.dstack([rgb8[idx], np.where(idx > 0, 255, 0).astype(np.uint8)])
Image.fromarray(tex_rgba, 'RGBA').save(os.path.join(OUT, 'chainsaw_texture.png'))
os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
prev = Image.new('RGBA', (TW, TH), (40, 0, 40, 255)); prev.alpha_composite(Image.fromarray(tex_rgba, 'RGBA'))
prev.convert('RGB').resize((TW * 3, TH * 3), Image.NEAREST).save(os.path.join(ROOT, 'docs', 'chainsaw_texture.png'))

# ---------------------------------------------------------------- exports
VOFS = VRAM_Y   # texture page v of image row 0


def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


verts, vkey = [], {}


def vid(p):
    k = fx(p)
    if k not in vkey:
        vkey[k] = len(verts); verts.append(k)
    return vkey[k]


body_prims = []
for f in FACES:
    v = [vid(p) for p in f['p']]; uv = [(u, t + VOFS) for u, t in f['uv']]
    if len(v) == 4:
        v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
    body_prims.append((len(f['p']), v, uv))
nbody_verts = len(verts)
chain_v0 = len(verts)
chain_prims = []
cverts, ckey = [], {}
for c in CHAIN:
    v = []
    for p in c['p']:
        k = fx(p)
        if k not in ckey:
            ckey[k] = len(cverts); cverts.append(k)
        v.append(chain_v0 + ckey[k])
    uv = [(u, t + VOFS) for u, t in c['uv']]
    v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
    chain_prims.append((4, v, uv))
verts += cverts
assert all(0 <= t < 256 and 0 <= u < 256 for _, _, uv in body_prims + chain_prims for u, t in uv)
assert len(verts) <= 512

with open(os.path.join(ROOT, 'assets', 'chainsaw_mesh.h'), 'w') as fh:
    fh.write('/* PS1 arm chainsaw (tools/build_chainsaw.py): vertices local to the right-hand mount\n'
             '   (DEMON_MOUNT_*), same axes as the demon model. Bone 0 = body, bones 1..4 = the chain\n'
             '   with the 4 animation frames (same vertices, UVs one strip frame apart): draw one of them. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\n'
             'typedef struct { short x, y, z, pad; } DVERT;\n'
             'typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n'
             'typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n#endif\n')
    nprim = len(body_prims) + CH_FRAMES * len(chain_prims)
    fh.write(f'#define SAW_NVERTS {len(verts)}\n#define SAW_NPRIMS {nprim}\n#define SAW_NBONES {1 + CH_FRAMES}\n#define SAW_CHAIN_FRAMES {CH_FRAMES}\n')
    fh.write('static const DVERT saw_verts[] = {\n' + ''.join(f'  {{{x},{y},{z},0}},\n' for x, y, z in verts) + '};\n')
    fh.write('static const DPRIM saw_prims[] = {\n')
    for n, v, uv in body_prims:
        v = v + [0] * (4 - n); uv = uv + [(0, 0)] * (4 - n)
        fh.write(f"  {{{n},{{{','.join(map(str, v))}}},{{{','.join(str(u) for u, _ in uv)}}},{{{','.join(str(t) for _, t in uv)}}}}},\n")
    for k in range(CH_FRAMES):
        for n, v, uv in chain_prims:
            fh.write(f"  {{4,{{{','.join(map(str, v))}}},{{{','.join(str(u) for u, _ in uv)}}},{{{','.join(str(t + k * CH_ROWS) for _, t in uv)}}}}},\n")
    fh.write('};\nstatic const DBONE saw_bones[] = {\n')
    fh.write(f'  {{"body", -1, 0, 0, 0, 0, {nbody_verts}, 0, {len(body_prims)}}},\n')
    for k in range(CH_FRAMES):
        fh.write(f'  {{"chain{k}", -1, 0, 0, 0, {chain_v0}, {len(cverts)}, {len(body_prims) + k * len(chain_prims)}, {len(chain_prims)}}},\n')
    fh.write('};\n')

# viewer: flat triangle lists in model metres (Y up), uv in texture pixels, chain flag
pos, uvs, chf = [], [], []
for fi_, f in enumerate(FACES + CHAIN):
    is_chain = fi_ >= len(FACES)
    tri = [(0, 1, 2)] if len(f['p']) == 3 else [(0, 1, 2), (0, 2, 3)]
    for t in tri:
        for k in t:
            pos += list(map(float, np.round(f['p'][k], 5))); uvs += [f['uv'][k][0] + 0.5, f['uv'][k][1] + 0.5]
            chf.append(1 if is_chain else 0)
json.dump(dict(pos=pos, uv=uvs, chain=chf, tris=len(chf) // 3, faces=len(FACES), chain_faces=len(CHAIN),
               tw=TW, th=TH, rows=CH_ROWS, frames=CH_FRAMES),
          open(os.path.join(OUT, 'chainsaw_viewer.json'), 'w'))
print(f'{len(FACES)} body faces, {len(CHAIN)} chain faces x {CH_FRAMES} frames, {len(verts)} verts')

# self-contained 3D viewer page (WebGL, PS1 look: 240p, vertex snapping, affine UVs)
import base64
tpl = open(os.path.join(ROOT, 'tools', 'chainsaw_viewer_template.html')).read()
b64 = base64.b64encode(open(os.path.join(OUT, 'chainsaw_texture.png'), 'rb').read()).decode()
page = tpl.replace('__MESH__', open(os.path.join(OUT, 'chainsaw_viewer.json')).read()).replace('__TEX__', 'data:image/png;base64,' + b64)
open(os.path.join(OUT, 'chainsaw_viewer.html'), 'w').write(page)
print('viewer:', os.path.join(OUT, 'chainsaw_viewer.html'), len(page), 'bytes')
