"""Procedural sci-fi texture tiles for the Deadbird lab (64x64 each, two 256x256 pages).
Everything is painted in RGB with baked lighting (bevels, rivet shadows, grime, ambient
occlusion in corners) and then quantised to a 255-colour CLUT with PS1 4x4 ordered dither.
Page A = structure (floors, walls, pipes, signs), page B = machines and props."""
import numpy as np

T = 64
rng = np.random.default_rng(7)


def noise(scale, seed, octaves=3):
    """tileable value noise, 0..1"""
    r = np.random.default_rng(seed)
    out = np.zeros((T, T))
    amp, tot = 1.0, 0.0
    s = scale
    for _ in range(octaves):
        g = r.random((s, s))
        g = np.concatenate([g, g[:1]], 0)
        g = np.concatenate([g, g[:, :1]], 1)
        y = np.linspace(0, s, T, endpoint=False)
        y0 = y.astype(int); fy = y - y0
        fy = fy * fy * (3 - 2 * fy)
        a = g[y0][:, y0] * (1 - fy)[None, :] + g[y0][:, y0 + 1] * fy[None, :]
        b = g[y0 + 1][:, y0] * (1 - fy)[None, :] + g[y0 + 1][:, y0 + 1] * fy[None, :]
        out += amp * (a * (1 - fy)[:, None] + b * fy[:, None])
        tot += amp
        amp *= 0.5
        s *= 2
    return out / tot


def new(col):
    return np.ones((T, T, 3)) * np.array(col, float)


def rect(img, x0, y0, x1, y1, col):
    img[y0:y1, x0:x1] = col


def bevel(img, x0, y0, x1, y1, col, hi=1.35, lo=0.55, w=1):
    """raised panel with baked light from the top-left"""
    c = np.array(col, float)
    img[y0:y1, x0:x1] = c
    img[y0:y0 + w, x0:x1] = c * hi
    img[y0:y1, x0:x0 + w] = c * hi
    img[y1 - w:y1, x0:x1] = c * lo
    img[y0:y1, x1 - w:x1] = c * lo


def inset(img, x0, y0, x1, y1, col):
    """recessed panel: shadow top-left, light bottom-right"""
    bevel(img, x0, y0, x1, y1, col, hi=0.5, lo=1.3)


def rivet(img, x, y, col=(150, 150, 155)):
    c = np.array(col, float)
    img[y, x] = c * 1.4
    img[y, x + 1] = c
    img[y + 1, x] = c
    img[y + 1, x + 1] = c * 0.5
    img[y + 2, x + 1] = img[y + 2, x + 1] * 0.6      # tiny cast shadow


def grime(img, amount=0.35, seed=1, streaks=True):
    n = noise(4, seed)
    m = 1 - amount * (1 - n)
    if streaks:                                       # rust / oil streaks running down
        r = np.random.default_rng(seed + 50)
        for _ in range(6):
            x = r.integers(0, T)
            y0 = r.integers(0, T // 2)
            ln = r.integers(8, 30)
            m[y0:y0 + ln, x] *= 0.8
    img *= m[..., None]
    return img


def ao_edges(img, strength=0.35, width=6):
    """baked ambient occlusion toward the tile border (dark seams between tiles)"""
    y, x = np.mgrid[0:T, 0:T]
    d = np.minimum(np.minimum(x, T - 1 - x), np.minimum(y, T - 1 - y)).astype(float)
    k = 1 - strength * np.clip(1 - d / width, 0, 1)
    img *= k[..., None]
    return img


def scratches(img, n=10, seed=3, col=(170, 170, 175)):
    r = np.random.default_rng(seed)
    for _ in range(n):
        x, y = r.integers(0, T, 2)
        dx, dy = r.integers(-1, 2), r.integers(-1, 2)
        for i in range(r.integers(3, 9)):
            xx, yy = (x + dx * i) % T, (y + dy * i) % T
            img[yy, xx] = img[yy, xx] * 0.4 + np.array(col) * 0.6
    return img


FONT = {  # 3x5 pixel font
    'A': '010101111101101', 'B': '110101110101110', 'C': '011100100100011', 'D': '110101101101110',
    'E': '111100110100111', 'F': '111100110100100', 'G': '011100101101011', 'H': '101101111101101',
    'I': '111010010010111', 'K': '101101110101101', 'L': '100100100100111', 'M': '101111111101101',
    'N': '110101101101101', 'O': '010101101101010', 'P': '110101110100100', 'R': '110101110101101',
    'S': '011100010001110', 'T': '111010010010010', 'U': '101101101101111', 'V': '101101101101010',
    'W': '101101111111101', 'X': '101101010101101', 'Y': '101101010010010', 'Z': '111001010100111',
    '0': '111101101101111', '1': '010110010010111', '2': '110001010100111', '3': '110001010001110',
    '4': '101101111001001', '5': '111100110001110', '6': '011100111101111', '7': '111001010010010',
    '8': '111101111101111', '9': '111101111001110', '-': '000000111000000', ' ': '000000000000000',
    '%': '101001010100101', '!': '010010010000010', ':': '000010000010000',
}


def text(img, s, x, y, col):
    c = np.array(col, float)
    for ch in s:
        g = FONT.get(ch, FONT[' '])
        for i, bit in enumerate(g):
            if bit == '1':
                yy, xx = y + i // 3, x + i % 3
                if 0 <= yy < T and 0 <= xx < T:
                    img[yy, xx] = c
        x += 4


def cyl_shade(img, axis=0, lo=0.45, hi=1.35):
    """baked cylinder lighting across the tile (axis 0: shading varies with y)"""
    t = np.linspace(0, np.pi, T)
    k = lo + (hi - lo) * np.sin(t) ** 1.5
    k = k + 0.25 * np.exp(-((np.linspace(0, 1, T) - 0.32) / 0.06) ** 2)   # specular stripe
    img *= (k[:, None, None] if axis == 0 else k[None, :, None])
    return img


# ---------------------------------------------------------------- page A: structure
def floor_plate():
    img = new((78, 82, 90))
    for py in (0, 32):
        for px in (0, 32):
            bevel(img, px + 1, py + 1, px + 31, py + 31, (82, 86, 94))
            for y in range(py + 4, py + 29, 4):          # diamond tread
                for x in range(px + 4 + (y // 4 % 2) * 2, px + 29, 4):
                    img[y, x] *= 1.35
                    img[y + 1, x + 1] *= 0.6
            for (rx, ry) in ((px + 3, py + 3), (px + 27, py + 3), (px + 3, py + 27), (px + 27, py + 27)):
                rivet(img, rx, ry)
    grime(img, 0.4, 11)
    scratches(img, 14, 5)
    return ao_edges(img, 0.3, 3)


def floor_grate():
    img = new((40, 44, 48))
    glow = noise(4, 9)
    for y in range(4, 60):
        for x in range(4, 60):
            if (x % 6) in (0, 1) or (y % 12) in (0, 1):
                img[y, x] = (62, 66, 72)
            else:
                g = 0.5 + 0.5 * glow[y, x]
                img[y, x] = (10 + 20 * g, 60 * g + 20, 70 * g + 24)   # cyan glow from the pit below
    bevel(img, 0, 0, 64, 4, (70, 74, 80)); bevel(img, 0, 60, 64, 64, (70, 74, 80))
    bevel(img, 0, 0, 4, 64, (70, 74, 80)); bevel(img, 60, 0, 64, 64, (70, 74, 80))
    return grime(img, 0.25, 12, False)


def wall_panel():
    img = new((92, 98, 108))
    for px in (0, 32):
        bevel(img, px + 1, 1, px + 31, 40, (96, 102, 112))
        bevel(img, px + 1, 41, px + 31, 63, (84, 90, 100))
        for (rx, ry) in ((px + 3, 3), (px + 27, 3), (px + 3, 36), (px + 27, 36)):
            rivet(img, rx, ry)
        rect(img, px + 6, 46, px + 26, 49, (60, 160, 170))     # glowing status strip
        img[47, px + 6:px + 26] = (140, 230, 235)
    rect(img, 4, 14, 14, 18, (200, 170, 40))                  # yellow ID tag
    text(img, 'B7', 5, 14, (30, 30, 30))
    grime(img, 0.35, 21)
    return ao_edges(img, 0.25, 3)


def wall_vent():
    img = wall_panel()
    inset(img, 12, 8, 52, 34, (40, 44, 50))
    for y in range(11, 32, 3):
        img[y, 14:50] = (95, 100, 108)
        img[y + 1, 14:50] = (30, 32, 36)
    return img


def hazard():
    img = np.zeros((T, T, 3))
    y, x = np.mgrid[0:T, 0:T]
    s = ((x + y) // 8) % 2 == 0
    img[s] = (205, 165, 30)
    img[~s] = (30, 28, 26)
    grime(img, 0.45, 31)
    scratches(img, 18, 8, (120, 110, 90))
    return ao_edges(img, 0.3, 3)


def ceiling_light():
    img = new((52, 56, 62))
    bevel(img, 1, 1, 63, 63, (56, 60, 66))
    inset(img, 8, 20, 56, 44, (40, 40, 44))
    rect(img, 10, 22, 54, 42, (200, 235, 240))
    rect(img, 12, 26, 52, 38, (245, 252, 255))
    for x in range(14, 52, 6):
        img[22:42, x] = (150, 190, 200)
    return grime(img, 0.2, 41, False)


def wall_lower():
    img = new((70, 72, 78))
    bevel(img, 0, 0, 64, 64, (72, 74, 80), w=2)
    for x in range(6, 64, 14):
        rivet(img, x, 6); rivet(img, x, 54)
    img[30:34] *= 0.7
    y = np.arange(T)
    img *= (1 - 0.45 * np.clip((y - 30) / 34, 0, 1))[:, None, None]    # dirt + AO toward the floor
    grime(img, 0.4, 51)
    scratches(img, 12, 52)
    return img


def pipe():
    img = new((120, 128, 132))
    for x in range(0, T, 16):
        img[:, x:x + 3] *= 0.6
        img[:, x + 3] *= 1.3
    grime(img, 0.3, 61, False)
    return cyl_shade(img, 0)


def bulkhead():
    img = new((84, 88, 96))
    bevel(img, 0, 0, 64, 64, (86, 90, 98), w=3)
    for i in range(0, 64, 12):                      # chevrons along the edges
        for k in range(6):
            for xx in (2, 61):
                yy = (i + k) % T
                img[yy, xx - 1:xx + 2] = (210, 170, 30) if k < 3 else (30, 28, 26)
    inset(img, 10, 10, 54, 54, (64, 68, 76))
    rect(img, 24, 28, 40, 36, (180, 40, 30))
    text(img, 'EXIT', 24, 30, (255, 200, 180))
    return grime(img, 0.3, 71)


def beam():
    img = new((56, 58, 62))
    bevel(img, 0, 0, 64, 10, (70, 72, 76))
    bevel(img, 0, 54, 64, 64, (70, 72, 76))
    inset(img, 0, 10, 64, 54, (48, 50, 54))
    for x in range(4, 64, 8):
        rivet(img, x, 3); rivet(img, x, 58)
    return grime(img, 0.35, 81)


def cables():
    img = new((20, 20, 22))
    cols = [(150, 30, 26), (30, 30, 34), (40, 80, 150), (30, 30, 34), (170, 140, 30), (40, 110, 60), (30, 30, 34), (150, 30, 26)]
    for i, c in enumerate(cols):
        y0 = i * 8
        band = np.array(c, float) * (0.5 + 0.8 * np.sin(np.linspace(0.2, np.pi - 0.2, 8)))[:, None]
        img[y0:y0 + 8] = band[:, None, :]
    for x in range(0, T, 21):
        img[:, x:x + 2] = (90, 90, 95)              # cable ties
    return grime(img, 0.2, 91, False)


def stair_tread():
    img = new((80, 82, 88))
    rect(img, 0, 0, 64, 8, (205, 165, 30))
    for x in range(0, 64, 8):
        img[0:8, x:x + 4] = (30, 28, 26)
    for y in range(12, 60, 5):
        img[y] *= 1.3
        img[y + 1] *= 0.6
    grime(img, 0.4, 101)
    return ao_edges(img, 0.25, 3)


def floor_light():
    img = new((36, 38, 42))
    bevel(img, 0, 0, 64, 64, (40, 42, 46), w=3)
    rect(img, 6, 22, 58, 42, (40, 200, 210))
    rect(img, 8, 27, 56, 37, (170, 255, 255))
    for x in range(10, 56, 8):
        img[22:42, x] = (30, 120, 130)
    return img


def sign():
    img = new((34, 30, 30))
    rect(img, 0, 0, 64, 4, (200, 40, 30)); rect(img, 0, 60, 64, 64, (200, 40, 30))
    for x in range(0, 64, 8):
        img[0:4, x:x + 4] = (230, 190, 40)
        img[60:64, x + 4:x + 8] = (230, 190, 40)
    text(img, 'DANGER', 8, 8, (240, 60, 40))
    # bird skull icon
    sk = ['00111100', '01111110', '11011011', '11111111', '01111110', '00111000', '00010000']
    for j, row in enumerate(sk):
        for i, b in enumerate(row):
            if b == '1':
                img[18 + j * 2:20 + j * 2, 40 + i * 2:42 + i * 2] = (220, 210, 190)
    text(img, 'SHINI', 4, 22, (230, 190, 40))
    text(img, 'GAMI', 4, 30, (230, 190, 40))
    text(img, 'ENERGY', 4, 42, (200, 200, 200))
    text(img, 'ZONE', 4, 50, (200, 200, 200))
    return grime(img, 0.3, 111, False)


def circuit():
    img = new((22, 26, 40))
    r = np.random.default_rng(121)
    for _ in range(22):
        x, y = r.integers(0, T, 2)
        horiz = r.random() < 0.5
        ln = r.integers(8, 30)
        for i in range(ln):
            xx, yy = ((x + i) % T, y) if horiz else (x, (y + i) % T)
            img[yy, xx] = (40, 170, 200)
        img[y, x] = (180, 255, 255)
    inset(img, 0, 0, 64, 64, (1, 1, 1)) if False else None
    return ao_edges(grime(img, 0.25, 122, False), 0.4, 4)


def dark_metal():
    img = new((54, 56, 60))
    bevel(img, 0, 0, 64, 64, (56, 58, 62), w=2)
    grime(img, 0.45, 131)
    scratches(img, 20, 132, (110, 110, 115))
    return img


# ---------------------------------------------------------------- page B: machines and props
def machine_panel():
    img = new((96, 100, 104))
    bevel(img, 0, 0, 64, 64, (98, 102, 106), w=2)
    inset(img, 6, 6, 58, 24, (20, 26, 24))
    for i, x in enumerate(range(9, 56, 6)):
        h = [9, 14, 6, 11, 15, 8, 12, 5][i % 8]
        rect(img, x, 22 - h, x + 4, 22, (40, 200, 90))
    for i, x in enumerate(range(8, 58, 10)):
        c = [(220, 40, 30), (40, 220, 60), (230, 190, 40), (40, 120, 230), (220, 40, 30)][i % 5]
        bevel(img, x, 30, x + 6, 36, c)
    for x in (14, 34, 50):                          # dials
        yy, xx = np.mgrid[0:T, 0:T]
        d = (xx - x) ** 2 + (yy - 48) ** 2
        img[d < 36] = (40, 42, 46)
        img[d < 25] = (200, 200, 190)
        img[48, x:x + 4] = (200, 30, 30)
    return grime(img, 0.25, 141, False)


def screen_graph():
    img = new((8, 22, 14))
    for x in range(0, 64, 8):
        img[:, x] = (16, 50, 30)
    for y in range(0, 64, 8):
        img[y] = (16, 50, 30)
    pts = 30 + 14 * np.sin(np.linspace(0, 9, 60)) * np.cos(np.linspace(0, 3, 60))
    for i in range(59):
        a, b = int(pts[i]), int(pts[i + 1])
        for yy in range(min(a, b), max(a, b) + 1):
            img[yy, i + 2] = (80, 255, 120)
    text(img, 'OUTPUT 98%', 2, 56, (120, 255, 150))
    img[0:2] = img[62:64] = (60, 66, 70)
    img[:, 0:2] = img[:, 62:64] = (60, 66, 70)
    return img


def screen_vitals():
    img = new((26, 6, 8))
    beat = [0, 0, 0, 0, 1, -6, 10, -3, 0, 0, 0, 0, 0, 0, 0, 0]
    y0 = 20
    prev = y0
    for x in range(2, 62):
        y = y0 + beat[x % 16] * 1
        for yy in range(min(prev, y), max(prev, y) + 1):
            img[yy, x] = (255, 70, 60)
        prev = y
    text(img, 'SHINIGAMI', 2, 34, (255, 120, 100))
    text(img, 'SOUL 07%', 2, 42, (255, 200, 80))
    rect(img, 2, 52, 62, 58, (60, 14, 14))
    rect(img, 2, 52, 9, 58, (255, 60, 40))
    img[0:2] = img[62:64] = (60, 66, 70)
    img[:, 0:2] = img[:, 62:64] = (60, 66, 70)
    return img


def server_rack():
    img = new((18, 18, 22))
    for y in range(2, 62, 6):
        bevel(img, 2, y, 62, y + 5, (34, 34, 40))
        r = np.random.default_rng(y)
        for x in range(6, 58, 4):
            if r.random() < 0.55:
                img[y + 2, x] = [(40, 255, 80), (60, 140, 255), (255, 180, 40)][r.integers(0, 3)]
    return img


def glass():
    img = new((70, 120, 140))
    y, x = np.mgrid[0:T, 0:T]
    img *= (0.6 + 0.4 * noise(2, 151))[..., None]
    for sx, w in ((10, 3), (16, 1), (44, 2)):
        img[:, sx:sx + w] = (190, 230, 240)
    return img


def energy():
    y, x = np.mgrid[0:T, 0:T] / T
    n = noise(4, 161, 4)
    sw = np.sin((x * 2 + n * 2.5) * np.pi * 2) * 0.5 + 0.5
    img = np.zeros((T, T, 3))
    img[..., 0] = 60 + 120 * sw * n
    img[..., 1] = 140 * sw + 60 * n
    img[..., 2] = 160 + 90 * sw
    return img


def coil():
    img = new((150, 90, 40))
    for y in range(0, T, 4):
        img[y] *= 1.4
        img[y + 2] *= 0.6
        img[y + 3] *= 0.5
    for x in (0, 31, 62):
        img[:, x:x + 2] = (60, 60, 64)
    return cyl_shade(img, 1, 0.5, 1.4)


def crate():
    img = new((80, 84, 74))
    bevel(img, 0, 0, 64, 64, (90, 94, 82), w=5)
    inset(img, 6, 6, 58, 58, (70, 74, 64))
    for i in range(6, 58):
        img[i, i - 1:i + 2] = (92, 96, 84)
        img[i, 63 - i - 1:63 - i + 2] = (92, 96, 84)
    rect(img, 20, 26, 44, 38, (200, 160, 30))
    text(img, 'S-07', 22, 29, (30, 28, 24))
    for (rx, ry) in ((2, 2), (59, 2), (2, 59), (59, 59)):
        rivet(img, rx, ry)
    return grime(img, 0.35, 171)


def canister():
    img = new((200, 160, 30))
    rect(img, 0, 22, 64, 34, (30, 28, 26))
    for x in range(0, 64, 8):
        img[22:34, x:x + 4] = (200, 160, 30)
    rect(img, 0, 0, 64, 4, (90, 90, 96)); rect(img, 0, 60, 64, 64, (90, 90, 96))
    text(img, 'SOUL', 24, 44, (40, 30, 20))
    grime(img, 0.35, 181)
    return cyl_shade(img, 1, 0.45, 1.3)


def machine_base():
    img = new((50, 52, 58))
    bevel(img, 0, 0, 64, 64, (54, 56, 62), w=3)
    for y in (16, 40):
        inset(img, 8, y, 56, y + 10, (24, 10, 6))
        for x in range(10, 54, 4):
            img[y + 2:y + 8, x:x + 2] = (255, 120, 30)
            img[y + 4:y + 6, x:x + 2] = (255, 210, 120)
    for x in range(4, 64, 10):
        rivet(img, x, 4); rivet(img, x, 58)
    return grime(img, 0.3, 191, False)


def keyboard():
    img = new((44, 46, 52))
    for y in range(6, 50, 7):
        for x in range(4 + (y // 7 % 2) * 3, 60, 7):
            bevel(img, x, y, x + 5, y + 5, (90, 92, 98))
    rect(img, 6, 52, 58, 60, (20, 30, 40))
    text(img, 'EXTRACT', 8, 54, (60, 220, 240))
    return grime(img, 0.2, 201, False)


def cage_bars():
    img = np.zeros((T, T, 3))
    for x in range(2, 60, 10):
        band = 90 * (0.5 + 0.8 * np.sin(np.linspace(0.3, np.pi - 0.3, 4)))
        img[:, x:x + 4] = np.stack([band, band, band * 1.1], -1)[None]
    img[0:4] = (80, 80, 86); img[60:64] = (80, 80, 86)
    img[30:33] = (70, 70, 76)
    return img          # black = transparent (texel 0)


def fan():
    img = np.zeros((T, T, 3))
    y, x = np.mgrid[0:T, 0:T] - 31.5
    r = np.hypot(x, y)
    a = np.arctan2(y, x)
    blade = (np.sin(a * 5 + r * 0.08) > 0.3) & (r < 30) & (r > 6)
    img[blade] = (110, 116, 124)
    img[blade] *= (0.6 + 0.6 * (np.sin(a * 5 + r * 0.08)[blade, None]))
    img[r < 7] = (70, 72, 80)
    img[r < 3] = (150, 150, 160)
    ring = (r >= 30) & (r < 32)
    img[ring] = (60, 62, 68)
    return img


def shini_body():
    img = new((30, 22, 36))
    for y in range(0, T, 6):
        for x in range(-(y // 6 % 2) * 4, T, 8):
            yy, xx = np.mgrid[0:T, 0:T]
            m = ((xx - x - 4) ** 2 / 16 + (yy - y - 4) ** 2 / 25) < 1
            img[m] = img[m] * 0.5 + np.array((60, 44, 70)) * 0.5
    img[np.mgrid[0:T, 0:T][0] % 6 == 5] *= 0.5
    rect(img, 30, 0, 34, 64, (170, 160, 140))        # bone spine ridge
    for y in range(4, 64, 8):
        rect(img, 28, y, 36, y + 2, (200, 190, 170))
    return grime(img, 0.2, 211, False)


def shini_skull():
    img = new((14, 10, 16))
    yy, xx = np.mgrid[0:T, 0:T]
    head = ((xx - 32) ** 2 / 400 + (yy - 26) ** 2 / 300) < 1
    beak = (yy > 30) & (yy < 60) & (np.abs(xx - 32) < (60 - yy) * 0.45)
    img[head | beak] = (205, 196, 178)
    img[(head | beak)] *= (0.7 + 0.3 * noise(4, 221)[head | beak])[:, None]
    for ex in (22, 42):                               # glowing orange sockets
        e = ((xx - ex) ** 2 + (yy - 24) ** 2) < 30
        img[e] = (255, 120, 20)
        img[((xx - ex) ** 2 + (yy - 24) ** 2) < 8] = (255, 230, 150)
    img[beak & (yy == 44)] *= 0.4
    return img


def lamp_glow():
    yy, xx = np.mgrid[0:T, 0:T] - 31.5
    r = np.hypot(xx, yy) / 32
    k = np.clip(1 - r, 0, 1) ** 2
    img = np.stack([255 * k, 230 * k, 200 * k], -1)
    return img


PAGE_A = [floor_plate, floor_grate, wall_panel, wall_vent, hazard, ceiling_light, wall_lower, pipe,
          bulkhead, beam, cables, stair_tread, floor_light, sign, circuit, dark_metal]
PAGE_B = [machine_panel, screen_graph, screen_vitals, server_rack, glass, energy, coil, crate,
          canister, machine_base, keyboard, cage_bars, fan, shini_body, shini_skull, lamp_glow]
NAMES_A = [f.__name__ for f in PAGE_A]
NAMES_B = [f.__name__ for f in PAGE_B]
# tiles where pure black means "see-through"
TRANSPARENT = {'cage_bars', 'fan', 'lamp_glow', 'energy'}


def build_page(funcs):
    page = np.zeros((256, 256, 3))
    mask = np.zeros((256, 256), bool)
    for i, f in enumerate(funcs):
        img = np.clip(f(), 0, 255)
        x, y = (i % 4) * 64, (i // 4) * 64
        page[y:y + 64, x:x + 64] = img
        if f.__name__ in TRANSPARENT:
            mask[y:y + 64, x:x + 64] = img.max(-1) < 10
    return page, mask


DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float) * 2.0


def quantise(page, mask):
    """255 colours + transparent index 0, PS1 ordered dither, 15-bit CLUT (STP set so the
    glass / energy tiles can be drawn semi-transparent)"""
    from PIL import Image
    op = Image.fromarray(page[~mask][None].astype(np.uint8))
    pal = np.array(op.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).getpalette()[:255 * 3]).reshape(-1, 3)
    pal = np.clip(np.round(pal / 8), 0, 31)
    pal8 = pal * 8 + pal // 4
    yy, xx = np.mgrid[0:256, 0:256]
    tgt = page + DM[yy % 4, xx % 4][..., None]
    idx = np.zeros((256, 256), np.uint8)
    for r0 in range(0, 256, 32):
        d2 = ((tgt[r0:r0 + 32, :, None, :] - pal8[None, None]) ** 2).sum(-1)
        idx[r0:r0 + 32] = d2.argmin(-1) + 1
    idx[mask] = 0
    clut = [0] + [0x8000 | int(r) | (int(g) << 5) | (int(b) << 10) for r, g, b in pal]
    clut += [0] * (256 - len(clut))
    rgb = np.array([[c & 31, (c >> 5) & 31, (c >> 10) & 31] for c in clut]) * 8
    return idx, clut, rgb[idx]
