"""PS1-style skeleton knight: bare bones in blackened, rusting plate armour, box-modelled on Deadbird's
skeleton (same bones, so the same animations drive him), baked/dithered 8bpp CLUT texture, TIM export,
a rigged GLB and a self-contained 3D viewer page. Same pipeline as tools/build_brute.py."""
import numpy as np, json, struct, math, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'tools', 'out', 'brute')
os.makedirs(OUT, exist_ok=True)
TEX = 256

V, VT, F = [], [], []          # positions, per-vertex param t, faces
MATS = ['skin', 'horn', 'tusk', 'metal', 'hoof']


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
# A hulking demon brute on Deadbird's bone names (so the same animations drive him), with his own
# proportions: a huge fat body, a thick neck that rolls down onto his shoulders with the head sunk
# into it, long fat arms with a cannon strapped on top of each forearm, short stubby legs, bull
# horns. +X = his left.
S2 = math.sqrt(2)
PART = ['torso']
BONE = ['pelvis']


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


# joints
HIP_Y, KNEE_Y, ANKLE_Y = .66, .38, .12
SH = (.44, 1.5, 0)
ARM_L = [SH, (.88, 1.49, -.01), (1.3, 1.49, 0)]          # T-pose: long arms straight out
ARM_R = [(-x, y, z) for x, y, z in ARM_L]
LEG = {1: [(.22, HIP_Y, 0), (.26, KNEE_Y, .03), (.27, ANKLE_Y, .02)]}
LEG[-1] = [(-x, y, z) for x, y, z in LEG[1]]

# pelvis: the bottom of the gut, the hips
loft([(0, .52, .01), (0, .88, .05)], [(.34, .29), (.43, .4)], 8, 'skin',
     ref=(0, 0, 1), rot=math.pi / 8, cap0=True, cap1=False, smooth=.8)

# torso: the gut swelling out front, a heavy chest, sloping into the shoulders
BONE[0] = 'torso'
loft([(0, .87, .05), (0, 1.16, .07), (0, 1.47, .0)], [(.43, .4), (.46, .41), (.42, .3)], 8, 'skin',
     ref=(0, 0, 1), rot=math.pi / 8, cap0=False, cap1=False, smooth=.8)

# neck: a fat trunk of rolls from the shoulders up into the jaw (creases between the rolls)
PART[0] = 'neck'
BONE[0] = 'neck'
loft([(0, 1.45, -.005), (0, 1.56, .0), (0, 1.62, .01), (0, 1.69, .02), (0, 1.76, .04)],
     [(.44, .32), (.3, .235), (.36, .29), (.24, .21), (.27, .245)], 6, 'skin',
     ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=False, smooth=.75)

# head: sunk low and forward into the neck - a squashed skull lattice, an underbite with tusks,
# bull horns sweeping out and forward
PART[0] = 'head'
BONE[0] = 'head'
HC = np.array((0, 1.84, .1))
HEADPTS = {
    (0, 1, 0): (0, .1, -.01),     (1, 1, 0): (.11, .085, -.01),
    (0, 1, 1): (0, .085, .11),    (1, 1, 1): (.1, .07, .1),
    (0, 1, -1): (0, .07, -.11),   (1, 1, -1): (.1, .06, -.1),
    (1, 0, 0): (.15, .0, -.0),
    (1, 0, 1): (.12, -.01, .14),  (0, 0, 1): (0, .0, .16),
    (1, 0, -1): (.12, .0, -.11),  (0, 0, -1): (0, .0, -.13),
    (0, -1, 0): (0, -.1, .0),     (1, -1, 0): (.14, -.09, .0),
    (1, -1, 1): (.13, -.1, .15),  (0, -1, 1): (0, -.11, .17),
    (1, -1, -1): (.11, -.06, -.1), (0, -1, -1): (0, -.07, -.1),
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
for s in (1, -1):
    loft([(s * .1, 1.74, .27), (s * .115, 1.83, .29)], [.026, 0], 3, 'tusk', ref=(0, 0, 1), rot=math.pi / 2, cap0=False)
    loft([(s * .13, 1.9, .02), (s * .3, 1.95, .04), (s * .42, 2.08, .16), (s * .44, 2.22, .26)],
         [.06, .048, .03, 0], 3, 'horn', ref=(0, 1, 0), rot=math.pi / 2, cap0=False)

# arms: fat upper arm, a heavier forearm with a cannon strapped along the top, a big fist
PART[0] = 'arm'
FISTS = []
for A in (ARM_L, ARM_R):
    sh, el, wr = map(np.array, A)
    s = np.sign(sh[0])
    sd = 'L' if s > 0 else 'R'
    BONE[0] = 'upper_arm_' + sd
    loft([sh - (s * .08, 0, 0), (sh + el) / 2, el + (s * .03, 0, 0)], [(.17, .16), (.16, .155), (.13, .13)], 6, 'skin',
         ref=(0, 1, 0), rot=math.pi / 6, cap0='dome', cap1=False, smooth=.8)
    BONE[0] = 'forearm_' + sd
    loft([el - (s * .02, 0, 0), el + (wr - el) * .45, wr], [(.13, .13), (.155, .15), (.11, .11)], 6, 'skin',
         ref=(0, 1, 0), rot=math.pi / 6, cap0=False, cap1=False, smooth=.8)
    PART[0] = 'cannon'
    loft([el + (s * .06, .2, 0), wr + (s * .24, .2, 0)], [.085, .085], 6, 'metal', ref=(0, 1, 0), rot=math.pi / 6,
         cap0=True, cap1=False, smooth=.7)                                  # barrel (open muzzle)
    loft([wr + (s * .18, .2, 0), wr + (s * .25, .2, 0)], [.11, .11], 6, 'metal', ref=(0, 1, 0), rot=math.pi / 6,
         cap0=True, cap1=True, smooth=.6)                                   # muzzle ring
    cube(el + (wr - el) * .5 + (0, .13, 0), (.13, .04, .07), 'metal')      # breech block on the forearm
    PART[0] = 'hand'
    BONE[0] = 'fist_' + sd
    FIST = (wr - (s * .02, 0, 0), wr + (s * .16, 0, 0))
    FISTS.append(FIST)
    loft(list(FIST), [(.12, .1), (.115, .1)], 4, 'skin', ref=(0, 0, 1), rot=math.pi / 4, smooth=.2, cap0=False, cap1='dome')
    PART[0] = 'arm'
MOUNT_POS = (FISTS[1][0] + FISTS[1][1]) / 2
MOUNT_DIR = nrm(FISTS[1][1] - FISTS[1][0])

# legs: short and fat, cloven hooves
PART[0] = 'leg'
for s in (1, -1):
    sd = 'L' if s > 0 else 'R'
    hp, kn, an = map(np.array, LEG[s])
    BONE[0] = 'thigh_' + sd
    loft([hp + (0, .05, 0), kn], [(.19, .18), (.15, .15)], 6, 'skin', ref=(0, 0, 1), rot=math.pi / 6,
         cap0=False, cap1='dome', smooth=.8)
    BONE[0] = 'calf_' + sd
    loft([kn + (0, .02, 0), an + (0, .02, .01)], [(.14, .15), (.1, .1)], 6, 'skin', ref=(0, 0, 1), rot=math.pi / 6,
         cap0=False, cap1=False, smooth=.8)
    BONE[0] = 'hoof_' + sd
    loft([an + (0, .04, .01), (an[0], 0, an[2] + .05)], [(.11, .11), (.14, .17)], 4, 'hoof', ref=(0, 0, 1),
         rot=math.pi / 4, cap0=False, cap1=True, smooth=.2)
P = np.array(V); TP = np.array(VT)

# ---------------------------------------------------------------- skeleton (rigid parts, pivots at joints)
# Deadbird's bone names and hierarchy, at this body's own joints
SKEL = [('pelvis', None, (0, HIP_Y, 0)), ('torso', 'pelvis', (0, .87, .04)), ('neck', 'torso', (0, 1.45, 0)),
        ('head', 'neck', (0, 1.72, .06))]
for sd in ('L', 'R'):
    A = ARM_L if sd == 'L' else ARM_R
    SKEL += [('upper_arm_' + sd, 'torso', A[0]), ('forearm_' + sd, 'upper_arm_' + sd, A[1]), ('fist_' + sd, 'forearm_' + sd, A[2])]
for sd, sg in (('L', 1), ('R', -1)):
    SKEL += [('thigh_' + sd, 'pelvis', LEG[sg][0]), ('calf_' + sd, 'thigh_' + sd, LEG[sg][1]),
             ('hoof_' + sd, 'calf_' + sd, LEG[sg][2])]
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
    # the face gets the most texels
    f['boost'] = {'torso': 1.0, 'neck': 1.1, 'head': 2.6, 'arm': 1.0, 'cannon': 1.2, 'hand': 1.1, 'leg': .9}.get(f['part'], 1.0)


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

# ---------------------------------------------------------------- procedural albedo (demon brute)
def hsh(*a):
    x = np.zeros_like(a[0], dtype=float)
    for i, v in enumerate(a):
        x = x + v * (12.9898 + 31.7 * i)
    return np.modf(np.abs(np.sin(x) * 43758.5453))[0]


def vnoise(p, f, seed=0.0):
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
edge = np.minimum(np.minimum(la, fw - la), np.minimum(lb, fh - lb))
mott = vnoise(pos, 11.0)
mott2 = vnoise(pos, 34.0, 5.0)
fine = vnoise(pos, 9.0, 9.0)                                                # long, wandering lines


def is_(name):
    return M == MATS.index(name)


HIDE = C(122, 34, 24)             # dark blood-red hide
HIDE_D = C(52, 12, 10)
BELLY = C(176, 96, 70)            # pale, stretched gut
EMBER = C(255, 110, 20)

# ---- hide: mottled, darker on the back and in the creases, a pale bloated gut, glowing fissures
m = is_('skin')
pm, Xm, Ym, Zm = part[m], X[m], Y[m], Z[m]
c = HIDE[None] * (.78 + .4 * mott[m])[:, None] * (1 + .08 * noise[m])[:, None]
c = c * (1 - .35 * np.clip(-Zm / .3, 0, 1))[:, None]                        # darker down the back
gut = ((pm == 'torso') & (Ym < 1.3)) & (Zm > .1)
bt = np.clip((Zm - .1) / .3, 0, 1) * np.clip((1.3 - Ym) / .25, 0, 1) * np.clip(1.2 - AX[m] / .35, 0, 1)
c = c * (1 - bt)[:, None] + BELLY[None] * (bt * (.85 + .3 * mott2[m]))[:, None]
c[gut & (np.abs(np.modf((Ym + .02 * np.sin(Xm * 30)) / .055)[0] - .5) < .04) & (bt > .3)] *= .78   # stretch marks
nav = (pm == 'torso') & (np.hypot(Xm, (Ym - 1.0) * 1.3) < .022) & (Zm > .3)
c[nav] = HIDE_D
# neck rolls: deep creases between the rolls (the loft's narrow rings), shine on the rolls
for cy, w in ((1.56, .022), (1.69, .02)):
    cr = (pm == 'neck') & (np.abs(Ym - cy) < w)
    c[cr] = HIDE_D * (.8 + .4 * mott[m][cr, None])
c[(pm == 'torso') & (Ym > 1.42)] *= .8                                       # where the rolls hang over the shoulders
# glowing fissures across the gut and shoulders (hellfire under the skin)
fis = (np.abs(fine[m] - .5) < .03) & gut & (bt > .35) & (mott2[m] > .45)
emi = np.zeros((m.sum(), 3)); emi[fis] = EMBER * .85
c[fis] = C(60, 12, 4)
# knuckles and fingers on the fists; dark nails
fm = pm == 'hand'
c[fm & (np.abs(np.modf(lb[m] / .05)[0] - .5) > .42)] *= .55
c[fm & (la[m] > fw[m] * .8)] = C(30, 20, 18)
# the face: deep-set burning eyes under a heavy brow, nostrils, a wide mouth with an underbite
hm = (pm == 'head')
for sx in (1, -1):
    d = np.hypot((Xm - sx * .065) / .03, (Ym - 1.865) / .016)
    sock = hm & (Zm > .1) & (d < 1)
    c[sock] = C(20, 4, 2)
    eye = hm & (Zm > .1) & (d < .55)
    emi[eye] = C(255, 200, 40) * (1.2 - d[eye, None]) + C(255, 255, 200) * np.clip(.2 - d[eye], 0, 1)[:, None] * 4
c[hm & (Zm > .08) & (np.abs(Ym - 1.888) < .012) & (AX[m] < .12)] *= .55      # brow ridge
for sx in (1, -1):
    c[hm & (Zm > .14) & (np.hypot(Xm - sx * .022, Ym - 1.82) < .012)] = C(20, 4, 2)   # nostrils
mouth = hm & (Zm > .1) & (np.abs(Ym - (1.765 + .012 * (AX[m] / .12) ** 2)) < .01) & (AX[m] < .12)
c[mouth] = C(24, 4, 4)
teeth = hm & (Zm > .1) & (np.abs(Ym - 1.778) < .009) & (AX[m] < .1) & (np.modf((Xm + 1) / .018)[0] > .3)
c[teeth] = C(206, 186, 140)
c[hm & (Zm < 0) & (Ym > 1.86)] *= .75                                          # the back of the skull
alb[m] = np.clip(c, 0, 1); emis[m] = emi; spec[m] = .35; gloss[m] = 12

# ---- horns: ridged, black at the root fading to bone at the tips
m = is_('horn') | is_('tusk')
t_ = np.clip((Y[m] - 1.9) / .3, 0, 1)
c = C(36, 28, 26)[None] * (1 - t_)[:, None] + C(196, 178, 140)[None] * t_[:, None]
c[is_('tusk')[m]] = C(210, 194, 150)
c *= (.8 + .3 * (np.abs(np.modf(lb[m] / .03)[0] - .5) < .38))[:, None]
alb[m] = np.clip(c, 0, 1); spec[m] = .4; gloss[m] = 18

# ---- cannons: gunmetal barrels with brass bands, rivets, scorch at the muzzle, a hot glowing bore
m = is_('metal')
pm, Xm, Ym, Zm = part[m], X[m], Y[m], Z[m]
c = C(62, 62, 68)[None] * (.8 + .4 * mott[m])[:, None] * (1 + .1 * noise[m])[:, None]
ax = AX[m]
band_ = (np.abs(np.modf(ax / .11)[0] - .5) < .07) & (ax < 1.35)
c[band_] = C(178, 134, 60) * (.8 + .3 * mott2[m][band_, None])
riv = (np.abs(np.modf(ax / .055)[0] - .5) < .1) & (np.abs(Ym - 1.62) < .012)
c[riv] = C(200, 200, 205)
scorch = np.clip((ax - 1.36) / .18, 0, 1)
c *= (1 - .6 * scorch)[:, None]
r_ = np.hypot(Ym - 1.69, Zm)
bore = (ax > 1.53) & (r_ < .075)                                              # the muzzle's face
c[bore] = C(20, 6, 2)
emi = np.zeros((m.sum(), 3))
emi[bore] = EMBER * np.clip(1.2 - r_[bore] / .075, 0, 1)[:, None] + C(255, 230, 120) * np.clip(.35 - r_[bore] / .075, 0, 1)[:, None] * 2
c[(edge[m] < .006)] *= 1.35
alb[m] = np.clip(c, 0, 1); emis[m] = emi; spec[m] = .85; gloss[m] = 34

# ---- hooves: black keratin, split at the front
m = is_('hoof')
c = C(30, 24, 24)[None] * (.8 + .4 * mott[m])[:, None]
c[(np.abs(X[m] - np.sign(X[m]) * .27) < .008) & (Z[m] > .1)] = C(8, 6, 6)
c *= (.8 + .3 * (np.abs(np.modf(Y[m] / .025)[0] - .5) < .4))[:, None]
alb[m] = np.clip(c, 0, 1); spec[m] = .5; gloss[m] = 20

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

with open(f'{OUT}/brute.tim', 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09))                         # ID, 8bpp + CLUT
    fh.write(struct.pack('<IHHHH', 12 + 512, 0, 494, 256, 1))        # CLUT at VRAM (0,494)
    fh.write(struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TEX * TEX, 768, 256, TEX // 2, TEX))  # image at VRAM (768,256)
    fh.write(idx.tobytes())

rgb = np.array([[(w & 31), (w >> 5) & 31, (w >> 10) & 31] for w in words])
rgb8 = (rgb * 8 + rgb // 4).astype(np.uint8)
out_rgb = rgb8[idx]
Image.fromarray(out_rgb).save(f'{OUT}/brute_texture.png')
Image.fromarray(out_rgb).resize((1024, 1024), Image.NEAREST).save(f'{OUT}/brute_texture_x4.png')

# ---------------------------------------------------------------- mesh exports (rigid parts)
import shutil
FX = 4096 * .25          # PS1 fixed point: 1 model unit = 1024 (4096 = 1.0 GTE unit, model scaled x0.25)
order = sorted(range(len(F)), key=lambda i: BIDX[F[i]['bone']])        # prims grouped by bone
vorder = sorted(range(len(P)), key=lambda i: (VBONE[i], i))            # verts grouped by bone
vremap = {old: new for new, old in enumerate(vorder)}
MOUNT_LOCAL = MOUNT_POS - BPIV[BIDX['fist_R']]

with open(f'{OUT}/brute.mtl', 'w') as fh:
    fh.write('newmtl brute\nKd 1 1 1\nillum 0\nmap_Kd brute_texture.png\n')


def write_obj(path, bones, local):
    with open(path, 'w') as fh:
        fh.write(f'# PS1 brute rigid parts - {"local space (origin = part pivot)" if local else "model space"}\nmtllib brute.mtl\n')
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
            fh.write('usemtl brute\ns off\n')
            for f in fs:
                fh.write('f ' + ' '.join(f'{vbase + loc[vi]}/{tbase + nt + k}' for k, vi in enumerate(f['v'])) + '\n')
                nt += len(f['v'])
            vbase += len(vs); tbase += nt


write_obj(f'{OUT}/brute.obj', BNAMES, local=False)
os.makedirs(f'{OUT}/parts', exist_ok=True)
for b in BNAMES:
    write_obj(f'{OUT}/parts/{b}.obj', [b], local=True)
shutil.copy(f'{OUT}/brute.mtl', f'{OUT}/parts/brute.mtl')

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
png = open(f'{OUT}/brute_texture.png', 'rb').read()
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
nodes.append(dict(name='brute_mesh', mesh=0, skin=0))
gltf = dict(
    asset=dict(version='2.0', generator='ps1 brute build.py'),
    extensionsUsed=['KHR_materials_unlit'],
    scene=0, scenes=[dict(nodes=[0, mesh_node])],
    nodes=nodes,
    skins=[dict(joints=list(range(len(SKEL))), skeleton=0, inverseBindMatrices=5)],
    meshes=[dict(name='brute', primitives=[dict(attributes=dict(POSITION=0, TEXCOORD_0=1, JOINTS_0=2, WEIGHTS_0=3),
                                                 indices=4, material=0)])],
    materials=[dict(name='brute', pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=1),
                    extensions=dict(KHR_materials_unlit={}))],
    textures=[dict(source=0, sampler=0)],
    samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)],
    images=[dict(bufferView=6, mimeType='image/png', name='brute_texture')],
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
with open(f'{OUT}/brute_rigged.glb', 'wb') as fh:
    fh.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binbuf)))
    fh.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    fh.write(struct.pack('<II', len(binbuf), 0x004E4942) + binbuf)

# ---- PS1 C header: bone table + per-bone vertex/prim ranges, vertices local to their bone pivot
def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


with open(f'{OUT}/brute_mesh.h', 'w') as fh:
    fh.write(f'/* PS1 brute, rigid hierarchical parts: {len(P)} verts, {len(F)} prims ({ntri} tris), {len(SKEL)} bones.\n'
             f'   Fixed point: 1 model unit = {int(FX)} (4096 = 1.0), Y down, faces +Z.\n'
             f'   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.\n'
             f'   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\n')
    fh.write('typedef struct { short x, y, z, pad; } DVERT;\n')
    fh.write('typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n')
    fh.write('typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n')
    fh.write('#endif\n')
    fh.write(f'#define BRUTE_NVERTS {len(P)}\n#define BRUTE_NPRIMS {len(F)}\n#define BRUTE_NBONES {len(SKEL)}\n')
    for i, n in enumerate(BNAMES):
        fh.write(f'#define BRUTE_BONE_{n.upper()} {i}\n')
    ml = fx(MOUNT_LOCAL); md = MOUNT_DIR * 4096
    fh.write(f'/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */\n')
    fh.write(f'#define BRUTE_MOUNT_BONE BRUTE_BONE_FIST_R\n#define BRUTE_MOUNT_X {ml[0]}\n#define BRUTE_MOUNT_Y {ml[1]}\n#define BRUTE_MOUNT_Z {ml[2]}\n')
    fh.write(f'#define BRUTE_MOUNT_DX {int(round(md[0]))}\n#define BRUTE_MOUNT_DY {int(round(-md[1]))}\n#define BRUTE_MOUNT_DZ {int(round(md[2]))}\n')
    fh.write('#ifndef BRUTE_MESH_NO_DATA\n')
    fh.write('static const DVERT brute_verts[] = {\n')
    for i in vorder:
        x, y, z = fx(P[i] - BPIV[VBONE[i]])
        fh.write(f'  {{{x},{y},{z},0}}, /* {BNAMES[VBONE[i]]} */\n')
    fh.write('};\nstatic const DPRIM brute_prims[] = {\n')
    for i in order:
        f = F[i]
        v = [vremap[k] for k in f['v']]; uv = list(f['uv'])
        if len(v) == 4:  # PS1 quad vertex order is Z: 0,1,3,2
            v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
        v += [0] * (4 - len(v)); uv += [(0, 0)] * (4 - len(uv))
        fh.write(f"  {{{len(f['v'])},{{{','.join(map(str, v))}}},{{{','.join(str(a) for a, _ in uv)}}},{{{','.join(str(b) for _, b in uv)}}}}},\n")
    fh.write('};\nstatic const DBONE brute_bones[] = {\n')
    for i, n in enumerate(BNAMES):
        vs = [k for k, old in enumerate(vorder) if VBONE[old] == i]
        ps = [k for k, old in enumerate(order) if F[old]['bone'] == n]
        off = BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0)
        ox, oy, oz = fx(off)
        fh.write(f'  {{"{n}", {BPAR[i]}, {ox}, {oy}, {oz}, {vs[0]}, {len(vs)}, {ps[0]}, {len(ps)}}},\n')
    fh.write('};\n#endif /* BRUTE_MESH_NO_DATA */\n')
print('done')

# ---- self-contained 3D viewer page (WebGL, PS1 look: 240p, vertex snapping, affine UVs, walk cycle)
import base64
tpl = open(os.path.join(ROOT, 'tools', 'viewer_template.html')).read()
b64 = base64.b64encode(open(f'{OUT}/brute_texture.png', 'rb').read()).decode()
page = (tpl.replace('<title>PS1 Skull Demon</title>', '<title>PS1 Demon Brute</title>')
           .replace('<b>SKULL DEMON</b>', '<b>DEMON BRUTE</b>')
           .replace('dist=3.4', 'dist=4.6').replace('trans(0,-1.0,0)', 'trans(0,-1.1,0)')
           .replace('__MESH__', open(f'{OUT}/mesh.json').read())
           .replace('__TEX__', 'data:image/png;base64,' + b64))
open(f'{OUT}/brute_viewer.html', 'w').write(page)
print('viewer:', f'{OUT}/brute_viewer.html', len(page), 'bytes')
