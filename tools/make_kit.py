#!/usr/bin/env python3
"""Deadbird UI kit (assets/src/kit_tex.png, 256x256 RGBA): the HUD and the menus' / editor's
panels and buttons, painted in the same forged-iron, bone and molten style as the touch controls
(tools/make_ui.py). The game re-quantises every cell to 16 colours (game/assets.c, KCELLS).

  HUD (also on the PS1; placed in the character page's spare rows and the strip):
  (0,0)    128x20  health bar frame: horned bone sigil, iron rails, thorns, end spike, notched trough
  (0,20)   90x8    blood fill (drawn cropped to the health)
  (90,20)  16x8    shine (additive, sweeps along the fill)
  (106,20) 22x8    lightning bolt (additive)
  (0,28)   16x4    the sigil's eye glow (additive)
  (128,0)  20x10   keycard slot;  (128,10) 16x6 keycard (grey: tinted to the key's colour)
  Menus / editor (Windows and Android): 9-slice cells, every corner painted
  (0,32)   32x32  panel                (32,32)  24x24 button          (56,32) 24x24 button, lit
  (80,32)  24x24  button, primary      (104,32) 24x24 bar plate       (128,32) 24x16 selected row
  (152,32) 16x16  inset field          (168,32) 16x16 selection frame (184,32) 24x24 plaque (toasts)
"""
import os, math
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, SS = 256, 256, 4
RGBA = np.zeros((H * SS, W * SS, 4))


def grid(w, h):
    ys, xs = np.mgrid[0:h * SS, 0:w * SS]
    return (xs + 0.5) / SS, (ys + 0.5) / SS


def put(x0, y0, col, a):
    h, w = a.shape
    dst = RGBA[y0 * SS:y0 * SS + h, x0 * SS:x0 * SS + w]
    al = np.clip(a, 0, 1)[..., None]
    dst[..., :3] = dst[..., :3] * (1 - al) + col * al
    dst[..., 3] = np.maximum(dst[..., 3], al[..., 0])


def C(*c):
    return np.array(c, float) / 255


def noise2(x, y, f, seed=0):
    r = np.random.default_rng(seed)
    g = r.uniform(0, 1, (64, 64))
    xi, yi = (x * f).astype(int) % 64, (y * f).astype(int) % 64
    fx, fy = (x * f) % 1, (y * f) % 1
    a = g[yi, xi]; b = g[yi, (xi + 1) % 64]; c = g[(yi + 1) % 64, xi]; d = g[(yi + 1) % 64, (xi + 1) % 64]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def lerp(a, b, t):
    t = np.asarray(t, float)
    return a * (1 - t[..., None]) + b * t[..., None]


MOLTEN = [C(60, 4, 2), C(200, 24, 6), C(255, 110, 20), C(255, 220, 130)]


def molten(t):
    t = np.clip(np.asarray(t, float), 0, 1) * 3
    i = np.minimum(t.astype(int), 2); f = t - i
    out = np.zeros(t.shape + (3,))
    for k in range(3):
        m = i == k
        out[m] = MOLTEN[k] * (1 - f[m, None]) + MOLTEN[k + 1] * f[m, None]
    return out


def iron(x, y, lam, base=C(52, 40, 44)):
    grain = 0.78 + 0.45 * noise2(x, y, 0.9, 1) * noise2(x, y, 0.33, 2)
    return base * (np.clip(lam, 0.15, 1.6) * grain)[..., None]


def bone(x, y, lam):
    g = 0.85 + 0.25 * noise2(x, y, 0.7, 7)
    return lerp(C(70, 50, 40), C(232, 210, 178), np.clip(lam * g, 0, 1))


def seg_d(x, y, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    t = np.clip(((x - ax) * vx + (y - ay) * vy) / max(vx * vx + vy * vy, 1e-9), 0, 1)
    return np.hypot(x - ax - t * vx, y - ay - t * vy)


def tri_mask(x, y, p):
    (ax, ay), (bx, by), (cx, cy) = p
    def s(px, py, qx, qy): return (x - qx) * (py - qy) - (px - qx) * (y - qy)
    d1, d2, d3 = s(ax, ay, bx, by), s(bx, by, cx, cy), s(cx, cy, ax, ay)
    return ~(((d1 < 0) | (d2 < 0) | (d3 < 0)) & ((d1 > 0) | (d2 > 0) | (d3 > 0)))


def box_d(x, y, x0, y0, x1, y1):
    """distance inside a box to its nearest edge (negative outside)"""
    return np.minimum(np.minimum(x - x0, x1 - x), np.minimum(y - y0, y1 - y))


# ================================================================ HUD
# ---- health bar frame (0,0) 128x20
x, y = grid(128, 20)
col = np.zeros(x.shape + (3,)); cov = np.zeros_like(x)
BX0, BX1, BY0, BY1 = 19.0, 116.0, 4.0, 18.0        # the bar's iron body
TX0, TX1, TY0, TY1 = 24.0, 114.0, 7.0, 15.0        # the trough (the fill goes here, 90x8)
body = (x > BX0) & (x < BX1) & (y > BY0) & (y < BY1)
d = box_d(x, y, BX0, BY0, BX1, BY1)
lam = np.where(y < (BY0 + BY1) / 2, 1.15, 0.55) * np.where(d < 1.0, 0.35, 1.0)
col = np.where(body[..., None], iron(x, y, lam), col)
cov = np.maximum(cov, body)
# bone bevel along the top rail, molten seam just above the trough
rail = body & (y > BY0 + 1) & (y < BY0 + 2.2) & (x > BX0 + 1) & (x < BX1 - 1)
col[rail] = bone(x, y, np.full_like(x, 0.95))[rail]
seam = body & (np.abs(y - (TY0 - 0.9)) < 0.45) & (x > TX0 - 1) & (x < TX1 + 1)
col[seam] = molten(np.full_like(x, 0.55))[seam]
seam2 = body & (np.abs(y - (TY1 + 0.9)) < 0.45) & (x > TX0 - 1) & (x < TX1 + 1)
col[seam2] = molten(np.full_like(x, 0.32))[seam2]
# rivets on the rails
for rx in range(30, 112, 14):
    for ry in (BY0 + 1.6, BY1 - 1.5):
        m = np.hypot(x - rx, y - ry) < 0.9
        col[m & body] = C(170, 150, 130)
# the trough: near-black blood, faint circuit lines, notches every 9 texels
tr = (x > TX0) & (x < TX1) & (y > TY0) & (y < TY1)
tcol = lerp(C(10, 2, 3), C(34, 6, 8), np.clip((y - TY0) / 8, 0, 1))
circ = ((np.abs(((x - TX0) % 9) - 4.5) < 0.3) & (np.abs(y - 11) < 1.2)) | (np.abs(y - 11) < 0.25)
tcol[circ] = C(48, 10, 10)
notch = (((x - TX0) % 9) < 0.8) & (x > TX0 + 1)
tcol[notch] = C(2, 0, 0)
col[tr] = tcol[tr]
inner_sh = tr & (y < TY0 + 1)
col[inner_sh] = C(0, 0, 0)
# thorns: bone hooks up off the top rail and down off the bottom
for k, tx in enumerate(range(30, 112, 12)):
    up = tri_mask(x, y, ((tx - 2.6, BY0 + 0.5), (tx + 2.0, BY0 + 0.5), (tx + 1.6, BY0 - 3.6)))
    col[up] = bone(x, y, 0.55 + 0.5 * np.clip((BY0 + 1 - y) / 4, 0, 1))[up]; cov = np.maximum(cov, up)
    if k % 2 == 0:
        dn = tri_mask(x, y, ((tx + 3.5, BY1 - 0.5), (tx + 8.5, BY1 - 0.5), (tx + 7.0, BY1 + 1.9)))
        col[dn] = bone(x, y, np.full_like(x, 0.4))[dn]; cov = np.maximum(cov, dn)
# end spike: iron blade with a bone edge pointing right
sp = tri_mask(x, y, ((BX1 - 1, BY0 + 0.5), (BX1 - 1, BY1 - 0.5), (127.5, 11.0)))
col[sp] = iron(x, y, np.where(y < 11, 1.3, 0.5))[sp]
spe = sp & (np.abs(y - 11 + (x - BX1) * 0.0) < 0.6)
col[spe] = molten(np.clip((x - BX1) / 12, 0, 1) * 0.5 + 0.35)[spe]
cov = np.maximum(cov, sp)
# black outline round everything so far
o = cov > 0
edge = np.zeros_like(o)
for ddx, ddy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
    edge |= np.roll(np.roll(o, ddx * SS, 1), ddy * SS, 0)
edge &= ~o
col[edge] = C(6, 2, 2); cov = np.maximum(cov, edge)
# the sigil: a horned bone diamond round a dark socket (the eye glow is drawn over it)
cx, cy = 11.0, 11.0
dd = np.abs(x - cx) + np.abs(y - cy)
for hs in (-1, 1):   # horns: from the diamond's upper sides, sweeping out and curling up
    hm = np.zeros_like(o); hl = np.zeros_like(x)
    for k in range(24):
        t = k / 23
        px = cx + hs * (3.5 + 5.5 * t - 1.5 * t * t)
        py = cy - 4.5 - 7.0 * t + 1.0 * math.sin(t * 3.1)
        m = np.hypot(x - px, y - py) < 1.9 * (1 - t) ** 0.8 + 0.35
        hm |= m
        hl = np.where(m, np.maximum(hl, 0.45 + 0.6 * t), hl)
    hm &= y > 0
    col[hm] = bone(x, y, hl)[hm]; cov = np.maximum(cov, hm)
dia = dd < 9.8
col[dia & (dd >= 9.0)] = C(6, 2, 2)
inner = dd < 9.0
col[inner] = bone(x, y, np.where((x - cx) + (y - cy) < 0, 1.0, 0.45))[inner]
ring = (dd < 6.6) & (dd > 5.7)
col[ring] = molten(np.full_like(x, 0.45))[ring]
sock = dd < 5.7
col[sock] = lerp(C(30, 4, 6), C(6, 0, 1), np.clip(dd / 5.7, 0, 1))[sock]
eye = (np.abs(x - cx) / 4.5) ** 2 + (np.abs(y - cy) / 1.7) ** 2 < 1
col[eye] = C(90, 14, 6)
pup = eye & (np.abs(x - cx) < 0.55)
col[pup] = C(0, 0, 0)
cov = np.maximum(cov, dia)
put(0, 0, col, cov)

# ---- blood fill (0,20) 90x8: wet highlight on top, deep crimson below, veins, notches
x, y = grid(90, 8)
t = y / 8
col = lerp(C(255, 96, 40), C(96, 4, 8), np.clip(t * 1.25, 0, 1))
col = np.where((y < 1.2)[..., None], lerp(C(255, 200, 140), C(255, 120, 60), y / 1.2), col)
vein = noise2(x, y, 0.6, 21)
col = col * (0.82 + 0.3 * vein)[..., None]
bub = (noise2(x, y, 1.3, 23) > 0.86) & (y > 2) & (y < 6)
col[bub] = C(255, 150, 90)
notch = ((x % 9) < 0.8) & (x > 1)
col[notch] = col[notch] * 0.35
col[y > 7.2] = C(60, 0, 4)
put(0, 20, np.clip(col, 0, 1), np.ones_like(x))

# ---- shine (90,20) 16x8: a slanted band of hot light (additive: black adds nothing)
x, y = grid(16, 8)
u = (x - 8) + (y - 4) * 0.7
g = np.clip(1 - np.abs(u) / 6.5, 0, 1) ** 1.6
col = lerp(C(0, 0, 0), C(255, 236, 190), g)
put(90, 20, col, (g > 0.04).astype(float))

# ---- lightning bolt (106,20) 22x8
x, y = grid(22, 8)
pts = [(21.5, 0.5), (15.5, 3.2), (17.0, 4.0), (10.0, 6.0), (11.5, 3.0), (5.0, 4.8), (6.0, 6.2), (0.5, 7.5)]
d = np.full_like(x, 99.0)
for (ax, ay), (bx, by) in zip(pts, pts[1:]):
    d = np.minimum(d, seg_d(x, y, ax, ay, bx, by))
d = np.minimum(d, seg_d(x, y, 10.0, 6.0, 7.5, 7.8) + 0.25)
core = np.clip(1.2 - d / 0.75, 0, 1); halo = np.clip(1 - d / 2.2, 0, 1) * 0.55
g = np.maximum(core, halo)
col = lerp(lerp(C(0, 0, 0), C(255, 120, 40), halo * 2), C(255, 250, 220), core)
put(106, 20, col, (g > 0.08).astype(float))

# ---- eye glow (0,28) 16x4
x, y = grid(16, 4)
e = np.clip(1 - ((x - 8) / 7.5) ** 2 - ((y - 2) / 2.0) ** 2, 0, 1)
col = lerp(C(0, 0, 0), C(255, 140, 40), e) + lerp(C(0, 0, 0), C(255, 240, 160), np.clip(e * 2 - 1, 0, 1)) * 0.5
put(0, 28, np.clip(col, 0, 1), (e > 0.03).astype(float))

# ---- keycard slot (128,0) 20x10: slanted bone frame round a dark socket
x, y = grid(20, 10)
sx = x - (10 - y) * 0.3                              # slant
outer = (sx > 1) & (sx < 18) & (y > 0.5) & (y < 9.5)
innerm = (sx > 3) & (sx < 16) & (y > 2.5) & (y < 7.5)
col = bone(x, y, np.where(y < 5, 0.95, 0.5))
col[innerm] = lerp(C(14, 4, 6), C(4, 1, 2), (y[innerm] - 2.5) / 5)
col[outer & ~innerm & ((np.abs(sx - 1.4) < 0.5) | (np.abs(sx - 17.6) < 0.5) | (y < 1) | (y > 9))] = C(8, 3, 3)
put(128, 0, col, outer.astype(float))
# ---- keycard (128,10) 16x6 (grey, tinted)
x, y = grid(16, 6)
sx = x - (6 - y) * 0.3
card = (sx > 1) & (sx < 14.5) & (y > 0.3) & (y < 5.7)
lum = 0.55 + 0.35 * (1 - y / 6) + 0.12 * ((np.abs(sx - 5) < 0.6) | (np.abs(y - 3) < 0.4))
lum[(sx > 9) & (sx < 12.5) & (y > 1.5) & (y < 4.5)] = 0.95
col = np.repeat(np.clip(lum, 0, 1)[..., None] * 0.75, 3, -1)
put(128, 10, col, card.astype(float))


# ================================================================ 9-slice cells
def plate9(x0, y0, w, h, face_top, face_bot, rim, rim_hi, seam, corner_bone=False, hot_top=False, rivets=True, inset=False):
    x, y = grid(w, h)
    d = box_d(x, y, 0, 0, w, h)
    col = np.zeros(x.shape + (3,))
    # outline, bevelled iron rim (lit top-left), seam line, face
    lam = np.where((x + y) < (w + h) / 2, 1.25, 0.55)
    if inset: lam = 2.0 - lam
    col[:] = lerp(face_top, face_bot, np.clip(y / h, 0, 1))
    col = col * (0.92 + 0.12 * noise2(x, y, 0.5, 31))[..., None]
    r = (d < 3.0)
    col[r] = (iron(x, y, lam, rim) if not inset else iron(x, y, lam, rim))[r]
    hi = (d >= 0.9) & (d < 1.6) & ((y < h / 2) if not inset else (y > h / 2))
    col[hi] = rim_hi
    s = (d >= 3.0) & (d < 3.6)
    col[s] = seam
    col[d < 0.9] = C(6, 2, 2)
    if hot_top:
        ht = (y >= 3.0) & (y < 3.9) & (d >= 3.0)
        col[ht] = C(255, 140, 50)
    if corner_bone:   # bone brackets with a rivet on every corner
        for cx0, cy0, sxn, syn in ((0, 0, 1, 1), (w, 0, -1, 1), (0, h, 1, -1), (w, h, -1, -1)):
            lx, ly = (x - cx0) * sxn, (y - cy0) * syn
            br = ((lx < 8.5) & (ly < 2.6) & (lx > 0.8) & (ly > 0.8)) | ((ly < 8.5) & (lx < 2.6) & (lx > 0.8) & (ly > 0.8))
            col[br] = bone(x, y, np.where(ly < 1.7, 1.0, 0.55))[br]
            spike = tri_mask(lx, ly, ((1.0, 1.0), (6.5, 2.2), (2.2, 6.5)))
            col[spike] = bone(x, y, np.full_like(x, 0.85))[spike]
            rv = np.hypot(lx - 4.2, ly - 4.2) < 0.9
            col[rv] = C(250, 230, 200)
    elif rivets:
        for cx0, cy0 in ((2, 2), (w - 2, 2), (2, h - 2), (w - 2, h - 2)):
            rv = np.hypot(x - cx0, y - cy0) < 0.75
            col[rv] = C(170, 150, 130)
    put(x0, y0, np.clip(col, 0, 1), np.ones_like(x))


plate9(0, 32, 32, 32, C(40, 11, 9), C(10, 3, 5), C(60, 44, 46), C(150, 120, 100), C(200, 50, 14), corner_bone=True, hot_top=True)
plate9(32, 32, 24, 24, C(44, 16, 14), C(18, 6, 7), C(56, 42, 44), C(130, 100, 90), C(120, 24, 10))
# lit button: the face burns
x, y = grid(24, 24)
plate9(56, 32, 24, 24, C(255, 150, 50), C(150, 30, 8), C(90, 60, 50), C(255, 220, 160), C(255, 210, 120))
plate9(80, 32, 24, 24, C(200, 54, 16), C(90, 14, 6), C(70, 50, 48), C(220, 180, 140), C(255, 150, 60), corner_bone=True)
plate9(104, 32, 24, 24, C(48, 18, 14), C(22, 7, 7), C(64, 48, 50), C(160, 130, 110), C(210, 64, 20), hot_top=False)
# selected row (128,32) 24x16: a molten bar with chevron ends
x, y = grid(24, 16)
t = np.clip(1 - np.abs(y - 8) / 8, 0, 1)
col = molten(0.12 + 0.36 * t)
col[(y < 1.0) | (y > 15.0)] = C(255, 170, 80)
ch = (np.abs(x - 2.5) + np.abs(y - 8) * 0.5 < 2.2) | (np.abs(x - 21.5) + np.abs(y - 8) * 0.5 < 2.2)
col[ch] = C(255, 240, 200)
put(128, 32, col, np.ones_like(x))
plate9(152, 32, 16, 16, C(6, 2, 3), C(16, 5, 6), C(40, 30, 32), C(150, 120, 100), C(70, 16, 8), rivets=False, inset=True)
# selection frame (168,32) 16x16: glowing rim, clear centre
x, y = grid(16, 16)
d = box_d(x, y, 0, 0, 16, 16)
fr = d < 3
col = np.zeros(x.shape + (3,))
col[:] = molten(np.clip(0.95 - d / 3, 0, 1))
col[d < 0.8] = C(255, 250, 220)
put(168, 32, col, fr.astype(float))
plate9(184, 32, 24, 24, C(26, 8, 8), C(8, 2, 3), C(150, 120, 100), C(235, 215, 185), C(170, 40, 12), rivets=True)

# ---------------------------------------------------------------- write
img = RGBA.reshape(H, SS, W, SS, 4).mean((1, 3))
a = img[..., 3]
rgb = np.clip(img[..., :3] / np.maximum(a[..., None], 1e-6), 0, 1)
out = np.dstack([np.round(rgb * 255), np.where(a > 0.45, 255, 0)]).astype(np.uint8)
Image.fromarray(out, 'RGBA').save(os.path.join(ROOT, 'assets', 'src', 'kit_tex.png'))
prev = Image.fromarray(out).resize((W * 3, H * 3), Image.NEAREST)
os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
prev.save(os.path.join(ROOT, 'docs', 'kit_atlas.png'))
print('kit ok')
