"""PS1-style sci-fi armoured trooper (future enemy): box-modelled on Deadbird's skeleton, baked/dithered
8bpp CLUT texture, TIM export. Same pipeline as tools/build_demon.py (shared code copied from it)."""
import numpy as np, json, struct, math, os
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'out', 'soldier')
os.makedirs(OUT, exist_ok=True)
TEX = 256

V, VT, F = [], [], []          # positions, per-vertex param t, faces
MATS = ['suit', 'plate', 'helmet', 'visor', 'joint', 'tech', 'glove', 'boot']


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
# A sci-fi armoured trooper on the same skeleton as Deadbird (same bone names and joint layout,
# so the same animations drive him): fabric undersuit, hard plates bolted over it, a sealed
# helmet with a wrap-around visor, a power pack on the back. +X = his left.
S2 = math.sqrt(2)
PART = ['torso']
BONE = ['pelvis']


def band(y0, y1, r0, r1, n, mat, z0=0.0, z1=0.0, rot=0.0, cap0=False, cap1=False, smooth=.5):
    """a ring of plates around the vertical axis (open unless capped)"""
    loft([(0, y0, z0), (0, y1, z1)], [r0, r1], n, mat, ref=(0, 0, 1), rot=rot, cap0=cap0, cap1=cap1, smooth=smooth)


def cube(c, h, mat, smooth=0.0, skip=()):
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
            if (ax, sg) in skip: continue            # a side pressed against the body: never seen
            face(q, mat, c, smooth=smooth)


# pelvis + armoured belt
loft([(0, .88, 0), (0, 1.10, .005)], [(.15, .11), (.15, .11)], 6, 'suit',
     ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=False)
band(.955, 1.045, (.182, .132), (.178, .13), 6, 'plate', rot=math.pi / 6)
cube((0, .93, .118), (.055, .045, .018), 'plate', skip=((2, -1),))                          # groin plate

# torso: undersuit, chest plate shell, ab plates, collar, power pack
BONE[0] = 'torso'
loft([(0, 1.08, 0), (0, 1.30, .005), (0, 1.53, -.005)], [(.15, .11), (.19, .13), (.13, .095)],
     6, 'suit', ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=False)
loft([(0, 1.24, .012), (0, 1.37, .018), (0, 1.49, .005)], [(.208, .152), (.228, .166), (.214, .138)], 6, 'plate',
     ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=False, smooth=.6)
band(1.11, 1.22, (.168, .128), (.185, .14), 6, 'plate', z0=.004, z1=.01, rot=math.pi / 6)
band(1.49, 1.555, (.15, .12), (.13, .105), 6, 'plate', rot=math.pi / 6)
cube((0, 1.33, -.205), (.14, .16, .06), 'tech', skip=((2, 1),))                             # power pack
for s in (1, -1):                                                           # coolant canisters
    loft([(s * .155, 1.2, -.21), (s * .155, 1.46, -.21)], [.04, .04], 4, 'tech', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=True)

# neck seal
PART[0] = 'neck'
BONE[0] = 'neck'
loft([(0, 1.49, -.005), (0, 1.64, .0)], [(.07, .068)] * 2, 6, 'joint', ref=(0, 0, 1), cap0=False, cap1=False, smooth=.6)

# helmet, wrap-around visor, rebreather, comms pods, antenna, crest
PART[0] = 'head'
BONE[0] = 'head'
HC = np.array((0, 1.72, .012))
loft([(0, 1.605, .012), (0, 1.72, .014), (0, 1.84, -.006)],
     [(.104, .114), (.128, .14), (.096, .106)], 6, 'helmet', ref=(0, 0, 1), rot=math.pi / 6,
     cap0=False, cap1='dome', smooth=.7)
VIS_Y0, VIS_Y1, VIS_R = 1.693, 1.757, (.137, .151)
vis = []
NA = 4
for k in range(NA + 1):
    a = math.radians(-62 + 124 * k / NA)
    x, z = math.sin(a) * VIS_R[0], math.cos(a) * VIS_R[1] + .014
    vis.append((addv((x, VIS_Y0, z)), addv((x, VIS_Y1, z - .006))))
for k in range(NA):
    face([vis[k][0], vis[k + 1][0], vis[k + 1][1], vis[k][1]], 'visor', HC, smooth=.6)
cube((0, 1.645, .118), (.055, .032, .03), 'helmet', skip=((2, -1),))                        # rebreather
for s in (1, -1):
    loft([(s * .118, 1.725, -.01), (s * .152, 1.725, -.01)], [.04, .04], 4, 'tech', ref=(0, 1, 0), rot=math.pi / 4, cap0=False, cap1=True)
cube((0, 1.862, -.02), (.016, .018, .085), 'plate')                         # crest

# arms (T-pose along X): undersuit, layered pauldron, forearm guard, armoured gauntlet
ARM_L = [(.205, 1.475, 0), (.48, 1.475, -.01), (.75, 1.475, 0)]
ARM_R = [(-x, y, z) for x, y, z in ARM_L]
FISTS = []
for A in (ARM_L, ARM_R):
    sh, el, wr = map(np.array, A)
    s = np.sign(sh[0])
    sd = 'L' if s > 0 else 'R'
    PART[0] = 'arm'
    BONE[0] = 'upper_arm_' + sd
    loft([sh, el + (s * .02, 0, 0)], [(.068, .066), .055], 4, 'suit', ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=False)
    loft([sh + (s * -.035, .03, 0), sh + (s * .06, .04, 0), sh + (s * .15, .02, 0)],
         [(.105, .112), (.118, .122), (.09, .1)], 6, 'plate', ref=(0, 1, 0), rot=math.pi / 6, cap0=False, cap1='dome')
    BONE[0] = 'forearm_' + sd
    loft([el - (s * .02, 0, 0), wr - (s * .005, 0, 0)], [(.066, .068), (.058, .058)], 6, 'plate', ref=(0, 1, 0),
         rot=math.pi / 6, cap0=False, cap1=False)
    PART[0] = 'hand'
    BONE[0] = 'fist_' + sd
    FIST = (wr - (s * .01, 0, 0), wr + (s * .085, 0, 0))
    FISTS.append(FIST)
    loft(list(FIST), [(.048, .042), (.05, .044)], 4, 'glove', ref=(0, 0, 1), rot=math.pi / 4, smooth=0, cap0=False, cap1='dome')
MOUNT_POS = (FISTS[1][0] + FISTS[1][1]) / 2
MOUNT_DIR = nrm(FISTS[1][1] - FISTS[1][0])

# legs: undersuit, thigh plate, knee cop, shin guard, armoured boot
PART[0] = 'leg'
for s in (1, -1):
    sd = 'L' if s > 0 else 'R'
    BONE[0] = 'thigh_' + sd
    loft([(s * .09, .97, 0), (s * .135, .55, .03)], [.092, .064], 4, 'suit',
         ref=(0, 0, 1), rot=math.pi / 4, cap0=False, cap1=False)
    loft([(s * .1, .93, .012), (s * .125, .66, .022)], [(.102, .1), (.092, .09)], 6, 'plate', ref=(0, 0, 1),
         rot=math.pi / 6, cap0=False, cap1=False)
    BONE[0] = 'calf_' + sd
    loft([(s * .136, .56, .04), (s * .149, .17, .02)], [(.074, .076), (.062, .064)], 6, 'plate', ref=(0, 0, 1),
         rot=math.pi / 6, cap0=False, cap1=False)
    cube((s * .136, .555, .098), (.05, .055, .03), 'plate', skip=((2, -1),))   # knee cop
    BONE[0] = 'hoof_' + sd
    loft([(s * .15, .2, .015), (s * .152, .07, .04), (s * .152, 0.0, .07)], [(.062, .07), (.072, .12), (.074, .132)],
         6, 'boot', ref=(0, 0, 1), rot=math.pi / 6, cap0=False, cap1=True)
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
assert ntri <= 720, ntri

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
    f['boost'] = {'torso': 1.35, 'neck': 1.0, 'head': 2.3, 'arm': 1.2, 'hand': 1.3, 'leg': 1.0}.get(f['part'], 1.0)


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

# ---------------------------------------------------------------- procedural albedo (sci-fi trooper)
def hsh(*a):
    x = np.zeros_like(a[0], dtype=float)
    for i, v in enumerate(a):
        x = x + v * (12.9898 + 31.7 * i)
    return np.modf(np.abs(np.sin(x) * 43758.5453))[0]


def vnoise(p, f, seed=0.0):
    """smooth value noise on world positions (grime, wear)"""
    q = p * f
    i = np.floor(q); t = q - i; t = t * t * (3 - 2 * t)
    out = 0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (t[:, 0] if dx else 1 - t[:, 0]) * (t[:, 1] if dy else 1 - t[:, 1]) * (t[:, 2] if dz else 1 - t[:, 2])
                out = out + w * hsh(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz + seed)
    return out


def stencil(txt, u, v, u0, u1, v0, v1):
    """sample a text mask over [u0,u1]x[v0,v1] (any 2D frame)"""
    from PIL import ImageDraw, ImageFont
    fnt = ImageFont.load_default()
    im = Image.new('L', (len(txt) * 7 + 4, 14))
    ImageDraw.Draw(im).text((1, 0), txt, font=fnt, fill=255)
    im = im.crop(im.getbbox())
    mk = np.array(im, float) / 255
    uu = (u - u0) / (u1 - u0); vv = 1 - (v - v0) / (v1 - v0)
    ins = (uu >= 0) & (uu < 1) & (vv >= 0) & (vv < 1)
    iu = np.clip((uu * mk.shape[1]).astype(int), 0, mk.shape[1] - 1); iv = np.clip((vv * mk.shape[0]).astype(int), 0, mk.shape[0] - 1)
    return np.where(ins, mk[iv, iu], 0) > .45


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


def is_(name):
    return M == MATS.index(name)


PLATE = C(176, 178, 172)          # off-white armour
PLATE_D = C(96, 100, 104)
ORANGE = C(236, 112, 26)
SUIT = C(46, 48, 56)
STEEL = C(200, 204, 210)          # bare metal under chipped paint

# ---- armour plates: panel seams, bolts, chipped edges, grime, orange markings, stencils
m = is_('plate') | is_('helmet')
c = PLATE[None] * (0.86 + 0.22 * grime[m])[:, None] * (1 + .06 * noise[m])[:, None]
pl, pb = la[m], lb[m]
seam = (np.abs(np.modf(pl / .075)[0] - .5) > .47) | (np.abs(np.modf(pb / .09)[0] - .5) > .47)
c[seam] = PLATE_D * .7
bolt = (np.hypot(np.modf(pl / .075)[0] - .1, np.modf(pb / .09)[0] - .12) < .07)
c[bolt] = C(120, 124, 128)
c[bolt & (np.modf(pb / .09)[0] < .1)] = C(230, 232, 236)
chip = (edge[m] < .009) & (scuff[m] > .55)
c[chip] = STEEL * (.8 + .3 * noise[m][chip, None])
c *= (1 - .38 * np.clip(grime2[m] - .55, 0, 1) * 2.2)[:, None]               # dirt in patches
c *= (1 - .3 * np.clip((1.0 - pos[m, 1]) * 1.4, 0, 1))[:, None]             # lower legs dirtier
pm, Xm, Ym, Zm = part[m], X[m], Y[m], Z[m]
# orange markings: helmet centre stripe, pauldron rims + chevrons, shin stripes, chest bar
stripe = (pm == 'head') & (np.abs(Xm) < .016) & (Ym > 1.78)
arm_ = pm == 'arm'
d_sh = np.hypot(np.abs(Xm) - .27, Ym - 1.51)
stripe |= arm_ & (Ym > 1.46) & (np.abs(np.modf((np.abs(Xm) - .2) / .045)[0] - .5) < .12) & (np.abs(Xm) < .33) & (Zm > -.02)
stripe |= (pm == 'leg') & (Ym < .5) & (Ym > .25) & (np.abs(np.abs(Xm) - .148) < .012) & (Zm > .04)
stripe |= (pm == 'torso') & (np.abs(Ym - 1.255) < .011) & (np.abs(Xm) > .05) & (Zm > .05)
c[stripe] = ORANGE * (.85 + .3 * grime[m][stripe, None])
# stencils: chest unit number, left pauldron call sign, helmet side
st = (pm == 'torso') & (Zm > .05) & stencil('07', Xm, Ym, -.15, -.07, 1.33, 1.40)
st |= (pm == 'torso') & (Zm > .05) & stencil('SEC', Xm, Ym, .06, .16, 1.34, 1.385)
st |= arm_ & (Xm > .2) & stencil('A-7', -Zm, Ym, -.075, .075, 1.505, 1.555)
st |= (pm == 'head') & (np.abs(Xm) > .09) & stencil('SEC-7', -Zm * np.sign(Xm), Ym, -.08, .05, 1.795, 1.82)
c[st] = C(26, 26, 30)
# the helmet: darker brow above the visor, vent slots on the rebreather
hm = pm == 'head'
q = pos[m] - HC
c[hm & (Ym > 1.755) & (Ym < 1.785) & (Zm > .07)] *= .7
vent = hm & (Zm > .1) & (Ym < 1.675) & (np.abs(np.modf(Ym / .011)[0] - .5) > .3) & (np.abs(Xm) < .045)
c[vent] = C(20, 20, 24)
alb[m] = np.clip(c, 0, 1); spec[m] = .65; gloss[m] = 26

# ---- undersuit: ribbed dark fabric, stitched seams, wear at the joints
m = is_('suit')
rib = .82 + .18 * (np.abs(np.sin(lb[m] * 420)) > .5)
c = SUIT[None] * rib[:, None] * (.9 + .25 * grime[m])[:, None]
c[edge[m] < .006] *= 1.35                                                  # seam tape
c[(edge[m] < .009) & (edge[m] > .006) & (np.modf(la[m] / .012)[0] < .5)] = C(90, 92, 98)  # stitching
alb[m] = c; spec[m] = .12; gloss[m] = 8

# ---- rubber joints: accordion ribs
m = is_('joint')
r_ = np.abs(np.modf((la[m] + lb[m]) / .016)[0] - .5)
c = np.tile(C(28, 28, 32), (m.sum(), 1)) * (.7 + .8 * r_)[:, None]
alb[m] = c; spec[m] = .3; gloss[m] = 12

# ---- visor: glowing cyan glass with a horizon reflection, scanlines and a hot streak
m = is_('visor')
v = (Y[m] - VIS_Y0) / (VIS_Y1 - VIS_Y0)
glass = C(4, 30, 40)
alb[m] = glass
e = np.zeros((m.sum(), 3))
e[:] = C(20, 190, 230) * (.45 + .55 * v)[:, None]
e[v > .55] = e[v > .55] * .7 + C(160, 240, 255) * .3                        # reflected sky band
e[(np.abs(np.modf(Y[m] / .0045)[0] - .5) > .38)] *= .65                     # scanlines
streak = np.abs((X[m] * 1.4 + (Y[m] - 1.72) * 2.2) - .03) < .012
e[streak] = C(235, 255, 255)
e[np.abs(X[m]) > .1] *= .6                                                  # wraps away into shade
emis[m] = e; spec[m] = 1.0; gloss[m] = 60

# ---- tech: power pack, canisters, comms pods, filters, antenna
m = is_('tech')
c = np.tile(C(62, 64, 70), (m.sum(), 1)) * (.85 + .3 * grime[m])[:, None]
grille = (np.abs(np.modf(la[m] / .014)[0] - .5) > .3) & (lb[m] > .05) & (lb[m] < .2) & (la[m] > .04)
c[grille] *= .35
hazard = (np.modf((la[m] + lb[m]) / .03)[0] < .5) & (lb[m] < .035)
c[hazard] = ORANGE * .9
c[(lb[m] < .035) & ~hazard] = C(24, 24, 26)
can = (np.abs(AX[m] - .155) < .05) & (Z[m] < -.15)                          # canisters: glowing coolant bands
band_ = can & (np.abs(np.modf(Y[m] / .06)[0] - .5) < .1)
c[band_] = C(10, 40, 50)
emis_t = np.zeros((m.sum(), 3))
emis_t[band_] = C(30, 200, 230) * .8
led = (hsh(np.floor(la[m] / .012), np.floor(lb[m] / .012), fid[m]) > .965) & ~can
emis_t[led] = np.where((hsh(fid[m][led] * 1.0, fid[m][led] * 2.0) > .5)[:, None], C(255, 40, 30)[None], C(60, 255, 90)[None])
alb[m] = c; emis[m] = emis_t; spec[m] = .55; gloss[m] = 22

# ---- gloves: dark with armoured knuckles
m = is_('glove')
c = np.tile(C(40, 40, 46), (m.sum(), 1)) * (.9 + .2 * grime[m])[:, None]
kn = la[m] > fw[m] * .55
c[kn] = PLATE_D * (.9 + .2 * noise[m][kn, None])
c[kn & (np.abs(np.modf(lb[m] / .022)[0] - .5) > .42)] *= .5
alb[m] = c; spec[m] = .35; gloss[m] = 14

# ---- boots: armoured toe and shin cap, rubber sole with tread, mud
m = is_('boot')
c = np.tile(C(52, 52, 58), (m.sum(), 1)) * (.85 + .3 * grime[m])[:, None]
sole = Y[m] < .028
c[sole] = C(18, 18, 20) * (.7 + .6 * (np.abs(np.modf(Z[m][sole] / .018)[0] - .5) > .25))[:, None]
toe = (Z[m] > .08) & ~sole
c[toe] = PLATE_D * (.9 + .2 * noise[m][toe, None])
c *= (1 - .45 * np.clip(.09 - Y[m], 0, 1) / .09 * (grime2[m] > .45))[:, None]  # mud
alb[m] = c; spec[m] = .35; gloss[m] = 14

for name in ('plate', 'helmet', 'suit', 'joint', 'visor', 'tech', 'glove', 'boot'):
    assert (alb[is_(name)].sum() > 0) or name == 'visor', name

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

with open(f'{OUT}/soldier.tim', 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09))                         # ID, 8bpp + CLUT
    fh.write(struct.pack('<IHHHH', 12 + 512, 0, 494, 256, 1))        # CLUT at VRAM (0,494)
    fh.write(struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TEX * TEX, 768, 256, TEX // 2, TEX))  # image at VRAM (768,256)
    fh.write(idx.tobytes())

rgb = np.array([[(w & 31), (w >> 5) & 31, (w >> 10) & 31] for w in words])
rgb8 = (rgb * 8 + rgb // 4).astype(np.uint8)
out_rgb = rgb8[idx]
Image.fromarray(out_rgb).save(f'{OUT}/soldier_texture.png')
Image.fromarray(out_rgb).resize((1024, 1024), Image.NEAREST).save(f'{OUT}/soldier_texture_x4.png')

# ---------------------------------------------------------------- mesh exports (rigid parts)
import shutil
FX = 4096 * .25          # PS1 fixed point: 1 model unit = 1024 (4096 = 1.0 GTE unit, model scaled x0.25)
order = sorted(range(len(F)), key=lambda i: BIDX[F[i]['bone']])        # prims grouped by bone
vorder = sorted(range(len(P)), key=lambda i: (VBONE[i], i))            # verts grouped by bone
vremap = {old: new for new, old in enumerate(vorder)}
MOUNT_LOCAL = MOUNT_POS - BPIV[BIDX['fist_R']]

with open(f'{OUT}/soldier.mtl', 'w') as fh:
    fh.write('newmtl soldier\nKd 1 1 1\nillum 0\nmap_Kd soldier_texture.png\n')


def write_obj(path, bones, local):
    with open(path, 'w') as fh:
        fh.write(f'# PS1 soldier rigid parts - {"local space (origin = part pivot)" if local else "model space"}\nmtllib soldier.mtl\n')
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
            fh.write('usemtl soldier\ns off\n')
            for f in fs:
                fh.write('f ' + ' '.join(f'{vbase + loc[vi]}/{tbase + nt + k}' for k, vi in enumerate(f['v'])) + '\n')
                nt += len(f['v'])
            vbase += len(vs); tbase += nt


write_obj(f'{OUT}/soldier.obj', BNAMES, local=False)
os.makedirs(f'{OUT}/parts', exist_ok=True)
for b in BNAMES:
    write_obj(f'{OUT}/parts/{b}.obj', [b], local=True)
shutil.copy(f'{OUT}/soldier.mtl', f'{OUT}/parts/soldier.mtl')

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
png = open(f'{OUT}/soldier_texture.png', 'rb').read()
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
nodes.append(dict(name='soldier_mesh', mesh=0, skin=0))
gltf = dict(
    asset=dict(version='2.0', generator='ps1 soldier build.py'),
    extensionsUsed=['KHR_materials_unlit'],
    scene=0, scenes=[dict(nodes=[0, mesh_node])],
    nodes=nodes,
    skins=[dict(joints=list(range(len(SKEL))), skeleton=0, inverseBindMatrices=5)],
    meshes=[dict(name='soldier', primitives=[dict(attributes=dict(POSITION=0, TEXCOORD_0=1, JOINTS_0=2, WEIGHTS_0=3),
                                                 indices=4, material=0)])],
    materials=[dict(name='soldier', pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=1),
                    extensions=dict(KHR_materials_unlit={}))],
    textures=[dict(source=0, sampler=0)],
    samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)],
    images=[dict(bufferView=6, mimeType='image/png', name='soldier_texture')],
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
with open(f'{OUT}/soldier_rigged.glb', 'wb') as fh:
    fh.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binbuf)))
    fh.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
    fh.write(struct.pack('<II', len(binbuf), 0x004E4942) + binbuf)

# ---- PS1 C header: bone table + per-bone vertex/prim ranges, vertices local to their bone pivot
def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


with open(f'{OUT}/soldier_mesh.h', 'w') as fh:
    fh.write(f'/* PS1 soldier, rigid hierarchical parts: {len(P)} verts, {len(F)} prims ({ntri} tris), {len(SKEL)} bones.\n'
             f'   Fixed point: 1 model unit = {int(FX)} (4096 = 1.0), Y down, faces +Z.\n'
             f'   Vertices are LOCAL to their bone pivot; bone offsets are relative to the parent pivot.\n'
             f'   Render each bone with  world = parent_world * T(offset) * R(bone)  and draw its prim range. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\n')
    fh.write('typedef struct { short x, y, z, pad; } DVERT;\n')
    fh.write('typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n')
    fh.write('typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n')
    fh.write('#endif\n')
    fh.write(f'#define SOLDIER_NVERTS {len(P)}\n#define SOLDIER_NPRIMS {len(F)}\n#define SOLDIER_NBONES {len(SKEL)}\n')
    for i, n in enumerate(BNAMES):
        fh.write(f'#define SOLDIER_BONE_{n.upper()} {i}\n')
    ml = fx(MOUNT_LOCAL); md = MOUNT_DIR * 4096
    fh.write(f'/* right-hand weapon mount: local to bone fist_R, plus forward axis (4096 = 1.0) */\n')
    fh.write(f'#define SOLDIER_MOUNT_BONE SOLDIER_BONE_FIST_R\n#define SOLDIER_MOUNT_X {ml[0]}\n#define SOLDIER_MOUNT_Y {ml[1]}\n#define SOLDIER_MOUNT_Z {ml[2]}\n')
    fh.write(f'#define SOLDIER_MOUNT_DX {int(round(md[0]))}\n#define SOLDIER_MOUNT_DY {int(round(-md[1]))}\n#define SOLDIER_MOUNT_DZ {int(round(md[2]))}\n')
    fh.write('#ifndef SOLDIER_MESH_NO_DATA\n')
    fh.write('static const DVERT soldier_verts[] = {\n')
    for i in vorder:
        x, y, z = fx(P[i] - BPIV[VBONE[i]])
        fh.write(f'  {{{x},{y},{z},0}}, /* {BNAMES[VBONE[i]]} */\n')
    fh.write('};\nstatic const DPRIM soldier_prims[] = {\n')
    for i in order:
        f = F[i]
        v = [vremap[k] for k in f['v']]; uv = list(f['uv'])
        if len(v) == 4:  # PS1 quad vertex order is Z: 0,1,3,2
            v = [v[0], v[1], v[3], v[2]]; uv = [uv[0], uv[1], uv[3], uv[2]]
        v += [0] * (4 - len(v)); uv += [(0, 0)] * (4 - len(uv))
        fh.write(f"  {{{len(f['v'])},{{{','.join(map(str, v))}}},{{{','.join(str(a) for a, _ in uv)}}},{{{','.join(str(b) for _, b in uv)}}}}},\n")
    fh.write('};\nstatic const DBONE soldier_bones[] = {\n')
    for i, n in enumerate(BNAMES):
        vs = [k for k, old in enumerate(vorder) if VBONE[old] == i]
        ps = [k for k, old in enumerate(order) if F[old]['bone'] == n]
        off = BPIV[i] - (BPIV[BPAR[i]] if BPAR[i] >= 0 else 0)
        ox, oy, oz = fx(off)
        fh.write(f'  {{"{n}", {BPAR[i]}, {ox}, {oy}, {oz}, {vs[0]}, {len(vs)}, {ps[0]}, {len(ps)}}},\n')
    fh.write('};\n#endif /* SOLDIER_MESH_NO_DATA */\n')
print('done')
