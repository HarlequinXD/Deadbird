"""Procedural sci-fi textures for the Deadbird plant.

Texel density is fixed at 32 texels per metre: every 64x64 tile covers 2 x 2 metres of wall or
floor (1 metre = 1000 world units). Wall details - painted pipes, hazard paint, posters, signs,
door surrounds, room stencils, wall monitors, cable trays - are painted INTO the textures, so
walls are plain big quads with no floating decal polygons. Lighting is baked into the paint
(bevels, rivet shadows, grime, occlusion toward floor and corners); the result is quantised to a
255-colour CLUT per page with PS1 4x4 ordered dither.

Pages (8bpp, 256x256 = 16 tiles each):
  WALL1  generic wall bands: base (0-2 m), middle (2-4 m), top (4 m +), door surrounds
  WALL2  room-specific walls (dock, checkpoint, processing, extraction, cells, lab, vault,
         gallery, turbine, reactor) and hall walls
  FLOOR  floors, ceilings, stairs
  PROPS  machines and props (screens, racks, glass, plasma, crates, captives...)
  TRIM   trim for 3D props (pipes, beams, hazard, grates, plates)
  ANIM   animated screens: 4-frame strips (wall monitors with graphs / shinigami vitals, desk
         screens); the game steps along the strip
"""
import numpy as np

T = 64


def noise(scale, seed, octaves=3):
    """tileable value noise, 0..1"""
    r = np.random.default_rng(seed)
    out = np.zeros((T, T))
    amp, tot, s = 1.0, 0.0, scale
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
        tot += amp; amp *= 0.5; s *= 2
    return out / tot


def new(col):
    return np.ones((T, T, 3)) * np.array(col, float)


def rect(img, x0, y0, x1, y1, col):
    img[max(0, y0):min(T, y1), max(0, x0):min(T, x1)] = col


def bevel(img, x0, y0, x1, y1, col, hi=1.35, lo=0.55, w=1):
    c = np.array(col, float)
    img[y0:y1, x0:x1] = c
    img[y0:y0 + w, x0:x1] = c * hi
    img[y0:y1, x0:x0 + w] = c * hi
    img[y1 - w:y1, x0:x1] = c * lo
    img[y0:y1, x1 - w:x1] = c * lo


def inset(img, x0, y0, x1, y1, col):
    bevel(img, x0, y0, x1, y1, col, hi=0.5, lo=1.3)


def rivet(img, x, y, col=(150, 150, 155)):
    if not (0 <= x < T - 1 and 0 <= y < T - 2):
        return
    c = np.array(col, float)
    img[y, x] = c * 1.4; img[y, x + 1] = c; img[y + 1, x] = c; img[y + 1, x + 1] = c * 0.5
    img[y + 2, x + 1] = img[y + 2, x + 1] * 0.6


def grime(img, amount=0.35, seed=1, streaks=True):
    n = noise(4, seed)
    m = 1 - amount * (1 - n)
    if streaks:
        r = np.random.default_rng(seed + 50)
        for _ in range(7):
            x = r.integers(0, T); y0 = r.integers(0, T // 2); ln = r.integers(8, 30)
            m[y0:y0 + ln, x] *= 0.8
    img *= m[..., None]
    return img


def floor_ao(img, strength=0.4, px=14):
    """walls: darker toward the floor (bottom of a base tile)"""
    y = np.arange(T)
    img *= (1 - strength * np.clip((y - (T - px)) / px, 0, 1))[:, None, None]
    return img


def ao_edges(img, strength=0.35, width=6):
    y, x = np.mgrid[0:T, 0:T]
    d = np.minimum(np.minimum(x, T - 1 - x), np.minimum(y, T - 1 - y)).astype(float)
    img *= (1 - strength * np.clip(1 - d / width, 0, 1))[..., None]
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


FONT = {
    'A': '010101111101101', 'B': '110101110101110', 'C': '011100100100011', 'D': '110101101101110',
    'E': '111100110100111', 'F': '111100110100100', 'G': '011100101101011', 'H': '101101111101101',
    'I': '111010010010111', 'J': '001001001101010', 'K': '101101110101101', 'L': '100100100100111',
    'M': '101111111101101', 'N': '110101101101101', 'O': '010101101101010', 'P': '110101110100100',
    'Q': '010101101110011', 'R': '110101110101101', 'S': '011100010001110', 'T': '111010010010010',
    'U': '101101101101111', 'V': '101101101101010', 'W': '101101111111101', 'X': '101101010101101',
    'Y': '101101010010010', 'Z': '111001010100111', '0': '111101101101111', '1': '010110010010111',
    '2': '110001010100111', '3': '110001010001110', '4': '101101111001001', '5': '111100110001110',
    '6': '011100111101111', '7': '111001010010010', '8': '111101111101111', '9': '111101111001110',
    '-': '000000111000000', ' ': '000000000000000', '%': '101001010100101', '!': '010010010000010',
    ':': '000010000010000', '.': '000000000000010', '>': '100010001010100', '<': '001010100010001',
}


def text(img, s, x, y, col, scale=1):
    c = np.array(col, float)
    for ch in s:
        g = FONT.get(ch, FONT[' '])
        for i, bit in enumerate(g):
            if bit == '1':
                yy, xx = y + (i // 3) * scale, x + (i % 3) * scale
                img[max(0, yy):max(0, min(T, yy + scale)), max(0, xx):max(0, min(T, xx + scale))] = c
        x += 4 * scale


def text_w(s, scale=1):
    return len(s) * 4 * scale - scale


def ctext(img, s, y, col, scale=1):
    text(img, s, (T - text_w(s, scale)) // 2, y, col, scale)


def hpipe(img, y, r, col, brackets=True):
    """painted horizontal pipe: cylinder shading across its thickness, flanges, shadow below"""
    c = np.array(col, float)
    for k in range(-r, r + 1):
        yy = y + k
        if 0 <= yy < T:
            t = (k + r) / (2 * r)
            img[yy] = c * (0.45 + 0.95 * np.sin(np.pi * t) ** 1.2 + (0.35 if abs(t - 0.3) < 0.12 else 0))
    if y + r + 1 < T:
        img[y + r + 1] *= 0.55                                   # cast shadow on the wall
    for x in range(6, T, 21):                                    # flanges / brackets
        for k in range(-r - 1, r + 2):
            yy = y + k
            if 0 <= yy < T:
                img[yy, x:x + 2] = c * (0.7 if k < 0 else 0.45) if brackets else img[yy, x:x + 2]


def vpipe(img, x, r, col):
    c = np.array(col, float)
    for k in range(-r, r + 1):
        xx = x + k
        if 0 <= xx < T:
            t = (k + r) / (2 * r)
            img[:, xx] = c * (0.45 + 0.95 * np.sin(np.pi * t) ** 1.2 + (0.35 if abs(t - 0.3) < 0.12 else 0))
    if x + r + 1 < T:
        img[:, x + r + 1] *= 0.55
    for y in range(4, T, 19):
        img[y:y + 2, x - r - 1:x + r + 2] *= 0.6


def valve(img, x, y, col=(200, 40, 30)):
    yy, xx = np.mgrid[0:T, 0:T]
    d = np.hypot(xx - x, yy - y)
    ring = (d > 3.2) & (d < 5)
    img[ring] = np.array(col) * 0.9
    img[(np.abs(xx - x) < 1) & (d < 4) | (np.abs(yy - y) < 1) & (d < 4)] = np.array(col) * 0.7
    img[d < 1.5] = (40, 40, 40)


def hazard_band(img, y0, y1, flip=False):
    y, x = np.mgrid[0:T, 0:T]
    s = (((x + (-y if flip else y)) // 5) % 2 == 0)
    band = (y >= y0) & (y < y1)
    img[band & s] = (205, 165, 30)
    img[band & ~s] = (32, 30, 28)


def kickplate(img, h=10):
    bevel(img, 0, T - h, T, T, (62, 64, 70), w=1)
    for x in range(4, T, 12):
        rivet(img, x, T - h + 3)


def panel_base(col=(92, 98, 108), seam=True):
    img = new(col)
    if seam:
        for px in (0, 32):
            bevel(img, px + 1, 1, px + 31, 63, col)
            for (rx, ry) in ((px + 3, 3), (px + 27, 3), (px + 3, 58), (px + 27, 58)):
                rivet(img, rx, ry)
    return img


def poster(img, x0, y0, x1, y1, bg, border, tear=True):
    rect(img, x0 + 1, y0 + 1, x1 + 1, y1 + 1, (20, 20, 22))       # shadow
    rect(img, x0, y0, x1, y1, border)
    rect(img, x0 + 1, y0 + 1, x1 - 1, y1 - 1, bg)
    if tear:
        for k in range(4):
            img[y1 - 1 - k, x1 - 4 + k:x1] = img[y1 + 1, x1 - 1] * 0 + np.array((70, 74, 80))


def skull_icon(img, x, y, col=(220, 210, 190), eye=(255, 120, 20), s=1):
    sk = ['0011100', '0111110', '1101011', '1111111', '0111110', '0011100', '0001000']
    for j, row in enumerate(sk):
        for i, b in enumerate(row):
            if b == '1':
                rect(img, x + i * s, y + j * s, x + (i + 1) * s, y + (j + 1) * s, col)
    rect(img, x + 2 * s, y + 2 * s, x + 3 * s, y + 3 * s, eye)
    rect(img, x + 4 * s, y + 2 * s, x + 5 * s, y + 3 * s, eye)


# ================================================================ WALL1: generic wall bands
def base_plain():
    img = panel_base((88, 94, 104))
    hpipe(img, 18, 2, (110, 120, 126))
    kickplate(img)
    hazard_band(img, 60, 64)
    grime(img, 0.35, 201)
    return floor_ao(img)


def base_pipes():
    img = panel_base((84, 90, 100))
    hpipe(img, 14, 4, (120, 128, 132))
    hpipe(img, 27, 3, (150, 80, 40))
    hpipe(img, 37, 2, (60, 110, 150))
    valve(img, 44, 14)
    kickplate(img)
    grime(img, 0.4, 202)
    return floor_ao(img)


def base_vent():
    img = panel_base((88, 94, 104))
    inset(img, 8, 20, 56, 48, (36, 38, 44))
    for y in range(23, 46, 3):
        img[y, 10:54] = (96, 100, 108)
        img[y + 1, 10:54] = (24, 26, 30)
    kickplate(img)
    grime(img, 0.35, 203)
    return floor_ao(img)


def base_hazard():
    img = new((80, 80, 84))
    hazard_band(img, 4, 54)
    bevel(img, 0, 0, 64, 4, (90, 94, 100))
    kickplate(img)
    grime(img, 0.45, 204)
    scratches(img, 16, 204, (120, 110, 90))
    return floor_ao(img)


def mid_panel():
    img = panel_base((94, 100, 110))
    for px in (0, 32):
        rect(img, px + 6, 44, px + 26, 47, (60, 160, 170))
        img[45, px + 6:px + 26] = (150, 235, 240)
    rect(img, 4, 8, 16, 13, (200, 170, 40))
    text(img, 'B7', 6, 8, (30, 30, 30))
    return grime(img, 0.3, 211)


def mid_poster_power():
    img = panel_base((92, 98, 108))
    poster(img, 12, 6, 52, 58, (18, 30, 46), (180, 180, 170))
    skull_icon(img, 22, 10, s=3)
    for k in range(5):                                          # lightning bolt
        img[33 + k * 2:35 + k * 2, 30 + (k % 2) * 3:34 + (k % 2) * 3] = (90, 230, 255)
    text(img, 'SHINI', 21, 45, (90, 230, 255))
    text(img, 'POWER', 21, 51, (230, 230, 220))
    return grime(img, 0.3, 212)


def mid_poster_danger():
    img = panel_base((92, 98, 108))
    poster(img, 10, 8, 54, 56, (210, 180, 40), (30, 28, 26))
    yy, xx = np.mgrid[0:T, 0:T]
    a = np.arctan2(yy - 28, xx - 32)
    d = np.hypot(xx - 32, yy - 28)
    trefoil = (d > 4) & (d < 13) & ((np.floor((a + np.pi) / (np.pi / 3)) % 2) == 0)
    img[trefoil] = (30, 28, 26)
    img[d < 3] = (30, 28, 26)
    text(img, 'DANGER', 21, 44, (30, 28, 26))
    text(img, 'SOUL RAD', 17, 50, (140, 30, 20))
    return grime(img, 0.3, 213)


def mid_authorized():
    img = panel_base((92, 98, 108))
    rect(img, 4, 18, 60, 46, (170, 30, 26))
    rect(img, 6, 20, 58, 44, (200, 40, 32))
    ctext(img, 'AUTHORIZED', 23, (240, 230, 220))
    ctext(img, 'PERSONNEL', 30, (240, 230, 220))
    ctext(img, 'ONLY', 37, (240, 230, 220))
    return grime(img, 0.3, 214)


def mid_vpipes():
    img = panel_base((90, 96, 106))
    vpipe(img, 12, 4, (120, 128, 132))
    vpipe(img, 24, 2, (150, 80, 40))
    vpipe(img, 50, 3, (60, 110, 150))
    valve(img, 12, 30)
    return grime(img, 0.35, 215)


def mid_monitor():
    img = panel_base((92, 98, 108))
    bevel(img, 8, 10, 56, 44, (40, 42, 48), w=2)
    rect(img, 11, 13, 53, 41, (8, 26, 16))
    for x in range(11, 53, 6):
        img[13:41, x] = (16, 46, 28)
    pts = 27 + 9 * np.sin(np.linspace(0, 8, 42))
    for i in range(41):
        a, b = int(pts[i]), int(pts[i + 1])
        img[min(a, b):max(a, b) + 1, 11 + i] = (90, 255, 130)
    rect(img, 28, 44, 36, 52, (50, 52, 58))
    return grime(img, 0.25, 216)


def mid_arrow():
    img = panel_base((92, 98, 108))
    rect(img, 6, 22, 58, 42, (200, 160, 30))
    for k in range(10):
        img[32 - k:33 + k, 44 + k] = (30, 28, 26) if k < 9 else img[32, 44]
    rect(img, 12, 29, 44, 36, (30, 28, 26))
    ctext(img, 'EXIT', 14, (230, 230, 220))
    return grime(img, 0.3, 217)


def top_circuit():
    img = new((22, 26, 40))
    r = np.random.default_rng(121)
    for _ in range(26):
        x, y = r.integers(0, T, 2)
        horiz = r.random() < 0.5
        for i in range(r.integers(8, 30)):
            xx, yy = ((x + i) % T, y) if horiz else (x, (y + i) % T)
            img[yy, xx] = (40, 170, 200)
        img[y, x] = (180, 255, 255)
    return ao_edges(grime(img, 0.25, 122, False), 0.3, 3)


def top_beam():
    img = new((50, 54, 60))
    bevel(img, 0, 0, 64, 12, (66, 68, 74))                       # I-beam flange under the ceiling
    for x in range(4, 64, 8):
        rivet(img, x, 4)
    inset(img, 0, 12, 64, 22, (40, 42, 46))
    rect(img, 0, 30, 64, 38, (28, 28, 32))                       # cable tray
    for i, c in enumerate(((150, 30, 26), (40, 80, 150), (170, 140, 30), (40, 110, 60))):
        rect(img, 0, 31 + i * 2, 64, 32 + i * 2, c)
    for x in range(0, 64, 16):
        img[28:40, x:x + 2] = (90, 90, 96)
    rect(img, 0, 46, 64, 60, (70, 74, 82))
    for x in range(2, 64, 16):
        bevel(img, x, 48, x + 12, 58, (80, 84, 92))
    return grime(img, 0.35, 222)


def door_jamb_l():
    img = panel_base((86, 92, 102), seam=False)
    bevel(img, 0, 0, 52, 64, (88, 94, 104))
    for y in range(0, 64, 10):                                  # chevrons on the door side (right edge)
        for k in range(5):
            img[(y + k) % T, 52:64] = (210, 170, 30)
            img[(y + k + 5) % T, 52:64] = (30, 28, 26)
    rect(img, 20, 10, 32, 16, (200, 40, 30)); img[12, 22:30] = (255, 160, 140)   # warning lamp
    text(img, '>>', 18, 30, (230, 190, 40), 2)
    kickplate(img)
    return floor_ao(grime(img, 0.3, 231))


def door_jamb_r():
    return door_jamb_l()[:, ::-1].copy()


def door_lintel():
    img = panel_base((86, 92, 102), seam=False)
    hazard_band(img, 50, 64)
    bevel(img, 14, 14, 50, 36, (30, 30, 34), w=2)
    rect(img, 16, 16, 48, 34, (30, 120, 60))
    ctext(img, 'EXIT', 21, (220, 255, 220), 1)
    text(img, '>', 42, 21, (220, 255, 220))
    rect(img, 4, 4, 10, 10, (200, 40, 30)); rect(img, 54, 4, 60, 10, (200, 40, 30))
    return grime(img, 0.3, 232)


# ================================================================ WALL2: rooms and halls
def dock_plate():
    img = new((96, 92, 84))
    for x in range(0, 64, 8):                                   # corrugated sheet
        img[:, x:x + 4] *= 1.15
        img[:, x + 4:x + 8] *= 0.8
    rect(img, 0, 0, 64, 3, (60, 60, 64)); rect(img, 0, 61, 64, 64, (60, 60, 64))
    r = np.random.default_rng(3)
    for _ in range(4):                                          # dents
        x, y = r.integers(6, 58, 2)
        img[y - 2:y + 3, x - 2:x + 3] *= 0.75
    return floor_ao(grime(img, 0.45, 241))


def dock_number():
    img = dock_plate()
    hazard_band(img, 0, 6); hazard_band(img, 58, 64, True)
    text(img, 'DOCK', 8, 14, (230, 200, 60), 3)
    text(img, '07', 18, 34, (230, 230, 220), 4)
    return img


def checkpoint_panel():
    img = panel_base((70, 74, 86))
    bevel(img, 18, 8, 46, 40, (40, 42, 50), w=2)
    rect(img, 21, 11, 43, 37, (60, 14, 14))
    for y in range(13, 36, 4):
        img[y, 22:42] = (255, 60, 50)                           # red scan lines
    ctext(img, 'CHECKPOINT', 46, (230, 60, 50))
    ctext(img, 'ID REQUIRED', 53, (200, 200, 200))
    return grime(img, 0.3, 242)


def processing_panel():
    img = panel_base((86, 92, 96))
    rect(img, 0, 22, 64, 34, (40, 42, 46))
    for x in range(2, 64, 8):                                   # painted conveyor arrows
        for k in range(4):
            img[24 + k, x + k] = (230, 190, 40); img[31 - k, x + k] = (230, 190, 40)
    ctext(img, 'PROCESSING', 8, (230, 190, 40))
    ctext(img, 'LINE 3', 42, (200, 200, 200))
    return grime(img, 0.35, 243)


def extraction_panel():
    img = panel_base((84, 92, 104))
    for x in (12, 32, 52):                                      # painted tubes with glowing fluid
        rect(img, x - 4, 6, x + 4, 48, (40, 50, 60))
        rect(img, x - 2, 8, x + 2, 46, (40, 160, 190))
        img[8:46, x - 1] = (150, 240, 255)
    ctext(img, 'EXTRACTION', 52, (90, 230, 255))
    return grime(img, 0.3, 244)


def cell_wall():
    img = new((96, 90, 86))
    img *= (0.75 + 0.35 * noise(3, 245))[..., None]             # concrete
    for y in (20, 42):
        img[y] *= 0.6
    r = np.random.default_rng(9)
    for _ in range(3):                                          # dried stains
        x, y = r.integers(8, 56, 2)
        yy, xx = np.mgrid[0:T, 0:T]
        m = ((xx - x) ** 2 + (yy - y) ** 2 * 0.5) < r.integers(20, 60)
        img[m] = img[m] * 0.6 + np.array((80, 10, 10)) * 0.4
    for k in range(4):                                          # claw marks
        for i in range(14):
            img[10 + i, 40 + k * 3 + i // 4] = (40, 36, 34)
    return floor_ao(img)


def cell_warning():
    img = cell_wall()
    rect(img, 6, 6, 58, 30, (200, 160, 30))
    rect(img, 8, 8, 56, 28, (30, 28, 26))
    ctext(img, 'CELL', 10, (230, 190, 40), 1)
    ctext(img, 'DO NOT FEED', 18, (230, 60, 40))
    return img


def lab_panel():
    img = panel_base((142, 148, 152))
    rect(img, 0, 40, 64, 46, (40, 110, 190))
    img[41] = (120, 190, 240)
    return grime(img, 0.18, 246, False)


def lab_poster():
    img = lab_panel()
    poster(img, 14, 4, 50, 38, (230, 230, 225), (40, 110, 190), tear=False)
    skull_icon(img, 25, 7, (90, 90, 90), (200, 40, 30), s=2)
    ctext(img, 'SPECIMEN', 24, (40, 110, 190))
    ctext(img, 'HANDLING', 30, (40, 110, 190))
    return img


def vault_panel():
    img = new((26, 28, 36))
    for y in range(4, 60, 8):
        bevel(img, 4, y, 60, y + 6, (38, 40, 50))
        r = np.random.default_rng(y + 7)
        for x in range(8, 56, 4):
            if r.random() < 0.5:
                img[y + 3, x] = [(40, 255, 80), (60, 140, 255), (255, 180, 40)][r.integers(0, 3)]
    return img


def gallery_pipes():
    img = new((70, 74, 80))
    hpipe(img, 9, 6, (120, 128, 132))
    hpipe(img, 26, 5, (150, 80, 40))
    hpipe(img, 40, 3, (60, 110, 150))
    hpipe(img, 50, 3, (120, 128, 132))
    valve(img, 30, 26, (230, 190, 40))
    return floor_ao(grime(img, 0.4, 247))


def turbine_panel():
    img = panel_base((78, 92, 84))
    for (x, y) in ((16, 20), (48, 20)):                         # gauges
        yy, xx = np.mgrid[0:T, 0:T]
        d = np.hypot(xx - x, yy - y)
        img[d < 9] = (30, 32, 30); img[d < 7.5] = (210, 210, 190)
        for k in range(6):
            img[y - k, x + k // 2] = (200, 30, 30)
    ctext(img, 'TURBINE', 38, (230, 190, 40))
    ctext(img, 'MAX 9000', 45, (200, 200, 200))
    return grime(img, 0.35, 248)


def reactor_plate():
    img = new((56, 52, 50))
    for py in (0, 32):
        for px in (0, 32):
            bevel(img, px + 1, py + 1, px + 31, py + 31, (60, 56, 54), w=2)
            for (rx, ry) in ((px + 4, py + 4), (px + 26, py + 4), (px + 4, py + 26), (px + 26, py + 26)):
                rivet(img, rx, ry, (120, 110, 100))
    yy, xx = np.mgrid[0:T, 0:T]
    a = np.arctan2(yy - 32, xx - 32); d = np.hypot(xx - 32, yy - 32)
    tre = (d > 4) & (d < 14) & ((np.floor((a + np.pi) / (np.pi / 3)) % 2) == 0)
    img[tre] = (220, 110, 30)
    img[d < 3] = (220, 110, 30)
    return floor_ao(grime(img, 0.4, 249))


def reactor_warning():
    img = reactor_plate()
    rect(img, 4, 2, 60, 18, (220, 110, 30)); rect(img, 6, 4, 58, 16, (30, 26, 24))
    ctext(img, 'REACTOR', 5, (220, 110, 30))
    ctext(img, 'CORE 5', 11, (230, 230, 220))
    return img


def hall_wall():
    img = panel_base((86, 92, 102))
    hpipe(img, 8, 3, (120, 128, 132))
    hpipe(img, 17, 2, (150, 80, 40))
    rect(img, 4, 34, 60, 37, (60, 160, 170)); img[35, 4:60] = (150, 235, 240)    # guide light strip
    kickplate(img)
    hazard_band(img, 60, 64)
    return floor_ao(grime(img, 0.35, 251))


def hall_wall_top():
    img = new((54, 58, 64))
    rect(img, 0, 2, 64, 12, (28, 28, 32))                       # cable tray
    for i, c in enumerate(((150, 30, 26), (40, 80, 150), (170, 140, 30), (40, 110, 60))):
        rect(img, 0, 4 + i * 2, 64, 5 + i * 2, c)
    for x in range(0, 64, 16):
        img[0:14, x:x + 2] = (90, 90, 96)
    bevel(img, 0, 16, 64, 32, (62, 66, 72))
    bevel(img, 0, 32, 64, 64, (84, 90, 100))
    rect(img, 24, 36, 40, 42, (220, 240, 245))                 # bulkhead lamp
    img[42:44, 24:40] *= 0.5
    return grime(img, 0.3, 252)


# ================================================================ FLOOR: floors / ceilings (2 m tiles)
def plates(col=(78, 82, 90), tread=True, seed=301):
    img = new(col)
    for py in (0, 32):
        for px in (0, 32):
            bevel(img, px + 1, py + 1, px + 31, py + 31, col)
            if tread:
                for y in range(py + 4, py + 29, 4):
                    for x in range(px + 4 + (y // 4 % 2) * 2, px + 29, 4):
                        img[y, x] *= 1.35; img[y + 1, x + 1] *= 0.6
            for (rx, ry) in ((px + 3, py + 3), (px + 27, py + 3), (px + 3, py + 27), (px + 27, py + 27)):
                rivet(img, rx, ry)
    grime(img, 0.4, seed)
    scratches(img, 14, seed + 1)
    return ao_edges(img, 0.25, 2)


def floor_plate():
    return plates()


def floor_grate():
    img = new((40, 44, 48))
    glow = noise(4, 9)
    for y in range(3, 61):
        for x in range(3, 61):
            if (x % 6) in (0, 1) or (y % 12) in (0, 1):
                img[y, x] = (62, 66, 72)
            else:
                g = 0.5 + 0.5 * glow[y, x]
                img[y, x] = (10 + 20 * g, 60 * g + 20, 70 * g + 24)
    for (a, b, c, d) in ((0, 0, 64, 3), (0, 61, 64, 64), (0, 0, 3, 64), (61, 0, 64, 64)):
        bevel(img, a, b, c, d, (70, 74, 80))
    return grime(img, 0.25, 12, False)


def floor_edge():
    img = plates(seed=303)
    rect(img, 0, 54, 64, 60, (210, 170, 30))
    return img


def floor_hazard():
    img = new((80, 80, 84))
    hazard_band(img, 0, 64)
    grime(img, 0.45, 304)
    return scratches(img, 18, 304, (120, 110, 90))


def floor_arrow():
    img = plates(seed=305)
    for k in range(14):
        img[10 + k, 32 - k:32 + k + 1] = (210, 170, 30)
    rect(img, 26, 24, 38, 50, (210, 170, 30))
    return img


def floor_strip():
    img = plates(seed=306)
    rect(img, 24, 0, 40, 64, (30, 32, 36))
    rect(img, 27, 0, 37, 64, (40, 200, 210))
    rect(img, 29, 0, 35, 64, (170, 255, 255))
    for y in range(0, 64, 8):
        img[y, 27:37] = (30, 120, 130)
    return img


def floor_drain():
    img = plates(seed=307)
    yy, xx = np.mgrid[0:T, 0:T]
    d = np.hypot(xx - 32, yy - 32)
    img[d < 14] = (50, 52, 56); img[d < 12] = (34, 36, 40)
    for k in range(-10, 11, 4):
        img[(np.abs(yy - 32 - k) < 1) & (d < 11)] = (70, 72, 78)
    return img


def floor_concrete():
    img = new((92, 90, 86))
    img *= (0.75 + 0.35 * noise(4, 308))[..., None]
    img[31:33] *= 0.6; img[:, 31:33] *= 0.6
    return grime(img, 0.3, 308)


def ceiling_panel():
    img = new((52, 56, 62))
    for py in (0, 32):
        for px in (0, 32):
            bevel(img, px + 1, py + 1, px + 31, py + 31, (56, 60, 66))
    return grime(img, 0.25, 309, False)


def ceiling_light():
    img = ceiling_panel()
    inset(img, 8, 20, 56, 44, (40, 40, 44))
    rect(img, 10, 22, 54, 42, (200, 235, 240))
    rect(img, 12, 26, 52, 38, (245, 252, 255))
    for x in range(14, 52, 6):
        img[22:42, x] = (150, 190, 200)
    return img


def ceiling_cables():
    img = ceiling_panel()
    rect(img, 16, 0, 48, 64, (24, 24, 28))
    for i, c in enumerate(((150, 30, 26), (30, 30, 34), (40, 80, 150), (30, 30, 34), (170, 140, 30), (40, 110, 60), (150, 30, 26))):
        band = np.array(c, float) * (0.5 + 0.8 * np.sin(np.linspace(0.3, np.pi - 0.3, 4)))[None, :, None]
        img[:, 18 + i * 4:22 + i * 4] = band
    for y in range(0, 64, 16):
        img[y:y + 2, 14:50] = (90, 90, 96)
    return img


def stair_tread():
    img = new((80, 82, 88))
    rect(img, 0, 0, 64, 6, (205, 165, 30))
    for x in range(0, 64, 8):
        img[0:6, x:x + 4] = (30, 28, 26)
    for y in range(10, 60, 5):
        img[y] *= 1.3; img[y + 1] *= 0.6
    grime(img, 0.4, 311)
    return ao_edges(img, 0.25, 3)


def floor_lab():
    img = new((138, 142, 146))
    for py in range(0, 64, 16):
        for px in range(0, 64, 16):
            bevel(img, px, py, px + 16, py + 16, (140, 144, 148), hi=1.1, lo=0.8)
    return grime(img, 0.15, 312, False)


def floor_reactor():
    img = new((40, 36, 34))
    glow = noise(4, 313)
    for y in range(64):
        for x in range(64):
            if (x % 8) >= 2 and (y % 8) >= 2:
                g = glow[y, x]
                img[y, x] = (60 + 140 * g, 24 + 50 * g, 10)
    return img


def floor_cell():
    img = floor_concrete()
    r = np.random.default_rng(14)
    yy, xx = np.mgrid[0:T, 0:T]
    for _ in range(3):
        x, y = r.integers(8, 56, 2)
        m = ((xx - x) ** 2 + (yy - y) ** 2) < r.integers(30, 90)
        img[m] = img[m] * 0.55 + np.array((70, 8, 8)) * 0.45
    return img


def floor_dock():
    img = plates((84, 84, 86), tread=False, seed=315)
    for x in (14, 46):                                          # tyre marks
        img[:, x:x + 6] *= 0.7
        img[::3, x:x + 6] *= 0.85
    return img


# ================================================================ PROPS
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
    yy, xx = np.mgrid[0:T, 0:T]
    for x in (14, 34, 50):
        d = (xx - x) ** 2 + (yy - 48) ** 2
        img[d < 36] = (40, 42, 46); img[d < 25] = (200, 200, 190)
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
        img[min(a, b):max(a, b) + 1, i + 2] = (80, 255, 120)
    text(img, 'OUTPUT 98%', 2, 56, (120, 255, 150))
    img[0:2] = img[62:64] = (60, 66, 70); img[:, 0:2] = img[:, 62:64] = (60, 66, 70)
    return img


def screen_vitals():
    img = new((26, 6, 8))
    beat = [0, 0, 0, 0, 1, -6, 10, -3, 0, 0, 0, 0, 0, 0, 0, 0]
    prev = 20
    for x in range(2, 62):
        y = 20 + beat[x % 16]
        img[min(prev, y):max(prev, y) + 1, x] = (255, 70, 60)
        prev = y
    text(img, 'SHINIGAMI', 2, 34, (255, 120, 100))
    text(img, 'SOUL 07%', 2, 42, (255, 200, 80))
    rect(img, 2, 52, 62, 58, (60, 14, 14)); rect(img, 2, 52, 9, 58, (255, 60, 40))
    img[0:2] = img[62:64] = (60, 66, 70); img[:, 0:2] = img[:, 62:64] = (60, 66, 70)
    return img


def server_rack():
    img = vault_panel()
    return img


def glass():
    img = new((70, 120, 140))
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


def cyl_shade(img, axis=0, lo=0.45, hi=1.35):
    t = np.linspace(0, np.pi, T)
    k = lo + (hi - lo) * np.sin(t) ** 1.5
    k = k + 0.25 * np.exp(-((np.linspace(0, 1, T) - 0.32) / 0.06) ** 2)
    img *= (k[:, None, None] if axis == 0 else k[None, :, None])
    return img


def coil():
    img = new((150, 90, 40))
    for y in range(0, T, 4):
        img[y] *= 1.4; img[y + 2] *= 0.6; img[y + 3] *= 0.5
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
    img[0:4] = (80, 80, 86); img[60:64] = (80, 80, 86); img[30:33] = (70, 70, 76)
    return img


def fan():
    img = np.zeros((T, T, 3))
    y, x = np.mgrid[0:T, 0:T] - 31.5
    r = np.hypot(x, y); a = np.arctan2(y, x)
    blade = (np.sin(a * 5 + r * 0.08) > 0.3) & (r < 30) & (r > 6)
    img[blade] = (110, 116, 124)
    img[blade] *= (0.6 + 0.6 * (np.sin(a * 5 + r * 0.08)[blade, None]))
    img[r < 7] = (70, 72, 80); img[r < 3] = (150, 150, 160)
    img[(r >= 30) & (r < 32)] = (60, 62, 68)
    return img


def shini_body():
    img = new((30, 22, 36))
    yy, xx = np.mgrid[0:T, 0:T]
    for y in range(0, T, 6):
        for x in range(-(y // 6 % 2) * 4, T, 8):
            m = ((xx - x - 4) ** 2 / 16 + (yy - y - 4) ** 2 / 25) < 1
            img[m] = img[m] * 0.5 + np.array((60, 44, 70)) * 0.5
    img[yy % 6 == 5] *= 0.5
    rect(img, 30, 0, 34, 64, (170, 160, 140))
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
    for ex in (22, 42):
        img[((xx - ex) ** 2 + (yy - 24) ** 2) < 30] = (255, 120, 20)
        img[((xx - ex) ** 2 + (yy - 24) ** 2) < 8] = (255, 230, 150)
    img[beak & (yy == 44)] *= 0.4
    return img


def lamp_glow():
    yy, xx = np.mgrid[0:T, 0:T] - 31.5
    k = np.clip(1 - np.hypot(xx, yy) / 32, 0, 1) ** 2
    return np.stack([255 * k, 230 * k, 200 * k], -1)


# ================================================================ TRIM: for 3D props
def dark_metal():
    img = new((54, 56, 60))
    bevel(img, 0, 0, 64, 64, (56, 58, 62), w=2)
    grime(img, 0.45, 131)
    return scratches(img, 20, 132, (110, 110, 115))


def hazard():
    img = np.zeros((T, T, 3))
    y, x = np.mgrid[0:T, 0:T]
    s = ((x + y) // 8) % 2 == 0
    img[s] = (205, 165, 30); img[~s] = (30, 28, 26)
    grime(img, 0.45, 31)
    scratches(img, 18, 8, (120, 110, 90))
    return ao_edges(img, 0.3, 3)


def pipe():
    img = new((120, 128, 132))
    for x in range(0, T, 16):
        img[:, x:x + 3] *= 0.6; img[:, x + 3] *= 1.3
    grime(img, 0.3, 61, False)
    return cyl_shade(img, 0)


def bulkhead():
    img = new((84, 88, 96))
    bevel(img, 0, 0, 64, 64, (86, 90, 98), w=3)
    for i in range(0, 64, 12):
        for k in range(6):
            yy = (i + k) % T
            img[yy, 1:4] = (210, 170, 30) if k < 3 else (30, 28, 26)
            img[yy, 60:63] = (210, 170, 30) if k < 3 else (30, 28, 26)
    inset(img, 10, 10, 54, 54, (64, 68, 76))
    return grime(img, 0.3, 71)


def beam():
    img = new((56, 58, 62))
    bevel(img, 0, 0, 64, 10, (70, 72, 76)); bevel(img, 0, 54, 64, 64, (70, 72, 76))
    inset(img, 0, 10, 64, 54, (48, 50, 54))
    for x in range(4, 64, 8):
        rivet(img, x, 3); rivet(img, x, 58)
    return grime(img, 0.35, 81)


def cables():
    img = new((20, 20, 22))
    cols = [(150, 30, 26), (30, 30, 34), (40, 80, 150), (30, 30, 34), (170, 140, 30), (40, 110, 60), (30, 30, 34), (150, 30, 26)]
    for i, c in enumerate(cols):
        band = np.array(c, float) * (0.5 + 0.8 * np.sin(np.linspace(0.2, np.pi - 0.2, 8)))[:, None]
        img[i * 8:i * 8 + 8] = band[:, None, :]
    for x in range(0, T, 21):
        img[:, x:x + 2] = (90, 90, 95)
    return grime(img, 0.2, 91, False)


def floor_light():
    img = new((36, 38, 42))
    bevel(img, 0, 0, 64, 64, (40, 42, 46), w=3)
    rect(img, 6, 22, 58, 42, (40, 200, 210)); rect(img, 8, 27, 56, 37, (170, 255, 255))
    for x in range(10, 56, 8):
        img[22:42, x] = (30, 120, 130)
    return img


def plate_side():
    img = new((70, 72, 78))
    bevel(img, 0, 0, 64, 64, (72, 74, 80), w=2)
    for x in range(6, 64, 14):
        rivet(img, x, 6); rivet(img, x, 54)
    img[30:34] *= 0.7
    return grime(img, 0.4, 51)


def catwalk():
    return floor_grate()


def circuit():
    return top_circuit()


def generator_side():
    img = turbine_panel()
    return img


# ================================================================ ANIM: animated screens
# Each screen is a strip of 4 frames along one row of the page; faces map frame 0 and the game
# steps the U coordinate along the strip (+64 texels per frame), the PS1 way.
SCREEN_FRAMES = 4


def _graph_screen(img, x0, y0, x1, y1, k, pct):
    rect(img, x0, y0, x1, y1, (8, 26, 16))
    for x in range(x0, x1, 6):
        img[y0:y1, x] = (16, 46, 28)
    for y in range(y0, y1, 6):
        img[y, x0:x1] = (16, 46, 28)
    w = x1 - x0
    ph = k * 2 * np.pi / SCREEN_FRAMES
    mid = (y0 + y1 - 8) / 2
    amp = (y1 - y0 - 12) / 3
    pts = mid + amp * np.sin(np.linspace(0, 4 * np.pi, w) + ph) * (0.7 + 0.3 * np.cos(np.linspace(0, 2 * np.pi, w) - ph))
    for i in range(w - 1):
        a, b = int(pts[i]), int(pts[i + 1])
        img[min(a, b):max(a, b) + 1, x0 + i] = (90, 255, 130)
    text(img, 'OUT %d%%' % pct, x0 + 2, y1 - 6, (150, 255, 170))


def _vitals_screen(img, x0, y0, x1, y1, k, soul):
    rect(img, x0, y0, x1, y1, (26, 6, 8))
    beat = [0, 0, 0, 0, 0, 1, -5, 8, -3, 0, 0, 0, 0, 0, 0, 0]
    base = y0 + 7
    prev = base
    for x in range(x0 + 1, x1 - 1):
        y = base + beat[(x - k * 4) % 16]
        img[min(prev, y):max(prev, y) + 1, x] = (255, 70, 60)
        prev = y
    text(img, 'SOUL %02d%%' % soul, x0 + 2, y0 + 17, (255, 200, 80))
    if k % 2 == 0:
        text(img, 'CRITICAL', x0 + 2, y0 + 24, (255, 60, 40))
    bw = (x1 - x0 - 4) * soul // 100 + 1
    rect(img, x0 + 2, y1 - 5, x1 - 2, y1 - 2, (60, 14, 14))
    rect(img, x0 + 2, y1 - 5, x0 + 2 + bw, y1 - 2, (255, 60, 40))


def _wall_frame(kind, k):
    def f():
        img = panel_base((92, 98, 108))
        bevel(img, 5, 6, 59, 48, (36, 38, 44), w=2)              # monitor bezel on the wall
        if kind == 'graph':
            _graph_screen(img, 8, 9, 56, 45, k, (97, 98, 99, 98)[k])
        else:
            _vitals_screen(img, 8, 9, 56, 45, k, (7, 7, 6, 6)[k])
        img[9:45, 8] *= 1.3
        rect(img, 27, 48, 37, 54, (50, 52, 58))                  # wall bracket
        img[54:56, 25:39] *= 0.5                                 # its shadow
        rect(img, 50, 49, 53, 51, (40, 255, 80) if k % 2 else (20, 90, 30))   # power led
        return grime(img, 0.2, 261)
    f.__name__ = 'wall_%s%s' % (kind, '' if k == 0 else '_f%d' % k)
    return f


def _bare_frame(kind, k):
    def f():
        img = new((60, 66, 70))
        if kind == 'graph':
            _graph_screen(img, 2, 2, 62, 62, k, (97, 98, 99, 98)[k])
        else:
            _vitals_screen(img, 2, 2, 62, 62, k, (7, 7, 6, 6)[k])
        return img
    f.__name__ = 'scr_%s%s' % (kind, '' if k == 0 else '_f%d' % k)
    return f


ANIM = [_wall_frame('graph', k) for k in range(4)] + [_wall_frame('vitals', k) for k in range(4)] + \
       [_bare_frame('graph', k) for k in range(4)] + [_bare_frame('vitals', k) for k in range(4)]
ANIMATED = {'wall_graph', 'wall_vitals', 'scr_graph', 'scr_vitals'}


WALL1 = [base_plain, base_pipes, base_vent, base_hazard, mid_panel, mid_poster_power, mid_poster_danger,
         mid_authorized, mid_vpipes, mid_monitor, mid_arrow, top_circuit, top_beam, door_jamb_l, door_jamb_r, door_lintel]
WALL2 = [dock_plate, dock_number, checkpoint_panel, processing_panel, extraction_panel, cell_wall, cell_warning,
         lab_panel, lab_poster, vault_panel, gallery_pipes, turbine_panel, reactor_plate, reactor_warning,
         hall_wall, hall_wall_top]
FLOOR = [floor_plate, floor_grate, floor_edge, floor_hazard, floor_arrow, floor_strip, floor_drain, floor_concrete,
         ceiling_panel, ceiling_light, ceiling_cables, stair_tread, floor_lab, floor_reactor, floor_cell, floor_dock]
PROPS = [machine_panel, screen_graph, screen_vitals, server_rack, glass, energy, coil, crate,
         canister, machine_base, keyboard, cage_bars, fan, shini_body, shini_skull, lamp_glow]
TRIM = [dark_metal, hazard, pipe, bulkhead, beam, cables, floor_light, plate_side, catwalk, generator_side]
PAGES = [WALL1, PROPS, WALL2, FLOOR, TRIM, ANIM]         # page index = position here
PAGE_VRAM = [(384, 0), (512, 0), (384, 256), (512, 256), (640, 256), (896, 0)]
PAGE_NAMES = ['wall1', 'props', 'wall2', 'floor', 'trim', 'anim']
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
    if len(funcs) < 16:
        mask[(len(funcs) + 3) // 4 * 64:] = True                    # unused rows: transparent
        for i in range(len(funcs), (len(funcs) + 3) // 4 * 4):
            mask[(i // 4) * 64:(i // 4) * 64 + 64, (i % 4) * 64:(i % 4) * 64 + 64] = True
    return page, mask


DM = np.array([[-4, 0, -3, 1], [2, -2, 3, -1], [-3, 1, -4, 0], [3, -1, 2, -2]], float) * 2.0


def quantise(page, mask):
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


# name -> (page, tile)
INDEX = {}
for _p, _funcs in enumerate(PAGES):
    for _i, _f in enumerate(_funcs):
        INDEX.setdefault(_f.__name__, (_p, _i))
