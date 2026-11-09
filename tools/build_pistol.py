#!/usr/bin/env python3
"""Sci-fi laser pistol: a very low-poly separate model that attaches to the trooper's right hand.

Seven boxes (slide, emitter, muzzle cap, grip, trigger guard, energy cell, top rail) with the
hidden faces dropped. Texture 128x128 8bpp, one unique texel area per face (true planar UVs),
painted panels / grip texture / a glowing blue energy cell and emitter, baked key + fill + rim
light with box-shadows and AO, PS1 4x4 dither into a 255-colour CLUT.
TIM: image at VRAM (320,256), CLUT (0,495).  Writes assets/pistol_mesh.h + assets/pistol.tim.

Frame: the mount in the middle of the trooper's right fist (SOLDIER_MOUNT_*). a = down the arm
past the knuckles (the barrel points this way), up = back of the hand, sd = across.
"""
import math, os, struct
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TW, TH, VX, VY, CLUT_Y = 128, 128, 320, 256, 495
FX = 1024


def nrm(v):
    v = np.asarray(v, float); n = np.linalg.norm(v, axis=-1, keepdims=True)
    return v / np.where(n < 1e-12, 1, n)


# mount axes in model space (Y up), from soldier_mesh.h
import re
hdr = open(os.path.join(ROOT, 'assets', 'soldier_mesh.h')).read()
md = [int(re.search(rf'#define SOLDIER_MOUNT_D{c} (-?\d+)', hdr).group(1)) for c in 'XYZ']
D = nrm([md[0], -md[1], md[2]])
Y = np.array([0., 1., 0.])
S = nrm(np.cross(Y, D))


def M(a, up, sd):
    return a * D + up * Y + sd * S


FACES, BOXES = [], []


def box(lo, hi, mat, skip=()):
    (a0, u0, s0), (a1, u1, s1) = lo, hi
    BOXES.append((np.array(lo, float), np.array(hi, float)))
    bi = len(BOXES) - 1
    c = M((a0 + a1) / 2, (u0 + u1) / 2, (s0 + s1) / 2)
    sides = {'a-': [(a0, u0, s0), (a0, u1, s0), (a0, u1, s1), (a0, u0, s1)],
             'a+': [(a1, u0, s0), (a1, u1, s0), (a1, u1, s1), (a1, u0, s1)],
             'u-': [(a0, u0, s0), (a1, u0, s0), (a1, u0, s1), (a0, u0, s1)],
             'u+': [(a0, u1, s0), (a1, u1, s0), (a1, u1, s1), (a0, u1, s1)],
             's-': [(a0, u0, s0), (a1, u0, s0), (a1, u1, s0), (a0, u1, s0)],
             's+': [(a0, u0, s1), (a1, u0, s1), (a1, u1, s1), (a0, u1, s1)]}
    for k, q in sides.items():
        if k in skip:
            continue
        P = [M(*v) for v in q]
        n = np.cross(P[1] - P[0], P[2] - P[0]) + np.cross(P[2] - P[0], P[3] - P[0])
        if np.dot(n, sum(P) / 4 - c) < 0:
            P = P[::-1]
        fn = nrm(np.cross(P[1] - P[0], P[2] - P[0]) + np.cross(P[2] - P[0], P[3] - P[0]))
        FACES.append(dict(p=P, fn=fn, mat=mat, box=bi, side=k))


# slide / body over the fist, emitter + glowing muzzle, grip down through the fist,
# trigger guard, energy cell on the side, top rail with sights
box((-0.045, 0.030, -0.017), (0.160, 0.078, 0.017), 'body')
box((0.160, 0.040, -0.013), (0.200, 0.070, 0.013), 'emitter', skip=('a-',))
box((0.200, 0.046, -0.009), (0.206, 0.064, 0.009), 'muzzle', skip=('a-',))
box((-0.040, -0.085, -0.015), (0.012, 0.030, 0.015), 'grip', skip=('u+',))
box((0.012, -0.004, -0.006), (0.060, 0.004, 0.006), 'guard', skip=('a-',))
box((0.020, 0.040, 0.017), (0.110, 0.068, 0.024), 'cell', skip=('s-',))
box((-0.010, 0.078, -0.006), (0.130, 0.088, 0.006), 'rail', skip=('u-',))
MUZZLE = M(0.206, 0.055, 0.0)

# ---------------------------------------------------------------- planar UV atlas
for f in FACES:
    P = f['p']
    e1 = nrm(P[1] - P[0]); e2 = nrm(np.cross(f['fn'], e1))
    q = np.array([[np.dot(p - P[0], e1), np.dot(p - P[0], e2)] for p in P])
    f['e1'], f['e2'], f['q'], f['lo'] = e1, e2, q, q.min(0)
    f['ext'] = q.max(0) - q.min(0)


def pack(d):
    x = y = rowh = 0; pos = {}
    for i in sorted(range(len(FACES)), key=lambda i: -FACES[i]['ext'][1]):
        w = max(2, int(math.ceil(FACES[i]['ext'][0] * d))) + 3
        h = max(2, int(math.ceil(FACES[i]['ext'][1] * d))) + 3
        if x + w > TW:
            x = 0; y += rowh; rowh = 0
        pos[i] = (x, y); x += w; rowh = max(rowh, h)
    return pos, y + rowh


d = 1400.0
while True:
    pos, used_h = pack(d)
    if used_h <= TH:
        break
    d *= 0.97
print(f'pistol: {len(FACES)} faces, {d:.0f} texels/m')
for i, f in enumerate(FACES):
    f['dens'] = d; f['ox'], f['oy'] = pos[i][0] + 1, pos[i][1] + 1
    f['uv'] = [(int(round(f['ox'] + (qq[0] - f['lo'][0]) * d)), int(round(f['oy'] + (qq[1] - f['lo'][1]) * d))) for qq in f['q']]

# ---------------------------------------------------------------- paint + light
C = lambda *c: np.array(c, float) / 255
LOCAL = np.stack([D, Y, S])
KEY = nrm(M(-0.2, 0.85, -0.5)); FILL = nrm(M(0.4, -0.3, 0.7)); RIM = nrm(M(0.8, 0.2, 0.4)); VIEW = nrm(M(-0.3, 0.3, -1))


def hit_box(o, dvec, skip, tmax):
    hit = np.zeros(len(o), bool)
    for bi, (lo, hi) in enumerate(BOXES):
        if bi == skip:
            continue
        with np.errstate(divide='ignore', invalid='ignore'):
            inv = 1.0 / np.where(np.abs(dvec) < 1e-9, 1e-9, dvec)
            t0 = (lo - o) * inv; t1 = (hi - o) * inv
        tn = np.minimum(t0, t1).max(1); tx = np.maximum(t0, t1).min(1)
        hit |= (tx >= np.maximum(tn, 1e-5)) & (tn < tmax)
    return hit


rng = np.random.default_rng(5)
HEMI = nrm(rng.normal(size=(10, 3)))
img = np.zeros((TH, TW, 3)); used = np.zeros((TH, TW), bool)
for f in FACES:
    w = int(math.ceil(f['ext'][0] * d)) + 2; h = int(math.ceil(f['ext'][1] * d)) + 2
    xs, ys = np.meshgrid(np.arange(-1, w), np.arange(-1, h))
    tx = (f['ox'] + xs).ravel(); ty = (f['oy'] + ys).ravel()
    ok = (tx >= 0) & (tx < TW) & (ty >= 0) & (ty < TH); tx, ty = tx[ok], ty[ok]
    qx = (tx - f['ox'] + .5) / d + f['lo'][0]; qy = (ty - f['oy'] + .5) / d + f['lo'][1]
    P = f['p'][0] + qx[:, None] * f['e1'] + qy[:, None] * f['e2']
    loc = P @ LOCAL.T
    a, up, sd = loc[:, 0], loc[:, 1], loc[:, 2]
    ex, ey = f['ext']
    edge = np.minimum(np.minimum(qx - f['lo'][0], f['lo'][0] + ex - qx), np.minimum(qy - f['lo'][1], f['lo'][1] + ey - qy))
    n = len(P)
    mat = f['mat']
    em = np.zeros((n, 3)); metal = .6; shin = 24.0
    if mat in ('body', 'rail', 'guard'):
        col = np.tile(C(58, 62, 72), (n, 1))
        if mat == 'body':
            panel = (up > .05) & (a > .0) & (a < .14) & (np.abs(sd) > .016)            # white side panel
            col[panel] = C(196, 200, 204)
            col[panel & (np.abs(up - .051) < .0025)] = C(36, 140, 210)               # blue trim line
            vent = (up > .066) & (a > .03) & (a < .13) & (np.abs(np.modf(a / .012)[0] - .5) > .3) & (np.abs(sd) < .016)
            col[vent] = C(18, 20, 24)
            col[(np.abs(a - .14) < .003)] = C(30, 32, 38)                           # seam to the emitter
        if mat == 'rail':
            col[np.abs(np.modf(a / .01)[0] - .5) > .3] *= .6                         # rail teeth
            col[(a > .12)] = C(220, 80, 40)                                          # front sight dot
        col[edge < .0025] *= 1.35                                                    # worn edges
    elif mat == 'grip':
        col = np.tile(C(34, 34, 38), (n, 1))
        knurl = (np.abs(np.modf((a + up) / .008)[0] - .5) < .15) | (np.abs(np.modf((a - up) / .008)[0] - .5) < .15)
        col[knurl] = C(52, 52, 58)
        col[up < -.075] = C(70, 74, 82)                                              # base plate
        metal = .2; shin = 10
    elif mat == 'emitter':
        col = np.tile(C(70, 74, 84), (n, 1))
        rings = np.abs(np.modf(a / .012)[0] - .5) < .2
        col[rings] = C(20, 70, 110)
        em[rings] = C(30, 140, 255) * .55
    elif mat == 'muzzle':
        col = np.tile(C(10, 40, 70), (n, 1))
        r = np.hypot(up - .055, sd)
        em[:] = C(80, 190, 255) * np.clip(1.2 - r / .012, .3, 1.2)[:, None]
        em[r < .004] = C(220, 250, 255)
    elif mat == 'cell':
        col = np.tile(C(14, 40, 70), (n, 1))
        glow = np.clip(1 - np.abs(up - .054) / .016, 0, 1)
        em[:] = C(40, 160, 255) * (.45 + .55 * glow)[:, None]
        em[np.abs(np.modf(a / .018)[0] - .5) > .44] *= .3                         # cell dividers
        em[edge < .0025] *= .2
    nn = np.tile(f['fn'], (n, 1))
    o = loc + (nn @ LOCAL.T) * 1e-4
    sh = hit_box(o, np.tile(KEY @ LOCAL.T, (n, 1)), f['box'], 1.0)
    ao = np.zeros(n)
    for hd in HEMI:
        dl = np.tile(hd @ LOCAL.T, (n, 1)); ln = (nn @ LOCAL.T)
        dl[(dl * ln).sum(1) < 0] *= -1
        ao += hit_box(o, dl, f['box'], .03)
    ao = 1 - .55 * ao / len(HEMI)
    ndl = np.clip(nn @ KEY, 0, 1) * np.where(sh, .25, 1)
    diff = .32 * ao + .9 * ndl + .3 * np.clip(nn @ FILL, 0, 1) * ao + .4 * np.clip(nn @ RIM, 0, 1) ** 2
    spec = np.clip(nn @ nrm(KEY + VIEW), 0, 1) ** shin * np.where(sh, 0, 1) * (.3 + metal)
    c = col * diff[:, None] + spec[:, None] * (.5 * col + .5) + em
    img[ty, tx] = np.clip(c, 0, 1); used[ty, tx] = True

# ---------------------------------------------------------------- PS1 colour + exports
DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float)
c8 = img * 255
src = Image.fromarray(np.clip(c8, 0, 255).astype(np.uint8)[used][None])
pal = np.array(src.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).getpalette()[:765], float).reshape(-1, 3)
pal = np.clip(np.round(pal / 8), 0, 31); pal8 = pal * 8 + pal // 4
yy, xx = np.mgrid[0:TH, 0:TW]
tgt = c8 + (DM[yy % 4, xx % 4] * 2.2)[..., None]
idx = np.zeros((TH, TW), np.uint8)
idx[used] = ((tgt[used][:, None, :] - pal8[None]) ** 2).sum(2).argmin(1) + 1
words = [0] + [int(r) | (int(g) << 5) | (int(b) << 10) | 0x8000 for r, g, b in pal]
words += [0] * (256 - len(words))
with open(os.path.join(ROOT, 'assets', 'pistol.tim'), 'wb') as fh:
    fh.write(struct.pack('<II', 0x10, 0x09) + struct.pack('<IHHHH', 12 + 512, 0, CLUT_Y, 256, 1) + struct.pack('<256H', *words))
    fh.write(struct.pack('<IHHHH', 12 + TW * TH, VX, VY, TW // 2, TH) + idx.tobytes())
w8 = (np.array(words)[:, None] >> np.array([0, 5, 10])) & 31
prev = (w8 * 8 + w8 // 4).astype(np.uint8)[idx]; prev[idx == 0] = (40, 0, 40)
Image.fromarray(prev).resize((TW * 4, TH * 4), Image.NEAREST).save(os.path.join(ROOT, 'docs', 'pistol_texture.png'))


def fx(p):
    return int(round(p[0] * FX)), int(round(-p[1] * FX)), int(round(p[2] * FX))


verts, key = [], {}
prims = []
for f in FACES:
    v = []
    for p in f['p']:
        k = fx(p)
        if k not in key:
            key[k] = len(verts); verts.append(k)
        v.append(key[k])
    uv = [(u, t + VY % 256) for u, t in f['uv']]
    prims.append(([v[0], v[1], v[3], v[2]], [uv[0], uv[1], uv[3], uv[2]]))
mz = fx(MUZZLE)
with open(os.path.join(ROOT, 'assets', 'pistol_mesh.h'), 'w') as fh:
    fh.write('/* sci-fi laser pistol (tools/build_pistol.py): vertices local to the trooper\'s right-hand mount\n'
             '   (SOLDIER_MOUNT_*), same axes as the soldier model. One bone. */\n')
    fh.write('#ifndef DMODEL_TYPES_DEFINED\n#define DMODEL_TYPES_DEFINED\ntypedef struct { short x, y, z, pad; } DVERT;\n'
             'typedef struct { unsigned char n; unsigned short v[4]; unsigned char u[4], t[4]; } DPRIM;\n'
             'typedef struct { const char *name; short parent; short ox, oy, oz; unsigned short v0, vn, p0, pn; } DBONE;\n#endif\n')
    fh.write(f'#define PISTOL_NVERTS {len(verts)}\n#define PISTOL_NPRIMS {len(prims)}\n')
    fh.write(f'#define PISTOL_MUZZLE_X {mz[0]}\n#define PISTOL_MUZZLE_Y {mz[1]}\n#define PISTOL_MUZZLE_Z {mz[2]}\n')
    fh.write('static const DVERT pistol_verts[] = {\n' + ''.join(f'  {{{x},{y},{z},0}},\n' for x, y, z in verts) + '};\n')
    fh.write('static const DPRIM pistol_prims[] = {\n')
    for v, uv in prims:
        fh.write(f"  {{4,{{{','.join(map(str, v))}}},{{{','.join(str(u) for u, _ in uv)}}},{{{','.join(str(t) for _, t in uv)}}}}},\n")
    fh.write('};\n')
    fh.write(f'static const DBONE pistol_bones[] = {{ {{"pistol", -1, 0, 0, 0, 0, {len(verts)}, 0, {len(prims)}}} }};\n')
print(f'{len(prims)} quads, {len(verts)} verts')
