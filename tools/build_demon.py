"""PS1-style demon character: box-modelled mesh, baked/dithered 8bpp CLUT texture, TIM export."""
import numpy as np, json, struct, math, os
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out')
os.makedirs(OUT, exist_ok=True)
TEX = 256

V, VT, F = [], [], []          # positions, per-vertex param t, faces
MATS = ['skin', 'feather', 'beak', 'horn', 'chitin', 'hoof', 'metal', 'bar', 'tooth', 'claw', 'armour']


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
# ---------------------------------------------------------------- MODEL (Y up, faces +Z)
S2 = math.sqrt(2)
PART = ['torso']
BONE = ['pelvis']
# pelvis: separate rigid piece (y, rx, rz, z-offset)
PEL = [(0.93, .12, .088, 0), (1.00, .185, .12, -.008), (1.14, .096, .074, .012)]
loft([(0, y, z) for y, _, _, z in PEL], [(rx, rz) for _, rx, rz, _ in PEL], 8, 'skin',
     ref=(0, 0, 1), rot=0, cap1='dome')
# upper torso: separate piece. Uneven rings: 8 edges across the front (bust detail),
# only 2 across the back (back muscles are painted). Angles from +X toward +Z (front).
TA = [math.radians(d) for d in (0, 30, 55, 72, 90, 108, 125, 150, 180, 270)]
T = [(1.295, .125, .085, .014), (1.365, .155, .10, .008), (1.435, .162, .098, .004), (1.50, .18, .086, -.005)]
BONE[0] = 'torso'
T0 = len(V)
loft([(0, y, z) for y, _, _, z in T], [(rx, rz) for _, rx, rz, _ in T], 10, 'skin',
     ref=(0, 0, 1), rot=0, cap0=False, cap1='dome', angles=TA)
NV = len(TA)
def tv(ring, k): return T0 + ring * NV + k
# k: 1/7 = side-front, 2/6 = outer bust, 3/5 = inner bust (peak), 4 = centre front, 9 = back centre
SCULPT = {
    0: {1: (.118, 1.295, .052), 2: (.085, 1.298, .078), 3: (.045, 1.30, .095), 4: (0, 1.30, .098)},
    1: {1: (.145, 1.37, .085), 2: (.112, 1.356, .172), 3: (.062, 1.352, .207), 4: (0, 1.372, .126)},
    2: {1: (.152, 1.435, .07), 2: (.112, 1.432, .142), 3: (.06, 1.436, .15), 4: (0, 1.44, .11)},
}
for r_, ks in SCULPT.items():
    for k, (x, y, z) in ks.items():
        V[tv(r_, k)] = np.array((x, y, z))
        if k != 4:
            V[tv(r_, 8 - k)] = np.array((-x, y, z))
# abdomen: 4-point waist ring (sides, front centre, back centre) -> two planes front, two back,
# fanned up into the 10-point under-bust ring so the bust keeps its density
wa = [addv((.088, 1.12, .012)), addv((0, 1.12, .08)), addv((-.088, 1.12, .012)), addv((0, 1.12, -.056))]
b = [tv(0, k) for k in range(NV)]
ABC = np.array((0, 1.2, .012))
for tri in ((wa[0], b[0], b[1]), (wa[0], b[1], b[2]), (wa[0], b[2], wa[1]), (wa[1], b[2], b[3]), (wa[1], b[3], b[4]),
            (wa[2], b[8], b[7]), (wa[2], b[7], b[6]), (wa[2], b[6], wa[1]), (wa[1], b[6], b[5]), (wa[1], b[5], b[4])):
    face(list(tri), 'skin', ABC)
face([wa[0], b[0], b[9], wa[3]], 'skin', ABC)
face([wa[3], b[9], b[8], wa[2]], 'skin', ABC)
wctr = addv((0, 1.12 - .03, .012))
for k in range(4):
    face([wa[k], wa[(k + 1) % 4], wctr], 'skin', outward=np.array((0, -1, 0)), smooth=.75)

# neck: a plain separate box, ends hidden inside torso and head
PART[0] = 'neck'
BONE[0] = 'neck'
loft([(0, 1.47, -.008), (0, 1.64, 0)], [(.05 * S2, .044 * S2)] * 2, 4, 'skin',
     ref=(0, 0, 1), rot=math.pi / 4, cap0='dome', cap1='dome', smooth=0, tparam=False)

PART[0] = 'head'
BONE[0] = 'head'
# head: a box subdivided once (3x3x3 lattice surface -> 26 verts, 24 quads), then shaped
# into a skull. Key = (ix, iy, iz) with ix >= 0; mirrored across X.
HC = np.array((0, 1.705, .012))
HEADPTS = {
    (0, 1, 0): (0, .135, -.01),   (1, 1, 0): (.072, .112, -.01),
    (0, 1, 1): (0, .105, .082),   (1, 1, 1): (.064, .094, .074),
    (0, 1, -1): (0, .1, -.1),     (1, 1, -1): (.062, .088, -.088),
    (1, 0, 0): (.09, .012, -.005),
    (1, 0, 1): (.074, .004, .092), (0, 0, 1): (0, .014, .112),
    (1, 0, -1): (.078, .0, -.1),  (0, 0, -1): (0, .0, -.118),
    (0, -1, 0): (0, -.1, .0),     (1, -1, 0): (.062, -.085, .0),
    (1, -1, 1): (.044, -.112, .07), (0, -1, 1): (0, -.126, .088),
    (1, -1, -1): (.05, -.05, -.07), (0, -1, -1): (0, -.06, -.08),
}
HV = {}
for ix in (-1, 0, 1):
    for iy in (-1, 0, 1):
        for iz in (-1, 0, 1):
            if (ix, iy, iz) == (0, 0, 0):
                continue
            x, y, z = HEADPTS[(abs(ix), iy, iz)]
            HV[(ix, iy, iz)] = addv(HC + (np.sign(ix) * x, y, z), 0.0)
for axis in range(3):
    for sg in (-1, 1):
        o = [k for k in range(3) if k != axis]
        for u0 in (-1, 0):
            for v0 in (-1, 0):
                quad = []
                for du, dv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    key = [0, 0, 0]; key[axis] = sg; key[o[0]] = u0 + du; key[o[1]] = v0 + dv
                    quad.append(HV[tuple(key)])
                face(quad, 'skin', HC, smooth=.55)
# horns: curl up, out and back from the top of the skull
for s in (1, -1):
    loft([(s * .055, 1.80, .0), (s * .16, 1.93, -.07), (s * .222, 2.055, -.23)],
         [.042, .028, 0], 3, 'horn', ref=(0, 0, 1), rot=math.pi / 2, cap0=False)
HPIV, HSC, HDROP, HY = np.array((0, 1.70, -.02)), 1.3, np.zeros(3), 1.775   # legacy (beak paint, unused)

# left arm (+X): clawed hand
ARM_L = [(.17, 1.48, 0), (.445, 1.48, -.01), (.735, 1.48, 0)]   # T-pose: straight out to the sides
ARM_R = [(-.17, 1.48, 0), (-.445, 1.48, -.01), (-.735, 1.48, 0)]
PART[0] = 'arm'
for A in (ARM_L, ARM_R):
    sh, el, wr = map(np.array, A[:3])
    d_up = nrm(el - sh); d_fo = nrm(wr - el)
    sd = 'L' if A is ARM_L else 'R'
    # upper arm: deltoid -> biceps belly -> elbow (separate piece, capped at the elbow)
    BONE[0] = 'upper_arm_' + sd
    loft([sh, sh + (el - sh) * .45 + (0, 0, .008), el + d_up * .018],
         [(.066, .064), (.058, .068), .046], 4, 'skin', ref=(0, 0, 1), rot=math.pi / 4, cap0='dome', cap1='dome')
    # forearm: muscle bulge near the elbow tapering to a slim wrist (top tucked into upper arm)
    BONE[0] = 'forearm_' + sd
    loft([el - d_fo * .015, el + (wr - el) * .3, wr],
         [.046, (.054, .05), .034], 4, 'skin', ref=(0, 0, 1),
         rot=math.pi / 4, cap0='dome', cap1='dome')
# hands: closed fists as plain boxes (right fist doubles as the weapon mount in engine)
PART[0] = 'hand'
FISTS = []
for A in (ARM_L, ARM_R):
    wr = np.array(A[2]); d_fo = nrm(wr - np.array(A[1]))
    FIST = (wr - d_fo * .01, wr + d_fo * .075)
    FISTS.append(FIST)
    BONE[0] = 'fist_' + ('L' if A is ARM_L else 'R')
    loft(list(FIST), [(.032 * S2, .027 * S2)] * 2, 4, 'skin', ref=(0, 0, 1), rot=math.pi / 4, smooth=0, tparam=True, cap0='dome')
MOUNT_POS = (FISTS[1][0] + FISTS[1][1]) / 2
MOUNT_DIR = nrm(FISTS[1][1] - FISTS[1][0])
BY, BH, BT = 0, .05, .014   # (chainsaw removed; kept so the unused bar paint code stays valid)

# legs: human proportions (thigh, knee, calf, ankle), chitin-plated, ending in hooves
PART[0] = 'leg'
for s in (1, -1):
    sd = 'L' if s > 0 else 'R'
    # thigh: separate piece, capped at the knee
    BONE[0] = 'thigh_' + sd
    n0 = len(V)
    loft([(s * .085, .97, 0), (s * .115, .84, .01), (s * .135, .515, .035)],
         [.096, .102, .062], 5, 'chitin', ref=(0, 0, 1), rot=math.pi / 2, cap0='dome', cap1='dome')
    for i in range(n0, len(V)):
        VT[i] = [0, .2, .52][min((i - n0) // 5, 2)]
    VT[-2], VT[-1] = 0, .52
    # calf + shin: separate piece, top tucked inside the thigh
    BONE[0] = 'calf_' + sd
    n0 = len(V)
    loft([(s * .135, .56, .035), (s * .14, .40, 0), (s * .15, .15, .015)],
         [.052, .074, .037], 5, 'chitin', ref=(0, 0, 1), rot=math.pi / 2, cap0='dome', cap1='dome')
    for i in range(n0, len(V)):
        VT[i] = [.52, .68, 1.0][min((i - n0) // 5, 2)]
    VT[-2], VT[-1] = .52, 1.0
    BONE[0] = 'hoof_' + sd
    loft([(s * .15, .165, .02), (s * .152, 0.0, .045)],
         [.04, (.06, .07)], 4, 'hoof', ref=(0, 0, 1), rot=math.pi / 4, cap0='dome')

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
    # head/eyes/chainsaw get extra texel density
    f['boost'] = {'torso': 1.5, 'neck': 1.3, 'head': 2.3, 'arm': 1.35, 'hand': 1.4, 'leg': 1.0, 'saw': .65}.get(f['part'], 1.0)


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

# ---------------------------------------------------------------- procedural albedo
def hsh(*a):
    x = np.zeros_like(a[0], dtype=float)
    for i, v in enumerate(a):
        x = x + v * (12.9898 + 31.7 * i)
    return np.modf(np.abs(np.sin(x) * 43758.5453))[0]


M = mat
alb = np.zeros((N, 3)); emis = np.zeros((N, 3)); spec = np.zeros(N); gloss = np.full(N, 16.0)
noise = hsh(np.floor(pos[:, 0] * 180), np.floor(pos[:, 1] * 180), np.floor(pos[:, 2] * 180)) - .5

# scales (skin) - fish-scale rows in face-local coordinates
def scales(a, b, sz):
    """hexagonal voronoi scales: returns (centre-falloff, border-closeness)"""
    x, y = a / sz, b / (sz * .8)
    ry = np.floor(y)
    d1 = np.full(len(x), 9.0); d2 = np.full(len(x), 9.0); cyb = np.zeros(len(x))
    for dr in (-1, 0, 1):
        r = ry + dr
        off = (r % 2) * .5
        cx0 = np.floor(x - off)
        for dc in (-1, 0, 1):
            cx = cx0 + dc + off + .5
            cy = r + .5
            d = np.hypot(x - cx, (y - cy) * .8)
            closer = d < d1
            d2 = np.where(closer, d1, np.minimum(d2, d))
            cyb = np.where(closer, y - cy, cyb)
            d1 = np.where(closer, d, d1)
    return cyb, d2 - d1


ln = sn.copy()   # lighting normals (bump-mapped muscles)
m = M == MATS.index('skin')
brown = np.array((156, 36, 28)) / 255          # crimson scales (title art)
cyb, edge = scales(la[m], lb[m], .042)
shade = .93 - .28 * cyb
col = brown[None] * shade[:, None]
col[edge < .1] *= .55
torso_m = part[m] == 'torso'
col[torso_m] = col[torso_m] * .55 + brown * .45 * (1 + .1 * noise[m][torso_m])[:, None]
col *= (1 + .14 * noise[m])[:, None]
alb[m] = col; spec[m] = .14; gloss[m] = 10

# ---- muscle height field (world space) -> baked bump lighting
def g2(x, y, cx, cy, sx, sy):
    return np.exp(-((x - cx) / sx) ** 2 - ((y - cy) / sy) ** 2)


def seg_param(p, a, b):
    a, b = np.asarray(a), np.asarray(b)
    d = b - a
    t = np.clip(((p - a) @ d) / (d @ d), 0, 1)
    return t


def limb(p, a, b, front=(0, 0, 1), out=None):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = b - a
    t = np.clip(((p - a) @ d) / (d @ d), 0, 1)
    r = p - (a + np.outer(t, d))
    dist = np.linalg.norm(r, axis=1) + 1e-9
    rh = r / dist[:, None]
    ax = d / np.linalg.norm(d)
    fv = np.asarray(front, float); fv = nrm(fv - (fv @ ax) * ax)
    ov = np.cross(ax, fv)
    if out is not None and ov @ np.asarray(out) < 0:
        ov = -ov
    return t, rh @ fv, rh @ ov, dist


def gbell(t, c, w):
    return np.exp(-((t - c) / w) ** 2)


def muscles(p, prt):
    x, y, z = np.abs(p[:, 0]), p[:, 1], p[:, 2]
    h = np.zeros(len(p))
    fr = np.clip(z * 20, 0, 1); bk = np.clip(-z * 20, 0, 1)
    # ---- torso front: abs, obliques, serratus, hip bones, upper pecs/clavicles
    abs_ = sum(g2(x, y, .034, yc, .022, .026) for yc in (1.255, 1.185, 1.115))
    abs_ += .8 * g2(x, y, .03, 1.045, .024, .035)
    obl = g2(x, y, .074, 1.16, .02, .07)
    vline = np.exp(-((x - (.06 + (y - 1.0) * .45)) / .012) ** 2) * (y < 1.1) * (y > .98)
    navel = -1.2 * g2(x, y, 0, 1.075, .007, .009)
    ribs = sum(g2(x, y, .095 + .01 * k, 1.27 + .025 * k, .012, .009) for k in range(2))
    hipb = g2(x, y, .1, 1.05, .02, .025)
    pecs = g2(x, y, .07, 1.468, .05, .02) - .8 * np.exp(-((y - (1.49 + x * .12)) / .005) ** 2) * (x < .14)
    front = fr * (abs_ + obl * .8 - vline * .6 + navel + ribs * .6 + hipb * .5 + pecs * .7)
    # ---- back: spine, erectors, lats, shoulder blades, traps, glutes
    spine = -1.2 * np.exp(-(x / .01) ** 2) * (y > 1.0)
    erect = np.exp(-((x - .026) / .013) ** 2) * (y > 1.02) * (y < 1.32)
    lats = g2(x, y, .1, 1.3, .035, .09)
    scap = g2(x, y, .068, 1.42, .033, .045) - .5 * np.exp(-((x - .1) / .008) ** 2) * (y > 1.37) * (y < 1.47)
    glute = g2(x, y, .075, .955, .055, .05)
    back = bk * (spine + erect * .8 + lats + scap * .8 + glute)
    traps = g2(x, y, .065, 1.5, .055, .03)
    tor = prt == 'torso'
    h += tor * (front + back + traps * .7)
    # ---- neck: sternocleidomastoid ridges + throat
    nk = prt == 'neck'
    for sgn in (1, -1):
        t, f_, o_, _ = limb(p, (sgn * .036, 1.63, -.01), (sgn * .012, 1.48, .04))
        h += nk * (np.sign(p[:, 0]) == sgn) * np.exp(-((p[:, 0] - sgn * (.036 - .024 * t)) / .009) ** 2) * fr * .8
    h += nk * bk * (-np.exp(-(x / .008) ** 2) * .6 + g2(x, y, .03, 1.5, .03, .05) * .6)
    # ---- arms: deltoid, biceps, triceps, forearm groups
    arm = (prt == 'arm')
    for A in (ARM_L, ARM_R):
        sgn = np.sign(A[0][0])
        side = arm & (np.sign(p[:, 0]) == sgn)
        tu, fu, ou, du = limb(p, A[0], A[1], out=(sgn, 0, 0))
        tf, ff, of, df = limb(p, A[1], A[2], out=(sgn, 0, 0))
        up = du <= df
        delt = gbell(tu, .1, .15) * (.6 + .4 * np.clip(ou, 0, 1))
        bic = gbell(tu, .58, .2) * np.clip(ff * 0 + fu, 0, 1) ** 1.3
        tri = gbell(tu, .45, .25) * np.clip(-fu, 0, 1) * (.8 + .3 * np.clip(ou, 0, 1))
        sep = -.5 * gbell(fu, 0, .25) * gbell(tu, .62, .2)                 # groove between biceps/triceps
        brach = gbell(tf, .22, .18) * np.clip(of * .7 + ff * .5, 0, 1)
        flex = gbell(tf, .3, .22) * np.clip(-of, 0, 1) * .8
        ext = gbell(tf, .35, .25) * np.clip(-ff, 0, 1) * .6
        tend = -.3 * gbell(tf, .85, .12)
        h += side * np.where(up, delt * 1.1 + bic + tri + sep, brach + flex + ext + tend)
    # ---- legs (chitin over muscle): quads, teardrop, hamstrings, calves, shin
    leg = prt == 'leg'
    for sgn in (1, -1):
        side = leg & (np.sign(p[:, 0]) == sgn)
        tt, ft, ot, dt = limb(p, (sgn * .085, .97, 0), (sgn * .135, .515, .035), out=(sgn, 0, 0))
        tc, fc, oc, dc = limb(p, (sgn * .135, .56, .035), (sgn * .15, .15, .015), out=(sgn, 0, 0))
        th = dt <= dc
        quad = gbell(tt, .45, .25) * np.clip(ft, 0, 1) - .4 * gbell(ot + .2, 0, .15) * gbell(tt, .5, .3) * np.clip(ft, 0, 1)
        tear = gbell(tt, .83, .08) * np.clip(-ot * .8 + ft * .4, 0, 1)
        vlat = gbell(tt, .55, .25) * np.clip(ot, 0, 1) * .8
        ham = gbell(tt, .5, .3) * np.clip(-ft, 0, 1) * .8
        gast = gbell(tc, .22, .14) * np.clip(-fc, 0, 1) * (1 + .4 * np.clip(np.abs(oc) - .3, 0, 1))
        tib = gbell(tc, .3, .25) * np.clip(fc * .7 + oc * .5, 0, 1) * .6
        h += side * np.where(th, quad + tear + vlat + ham, gast * 1.2 + tib)
    # ---- skull: brow ridge, cheekbones, sunken sockets, nasal dip, jaw muscle
    hd = prt == 'head'
    q = p - HC
    qx, qy, qz = np.abs(q[:, 0]), q[:, 1], q[:, 2]
    ff = np.clip((qz - .02) * 25, 0, 1)
    brow = g2(qx, qy, .042, .056, .04, .012)
    cheek = g2(qx, qy, .062, -.018, .022, .018)
    sockets = g2(qx, qy, .042, .024, .024, .02)
    nasal = g2(qx, qy, 0, -.028, .012, .02)
    jaw = g2(qx, qy, .06, -.07, .02, .025)
    temple = g2(qx, qy, .085, .04, .02, .03) * np.clip(1 - np.abs(qz) * 10, 0, 1)
    h += hd * (ff * (brow * 1.1 + cheek - sockets * 1.6 - nasal + jaw * .6) - temple * .6)
    return h * .009


eps = .0025
gx_ = np.zeros((N, 3))
for k in range(3):
    dv = np.zeros(3); dv[k] = eps
    gx_[:, k] = (muscles(pos + dv, part) - muscles(pos - dv, part)) / (2 * eps)
hm = muscles(pos, part)
gt = gx_ - (gx_ * sn).sum(1, keepdims=True) * sn
sk = M == MATS.index('skin')
ch = M == MATS.index('chitin')
ln[sk] = sn[sk] - gt[sk] * 2.2
ln[ch] = sn[ch] - gt[ch] * 1.8
ln /= np.linalg.norm(ln, axis=1, keepdims=True)
alb[sk] *= np.clip(.8 + hm[sk] * 40, .7, 1.18)[:, None]
CH_SHADE = np.clip(.85 + hm * 30, .75, 1.15)

# ---- fist: finger grooves across the knuckle side, dark claw-nails
for FIST in FISTS:
    sgn = np.sign(FIST[0][0])
    hm_ = (part == 'hand') & (np.sign(pos[:, 0]) == sgn)
    ax = nrm(FIST[1] - FIST[0])
    side = nrm(np.cross(ax, (0, 1, 0)))
    along = ((pos[hm_] - FIST[0]) @ ax) / np.linalg.norm(FIST[1] - FIST[0])
    across = (pos[hm_] - FIST[0]) @ side
    c = alb[hm_]
    groove = (np.abs(np.modf((across + .1) / .016)[0] - .5) > .4) & (along > .55)
    c[groove] *= .55
    c[along > .82] *= 1.15
    nail = (along > .55) & (along < .7) & ((pos[hm_] - FIST[0]) @ np.array((0, -1, 0)) > .015)
    c[nail] = np.array((40, 26, 22)) / 255
    alb[hm_] = c

# ---- painted armour (overrides skin; uses un-bumped normals): dark steel corset with gold
#      trim and filigree, a gold fleur-de-lis on the sternum, laced back, shoulder straps,
#      steel bracers with gold rims (after the title art: bare scaled shoulders, no pauldrons)
IRON, IRON_D = np.array((66, 62, 74)) / 255, np.array((30, 28, 36)) / 255
BRONZE = np.array((214, 168, 70)) / 255          # gold
GOLD_D = np.array((130, 88, 30)) / 255
LEATHER = np.array((46, 26, 22)) / 255
armour = np.zeros(N, bool)


def paint(mask, colr, sp=.85, gl=34):
    alb[mask] = colr[mask] if colr.ndim == 2 else colr
    spec[mask] = sp; gloss[mask] = gl
    ln[mask] = sn[mask]
    armour[mask] = True


X, Y, Z = pos[:, 0], pos[:, 1], pos[:, 2]
AX = np.abs(X)
tor = part == 'torso'
brushed = (1 + .12 * noise)[:, None]
front = Z > -.012
# sweetheart neckline: the top edge dips between the cups
top = 1.452 - .05 * np.exp(-(AX / .024) ** 2)
cor = tor & (Y > 1.0) & (Y < top)
c = IRON[None] * brushed * np.ones((N, 1))
# vertical plate seams on the waist, cup outlines, V filigree, trims
seam = (Y < 1.29) & (np.abs(np.modf((AX + .025) / .05)[0] - .5) > .46)
c[seam] = IRON_D
for cx in (.062, -.062):
    dcup = np.hypot(X - cx, (Y - 1.362) * 1.15)
    c[front & (np.abs(dcup - .072) < .0045)] = BRONZE
    c[front & (dcup < .068)] *= 1.12                                   # cups catch more light
for off in (0, .045):
    v = front & (np.abs(Y - (1.29 - off - 1.5 * AX)) < .0042) & (Y > 1.04) & (AX < .14)
    c[v] = BRONZE
c[np.abs(Y - top) < .013] = BRONZE                                      # top edge
c[(Y > 1.0) & (Y < 1.016)] = BRONZE                                     # bottom edge
rv = (np.abs(Y - top + .006) < .004) & (np.abs(np.modf(X / .022)[0]) < .2)
c[rv] = np.array((250, 232, 180)) / 255
# fleur-de-lis on the sternum
fx_, fy = X, Y - 1.20
lis = (AX < .013 * np.clip(1 - np.abs(fy - .012) / .05, 0, 1)) & (fy > -.04) & (fy < .062)
curl = (np.abs(np.hypot(AX - .024, fy - .016) - .014) < .0042) & (fy > .006)
bar_ = (np.abs(fy + .006) < .0045) & (AX < .03)
tail = (np.abs(np.hypot(AX - .02, fy + .03) - .012) < .004) & (fy < -.02)
emb = front & (lis | curl | bar_ | tail)
c[emb] = BRONZE * 1.1
c[front & (lis | curl) & (X > 0)] = BRONZE * 1.25                       # lit side
# back: laced panel with gold eyelets
back = ~front
c[back & (AX < .018)] = LEATHER
lace = back & (AX < .018) & (np.abs(np.modf((Y + AX * 1.6) / .03)[0] - .5) < .1)
lace |= back & (AX < .018) & (np.abs(np.modf((Y - AX * 1.6) / .03)[0] - .5) < .1)
c[lace] = np.array((120, 30, 24)) / 255
c[back & (np.abs(AX - .021) < .004) & (np.abs(np.modf(Y / .03)[0] - .5) < .2)] = BRONZE
paint(cor, c)
# shoulder straps from the corset over the shoulders, gold buckles
st = tor & (Y >= top - .01) & (np.abs(AX - .088) < .015)
cs = np.tile(LEATHER, (N, 1)) * brushed
cs[np.abs(Y - 1.47) < .009] = BRONZE
cs[(np.abs(AX - .088) > .011)] *= 1.6                                   # stitched edges
paint(st, cs, .35, 12)
# belt with gold studs and buckle
belt = tor & (Y > .975) & (Y <= 1.0)
cbelt = np.tile(LEATHER, (N, 1)) * brushed
stud = (np.abs(np.modf((X + 2) / .03)[0] - .5) < .17) & (np.abs(Y - .988) < .006)
cbelt[stud] = BRONZE * 1.2
cbelt[(AX < .028) & (Z > 0)] = BRONZE
cbelt[(AX < .012) & (Z > 0) & (np.abs(Y - .988) < .008)] = np.array((110, 20, 14)) / 255
paint(belt, cbelt, .4, 14)
# armoured hip plates / fauld below the belt
fauld = tor & (Y <= .975)
cf = IRON[None] * brushed * np.ones((N, 1))
cf[np.abs(np.modf((np.arctan2(X, Z) + 4) / .35)[0] - .5) > .44] = IRON_D
cf[Y > .962] = BRONZE
paint(fauld, cf)
# tassets on upper thighs (front/outer), overlapping lames with gold edges
leg = part == 'leg'
tas = leg & (Y > .80) & ((Z > -.03) | (AX > .17))
ct = IRON[None] * brushed * np.ones((N, 1))
lame = np.modf((Y - .80) / .045)[0]
ct *= (.75 + .45 * lame)[:, None]
ct[lame < .12] = BRONZE * .9
paint(tas, ct)
# steel bracers on the forearms, gold rims and a raised centre band
arm = part == 'arm'
for A in (ARM_L, ARM_R):
    tf = seg_param(pos, A[1], A[2])
    du = np.linalg.norm(pos - (np.asarray(A[1]) + np.outer(tf, np.subtract(A[2], A[1]))), axis=1)
    br = arm & (np.sign(X) == np.sign(A[0][0])) & (tf > .18) & (tf < .98) & (du < .08)
    cbr = IRON[None] * brushed * np.ones((N, 1))
    cbr[(tf < .25) | (tf > .91)] = BRONZE
    cbr[(np.abs(tf - .58) < .03)] = IRON * 1.35
    cbr[(np.abs(tf - .58) < .008)] = GOLD_D
    paint(br, cbr)
# breastplate cups (geometry)
m = M == MATS.index('armour')
cc = IRON[None] * brushed[m] * 1.15
cc[tp[m] < .3] = BRONZE
alb[m] = cc; spec[m] = .95; gloss[m] = 40; armour[m] = True

# feathers: dark plumage, V-shaped barbs, crimson sheen
m = M == MATS.index('feather')
a, b = la[m], lb[m]
fb = np.modf(b / .022 + np.abs(np.modf(a / .03)[0] - .5) * 1.2)[0]
base = np.array((66, 42, 40)) / 255
col = base[None] * (.75 + .5 * fb)[:, None]
edge = fb > .82
col[edge] = np.array((120, 34, 30)) / 255
col *= (1 + .25 * noise[m])[:, None]
alb[m] = col; spec[m] = .18; gloss[m] = 12

# skull face painted on the scaled head: sockets with glowing orange eyes (no pupils),
# nasal cavity, grinning teeth
hd = part == 'head'
q = pos - HC
qx, qy, qz = np.abs(q[:, 0]), q[:, 1], q[:, 2]
frontf = hd & (qz > .03)
bone = frontf & ((qx / .062) ** 2 + ((qy + .018) / .078) ** 2 < 1)      # the skull face on the scaled head
bcol = np.array((178, 156, 132)) / 255 * (1 + .18 * noise[bone])[:, None]
crack = hsh(np.floor(pos[bone, 0] * 260), np.floor(pos[bone, 1] * 90)) > .93
bcol[crack] *= .55
bshade = np.clip(1 - np.clip(qx[bone] - .03, 0, 1) * 9, .55, 1)               # cheekbones fall into shadow
alb[bone] = bcol * bshade[:, None]; spec[bone] = .3; gloss[bone] = 18; ln[bone] = sn[bone]
sock = ((qx - .042) / .031) ** 2 + ((qy - .024) / .025) ** 2
eye = ((qx - .042) / .018) ** 2 + ((qy - .022) / .014) ** 2
m = frontf & (sock < 1)
alb[m] = np.array((26, 12, 10)) / 255 * (1 + .15 * noise[m])[:, None]
emis[m] = np.array((.45, .3, 0)) * np.clip(1 - sock[m], 0, 1)[:, None] ** 2      # yellow glow spilling in the socket
m = frontf & (eye < 1)
g_ = np.clip(1 - eye[m], 0, 1)[:, None]
emis[m] = np.array((1.0, .72, .08)) * (.75 + .25 * g_) + np.array((.0, .25, .35)) * g_ ** 2
alb[m] = 0; spec[m] = 0
nas = frontf & (qy > -.05) & (qy < -.006) & (qx < .004 + .013 * np.clip((-.006 - qy) / .044, 0, 1))
alb[nas] = np.array((22, 10, 8)) / 255
ym = -.074 + .012 * (qx / .05) ** 2
band = frontf & (qx < .058) & (np.abs(qy - ym) < .034)
alb[band] = alb[band] * .7 + np.array((60, 40, 32)) / 255 * .3                 # jaw shadow
tooth_u = frontf & (qx < .052) & (qy > ym) & (qy < ym + .02)
tooth_l = frontf & (qx < .046) & (qy < ym) & (qy > ym - .017)
fang = frontf & (np.abs(qx - .03) < .006) & (qy < ym) & (qy > ym - .02 + 1.6 * np.abs(qx - .03))
teeth = tooth_u | tooth_l | fang
tcol = np.tile(np.array((214, 196, 150)) / 255, (N, 1)) * (1 + .1 * noise)[:, None]
tcol *= (.85 + .3 * np.clip((qy - ym) / .02, -1, 1) * np.sign(qy - ym))[:, None]
gap_u = np.abs(np.modf(qx / .0105)[0] - .5) > .4
gap_l = np.abs(np.modf(qx / .0105 + .5)[0] - .5) > .4
tcol[(tooth_u & gap_u) | (tooth_l & gap_l)] = np.array((40, 16, 12)) / 255
alb[teeth] = tcol[teeth]; spec[teeth] = .6; gloss[teeth] = 30; ln[teeth] = sn[teeth]
alb[frontf & (np.abs(qy - ym) < .0022) & (qx < .05)] = np.array((14, 6, 5)) / 255

# beak
m = M == MATS.index('beak')
tt = np.clip((pos[m, 2] - HPIV[2] - (.10 - HPIV[2]) * HSC - HDROP[2]) / (.215 * HSC), 0, 1)
col = (np.array((150, 140, 110)) / 255)[None] * (1 - tt[:, None] * .75)
col *= (1 + .2 * noise[m])[:, None]
alb[m] = col; spec[m] = .5; gloss[m] = 24

# horns: ivory -> black tip, ridged
m = M == MATS.index('horn')
t_ = tp[m]
ridge = (np.modf(t_ * 13)[0] < .3)
col = (1 - t_[:, None]) * np.array((168, 138, 122))[None] / 255 + t_[:, None] * np.array((40, 28, 26))[None] / 255
col[ridge] *= .7
alb[m] = col * (1 + .15 * noise[m])[:, None]; spec[m] = .4; gloss[m] = 20

# chitin legs: overlapping plates following the human leg shape
m = (M == MATS.index('chitin')) & ~armour
t_ = tp[m]
plate_edges = np.array([0, .2, .32, .45, .5, .55, .62, .68, .77, .86, .93, 1.0])
k = np.clip(np.searchsorted(plate_edges, t_) - 1, 0, len(plate_edges) - 2)
seg = (t_ - plate_edges[k]) / (plate_edges[k + 1] - plate_edges[k])
base = np.array((110, 30, 26)) / 255
col = base[None] * (1.25 - .55 * seg)[:, None]          # bright plate top, shadowed overlap
col[seg < .1] = np.array((176, 70, 52)) / 255           # lit plate lip
knee = (t_ > .45) & (t_ < .55)
col[knee] = np.array((130, 44, 36)) / 255 * (1.1 - .3 * seg[knee])[:, None]
ridge = np.abs(np.modf(la[m] / .03)[0] - .5) < .08
col[ridge] *= .7
spots = hsh(np.floor(la[m] / .016), np.floor(lb[m] / .016), fid[m]) > .9
col[spots] *= .6
alb[m] = col * (1 + .12 * noise[m])[:, None] * CH_SHADE[m][:, None]; spec[m] = .75; gloss[m] = 30

# hooves
m = M == MATS.index('hoof')
stri = np.modf(la[m] / .012)[0] < .3
col = np.full((m.sum(), 3), 32 / 255) * np.array((1, .9, .85))[None]
col[stri] *= .7
alb[m] = col * (1 + .25 * noise[m])[:, None]; spec[m] = .4; gloss[m] = 18

# claws / spikes
m = M == MATS.index('claw')
alb[m] = (np.array((60, 44, 36)) / 255)[None] * (1 + .2 * noise[m])[:, None]; spec[m] = .5; gloss[m] = 24

# chainsaw housing: grimy painted metal, rivets, rust
m = M == MATS.index('metal')
col = np.tile(np.array((150, 40, 30)) / 255, (m.sum(), 1))
gr = (np.modf(la[m] / .03)[0] < .1) | (np.modf(lb[m] / .03)[0] < .1)
col[gr] = np.array((70, 66, 62)) / 255
riv = (np.hypot(np.modf(la[m] / .05)[0] - .5, np.modf(lb[m] / .05)[0] - .5) < .16)
col[riv] = np.array((190, 185, 170)) / 255
rust = hsh(np.floor(pos[m, 0] * 60), np.floor(pos[m, 1] * 60), np.floor(pos[m, 2] * 60)) > .7
col[rust & ~riv] = col[rust & ~riv] * .5 + np.array((90, 50, 20)) / 255 * .5
alb[m] = col * (1 + .15 * noise[m])[:, None]; spec[m] = .6; gloss[m] = 30

# bar: steel with chain band along edges and blood
m = M == MATS.index('bar')
yy = np.abs(pos[m, 1] - BY)
col = np.tile(np.array((150, 156, 165)) / 255, (m.sum(), 1))
chain = yy > BH * .62
link = np.modf(pos[m, 2] / .02)[0] < .5
col[chain & link] = np.array((60, 60, 64)) / 255
col[chain & ~link] = np.array((110, 108, 104)) / 255
blood = hsh(np.floor(pos[m, 2] * 40), np.floor(pos[m, 1] * 50)) > .55
blood &= hsh(np.floor(pos[m, 2] * 9), np.floor(pos[m, 1] * 9), 7 * np.ones(m.sum())) > .45
col[blood] = np.array((110, 8, 6)) / 255
alb[m] = col * (1 + .12 * noise[m])[:, None]; spec[m] = .9; gloss[m] = 40
m = M == MATS.index('tooth')
alb[m] = np.array((175, 175, 180)) / 255; spec[m] = .9; gloss[m] = 40

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

with open(f'{OUT}/demon.tim', 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09))                         # ID, 8bpp + CLUT
    fh.write(struct.pack('<IHHHH', 12 + 512, 0, 480, 256, 1))        # CLUT at VRAM (0,480)
    fh.write(struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TEX * TEX, 640, 0, TEX // 2, TEX))  # image at VRAM (640,0)
    fh.write(idx.tobytes())

rgb = np.array([[(w & 31), (w >> 5) & 31, (w >> 10) & 31] for w in words])
rgb8 = (rgb * 8 + rgb // 4).astype(np.uint8)
out_rgb = rgb8[idx]
Image.fromarray(out_rgb).save(f'{OUT}/demon_texture.png')
Image.fromarray(out_rgb).resize((1024, 1024), Image.NEAREST).save(f'{OUT}/demon_texture_x4.png')

# ---------------------------------------------------------------- mesh exports (rigid parts)
import shutil
FX = 4096 * .25          # PS1 fixed point: 1 model unit = 1024 (4096 = 1.0 GTE unit, model scaled x0.25)
order = sorted(range(len(F)), key=lambda i: BIDX[F[i]['bone']])        # prims grouped by bone
vorder = sorted(range(len(P)), key=lambda i: (VBONE[i], i))            # verts grouped by bone
vremap = {old: new for new, old in enumerate(vorder)}
MOUNT_LOCAL = MOUNT_POS - BPIV[BIDX['fist_R']]

with open(f'{OUT}/demon.mtl', 'w') as fh:
    fh.write('newmtl demon\nKd 1 1 1\nillum 0\nmap_Kd demon_texture.png\n')


def write_obj(path, bones, local):
    with open(path, 'w') as fh:
        fh.write(f'# PS1 demon rigid parts - {"local space (origin = part pivot)" if local else "model space"}\nmtllib demon.mtl\n')
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
            fh.write('usemtl demon\ns off\n')
            for f in fs:
                fh.write('f ' + ' '.join(f'{vbase + loc[vi]}/{tbase + nt + k}' for k, vi in enumerate(f['v'])) + '\n')
                nt += len(f['v'])
            vbase += len(vs); tbase += nt


write_obj(f'{OUT}/demon.obj', BNAMES, local=False)
os.makedirs(f'{OUT}/parts', exist_ok=True)
for b in BNAMES:
    write_obj(f'{OUT}/parts/{b}.obj', [b], local=True)
shutil.copy(f'{OUT}/demon.mtl', f'{OUT}/parts/demon.mtl')

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
png = open(f'{OUT}/demon_texture.png', 'rb').read()
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
nodes.append(dict(name='demon_mesh', mesh=0, skin=0))
gltf = dict(
    asset=dict(version='2.0', generator='ps1 demon build.py'),
    extensionsUsed=['KHR_materials_unlit'],
    scene=0, scenes=[dict(nodes=[0, mesh_node])],
    nodes=nodes,
    skins=[dict(joints=list(range(len(SKEL))), skeleton=0, inverseBindMatrices=5)],
    meshes=[dict(name='demon', primitives=[dict(attributes=dict(POSITION=0, TEXCOORD_0=1, JOINTS_0=2, WEIGHTS_0=3),
                                                 indices=4, material=0)])],
    materials=[dict(name='demon', pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=1),
                    extensions=dict(KHR_materials_unlit={}))],
    textures=[dict(source=0, sampler=0)],
    samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)],
    images=[dict(bufferView=6, mimeType='image/png', name='demon_texture')],
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
with open(f'{OUT}/demon_rigged.glb', 'wb') as fh:
    fh.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binbuf)))
    fh.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    fh.write(struct.pack('<II', len(binbuf), 0x004E4942) + binbuf)

# ---- PS1 C header: bone table + per-bone vertex/prim ranges, vertices local to their bone pivot
def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


with open(f'{OUT}/demon_mesh.h', 'w') as fh:
    fh.write(f'/* PS1 demon, rigid hierarchical parts: {len(P)} verts, {len(F)} prims ({ntri} tris), {len(SKEL)} bones.\n'
             f'   Fixed point: 1 model unit = {int(FX)} (4096 = 1.0), Y down, faces +Z.\n'
             f'   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.\n'
             f'   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\n')
    fh.write('typedef struct { short x, y, z, pad; } DVERT;\n')
    fh.write('typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n')
    fh.write('typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n')
    fh.write('#endif\n')
    fh.write(f'#define DEMON_NVERTS {len(P)}\n#define DEMON_NPRIMS {len(F)}\n#define DEMON_NBONES {len(SKEL)}\n')
    for i, n in enumerate(BNAMES):
        fh.write(f'#define DEMON_BONE_{n.upper()} {i}\n')
    ml = fx(MOUNT_LOCAL); md = MOUNT_DIR * 4096
    fh.write(f'/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */\n')
    fh.write(f'#define DEMON_MOUNT_BONE DEMON_BONE_FIST_R\n#define DEMON_MOUNT_X {ml[0]}\n#define DEMON_MOUNT_Y {ml[1]}\n#define DEMON_MOUNT_Z {ml[2]}\n')
    fh.write(f'#define DEMON_MOUNT_DX {int(round(md[0]))}\n#define DEMON_MOUNT_DY {int(round(-md[1]))}\n#define DEMON_MOUNT_DZ {int(round(md[2]))}\n')
    fh.write('#ifndef DEMON_MESH_NO_DATA\n')
    fh.write('static const DVERT demon_verts[] = {\n')
    for i in vorder:
        x, y, z = fx(P[i] - BPIV[VBONE[i]])
        fh.write(f'  {{{x},{y},{z},0}}, /* {BNAMES[VBONE[i]]} */\n')
    fh.write('};\nstatic const DPRIM demon_prims[] = {\n')
    for i in order:
        f = F[i]
        v = [vremap[k] for k in f['v']]; uv = list(f['uv'])
        if len(v) == 4:  # PS1 quad vertex order is Z: 0,1,3,2
            v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
        v += [0] * (4 - len(v)); uv += [(0, 0)] * (4 - len(uv))
        fh.write(f"  {{{len(f['v'])},{{{','.join(map(str, v))}}},{{{','.join(str(a) for a, _ in uv)}}},{{{','.join(str(b) for _, b in uv)}}}}},\n")
    fh.write('};\nstatic const DBONE demon_bones[] = {\n')
    for i, n in enumerate(BNAMES):
        vs = [k for k, old in enumerate(vorder) if VBONE[old] == i]
        ps = [k for k, old in enumerate(order) if F[old]['bone'] == n]
        off = BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0)
        ox, oy, oz = fx(off)
        fh.write(f'  {{"{n}", {BPAR[i]}, {ox}, {oy}, {oz}, {vs[0]}, {len(vs)}, {ps[0]}, {len(ps)}}},\n')
    fh.write('};\n#endif /* DEMON_MESH_NO_DATA */\n')
print('done')
