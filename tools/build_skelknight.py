"""PS1-style skeleton knight: bare bones in blackened, rusting plate armour, box-modelled on Deadbird's
skeleton (same bones, so the same animations drive him), baked/dithered 8bpp CLUT texture, TIM export,
a rigged GLB and a self-contained 3D viewer page. Same pipeline as tools/build_skelknight.py."""
import numpy as np, json, struct, math, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tools', 'out', 'skelknight')
os.makedirs(OUT, exist_ok=True)
TEX = 256

V, VT, F = [], [], []          # positions, per-vertex param t, faces
MATS = ['bone', 'rib', 'steel', 'mail', 'cloth', 'leather', 'horn']


def nrm(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-12 else v


def addv(p, t=0.0):
    V.append(np.asarray(p, float)); VT.append(t); return len(V) - 1


def face(idx, mat, inside_pt=None, outward=None, smooth=0.6):
    """Add a face, flipping winding so its normal points away from inside_pt / along outward."""
    P = [V[i] for i in idx]
    n = np.cross(P[1] - P[0], P[2] - P[0])
    if len(P) == 4:
        n = n + np.cross(P[2] - P[0], P[3] - P[0])
    c = sum(P) / len(P)
    ref = outward if outward is not None else (c - inside_pt)
    if np.dot(n, ref) < 0:
        idx = idx[::-1]
    F.append(dict(v=list(idx), mat=mat, smooth=smooth, part=PART[0] if 'PART' in globals() else 'x', bone=BONE[0] if 'BONE' in globals() else 'x'))


def loft(pts, radii, n, mat, ref=(0, 0, 1), rot=0.0, cap0=True, cap1=True, smooth=0.6, tparam=True, angles=None):
    if angles is not None:
        n = len(angles)
    pts = [np.asarray(p, float) for p in pts]
    m = len(pts)
    rings, vprev = [], None
    for i, c in enumerate(pts):
        t = nrm(pts[min(i + 1, m - 1)] - pts[max(i - 1, 0)])
        base = np.asarray(ref, float) if vprev is None else vprev
        v = nrm(base - np.dot(base, t) * t)
        u = np.cross(t, v)
        vprev = v
        r = radii[i]
        rx, ry = (r, r) if np.isscalar(r) else r
        tp = i / (m - 1) if tparam else 0.0
        if rx == 0 and ry == 0:
            rings.append([addv(c, tp)])
            continue
        ring = []
        for k in range(n):
            a = rot + (angles[k] if angles is not None else 2 * math.pi * k / n)
            ring.append(addv(c + rx * math.cos(a) * u + ry * math.sin(a) * v, tp))
        rings.append(ring)
    for i in range(m - 1):
        A, B = rings[i], rings[i + 1]
        mid = (pts[i] + pts[i + 1]) / 2
        for k in range(n):
            k1 = (k + 1) % n
            if len(B) == 1:
                face([A[k], A[k1], B[0]], mat, mid, smooth=smooth)
            elif len(A) == 1:
                face([A[0], B[k], B[k1]], mat, mid, smooth=smooth)
            else:
                face([A[k], A[k1], B[k1], B[k]], mat, mid, smooth=smooth)
    for cap, ring, d in ((cap0, rings[0], pts[0] - pts[1]), (cap1, rings[-1], pts[-1] - pts[-2])):
        if not cap or len(ring) == 1:
            continue
        if cap == 'dome':
            # rounded joint cap: centre vertex pushed outward, fanned to every ring corner
            cpos = sum(V[i] for i in ring) / len(ring)
            rad_ = np.mean([np.linalg.norm(V[i] - cpos) for i in ring])
            ctr = addv(cpos + nrm(d) * rad_ * DOME, VT[ring[0]])
            for k in range(len(ring)):
                face([ring[k], ring[(k + 1) % len(ring)], ctr], mat, outward=d, smooth=.75)
        elif len(ring) == 4:
            face(ring, mat, outward=d, smooth=0)
        else:
            for k in range(1, len(ring) - 1):
                face([ring[0], ring[k], ring[k + 1]], mat, outward=d, smooth=0)


DOME = .45

# ---------------------------------------------------------------- MODEL (Y up, faces +Z, metres)
# A skeleton knight on the same skeleton as Deadbird (same bone names and joint layout, so the same
# animations drive him): bare bones - skull, ribcage, spine, arm and leg bones - inside a set of
# blackened, rusting plate armour that only covers the front of him, so the bones show between and
# behind the plates. +X = his left.
S2 = math.sqrt(2)
PART = ['torso']
BONE = ['pelvis']


def band(y0, y1, r0, r1, n, mat, z0=0.0, z1=0.0, rot=0.0, cap0=False, cap1=False, smooth=.5):
    """a ring of plates around the vertical axis (open unless capped)"""
    loft([(0, y0, z0), (0, y1, z1)], [r0, r1], n, mat, ref=(0, 0, 1), rot=rot, cap0=cap0, cap1=cap1, smooth=smooth)


def cube(c, h, mat, smooth=0.0):
    """axis-aligned box: centre c, half sizes h"""
    c = np.asarray(c, float)
    ids = {}
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                ids[(sx, sy, sz)] = addv(c + (sx * h[0], sy * h[1], sz * h[2]))
    for ax in range(3):
        for sg in (-1, 1):
            o = [k for k in range(3) if k != ax]
            q = []
            for du, dv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                key = [0, 0, 0]; key[ax] = sg; key[o[0]] = du; key[o[1]] = dv
                q.append(ids[tuple(key)])
            face(q, mat, c, smooth=smooth)


def slab(c, h, mat, ends=True):
    """a thin hanging piece: a box without its top and bottom (ends=False: without its sides too)"""
    c = np.asarray(c, float)
    q = lambda *k: [addv(c + np.multiply(h, kk)) for kk in k]
    for ax, sg in ((2, 1), (2, -1), (0, 1), (0, -1)) if ends else ((2, 1), (2, -1), (1, 1), (1, -1)):
        o = [k for k in range(3) if k != ax and (k != 1 or not ends)]
        o = o if len(o) == 2 else [k for k in range(3) if k != ax]
        pts = []
        for du, dv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            key = [0, 0, 0]; key[ax] = sg; key[o[0]] = du; key[o[1]] = dv
            pts.append(key)
        face(q(*pts), mat, c)


def shell(cs, rads, a0, a1, n, mat, smooth=.6):
    """a part-ring shell (a plate that covers only an arc): rings round vertical centres cs, radii
    (rx, rz), from angle a0 to a1 (0 = +X, pi/2 = front +Z); faces point away from the axis"""
    rings = []
    for c, (rx, rz) in zip(cs, rads):
        c = np.asarray(c, float)
        rings.append([addv(c + (rx * math.cos(a), 0, rz * math.sin(a))) for a in np.linspace(a0, a1, n + 1)])
    for i in range(len(rings) - 1):
        ax = (np.asarray(cs[i], float) + np.asarray(cs[i + 1], float)) / 2
        for k in range(n):
            q = [rings[i][k], rings[i][k + 1], rings[i + 1][k + 1], rings[i + 1][k]]
            mid = sum(V[j] for j in q) / 4
            face(q, mat, outward=(mid - ax) * (1, 0, 1), smooth=smooth)


# pelvis: the hip bone under a chainmail skirt, a belt, a torn tabard front and back
band(1.05, .79, (.165, .125), (.205, .16), 6, 'mail', rot=math.pi / 6)
band(1.015, 1.075, (.172, .132), (.172, .132), 6, 'leather', rot=math.pi / 6)
slab((0, .82, .158), (.088, .2, .008), 'cloth')
slab((0, .84, -.15), (.092, .19, .008), 'cloth')

# torso: ribcage and spine; a breastplate over the front only, a gorget at the neck
BONE[0] = 'torso'
loft([(0, 1.13, 0), (0, 1.3, .005), (0, 1.47, -.01)], [(.11, .08), (.155, .11), (.115, .085)],
     6, 'rib', ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=False)     # (the spine is painted on its back)
shell([(0, 1.16, .0), (0, 1.3, .005), (0, 1.445, -.008)], [(.152, .118), (.19, .142), (.168, .12)],
      math.radians(-12), math.radians(192), 6, 'steel')
band(1.43, 1.53, (.125, .1), (.092, .08), 6, 'steel', rot=math.pi / 6)

# neck: vertebrae
PART[0] = 'neck'
BONE[0] = 'neck'
loft([(0, 1.49, -.02), (0, 1.64, -.02)], [.026, .026], 4, 'bone', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=False, smooth=0)

# head: skull (cranium, cheekbones, jaw), an open-faced horned helmet, a mail neck guard
PART[0] = 'head'
BONE[0] = 'head'
HC = np.array((0, 1.705, .012))
loft([(0, 1.605, .02), (0, 1.69, .02), (0, 1.80, -.002)],
     [(.07, .08), (.098, .108), (.082, .092)], 8, 'bone', ref=(0, 0, 1), rot=math.pi / 8,
     cap0=True, cap1='dome', smooth=.7)
slab((0, 1.588, .05), (.056, .02, .048), 'bone', ends=False)            # jaw
loft([(0, 1.742, .004), (0, 1.83, -.006)], [(.122, .132), (.108, .118)],
     6, 'steel', ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1='dome', smooth=.7)
shell([(0, 1.75, -.004), (0, 1.62, -.03)], [(.124, .134), (.13, .13)], math.radians(200), math.radians(340), 3, 'mail')
for s in (1, -1):                                                           # horns
    loft([(s * .1, 1.80, -.01), (s * .19, 1.86, -.06), (s * .22, 1.97, -.15)], [.028, .019, 0], 3, 'horn',
         ref=(0, 0, 1), rot=math.pi / 4, cap0=False)

# arms (T-pose along X): humerus under a layered pauldron; forearm bones with a vambrace; gauntlet
ARM_L = [(.205, 1.475, 0), (.48, 1.475, -.01), (.75, 1.475, 0)]
ARM_R = [(-x, y, z) for x, y, z in ARM_L]
FISTS = []
for A in (ARM_L, ARM_R):
    sh, el, wr = map(np.array, A)
    s = np.sign(sh[0])
    sd = 'L' if s > 0 else 'R'
    PART[0] = 'arm'
    BONE[0] = 'upper_arm_' + sd
    loft([sh, el], [.03, .03], 4, 'bone', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=True)
    loft([sh + (s * -.04, .035, 0), sh + (s * .14, .03, 0)],
         [(.11, .118), (.104, .112)], 6, 'steel', ref=(0, 1, 0), rot=math.pi / 6, cap0=False, cap1='dome')
    BONE[0] = 'forearm_' + sd
    loft([el, wr], [(.02, .036), (.016, .028)], 4, 'bone', ref=(0, 1, 0), rot=math.pi / 4, cap0='dome', cap1=False)   # radius + ulna
    loft([el + (s * .085, 0, 0), wr - (s * .012, 0, 0)], [(.052, .054), (.046, .047)], 6, 'steel', ref=(0, 1, 0),
         rot=math.pi / 6, cap0=False, cap1=False)
    PART[0] = 'hand'
    BONE[0] = 'fist_' + sd
    FIST = (wr - (s * .01, 0, 0), wr + (s * .085, 0, 0))
    FISTS.append(FIST)
    loft(list(FIST), [(.048, .042), (.05, .044)], 4, 'steel', ref=(0, 0, 1), rot=math.pi / 4, smooth=0, cap0=False, cap1='dome')
MOUNT_POS = (FISTS[1][0] + FISTS[1][1]) / 2
MOUNT_DIR = nrm(FISTS[1][1] - FISTS[1][0])

# legs: femur with a front thigh plate, knee cop; shin bone with a front greave; armoured sabaton
PART[0] = 'leg'
for s in (1, -1):
    sd = 'L' if s > 0 else 'R'
    BONE[0] = 'thigh_' + sd
    loft([(s * .09, .96, 0), (s * .135, .56, .03)], [.034, .029], 4, 'bone', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=False)
    shell([(s * .1, .92, .012), (s * .118, .78, .02), (s * .13, .64, .028)], [(.074, .072), (.07, .07), (.06, .06)],
          math.radians(-20), math.radians(200), 3, 'steel')
    BONE[0] = 'calf_' + sd
    loft([(s * .135, .55, .035), (s * .15, .17, .015)], [.03, .022], 4, 'bone', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=False)
    shell([(s * .136, .6, .05), (s * .141, .42, .03), (s * .148, .21, .02)], [(.06, .066), (.062, .066), (.05, .05)],
          math.radians(-25), math.radians(205), 3, 'steel')                 # greave up over the knee
    BONE[0] = 'hoof_' + sd
    loft([(s * .15, .19, .015), (s * .152, .07, .04), (s * .152, 0.0, .07)], [(.052, .058), (.066, .11), (.068, .126)],
         4, 'steel', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=True)
P = np.array(V); TP = np.array(VT)

# ---------------------------------------------------------------- skeleton (rigid parts, pivots at joints)
# name, parent, pivot in model space (Y up, faces +Z; L = character's left = +X)
SKEL = [('pelvis', None, (0, .97, 0)), ('torso', 'pelvis', (0, 1.12, .012)), ('neck', 'torso', (0, 1.5, -.005)),
        ('head', 'neck', (0, 1.62, .005))]
for sd, sg in (('L', 1), ('R', -1)):
    A = ARM_L if sd == 'L' else ARM_R
    SKEL += [('upper_arm_' + sd, 'torso', A[0]), ('forearm_' + sd, 'upper_arm_' + sd, A[1]), ('fist_' + sd, 'forearm_' + sd, A[2])]
for sd, sg in (('L', 1), ('R', -1)):
    SKEL += [('thigh_' + sd, 'pelvis', (sg * .085, .97, 0)), ('calf_' + sd, 'thigh_' + sd, (sg * .135, .54, .035)),
             ('hoof_' + sd, 'calf_' + sd, (sg * .15, .16, .02))]
BNAMES = [b[0] for b in SKEL]
BIDX = {n: i for i, n in enumerate(BNAMES)}
BPIV = np.array([b[2] for b in SKEL], float)
BPAR = [BIDX[b[1]] if b[1] else -1 for b in SKEL]
VBONE = np.full(len(V), -1)
for f in F:
    for vi in f['v']:
        assert VBONE[vi] in (-1, BIDX[f['bone']]), ('vertex shared across bones', f['bone'])
        VBONE[vi] = BIDX[f['bone']]
assert (VBONE >= 0).all()
print('bones', len(SKEL), {n: int((VBONE == i).sum()) for i, n in enumerate(BNAMES)})
ntri = sum(len(f['v']) - 2 for f in F)
print('verts', len(P), 'faces', len(F), 'triangles', ntri)
assert ntri <= 500, ntri

# smooth vertex normals (per loft, since lofts don't share vertices)
VN = np.zeros_like(P)
for f in F:
    q = P[f['v']]
    n = np.cross(q[1] - q[0], q[2] - q[0])
    if len(q) == 4:
        n += np.cross(q[2] - q[0], q[3] - q[0])
    f['n'] = nrm(n)
    VN[f['v']] += n
VN = np.array([nrm(n) for n in VN])

# ---------------------------------------------------------------- UV packing (one island per face)
for f in F:
    q = P[f['v']]
    if len(q) == 4:
        f['w'] = (np.linalg.norm(q[1] - q[0]) + np.linalg.norm(q[2] - q[3])) / 2
        f['h'] = (np.linalg.norm(q[3] - q[0]) + np.linalg.norm(q[2] - q[1])) / 2
    else:
        f['w'] = np.linalg.norm(q[1] - q[0]); f['h'] = np.linalg.norm(q[2] - q[0])
    # the skull's face gets the most texels
    f['boost'] = {'torso': 1.35, 'neck': 1.0, 'head': 2.6, 'arm': 1.15, 'hand': 1.2, 'leg': 1.0}.get(f['part'], 1.0)


def pack(k):
    sz = {i: (int(np.clip(round(F[i]['w'] * k * F[i]['boost']), 4, 120)),
              int(np.clip(round(F[i]['h'] * k * F[i]['boost']), 4, 120))) for i in range(len(F))}
    order = sorted(range(len(F)), key=lambda i: -(sz[i][1] * 1000 + sz[i][0]))
    sky = np.zeros(TEX, int)
    rects = {}
    for i in order:
        w, h = sz[i]
        best = None
        for x in range(0, TEX - w + 1):
            y = sky[x:x + w + 1].max() if x + w < TEX else sky[x:x + w].max()
            if y + h <= TEX and (best is None or y < best[1]):
                best = (x, y)
        if best is None:
            return None
        x, y = int(best[0]), int(best[1])
        rects[i] = (x, y, w, h)
        sky[x:min(TEX, x + w + 1)] = y + h + 1
    return rects


lo, hi = 10, 2000
while hi - lo > 1:
    mid = (lo + hi) // 2
    lo, hi = (mid, hi) if pack(mid) else (lo, mid)
rects = pack(lo)
print('texel density', lo, 'px/unit')

# per-face UVs in PS1 integer texel coordinates
for i, f in enumerate(F):
    x, y, w, h = rects[i]
    x1, y1 = x + w - 1, y + h - 1
    f['rect'] = (x, y, w, h)
    f['uv'] = [(x, y), (x1, y), (x1, y1), (x, y1)] if len(f['v']) == 4 else [(x, y), (x1, y), (x, y1)]

# ---------------------------------------------------------------- gather texel samples
pix, pos, sn, fn, tp, mat, la, lb, fid, part = [], [], [], [], [], [], [], [], [], []
for i, f in enumerate(F):
    x, y, w, h = f['rect']
    gy, gx = np.mgrid[0:h, 0:w]
    s = gx.ravel() / max(w - 1, 1); t = gy.ravel() / max(h - 1, 1)
    ids = f['v']
    if len(ids) == 4:
        up = s >= t
        W = np.zeros((len(s), 4))
        W[up, 0] = 1 - s[up]; W[up, 1] = s[up] - t[up]; W[up, 2] = t[up]
        lo_ = ~up
        W[lo_, 0] = 1 - t[lo_]; W[lo_, 2] = s[lo_]; W[lo_, 3] = t[lo_] - s[lo_]
    else:
        W = np.stack([1 - s - t, s, t], 1)
        W = np.clip(W, 0, None); W /= W.sum(1, keepdims=True)
    pp = W @ P[ids]
    vn = W @ VN[ids]
    vn /= np.linalg.norm(vn, axis=1, keepdims=True) + 1e-12
    mixn = f['smooth'] * vn + (1 - f['smooth']) * f['n']
    mixn /= np.linalg.norm(mixn, axis=1, keepdims=True)
    pix.append(np.stack([gx.ravel() + x, gy.ravel() + y], 1)); pos.append(pp); sn.append(mixn)
    fn.append(np.repeat(f['n'][None], len(s), 0)); tp.append(W @ TP[ids])
    mat.append(np.full(len(s), MATS.index(f['mat']))); la.append(s * f['w']); lb.append(t * f['h'])
    fid.append(np.full(len(s), i)); part.append(np.full(len(s), f['part'], dtype=object))
pix, pos, sn, fn, tp, mat, la, lb, fid, part = map(np.concatenate, (pix, pos, sn, fn, tp, mat, la, lb, fid, part))
N = len(pos)
print('texels', N)

# ---------------------------------------------------------------- ray casting (shadows + AO)
tris = []
for f in F:
    v = f['v']
    tris.append(P[[v[0], v[1], v[2]]])
    if len(v) == 4:
        tris.append(P[[v[0], v[2], v[3]]])
tris = np.array(tris)


def trace(O, D, maxt=1e9):
    best = np.full(len(O), np.inf)
    for v0, v1, v2 in tris:
        e1, e2 = v1 - v0, v2 - v0
        p = np.cross(D, e2)
        det = p @ e1
        ok = np.abs(det) > 1e-10
        inv = np.where(ok, 1 / np.where(ok, det, 1), 0)
        sv = O - v0
        u = np.einsum('ij,ij->i', sv, p) * inv
        q = np.cross(sv, e1)
        vv = np.einsum('ij,ij->i', D, q) * inv
        tt = (q @ e2) * inv
        hit = ok & (u >= 0) & (vv >= 0) & (u + vv <= 1) & (tt > 1e-4) & (tt < maxt)
        best = np.where(hit & (tt < best), tt, best)
    return best


L = nrm(np.array((.55, .85, .65)))
O = pos + fn * 0.006 + sn * 0.004
shadow = (trace(O, np.repeat(L[None], N, 0)) == np.inf).astype(float)
# soften: average with neighbour samples a little later via blur on lit term
dirs = []
rng = np.random.default_rng(3)
ao = np.zeros(N)
NAO = 10
for k in range(NAO):
    d = rng.normal(size=(N, 3))
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    d = np.where((d * sn).sum(1, keepdims=True) < 0, -d, d)
    d = nrm_d = d + sn * 0.6
    d /= np.linalg.norm(d, axis=1, keepdims=True)
    h = trace(O, d, 0.22)
    ao += np.where(h < 0.22, h / 0.22 * 0.5, 1.0)
ao /= NAO
print('raytrace done')

# ---------------------------------------------------------------- procedural albedo (skeleton knight)
def hsh(*a):
    x = np.zeros_like(a[0], dtype=float)
    for i, v in enumerate(a):
        x = x + v * (12.9898 + 31.7 * i)
    return np.modf(np.abs(np.sin(x) * 43758.5453))[0]


def vnoise(p, f, seed=0.0):
    """smooth value noise on world positions (grime, rust, wear)"""
    q = p * f
    i = np.floor(q); t = q - i; t = t * t * (3 - 2 * t)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (t[:, 0] if dx else 1 - t[:, 0]) * (t[:, 1] if dy else 1 - t[:, 1]) * (t[:, 2] if dz else 1 - t[:, 2])
                out = out + w * hsh(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz + seed)
    return out


def C(*c):
    return np.array(c, float) / 255


M = mat
alb = np.zeros((N, 3)); emis = np.zeros((N, 3)); spec = np.zeros(N); gloss = np.full(N, 16.0)
noise = hsh(np.floor(pos[:, 0] * 180), np.floor(pos[:, 1] * 180), np.floor(pos[:, 2] * 180)) - .5
ln = sn.copy()
X, Y, Z = pos[:, 0], pos[:, 1], pos[:, 2]
AX = np.abs(X)
fw = np.array([F[i]['w'] for i in fid]); fh = np.array([F[i]['h'] for i in fid])
edge = np.minimum(np.minimum(la, fw - la), np.minimum(lb, fh - lb))         # distance to the face's border
grime = vnoise(pos, 9.0)
grime2 = vnoise(pos, 31.0, 5.0)
scuff = vnoise(pos, 70.0, 9.0)
crack = vnoise(pos, 140.0, 3.0)


def is_(name):
    return M == MATS.index(name)


BONEC = C(212, 196, 162)          # old ivory
BONE_D = C(120, 96, 70)
STEELC = C(64, 66, 74)            # blackened steel
STEEL_HI = C(168, 168, 172)
RUST = C(122, 58, 26)
BRASS = C(176, 132, 58)
CRIMSON = C(112, 18, 16)
EMBER = C(255, 96, 24)

# ---- bone: ivory, stained in the hollows, hairline cracks, darker at the joints
m = is_('bone') | is_('rib')
c = BONEC[None] * (0.82 + 0.26 * grime[m])[:, None] * (1 + .07 * noise[m])[:, None]
c = c * (1 - .35 * np.clip(grime2[m] - .5, 0, 1) * 2)[:, None] + BONE_D[None] * (.25 * np.clip(grime2[m] - .5, 0, 1) * 2)[:, None]
cr = np.abs(crack[m] - .5) < .018
c[cr] = BONE_D * .55
pm, Xm, Ym, Zm = part[m], X[m], Y[m], Z[m]
# the ribcage: curved ribs with dark gaps, a sternum down the front, the spine's shadow behind
rm = M[m] == MATS.index('rib')
ang = np.arctan2(Zm, Xm)
ribv = np.modf((Ym + .03 * np.cos(ang * 2) - 1.13) / .042)[0]
gap = rm & (ribv > .62) & ~((np.abs(Xm) < .022) & (Zm > 0))
c[gap] = C(22, 10, 8)
c[rm & (np.abs(Xm) < .022) & (Zm > 0)] *= .92                               # sternum
c[rm & (ribv > .5) & (ribv <= .62)] *= .7                                   # under-rib shade
spn = rm & (Zm < -.04) & (np.abs(Xm) < .02)                                 # the spine down the back
c[spn] = np.where((np.modf(Ym[spn] / .028)[0] < .25)[:, None], C(40, 22, 14)[None], (BONEC * 1.05)[None])
# the skull's face: sockets with ember eyes, the nose hole, teeth, cheekbones, sutures
hm = (pm == 'head')
fr = hm & (Zm > .05)
for sx in (1, -1):
    d = np.hypot((Xm - sx * .037) / .026, (Ym - 1.705) / .021)
    sock = fr & (d < 1)
    c[sock] = C(16, 6, 4) * (.5 + .5 * d[sock, None])
    eye = fr & (d < .42)
    emis[np.flatnonzero(m)[eye]] = EMBER * (1.15 - d[eye, None] * 1.2) + C(255, 220, 140) * np.clip(.25 - d[eye], 0, 1)[:, None] * 3
    c[fr & (d >= 1) & (d < 1.25)] *= .72                                     # brow and cheek rims
nose = fr & (Ym < 1.68) & (Ym > 1.64) & (np.abs(Xm) < (1.68 - Ym) * .55)
c[nose] = C(18, 8, 6)
teeth = hm & (Zm > .06) & (np.abs(Ym - 1.609) < .011) & (np.abs(Xm) < .045)
tooth = np.modf((Xm + .1) / .0105)[0]
c[teeth] = np.where((tooth[teeth] < .22)[:, None], C(30, 16, 10)[None], (BONEC * 1.08)[None])
c[hm & (Zm > .06) & (np.abs(Ym - 1.598) < .002) & (np.abs(Xm) < .05)] = C(26, 12, 8)   # the bite line
c[hm & (Zm > .02) & (np.abs(Ym - 1.672) < .005) & (np.abs(Xm) > .055)] *= .75      # cheekbones
c[hm & (Zm < .0) & (np.abs(Xm - .012 * np.sin(Ym * 260)) < .002) & (Ym > 1.72)] = BONE_D * .6   # suture
alb[m] = np.clip(c, 0, 1); spec[m] = .3; gloss[m] = 14

# ---- blackened steel: hammered, rusting in patches, bright worn edges, brass trim, rivets
m = is_('steel')
c = STEELC[None] * (0.85 + 0.3 * grime[m])[:, None] * (1 + .1 * noise[m])[:, None]
rust = np.clip((grime2[m] - .63) * 3.0, 0, 1) * (.5 + .5 * scuff[m])          # in patches
c = c * (1 - rust)[:, None] + RUST[None] * (rust * (.75 + .5 * noise[m] + .5))[:, None]
pm, Xm, Ym, Zm = part[m], X[m], Y[m], Z[m]
wear = (edge[m] < .005) & (scuff[m] > .6)
c[wear] = STEEL_HI * (.75 + .35 * noise[m][wear, None])
trim = (Zm > -1) & ((pm == 'torso') & ((np.abs(Ym - 1.163) < .007) | (np.abs(Ym - 1.44) < .007)) | (pm == 'head') & (np.abs(Ym - 1.746) < .007))   # brass rims: breastplate, helmet
c[trim] = BRASS * (.75 + .35 * grime[m][trim, None])
riv = trim & (np.modf((np.arctan2(Zm, Xm) + 4) / .22)[0] < .18)      # rivets along the rims
c[riv] = C(230, 200, 130)
scr = np.abs(np.modf((Xm * 3 + Ym * 7 + Zm * 5) * 9)[0] - .5) < .012
c[scr & (scuff[m] > .6)] *= 1.6                                             # scratches
# the breastplate's sigil: a crimson bird skull's beak (a long downward chevron) and a ridge
ch = (pm == 'torso') & (Zm > .1) & (Ym > 1.2) & (Ym < 1.42)
v_ = (np.abs(Xm) * 1.6 - (Ym - 1.22)) 
c[ch & (np.abs(v_) < .012)] = CRIMSON * 1.2
c[ch & (np.abs(Xm) < .006) & (Ym > 1.3)] = STEEL_HI * .8                   # the central ridge
c[(pm == 'head') & (Zm > .1) & (np.abs(Xm) < .011)] = STEEL_HI * .75        # nasal guard
kn = (pm == 'leg') & (Ym > .5) & (Zm > .06)                                 # knee cop: a ridged boss
c[kn] = STEEL_HI * .55 * (1 + .4 * (np.abs(np.modf(Ym[kn] / .03)[0] - .5) > .35))[:, None]
alb[m] = np.clip(c, 0, 1); spec[m] = .8; gloss[m] = 30

# ---- chainmail: rows of rings, dark gaps, rust at the hem
m = is_('mail')
ru = np.modf(la[m] / .011)[0]; rv = np.modf(lb[m] / .009 + .5 * (np.floor(la[m] / .011) % 2))[0]
ring = np.abs(np.hypot(ru - .5, rv - .5) - .3) < .12
c = np.tile(C(28, 28, 32), (m.sum(), 1))
c[ring] = C(120, 120, 128) * (.7 + .5 * grime[m][ring, None])
c[ring & (rv < .4)] *= 1.3
c = c * (1 - .5 * np.clip(.92 - Y[m], 0, 1)[:, None] * 4 * (grime2[m] > .5)[:, None]) + RUST * (.4 * np.clip(.92 - Y[m], 0, 1) * 4 * (grime2[m] > .5))[:, None]
alb[m] = np.clip(c, 0, 1); spec[m] = .6; gloss[m] = 20

# ---- tabard: worn crimson cloth, a woven texture, a black sigil, a frayed and filthy hem
m = is_('cloth')
wv = .88 + .12 * ((np.abs(np.sin(la[m] * 520)) > .5) ^ (np.abs(np.sin(lb[m] * 520)) > .5))
c = CRIMSON[None] * wv[:, None] * (.8 + .4 * grime[m])[:, None]
hem = Y[m] < .66 + .03 * hsh(np.floor(X[m] / .012), 1.0 * (Z[m] > 0))
c[hem] *= .45
c *= (1 - .4 * np.clip(.8 - Y[m], 0, 1) * 4)[:, None]
sig = (np.abs(X[m]) * 1.4 - (.98 - Y[m]) * .5 < .012) & (np.abs(X[m]) * 1.4 - (.98 - Y[m]) * .5 > -.012) & (Y[m] < .98) & (Y[m] > .84)
c[sig] = C(20, 8, 8)
c[edge[m] < .006] *= .6
alb[m] = np.clip(c, 0, 1); spec[m] = .08; gloss[m] = 6

# ---- leather belt with a brass buckle
m = is_('leather')
c = np.tile(C(70, 44, 28), (m.sum(), 1)) * (.8 + .4 * grime[m])[:, None]
c[edge[m] < .005] *= .6
buck = (np.abs(X[m]) < .03) & (Z[m] > .1)
c[buck] = BRASS * (.8 + .3 * noise[m][buck, None])
c[buck & (np.abs(X[m]) < .018) & (np.abs(Y[m] - 1.045) < .012)] = C(40, 26, 16)
alb[m] = np.clip(c, 0, 1); spec[m] = .25; gloss[m] = 10

# ---- horns: dark ridged bone, paler at the tips
m = is_('horn')
rd = np.abs(np.modf(lb[m] / .02)[0] - .5) > .38
c = np.tile(C(58, 48, 42), (m.sum(), 1)) * (.85 + .3 * grime[m])[:, None]
c[rd] *= .6
c = c + (C(150, 136, 110) - C(58, 48, 42))[None] * np.clip((Y[m] - 1.88) / .09, 0, 1)[:, None]
alb[m] = np.clip(c, 0, 1); spec[m] = .4; gloss[m] = 18

for name in MATS:
    assert alb[is_(name)].sum() > 0, name

# ---------------------------------------------------------------- baked lighting
key = np.clip(ln @ L, 0, None)
F2 = nrm(np.array((-.7, .15, -.5)))
fill = np.clip(ln @ F2, 0, None)
G = nrm(np.array((0, -1, .35)))
hell = np.clip(ln @ G, 0, None)
ground = .7 + .3 * np.clip(pos[:, 1] / .5, 0, 1)
B2 = nrm(np.array((-.35, .75, -.7)))
backl = np.clip(ln @ B2, 0, None)
light = (key * shadow)[:, None] * np.array((1.05, .95, .82))[None] * 1.05 \
    + fill[:, None] * np.array((.35, .38, .6))[None] * .55 \
    + hell[:, None] * np.array((.9, .2, .06))[None] * .5 \
    + backl[:, None] * np.array((.8, .7, .62))[None] * .5 \
    + (ao * ground)[:, None] * np.array((.42, .37, .42))[None]
Vd = nrm(np.array((0, .25, 1)))
H = nrm(L + Vd)
spc = (np.clip(ln @ H, 0, None) ** gloss) * spec * shadow
col = alb * light * (.55 + .45 * ao)[:, None] + spc[:, None] * np.array((1, .95, .85))[None] + emis
col = np.clip(col, 0, 1)

img = np.zeros((TEX, TEX, 3))
img[pix[:, 1], pix[:, 0]] = col
used = np.zeros((TEX, TEX), bool); used[pix[:, 1], pix[:, 0]] = True

# ---------------------------------------------------------------- PS1 colour pipeline
# 1) 15-bit + 4x4 PS1 dither matrix, 2) 255-colour CLUT (index 0 reserved = transparent black)
DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float)
c8 = img * 255
src = Image.fromarray(np.clip(c8, 0, 255).astype(np.uint8)[used][None])
pal_img = src.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
pal = np.array(pal_img.getpalette()[:255 * 3], float).reshape(-1, 3)
pal = np.clip(np.round(pal / 8), 0, 31)              # 5-bit per channel
pal8 = pal * 8 + pal // 4
yy_, xx_ = np.mgrid[0:TEX, 0:TEX]
dith = DM[yy_ % 4, xx_ % 4] * 2.2                    # PS1 matrix, scaled to the CLUT spacing
target = c8 + dith[..., None]
flat = target[used]
d2 = ((flat[:, None, :] - pal8[None]) ** 2).sum(2)
idx = np.zeros((TEX, TEX), np.uint8)
idx[used] = d2.argmin(1).astype(np.uint8) + 1        # +1: index 0 = transparent
clut = np.zeros((256, 3), int); clut[1:256] = pal[:255].astype(int)

# 15-bit words: STP bit set on opaque black so it isn't transparent
words = []
for i, (r, g, b) in enumerate(clut):
    w = int(r) | (int(g) << 5) | (int(b) << 10)
    if i > 0 and w == 0:
        w = 0x8000
    words.append(w)

with open(f'{OUT}/skelknight.tim', 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09))                         # ID, 8bpp + CLUT
    fh.write(struct.pack('<IHHHH', 12 + 512, 0, 494, 256, 1))        # CLUT at VRAM (0,494)
    fh.write(struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TEX * TEX, 768, 256, TEX // 2, TEX))  # image at VRAM (768,256)
    fh.write(idx.tobytes())

rgb = np.array([[(w & 31), (w >> 5) & 31, (w >> 10) & 31] for w in words])
rgb8 = (rgb * 8 + rgb // 4).astype(np.uint8)
out_rgb = rgb8[idx]
Image.fromarray(out_rgb).save(f'{OUT}/skelknight_texture.png')
Image.fromarray(out_rgb).resize((1024, 1024), Image.NEAREST).save(f'{OUT}/skelknight_texture_x4.png')

# ---------------------------------------------------------------- mesh exports (rigid parts)
import shutil
FX = 4096 * .25          # PS1 fixed point: 1 model unit = 1024 (4096 = 1.0 GTE unit, model scaled x0.25)
order = sorted(range(len(F)), key=lambda i: BIDX[F[i]['bone']])        # prims grouped by bone
vorder = sorted(range(len(P)), key=lambda i: (VBONE[i], i))            # verts grouped by bone
vremap = {old: new for new, old in enumerate(vorder)}
MOUNT_LOCAL = MOUNT_POS - BPIV[BIDX['fist_R']]

with open(f'{OUT}/skelknight.mtl', 'w') as fh:
    fh.write('newmtl skelknight\nKd 1 1 1\nillum 0\nmap_Kd skelknight_texture.png\n')


def write_obj(path, bones, local):
    with open(path, 'w') as fh:
        fh.write(f'# PS1 skelknight rigid parts - {"local space (origin = part pivot)" if local else "model space"}\nmtllib skelknight.mtl\n')
        vbase = 1; tbase = 1
        for b in bones:
            bi = BIDX[b]
            vs = [i for i in vorder if VBONE[i] == bi]
            loc = {old: k for k, old in enumerate(vs)}
            fs = [F[i] for i in order if F[i]['bone'] == b]
            fh.write(f'o {b}\n')
            for i in vs:
                p = P[i] - (BPIV[bi] if local else 0)
                fh.write(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}\n')
            nt = 0
            for f in fs:
                for u, v in f['uv']:
                    fh.write(f'vt {(u + .5) / TEX:.6f} {1 - (v + .5) / TEX:.6f}\n')
            fh.write('usemtl skelknight\ns off\n')
            for f in fs:
                fh.write('f ' + ' '.join(f'{vbase + loc[vi]}/{tbase + nt + k}' for k, vi in enumerate(f['v'])) + '\n')
                nt += len(f['v'])
            vbase += len(vs); tbase += nt


write_obj(f'{OUT}/skelknight.obj', BNAMES, local=False)
os.makedirs(f'{OUT}/parts', exist_ok=True)
for b in BNAMES:
    write_obj(f'{OUT}/parts/{b}.obj', [b], local=True)
shutil.copy(f'{OUT}/skelknight.mtl', f'{OUT}/parts/skelknight.mtl')

skel = dict(
    units='model units, Y up, character faces +Z, L = character left (+X)',
    bones=[dict(name=n, parent=SKEL[i][1], pivot=np.round(BPIV[i], 4).tolist(),
                offset_from_parent=np.round(BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0), 4).tolist(),
                triangles=sum(len(f['v']) - 2 for f in F if f['bone'] == n))
           for i, n in enumerate(BNAMES)],
    attachments=dict(right_hand_mount=dict(bone='fist_R', position_local=np.round(MOUNT_LOCAL, 4).tolist(),
                                          position_model=np.round(MOUNT_POS, 4).tolist(),
                                          forward=np.round(MOUNT_DIR, 4).tolist())))
json.dump(skel, open(f'{OUT}/skeleton.json', 'w'), indent=2)
json.dump(dict(right_hand_mount=skel['attachments']['right_hand_mount'], note='see skeleton.json'),
          open(f'{OUT}/attachment_points.json', 'w'), indent=2)

# flat triangle lists (viewer + glTF): one vertex per face corner (UVs are per face)
tp_, tu_, tb_ = [], [], []
for f in F:
    v, uv = f['v'], f['uv']
    for tri in ([0, 1, 2], [0, 2, 3]) if len(v) == 4 else ([0, 1, 2],):
        for k in tri:
            tp_.append(P[v[k]]); tu_.append(uv[k]); tb_.append(VBONE[v[k]])
tp_ = np.array(tp_, np.float32); tu_ = np.array(tu_, float); tb_ = np.array(tb_)
json.dump(dict(pos=np.round(tp_, 4).ravel().tolist(), uv=tu_.astype(int).ravel().tolist(), bone=tb_.tolist(),
               bones=[dict(name=n, parent=BPAR[i], pivot=np.round(BPIV[i], 4).tolist()) for i, n in enumerate(BNAMES)],
               tris=ntri, verts=len(P)), open(f'{OUT}/mesh.json', 'w'))

# ---- rigged GLB: one skinned mesh, every vertex weighted 1.0 to its part's joint
def pad4(b, fill=b'\0'):
    return b + fill * ((4 - len(b) % 4) % 4)


nv = len(tp_)
uvf = ((tu_ + .5) / TEX).astype(np.float32)
joints = np.zeros((nv, 4), np.uint8); joints[:, 0] = tb_
weights = np.zeros((nv, 4), np.float32); weights[:, 0] = 1
idxs = np.arange(nv, dtype=np.uint16)
ibm = np.zeros((len(SKEL), 4, 4), np.float32)
for i in range(len(SKEL)):
    m_ = np.eye(4, dtype=np.float32); m_[:3, 3] = -BPIV[i]
    ibm[i] = m_.T                                   # column-major
png = open(f'{OUT}/skelknight_texture.png', 'rb').read()
blobs = [tp_.tobytes(), uvf.tobytes(), joints.tobytes(), weights.tobytes(), idxs.tobytes(), ibm.tobytes(), png]
views, binbuf = [], b''
for k, bl in enumerate(blobs):
    off = len(binbuf)
    binbuf = pad4(binbuf + bl)
    views.append(dict(buffer=0, byteOffset=off, byteLength=len(bl)))
for k, tgt in enumerate([34962, 34962, 34962, 34962, 34963]):
    views[k]['target'] = tgt
nodes = []
for i, n in enumerate(BNAMES):
    t = BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0)
    nodes.append(dict(name=n, translation=[float(x) for x in t],
                      children=[j for j in range(len(SKEL)) if BPAR[j] == i] or None))
for nd in nodes:
    if nd['children'] is None:
        del nd['children']
mesh_node = len(nodes)
nodes.append(dict(name='skelknight_mesh', mesh=0, skin=0))
gltf = dict(
    asset=dict(version='2.0', generator='ps1 skelknight build.py'),
    extensionsUsed=['KHR_materials_unlit'],
    scene=0, scenes=[dict(nodes=[0, mesh_node])],
    nodes=nodes,
    skins=[dict(joints=list(range(len(SKEL))), skeleton=0, inverseBindMatrices=5)],
    meshes=[dict(name='skelknight', primitives=[dict(attributes=dict(POSITION=0, TEXCOORD_0=1, JOINTS_0=2, WEIGHTS_0=3),
                                                 indices=4, material=0)])],
    materials=[dict(name='skelknight', pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=1),
                    extensions=dict(KHR_materials_unlit={}))],
    textures=[dict(source=0, sampler=0)],
    samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)],
    images=[dict(bufferView=6, mimeType='image/png', name='skelknight_texture')],
    accessors=[
        dict(bufferView=0, componentType=5126, count=nv, type='VEC3',
             min=[float(x) for x in tp_.min(0)], max=[float(x) for x in tp_.max(0)]),
        dict(bufferView=1, componentType=5126, count=nv, type='VEC2'),
        dict(bufferView=2, componentType=5121, count=nv, type='VEC4'),
        dict(bufferView=3, componentType=5126, count=nv, type='VEC4'),
        dict(bufferView=4, componentType=5123, count=nv, type='SCALAR'),
        dict(bufferView=5, componentType=5126, count=len(SKEL), type='MAT4')],
    bufferViews=views, buffers=[dict(byteLength=len(binbuf))])
js = pad4(json.dumps(gltf, separators=(',', ':')).encode(), b' ')
with open(f'{OUT}/skelknight_rigged.glb', 'wb') as fh:
    fh.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binbuf)))
    fh.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    fh.write(struct.pack('<II', len(binbuf), 0x004E4942) + binbuf)

# ---- PS1 C header: bone table + per-bone vertex/prim ranges, vertices local to their bone pivot
def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


with open(f'{OUT}/skelknight_mesh.h', 'w') as fh:
    fh.write(f'/* PS1 skelknight, rigid hierarchical parts: {len(P)} verts, {len(F)} prims ({ntri} tris), {len(SKEL)} bones.\n'
             f'   Fixed point: 1 model unit = {int(FX)} (4096 = 1.0), Y down, faces +Z.\n'
             f'   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.\n'
             f'   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\n')
    fh.write('typedef struct { short x, y, z, pad; } DVERT;\n')
    fh.write('typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n')
    fh.write('typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n')
    fh.write('#endif\n')
    fh.write(f'#define SKELKNIGHT_NVERTS {len(P)}\n#define SKELKNIGHT_NPRIMS {len(F)}\n#define SKELKNIGHT_NBONES {len(SKEL)}\n')
    for i, n in enumerate(BNAMES):
        fh.write(f'#define SKELKNIGHT_BONE_{n.upper()} {i}\n')
    ml = fx(MOUNT_LOCAL); md = MOUNT_DIR * 4096
    fh.write(f'/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */\n')
    fh.write(f'#define SKELKNIGHT_MOUNT_BONE SKELKNIGHT_BONE_FIST_R\n#define SKELKNIGHT_MOUNT_X {ml[0]}\n#define SKELKNIGHT_MOUNT_Y {ml[1]}\n#define SKELKNIGHT_MOUNT_Z {ml[2]}\n')
    fh.write(f'#define SKELKNIGHT_MOUNT_DX {int(round(md[0]))}\n#define SKELKNIGHT_MOUNT_DY {int(round(-md[1]))}\n#define SKELKNIGHT_MOUNT_DZ {int(round(md[2]))}\n')
    fh.write('#ifndef SKELKNIGHT_MESH_NO_DATA\n')
    fh.write('static const DVERT skelknight_verts[] = {\n')
    for i in vorder:
        x, y, z = fx(P[i] - BPIV[VBONE[i]])
        fh.write(f'  {{{x},{y},{z},0}}, /* {BNAMES[VBONE[i]]} */\n')
    fh.write('};\nstatic const DPRIM skelknight_prims[] = {\n')
    for i in order:
        f = F[i]
        v = [vremap[k] for k in f['v']]; uv = list(f['uv'])
        if len(v) == 4:  # PS1 quad vertex order is Z: 0,1,3,2
            v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
        v += [0] * (4 - len(v)); uv += [(0, 0)] * (4 - len(uv))
        fh.write(f"  {{{len(f['v'])},{{{','.join(map(str, v))}}},{{{','.join(str(a) for a, _ in uv)}}},{{{','.join(str(b) for _, b in uv)}}}}},\n")
    fh.write('};\nstatic const DBONE skelknight_bones[] = {\n')
    for i, n in enumerate(BNAMES):
        vs = [k for k, old in enumerate(vorder) if VBONE[old] == i]
        ps = [k for k, old in enumerate(order) if F[old]['bone'] == n]
        off = BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0)
        ox, oy, oz = fx(off)
        fh.write(f'  {{"{n}", {BPAR[i]}, {ox}, {oy}, {oz}, {vs[0]}, {len(vs)}, {ps[0]}, {len(ps)}}},\n')
    fh.write('};\n#endif /* SKELKNIGHT_MESH_NO_DATA */\n')
print('done')

# ---- self-contained 3D viewer page (WebGL, PS1 look: 240p, vertex snapping, affine UVs, walk cycle)
import base64
tpl = open(os.path.join(ROOT, 'tools', 'viewer_template.html')).read()
b64 = base64.b64encode(open(f'{OUT}/skelknight_texture.png', 'rb').read()).decode()
page = (tpl.replace('<title>PS1 Skull Demon</title>', '<title>PS1 Skeleton Knight</title>')
           .replace('<b>SKULL DEMON</b>', '<b>SKELETON KNIGHT</b>')
           .replace('__MESH__', open(f'{OUT}/mesh.json').read())
           .replace('__TEX__', 'data:image/png;base64,' + b64))
open(f'{OUT}/skelknight_viewer.html', 'w').write(page)
print('viewer:', f'{OUT}/skelknight_viewer.html', len(page), 'bytes')
