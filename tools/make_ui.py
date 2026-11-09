#!/usr/bin/env python3
"""Deadbird UI atlas (128x256, 8bpp CLUT, PS1 dither): sci-fi hell touch controls and menu plates.

  (0,0)    64x64  stick frame: forged iron ring, horn spikes, tech notches, molten seam
  (64,0)   64x64  rune ring: glowing glyphs (drawn additive, spins)
  (0,64)   48x48  button, idle: horned iron bezel, dark circuit face, dim neon rim
  (48,64)  48x48  button, pressed: rim and face burning
  (96,64)  32x32  stick knob: a demon eye
  (0,112)  32x32  icons: jump, sprint, camera left, camera right
  (0,176)  32x32  icon: chainsaw attack
  (0,144)  32x32  icon: pause;  (32,144) 32x32 glow (additive);  (64,144) 64x64 sigil (additive)
  (0,208)  24x32  menu plate cap (idle);   (24,208) 16x32 plate middle (idle)
  (40,208) 24x32  menu plate cap (lit);    (64,208) 16x32 plate middle (lit)
  (80,208) 24x32  selector claw
Index 0 = transparent. TIM at VRAM (320,0), CLUT (0,492).
"""
import os, math, struct
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, SS = 128, 256, 4
rng = np.random.default_rng(13)
RGBA = np.zeros((H * SS, W * SS, 4))          # straight colour + coverage


def grid(x0, y0, w, h):
    ys, xs = np.mgrid[0:h * SS, 0:w * SS]
    return (xs + 0.5) / SS, (ys + 0.5) / SS      # in output pixels, local to the cell


def put(x0, y0, col, a):
    """composite a cell (col h*SS x w*SS x 3, a coverage) over the atlas"""
    h, w = a.shape
    dst = RGBA[y0 * SS:y0 * SS + h, x0 * SS:x0 * SS + w]
    al = a[..., None]
    dst[..., :3] = dst[..., :3] * (1 - al) + col * al
    dst[..., 3] = np.maximum(dst[..., 3], a)


def C(*c):
    return np.array(c, float) / 255


def noise2(x, y, f, seed=0):
    r = np.random.default_rng(seed)
    g = r.uniform(0, 1, (64, 64))
    xi, yi = (x * f).astype(int) % 64, (y * f).astype(int) % 64
    fx, fy = (x * f) % 1, (y * f) % 1
    a = g[yi, xi]; b = g[yi, (xi + 1) % 64]; c = g[(yi + 1) % 64, xi]; d = g[(yi + 1) % 64, (xi + 1) % 64]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def iron(x, y, nx, ny, base=C(46, 36, 40)):
    """beaten dark iron, lit from the top left by a bevel normal (nx, ny)"""
    lam = 0.55 + 0.6 * np.clip(-(nx * 0.6 + ny * 0.8), -1, 1)
    grain = 0.75 + 0.5 * noise2(x, y, 0.9, 1) * noise2(x, y, 0.33, 2)
    return base[None, None] * (lam * grain)[..., None]


def lerp(a, b, t):
    return a[None, None] * (1 - t[..., None]) + b[None, None] * t[..., None]


def smooth(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


MOLTEN = [C(60, 4, 2), C(200, 24, 6), C(255, 110, 20), C(255, 220, 130)]


def molten(t):
    t = np.clip(t, 0, 1) * 3
    i = np.minimum(t.astype(int), 2); f = t - i
    out = np.zeros(t.shape + (3,))
    for k in range(3):
        m = i == k
        out[m] = MOLTEN[k] * (1 - f[m, None]) + MOLTEN[k + 1] * f[m, None]
    return out


def horn(x, y, bx, by, ang, length, width):
    """coverage of a curved horn from (bx,by) toward angle ang (radians), tapering"""
    cov = np.zeros_like(x); shade = np.zeros_like(x)
    for k in range(24):
        t = k / 23
        a = ang + 0.5 * t * t
        px = bx + math.cos(a) * length * t; py = by + math.sin(a) * length * t
        r = width * (1 - t) ** 1.2 + 0.35
        d = np.hypot(x - px, y - py)
        m = d < r
        cov = np.maximum(cov, m.astype(float))
        shade = np.where(m, np.maximum(shade, 1 - d / max(r, 1e-6) * 0.6 + 0.2 * (1 - t)), shade)
    return cov, shade


# ---------------------------------------------------------------- stick frame
x, y = grid(0, 0, 64, 64)
dx, dy = x - 32, y - 32
r = np.hypot(dx, dy); ang = np.arctan2(dy, dx)
ring = (r > 24) & (r < 30)
nx, ny = dx / np.maximum(r, 1e-6), dy / np.maximum(r, 1e-6)
bev = np.where(r < 27, -1, 1)
col = iron(x, y, nx * bev, ny * bev)
notch = ((ang / (2 * np.pi) * 16) % 1 < 0.18) & (r > 27.5)
col[notch] *= 0.25
seam = np.abs(r - 24.6) < 0.7
col[seam] = molten(np.full(seam.sum(), 0.62))
rivet = np.zeros_like(r, bool)
for k in range(8):
    a = 2 * np.pi * (k + 0.5) / 8
    rivet |= np.hypot(x - 32 - 27 * math.cos(a), y - 32 - 27 * math.sin(a)) < 1.1
col[rivet] = C(150, 130, 120)
cov = ring.astype(float)
for a0 in (-3 * math.pi / 4, -math.pi / 4, math.pi / 4, 3 * math.pi / 4):
    hc, hs = horn(x, y, 32 + 28 * math.cos(a0), 32 + 28 * math.sin(a0), a0, 5.5, 2.2)
    col = np.where(hc[..., None] > 0, lerp(C(70, 56, 52), C(210, 190, 170), hs * 0.8), col)
    cov = np.maximum(cov, hc)
put(0, 0, col, cov)

# ---------------------------------------------------------------- rune ring (glyphs on a circle)
x, y = grid(64, 0, 64, 64)
dx, dy = x - 32, y - 32
cov = np.zeros_like(x); glow = np.zeros_like(x)
NG = 12
for k in range(NG):
    a = 2 * np.pi * k / NG
    gr = np.random.default_rng(100 + k)
    # each glyph: 3-4 strokes on a 3x5 grid, placed tangentially at radius 19.5
    tx, ty = -math.sin(a), math.cos(a); rx, ry = math.cos(a), math.sin(a)
    cx, cy = 32 + 19.5 * rx, 32 + 19.5 * ry
    pts = [(gx - 1, gy - 2) for gx in range(3) for gy in range(5)]
    for _ in range(gr.integers(3, 5)):
        p0, p1 = gr.choice(len(pts), 2, replace=False)
        (u0, v0), (u1, v1) = pts[p0], pts[p1]
        ax0, ay0 = cx + (u0 * tx + v0 * rx) * 1.25, cy + (u0 * ty + v0 * ry) * 1.25
        ax1, ay1 = cx + (u1 * tx + v1 * rx) * 1.25, cy + (u1 * ty + v1 * ry) * 1.25
        vx, vy = ax1 - ax0, ay1 - ay0
        t = np.clip(((x - ax0) * vx + (y - ay0) * vy) / max(vx * vx + vy * vy, 1e-6), 0, 1)
        d = np.hypot(x - ax0 - t * vx, y - ay0 - t * vy)
        cov = np.maximum(cov, (d < 0.6).astype(float))
        glow = np.maximum(glow, np.clip(1 - d / 2.0, 0, 1))
r = np.hypot(dx, dy)
circ = (np.abs(r - 23.3) < 0.45) | (np.abs(r - 15.7) < 0.45)
cov = np.maximum(cov, circ.astype(float))
col = molten(0.35 + 0.65 * cov)
a_all = np.maximum(cov, glow * 0.9)
col = col * np.maximum(cov, glow * 0.55)[..., None]
put(64, 0, col, (a_all > 0.25).astype(float))


# ---------------------------------------------------------------- buttons
def button(x0, y0, lit):
    x, y = grid(x0, y0, 48, 48)
    dx, dy = x - 24, y - 26
    # octagonal bezel
    oct_r = np.maximum(np.abs(dx), np.abs(dy)) * 0.92 + (np.abs(dx) + np.abs(dy)) * 0.29
    nxx, nyy = np.sign(dx) * 0.7, np.sign(dy) * 0.7
    bez = (oct_r < 19.5) & (oct_r > 14.5)
    face = oct_r <= 14.5
    col = np.zeros(x.shape + (3,)); cov = np.zeros_like(x)
    bevel = np.where(oct_r < 17, -1, 1)
    col = np.where(bez[..., None], iron(x, y, nxx * bevel, nyy * bevel), col)
    rim = np.abs(oct_r - 14.8) < 0.55
    rim2 = np.abs(oct_r - 18.9) < 0.4
    # face: dark glass over circuitry, or burning when pressed
    fr = np.hypot(dx, dy)
    circuit = (((x * 0.5).astype(int) + (y * 0.25).astype(int)) % 5 == 0) & ((noise2(x, y, 0.5, 9) > 0.55))
    if lit:
        fcol = molten(np.clip(1.0 - fr / 18 + 0.15 * noise2(x, y, 0.8, 4), 0, 1) * 0.85)
        fcol[circuit] = fcol[circuit] * 1.3
        col = np.where(face[..., None], np.clip(fcol, 0, 1), col)
        col[rim] = C(255, 230, 150)
        col[rim2] = C(255, 120, 30)
    else:
        fcol = lerp(C(26, 6, 8), C(8, 2, 4), np.clip(fr / 15, 0, 1))
        fcol[circuit] = C(90, 14, 10)
        col = np.where(face[..., None], fcol, col)
        col[rim] = C(170, 30, 12)
        col[rim2] = C(90, 16, 10)
    cov = (bez | face).astype(float)
    # bolts on the diagonal faces
    for bx, by in ((-12, -12), (12, -12), (-12, 12), (12, 12)):
        m = np.hypot(dx - bx, dy - by) < 1.2
        col[m] = C(170, 150, 140); cov[m] = 1
    # horns rising from the top corners
    for s in (-1, 1):
        hc, hs = horn(x, y, 24 + s * 9, 26 - 15, -math.pi / 2 - s * 0.55, 10, 2.6)
        hc[hc > 0] = hc[hc > 0]
        col = np.where(hc[..., None] > 0, lerp(C(80, 60, 52), C(235, 215, 190), hs * 0.85), col)
        cov = np.maximum(cov, hc)
    put(x0, y0, col, cov)


button(0, 64, False)
button(48, 64, True)

# ---------------------------------------------------------------- knob: demon eye
x, y = grid(96, 64, 32, 32)
dx, dy = x - 16, y - 16
r = np.hypot(dx, dy)
col = np.zeros(x.shape + (3,))
rim = (r > 11.5) & (r < 15)
col[rim] = iron(x, y, dx / np.maximum(r, 1e-6), dy / np.maximum(r, 1e-6))[rim]
iris = r <= 11.5
col[iris] = molten(np.clip(1.05 - r / 13 + 0.25 * noise2(x, y, 1.6, 7) * (r > 3), 0, 1))[iris]
streak = iris & (((np.arctan2(dy, dx) * 9 / np.pi) % 1) < 0.18) & (r > 4)
col[streak] *= 0.6
pupil = (np.abs(dx) / 1.6) ** 2 + (dy / 8.5) ** 2 < 1
col[pupil] = C(6, 2, 2)
col[np.hypot(dx + 3.5, dy + 4.5) < 1.6] = C(255, 245, 220)
put(96, 64, col, (r < 15).astype(float))

# ---------------------------------------------------------------- glow (additive)
x, y = grid(32, 144, 32, 32)
r = np.hypot(x - 16, y - 16)
g = np.clip(1 - r / 16, 0, 1) ** 2
put(32, 144, molten(g * 0.9) * g[..., None] * 1.1, (g > 0.04).astype(float))


# ---------------------------------------------------------------- icons
def icon(x0, y0, shape_fn):
    x, y = grid(x0, y0, 32, 32)
    inside = shape_fn(x - 16, y - 16)            # boolean glyph mask
    # dark outline 1.2 px around the glyph
    from scipy.ndimage import binary_dilation
    out = binary_dilation(inside, iterations=int(1.4 * SS))
    col = np.zeros(x.shape + (3,))
    col[out] = C(14, 4, 4)
    shade = np.clip(0.75 - (y - 16) / 40, 0, 1)
    col[inside] = lerp(C(255, 120, 40), C(255, 240, 210), shade)[inside]
    put(x0, y0, col, out.astype(float))


def seg_mask(x, y, pts, w):
    m = np.zeros_like(x, bool)
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        vx, vy = bx - ax, by - ay
        t = np.clip(((x - ax) * vx + (y - ay) * vy) / max(vx * vx + vy * vy, 1e-6), 0, 1)
        m |= np.hypot(x - ax - t * vx, y - ay - t * vy) < w
    return m


def jump_icon(x, y):
    m = seg_mask(x, y, [(-7, 1), (0, -6), (7, 1)], 1.6) | seg_mask(x, y, [(-7, 8), (0, 1), (7, 8)], 1.6)
    for s in (-1, 1):   # bat wing scallops
        m |= seg_mask(x, y, [(s * 4, -4), (s * 11, -9), (s * 12, -2), (s * 9, -1), (s * 8, 3)], 0.9)
    return m


def flame_icon(x, y):
    m = np.zeros_like(x, bool)
    for cx, h, w in ((0, 21, 7.0), (-6.5, 14, 4.6), (6.5, 15, 4.8)):
        t = np.clip((11 - y) / h, 0, 1)
        ww = w * np.sqrt(np.clip(1 - t, 0, 1)) * (0.6 + 0.5 * np.sin(np.pi * np.clip(t * 1.2, 0, 1)))
        m |= (np.abs(x - cx - 2.0 * np.sin(t * 3)) < ww) & (y < 12) & (y > 11 - h)
    return m


def arrow_icon(sign):
    def f(x, y):
        a = np.arctan2(y - 3, x * sign)
        r = np.hypot(x, y - 3)
        arc = (np.abs(r - 8) < 1.7) & (a < -0.2) & (a > -2.9)
        head = seg_mask(x * sign, y, [(5.5, -9.5), (8.2, -2.0)], 1.5) | seg_mask(x * sign, y, [(8.2, -2.0), (12.5, -6.5)], 1.5)
        claw = seg_mask(x * sign, y, [(-8, 3), (-10, 8)], 1.1) | seg_mask(x * sign, y, [(-6, 4), (-6.5, 9)], 1.1)
        return arc | head | claw
    return f


def pause_icon(x, y):
    m = np.zeros_like(x, bool)
    for cx in (-4.5, 4.5):
        m |= (np.abs(x - cx) < 1.6) & (np.abs(y) < 8)
        for ey in (-8, 8):
            for ex in (-1.6, 1.6):
                m |= np.hypot(x - cx - ex, y - ey) < 1.9
    return m


def saw_icon(x, y):
    """a chainsaw bar slashing diagonally, teeth along both edges, with a motion arc"""
    ax, ay, bx, by = -11.0, 10.0, 9.0, -10.0
    vx, vy = bx - ax, by - ay; L = math.hypot(vx, vy); ux, uy = vx / L, vy / L
    t = ((x - ax) * ux + (y - ay) * uy)                   # along the bar
    d = (x - ax) * -uy + (y - ay) * ux                    # across
    bar = (t > 0) & (t < L) & (np.abs(d) < 3.6)
    nose = np.hypot(x - bx, y - by) < 3.6
    teeth = (t > 2) & (t < L + 2) & (np.abs(d) < 5.4) & (((t * 0.55) % 1) < 0.45) & (np.abs(d) > 3.0)
    housing = (t > -6) & (t < 1) & (np.abs(d) < 6)
    arc_r = np.hypot(x + 2, y - 2)
    arc = (np.abs(arc_r - 13.5) < 1.1) & (np.arctan2(y - 2, x + 2) > -0.2) & (np.arctan2(y - 2, x + 2) < 1.6)
    return bar | nose | teeth | housing | arc


icon(0, 176, saw_icon)
icon(0, 112, jump_icon)
icon(32, 112, flame_icon)
icon(64, 112, arrow_icon(-1))
icon(96, 112, arrow_icon(1))
icon(0, 144, pause_icon)

# ---------------------------------------------------------------- sigil (pentagram in a circle, additive)
x, y = grid(64, 144, 64, 64)
dx, dy = x - 32, y - 32
r = np.hypot(dx, dy)
pts = [(32 + 26 * math.cos(-math.pi / 2 + k * 4 * math.pi / 5), 32 + 26 * math.sin(-math.pi / 2 + k * 4 * math.pi / 5)) for k in range(6)]
star = seg_mask(x, y, pts, 0.55)
circ = np.abs(r - 26.5) < 0.6
m = star | circ
gl = np.clip(1 - np.minimum.reduce([np.abs(r - 26.5)]) / 2.2, 0, 1)
col = molten(np.full(r.shape, 0.55)) * (m * 0.85 + 0.15 * gl)[..., None]
put(64, 144, col, ((m | (gl > 0.5)) & (r < 31)).astype(float))


# ---------------------------------------------------------------- menu plates
def plate(x0, y0, cap, lit):
    w = 24 if cap else 16
    x, y = grid(x0, y0, w, 32)
    col = np.zeros(x.shape + (3,)); cov = np.zeros_like(x)
    top, bot = 5.0, 27.0
    if cap:
        # angular left end: slanted edges meeting a horn spike pointing left
        edge = 10 + np.abs(y - 16) * 0.55
        body = (x > edge - 6) & (y > top) & (y < bot) & (x > 4 + np.abs(y - 16) * 0.9)
        cov = body.astype(float)
        hc, hs = horn(x, y, 7, 16, math.pi, 6.5, 2.4)
        spike = hc > 0
    else:
        body = (y > top) & (y < bot)
        cov = body.astype(float)
        spike = np.zeros_like(body)
    rail = body & ((y < top + 3.2) | (y > bot - 3.2))
    panel = body & ~rail
    col[rail] = iron(x, y, np.zeros_like(x), np.where(y < 16, -1.0, 1.0))[rail]
    neon_t = np.abs(y - (top + 3.4)) < 0.6
    neon_b = np.abs(y - (bot - 3.4)) < 0.6
    if lit:
        pc = molten(np.clip(0.75 - np.abs(y - 16) / 18 + 0.2 * noise2(x, y, 0.7, 5), 0, 1) * 0.9)
        col[panel] = pc[panel]
        col[(neon_t | neon_b) & body] = C(255, 220, 140)
    else:
        pc = lerp(C(30, 8, 10), C(10, 3, 5), np.abs(y - 16) / 12)
        grid_l = ((x.astype(int) % 6 == 0) | (y.astype(int) % 6 == 0)) & panel
        col[panel] = pc[panel]
        col[grid_l] = C(46, 10, 10)
        col[(neon_t | neon_b) & body] = C(170, 28, 12)
    if cap:
        col[spike] = lerp(C(80, 60, 52), C(235, 215, 190), hs * 0.85)[spike]
        cov = np.maximum(cov, spike.astype(float))
        bolt = np.hypot(x - 15, y - 16) < 1.3
        col[bolt] = C(200, 180, 160) if not lit else C(255, 250, 220)
    put(x0, y0, col, cov)


plate(0, 208, True, False)
plate(24, 208, False, False)
plate(40, 208, True, True)
plate(64, 208, False, True)
# selector claw: a burning talon pointing right
x, y = grid(80, 208, 24, 32)
m = np.zeros_like(x, bool)
for k, (yy, ln) in enumerate(((10, 15), (16, 19), (22, 15))):
    t = np.clip((x - 3) / ln, 0, 1)
    m |= (np.abs(y - yy - 3 * t * t * (1 if yy < 16 else -1 if yy > 16 else 0)) < 2.4 * (1 - t) + 0.3) & (x > 3) & (x < 3 + ln)
col = molten(np.clip(0.4 + (x - 3) / 22, 0, 1))
put(80, 208, col, m.astype(float))

# ---------------------------------------------------------------- downsample, quantise, dither, TIM
img = RGBA.reshape(H, SS, W, SS, 4).mean((1, 3))
alpha = img[..., 3] > 0.45
rgb = np.clip(img[..., :3] / np.maximum(img[..., 3:4], 1e-6), 0, 1) * 255
src = Image.fromarray(rgb.astype(np.uint8)[alpha][None])
pal = np.array(src.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).getpalette()[:765], float).reshape(-1, 3)
pal = np.clip(np.round(pal / 8), 0, 31); pal8 = pal * 8 + pal // 4
DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float)
yy, xx = np.mgrid[0:H, 0:W]
tgt = rgb + (DM[yy % 4, xx % 4] * 2.0)[..., None]
idx = np.zeros((H, W), np.uint8)
idx[alpha] = ((tgt[alpha][:, None, :] - pal8[None]) ** 2).sum(2).argmin(1) + 1
words = [0] + [int(r) | (int(g) << 5) | (int(b) << 10) for r, g, b in pal]
words = [w | 0x8000 if i else w for i, w in enumerate(words)]   # STP on every colour: additive sprites blend + [0] * (256 - len(words))
tim = (struct.pack('<II', 0x10, 0x09) + struct.pack('<IHHHH', 12 + 512, 0, 492, 256, 1) + struct.pack('<256H', *words)
       + struct.pack('<IHHHH', 12 + W * H, 320, 0, W // 2, H) + idx.tobytes())
lines = ['/* generated by tools/make_ui.py */', f'#define UI_TIM_SIZE {len(tim)}', f'static const unsigned char ui_tim[{len(tim)}] = {{']
lines += ['  ' + ','.join(map(str, tim[i:i + 32])) + ',' for i in range(0, len(tim), 32)]
open(os.path.join(ROOT, 'assets', 'ui_tim.h'), 'w').write('\n'.join(lines) + '\n};\n')
w8 = (np.array(words)[:, None] >> np.array([0, 5, 10])) & 31
prev = (w8 * 8 + w8 // 4).astype(np.uint8)[idx]
prev[idx == 0] = (40, 0, 40)
Image.fromarray(prev).resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(ROOT, 'docs', 'ui_atlas.png'))
print('ui atlas ok')
