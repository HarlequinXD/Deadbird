#!/usr/bin/env python3
"""Deadbird sound effects and ambience, synthesised from scratch (no recorded samples).

Sci-fi hell power plant: heavy hooves on steel grating, a two-stroke chainsaw that idles and
revs, booming landings, hellish UI chimes, mains hum and a throbbing reactor drone, steam
bursts, distant metal clanks and the moans of captive shinigami.

Writes
  assets/sfx_pcm.h    signed 8-bit PCM per sound (+ rate, loop flag) for the software mixer
                      (Android, Windows, headless)
  assets/sfx_adpcm.h  the same sounds as PS1 SPU ADPCM (16-byte blocks, loop flags set) for the
                      PlayStation build
  docs/sfx/*.wav      previews
"""
import os, math, struct, wave
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rng = np.random.default_rng(666)
SR_HI, SR_LO = 22050, 11025


# ---------------------------------------------------------------- building blocks
def t_(n, sr):
    return np.arange(n) / sr


def env_exp(n, sr, tau, attack=0.002):
    t = t_(n, sr)
    a = np.clip(t / max(attack, 1e-6), 0, 1)
    return a * np.exp(-t / tau)


def noise(n):
    return rng.uniform(-1, 1, n)


def onepole_lp(x, sr, fc):
    a = math.exp(-2 * math.pi * fc / sr)
    y = np.empty_like(x); s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s; y[i] = s
    return y


def onepole_hp(x, sr, fc):
    return x - onepole_lp(x, sr, fc)


def bandpass(x, sr, fc, q):
    """RBJ biquad band-pass"""
    w = 2 * math.pi * fc / sr; al = math.sin(w) / (2 * q)
    b0, b1, b2 = al, 0, -al
    a0, a1, a2 = 1 + al, -2 * math.cos(w), 1 - al
    b0, b1, b2, a1, a2 = b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0
    y = np.zeros_like(x); x1 = x2 = y1 = y2 = 0.0
    for i, v in enumerate(x):
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, v, y1, o
        y[i] = o
    return y


def sweep_sine(n, sr, f0, f1, curve=3.0):
    t = t_(n, sr)
    f = f1 + (f0 - f1) * np.exp(-t * curve / (n / sr))
    return np.sin(2 * np.pi * np.cumsum(f) / sr)


def metal(n, sr, base, tau, parts=(1.0, 2.76, 5.40, 8.93), amps=(1, .6, .4, .25)):
    """inharmonic struck-metal partials"""
    t = t_(n, sr); y = np.zeros(n)
    for p, a in zip(parts, amps):
        y += a * np.sin(2 * np.pi * base * p * t + rng.uniform(0, 6)) * np.exp(-t / (tau / (0.6 + p * 0.25)))
    return y


def saw(f, n, sr, phase=0.0):
    ph = (np.cumsum(np.full(n, f) if np.isscalar(f) else f) / sr + phase) % 1.0
    return 2 * ph - 1


def formant(x, sr, freqs=((600, 6), (1040, 8), (2250, 10)), gains=(1, .5, .25)):
    return sum(g * bandpass(x, sr, f, q) for (f, q), g in zip(freqs, gains))


def reverb(x, sr, decay=1.2, mix=0.35, damp=3000):
    """small Schroeder reverb (4 combs + 2 allpasses)"""
    x = fade(x, sr, 0, 0.06)                 # no click where the dry sound ends
    out = np.zeros(len(x) + int(sr * decay))
    xx = np.concatenate([x, np.zeros(len(out) - len(x))])
    for d in (0.0297, 0.0371, 0.0411, 0.0437):
        D = int(d * sr); g = 10 ** (-3 * d / decay)
        y = xx.copy()
        for i in range(D, len(y)):
            y[i] += g * y[i - D]
        out += onepole_lp(y, sr, damp) / 4
    for d, g in ((0.005, 0.7), (0.0017, 0.7)):
        D = max(1, int(d * sr)); y = np.zeros_like(out)
        for i in range(len(out)):
            xd = out[i - D] if i >= D else 0.0
            yd = y[i - D] if i >= D else 0.0
            y[i] = -g * out[i] + xd + g * yd
        out = y
    res = xx * (1 - mix) + out * mix
    return res


def norm(x, peak=0.95):
    m = np.max(np.abs(x))
    return x * (peak / m) if m > 0 else x


def fade(x, sr, fin=0.002, fout=0.02):
    n = len(x); a = np.ones(n)
    i = int(fin * sr); o = int(fout * sr)
    if i: a[:i] = np.linspace(0, 1, i)
    if o: a[-o:] *= np.linspace(1, 0, o)
    return x * a


def make_loop(fn, n, sr, xf=None):
    """render n + crossfade samples and fold the tail onto the head so the loop is seamless"""
    xf = xf or n // 8
    y = fn(n + xf)
    head, tail = y[:xf], y[n:n + xf]
    w = np.linspace(0, 1, xf)
    y = y[:n].copy()
    y[:xf] = head * w + tail * (1 - w)
    return y


def n28(sec, sr):
    """sample count rounded to whole PS1 ADPCM blocks (28 samples)"""
    return int(round(sec * sr / 28)) * 28


SOUNDS = []   # (name, data float -1..1, sr, loop)


def add(name, y, sr, loop=False, peak=0.95):
    if not loop:
        y = fade(y, sr)
    SOUNDS.append((name, norm(y, peak), sr, loop))


# ---------------------------------------------------------------- footsteps: hoof on steel grating
def hoof(sr, weight, seed):
    r = np.random.default_rng(seed)
    n = int(0.42 * sr)
    t = t_(n, sr)
    thump = sweep_sine(n, sr, 95 * weight ** -0.3, 42, 5) * env_exp(n, sr, 0.045 * weight)
    clank = metal(n, sr, r.uniform(330, 410), 0.09, amps=(0.8, .5, .45, .3)) * env_exp(n, sr, 0.05)
    grit = onepole_lp(noise(n), sr, 2500) * env_exp(n, sr, 0.018)
    rattle = bandpass(noise(n), sr, 3200, 3) * env_exp(n, sr, 0.035) * (r.uniform(.6, 1))
    y = 1.6 * thump + 0.45 * clank + 0.7 * grit + 0.25 * rattle
    return np.tanh(1.4 * y)


add('step', hoof(SR_HI, 1.0, 1), SR_HI)
add('step_run', np.concatenate([hoof(SR_HI, 1.35, 2)[:int(0.36 * SR_HI)]]), SR_HI)

# ---------------------------------------------------------------- landing: boom + grating crash + saw clatter
n = int(0.9 * SR_HI)
boom = sweep_sine(n, SR_HI, 120, 30, 5) * env_exp(n, SR_HI, 0.18)
crash = metal(n, SR_HI, 210, 0.35, parts=(1, 2.3, 3.9, 6.1, 8.7), amps=(.7, .6, .5, .35, .2))
grit = onepole_lp(noise(n), SR_HI, 3000) * env_exp(n, SR_HI, 0.05)
t = t_(n, SR_HI)
clatter = bandpass(noise(n), SR_HI, 2400, 2) * (np.sin(2 * np.pi * 26 * t) > 0.6) * env_exp(n, SR_HI, 0.12, 0.03)
land = np.tanh(1.8 * boom + 0.5 * crash + 0.6 * grit + 0.35 * clatter)
add('land', reverb(land, SR_HI, 0.8, 0.25)[:int(1.1 * SR_HI)], SR_HI)

# ---------------------------------------------------------------- jump: whoosh + demonic exhale
n = int(0.45 * SR_HI)
t = t_(n, SR_HI)
sw = np.sin(np.pi * np.clip(t / 0.4, 0, 1)) ** 2
whoosh = bandpass(noise(n), SR_HI, 900, 1.2) * sw
f = 120 - 30 * t / 0.45
breath = formant(saw(f, n, SR_HI) * 0.5 + noise(n) * 0.5, SR_HI, ((500, 5), (900, 6), (2400, 8))) * env_exp(n, SR_HI, 0.12, 0.03)
add('jump', 1.2 * whoosh + 0.9 * breath, SR_HI)


# ---------------------------------------------------------------- chainsaw loops (two-stroke engine)
def engine(nn, sr, fire, harsh, whine):
    t = t_(nn, sr)
    ph = (t * fire) % 1.0
    pulse = np.exp(-ph * 9) * (1 + 0.25 * np.sin(2 * np.pi * t * fire * 0.5))   # uneven firing
    body = bandpass(pulse - pulse.mean(), sr, fire * 3.1, 1.2) * 3 + bandpass(pulse, sr, fire * 1.0, 1.5) * 2
    rasp = onepole_hp(noise(nn), sr, 1500) * pulse * harsh
    chain = bandpass(noise(nn), sr, 3800, 4) * whine + whine * 0.3 * np.sin(2 * np.pi * 1180 * t) * (0.6 + 0.4 * pulse)
    y = body + rasp + chain
    return np.tanh(2.2 * y / (np.std(y) * 3 + 1e-9))


add('saw_idle', make_loop(lambda m: engine(m, SR_HI, 31, 0.5, 0.35), n28(0.581, SR_HI), SR_HI), SR_HI, loop=True, peak=0.8)
add('saw_rev', make_loop(lambda m: engine(m, SR_HI, 96, 1.0, 1.0), n28(0.5, SR_HI), SR_HI), SR_HI, loop=True, peak=0.85)

# ---------------------------------------------------------------- chainsaw combo: swing + slam
n = int(0.5 * SR_HI)
t = t_(n, SR_HI)
fire = 70 + 110 * np.clip(t / 0.18, 0, 1)                    # throttle snaps open
ph = np.cumsum(fire) / SR_HI
pulse = np.exp(-(ph % 1.0) * 7)
rev = bandpass(pulse - pulse.mean(), SR_HI, 400, 1.0) * 3 + onepole_hp(noise(n), SR_HI, 1500) * pulse * 1.2
scream = np.sin(2 * np.pi * np.cumsum(900 + 700 * np.clip(t / 0.2, 0, 1)) / SR_HI) * 0.35
air = bandpass(noise(n), SR_HI, 1100, 1.0) * np.sin(np.pi * np.clip(t / 0.32, 0, 1)) ** 2 * 1.6
sw_env = np.clip(t / 0.02, 0, 1) * np.exp(-np.maximum(t - 0.22, 0) / 0.09)
add('swing', np.tanh(1.6 * (rev + scream) * sw_env / 3) + air * 0.6, SR_HI)

n = int(1.0 * SR_HI)
t = t_(n, SR_HI)
boom = sweep_sine(n, SR_HI, 140, 32, 6) * env_exp(n, SR_HI, 0.22)
crash = metal(n, SR_HI, 260, 0.45, parts=(1, 2.2, 3.7, 5.3, 7.9), amps=(.8, .7, .6, .4, .3))
grind = bandpass(noise(n), SR_HI, 2600, 2) * (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 38 * t))) * env_exp(n, SR_HI, 0.25, 0.01)
slam = np.tanh(2.2 * boom + 0.6 * crash + 0.7 * grind + 0.6 * onepole_lp(noise(n), SR_HI, 2500) * env_exp(n, SR_HI, 0.04))
add('slam', reverb(slam, SR_HI, 1.0, 0.3)[:int(1.4 * SR_HI)], SR_HI)

# ---------------------------------------------------------------- blade impacts
n = int(0.6 * SR_HI)
t = t_(n, SR_HI)
screech = sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) * a for f, a in ((2150, 1), (3310, .7), (4870, .45), (1420, .5)))
screech *= (0.6 + 0.4 * np.sign(np.sin(2 * np.pi * 47 * t)))          # teeth chattering on the edge
crackle = onepole_hp(noise(n), SR_HI, 3000) * (rng.uniform(0, 1, n) > 0.93) * 2.5
clang = metal(n, SR_HI, 520, 0.12, parts=(1, 2.3, 3.9), amps=(1, .6, .4))
g_env = env_exp(n, SR_HI, 0.16, 0.003)
add('grind', np.tanh(1.4 * (0.45 * screech + crackle) * g_env + 0.8 * clang), SR_HI)

n = int(0.55 * SR_HI)
t = t_(n, SR_HI)
bog = 60 + 40 * np.exp(-t / 0.08)                                     # the engine bogs down in meat
ph = np.cumsum(bog) / SR_HI
pul = np.exp(-(ph % 1.0) * 6)
wet = bandpass(noise(n), SR_HI, 650, 1.3) * (0.5 + 0.5 * np.abs(np.sin(2 * np.pi * 23 * t)))
thud = sweep_sine(n, SR_HI, 110, 45, 6) * env_exp(n, SR_HI, 0.08)
rip = (bandpass(pul - pul.mean(), SR_HI, 300, 1) * 3 + wet * 1.4) * env_exp(n, SR_HI, 0.2, 0.004) + 1.4 * thud
add('rip', np.tanh(1.5 * rip), SR_HI)

# ---------------------------------------------------------------- UI
n = int(0.12 * SR_HI)
tick = metal(n, SR_HI, 1650, 0.03, parts=(1, 1.52, 2.8), amps=(1, .4, .2)) + 0.4 * sweep_sine(n, SR_HI, 400, 120, 6) * env_exp(n, SR_HI, 0.02)
add('ui_move', tick, SR_HI, peak=0.6)

n = int(0.9 * SR_HI)
t = t_(n, SR_HI)
bell = metal(n, SR_HI, 196, 0.6, parts=(1, 2.0, 2.97, 4.2, 5.43), amps=(1, .7, .5, .4, .25))
sub = np.tanh(3 * sweep_sine(n, SR_HI, 90, 41, 6) * env_exp(n, SR_HI, 0.15))
swell = bandpass(noise(n), SR_HI, 700, 2) * np.clip(1 - t / 0.25, 0, 1) * (t < 0.25)
ok = reverb(0.7 * bell + 0.8 * sub + 0.3 * swell, SR_HI, 1.4, 0.4)[:int(1.3 * SR_HI)]
add('ui_ok', ok, SR_HI, peak=0.8)

n = int(0.7 * SR_HI)
gong = metal(n, SR_HI, 98, 0.7, parts=(1, 1.47, 2.09, 2.56, 3.39), amps=(1, .8, .6, .4, .3))
add('pause', reverb(0.8 * gong + 0.8 * np.tanh(2 * sweep_sine(n, SR_HI, 70, 35, 4) * env_exp(n, SR_HI, 0.2)), SR_HI, 1.0, 0.35)[:int(1.1 * SR_HI)], SR_HI, peak=0.75)

# ---------------------------------------------------------------- ambience loops (11 kHz: all low end)
L = n28(2.4, SR_LO)


def hum(nn):
    t = t_(nn, SR_LO)
    y = (np.sin(2 * np.pi * 50 * t) + 0.6 * np.sin(2 * np.pi * 100 * t + 1) + 0.35 * np.sin(2 * np.pi * 150 * t + 2)
         + 0.2 * np.sin(2 * np.pi * 250 * t)) * (0.8 + 0.2 * np.sin(2 * np.pi * t / 1.2))
    air = onepole_lp(noise(nn), SR_LO, 300) * 0.5
    tone = 0.08 * np.sin(2 * np.pi * 440 * t) * (0.5 + 0.5 * np.sin(2 * np.pi * t / 0.8))   # transformer whine
    return y + air + tone


add('amb_hum', make_loop(hum, L, SR_LO, L // 4), SR_LO, loop=True, peak=0.7)


def reactor(nn):
    t = t_(nn, SR_LO)
    throb = 0.55 + 0.45 * np.sin(2 * np.pi * t / 1.2) ** 2
    drone = np.sin(2 * np.pi * 33 * t) + 0.7 * np.sin(2 * np.pi * 66.5 * t) + 0.4 * saw(99, nn, SR_LO) * 0.5
    drone = onepole_lp(drone, SR_LO, 300)
    rush = bandpass(noise(nn), SR_LO, 500, 1.5) * 0.5
    vib = 1 + 0.01 * np.sin(2 * np.pi * 5 * t)
    choir = sum(np.sin(2 * np.pi * f * vib * t + k) for k, f in enumerate((220, 261.6, 311.1, 329.6))) * 0.12   # unsettling cluster
    return throb * drone + throb * rush + choir * (0.5 + 0.5 * throb)


add('amb_reactor', make_loop(reactor, n28(2.4, SR_LO), SR_LO, n28(0.6, SR_LO)), SR_LO, loop=True, peak=0.75)

n = int(1.3 * SR_LO)
t = t_(n, SR_LO)
st_env = np.clip(t / 0.05, 0, 1) * np.exp(-np.maximum(t - 0.25, 0) / 0.3)
steam = onepole_hp(noise(n), SR_LO, 1800) * st_env + 0.4 * bandpass(noise(n), SR_LO, 2600, 3) * st_env
add('amb_steam', steam, SR_LO, peak=0.7)

n = int(0.5 * SR_LO)
clank = metal(n, SR_LO, 140, 0.25, parts=(1, 2.4, 3.7, 5.9), amps=(1, .7, .5, .3)) + 0.6 * onepole_lp(noise(n), SR_LO, 900) * env_exp(n, SR_LO, 0.03)
add('amb_clank', reverb(clank, SR_LO, 2.2, 0.6, 1800)[:int(2.4 * SR_LO)], SR_LO, peak=0.7)

n = int(2.2 * SR_LO)
t = t_(n, SR_LO)
pitch = 175 - 45 * (t / 2.2) + 6 * np.sin(2 * np.pi * 5.5 * t) + 25 * np.exp(-t / 0.3)
voice = saw(pitch, n, SR_LO) + 0.5 * saw(pitch * 1.006, n, SR_LO)
moan = formant(voice, SR_LO, ((480, 7), (820, 9), (2300, 12)), (1, .6, .2)) * np.sin(np.pi * np.clip(t / 2.2, 0, 1)) ** 1.5
add('amb_wail', reverb(moan, SR_LO, 2.5, 0.55, 2200)[:int(3.4 * SR_LO)], SR_LO, peak=0.7)

n = int(1.6 * SR_LO)
t = t_(n, SR_LO)
alarm = np.sign(np.sin(2 * np.pi * 330 * t)) * 0.5 + np.sin(2 * np.pi * 660 * t)
alarm *= ((t % 0.4) < 0.22) * (t < 1.2)
add('amb_alarm', reverb(onepole_lp(alarm, SR_LO, 1500), SR_LO, 1.5, 0.5, 1500)[:int(2.0 * SR_LO)], SR_LO, peak=0.55)


# ---------------------------------------------------------------- PS1 SPU ADPCM encoder
FILT = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def adpcm_encode(x, loop):
    """x: int16 samples (len % 28 == 0). Returns bytes of 16-byte SPU blocks."""
    out = bytearray()
    s1 = s2 = 0                              # decoder history (as the SPU will reconstruct it)
    nb = len(x) // 28
    for b in range(nb):
        blk = x[b * 28:(b + 1) * 28].astype(np.int64)
        best = None
        for fi, (k0, k1) in enumerate(FILT):
            # predict with the true signal to pick a shift, then encode tracking the decoder
            p1, p2 = s1, s2
            res = []
            for v in blk:
                pr = (p1 * k0 + p2 * k1 + 32) >> 6
                res.append(v - pr); p2, p1 = p1, v
            m = max(abs(r) for r in res)
            sh = 12
            while sh > 0 and m > (7 << (12 - sh)) * 1:
                sh -= 1
            for shift in (sh, max(sh - 1, 0)):
                p1, p2 = s1, s2; err = 0; nib = []
                for v in blk:
                    pr = (p1 * k0 + p2 * k1 + 32) >> 6
                    q = int(round((v - pr) * (1 << shift) / 4096))
                    q = max(-8, min(7, q))
                    d = ((q << 12) >> shift) + pr
                    d = max(-32768, min(32767, d))
                    err += (d - v) ** 2
                    nib.append(q & 15); p2, p1 = p1, d
                if best is None or err < best[0]:
                    best = (err, fi, shift, nib, p1, p2)
        _, fi, shift, nib, s1, s2 = best
        flags = 0
        if loop and b == 0:
            flags |= 4                       # loop start
        if b == nb - 1:
            flags |= 3 if loop else 1        # loop end (+ repeat for loops)
        out += bytes([shift | (fi << 4), flags]) + bytes(nib[i] | (nib[i + 1] << 4) for i in range(0, 28, 2))
    return bytes(out)


# ---------------------------------------------------------------- export
os.makedirs(os.path.join(ROOT, 'docs', 'sfx'), exist_ok=True)
pcm_lines, ad_lines, tab_p, tab_a = [], [], [], []
total_p = total_a = 0
for name, y, sr, loop in SOUNDS:
    n = len(y)
    if n % 28:
        y = np.concatenate([y, np.zeros(28 - n % 28)]) if not loop else y[:n - n % 28]
    s16 = np.clip(np.round(y * 32767), -32768, 32767).astype(np.int16)
    s8 = np.clip(np.round(y * 127), -128, 127).astype(np.int8)
    with wave.open(os.path.join(ROOT, 'docs', 'sfx', name + '.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((np.tile(s16, 3) if loop else s16).tobytes())
    pcm_lines.append(f'static const signed char sfx_pcm_{name}[{len(s8)}] = {{')
    b = s8.tobytes()
    for i in range(0, len(b), 32):
        pcm_lines.append('  ' + ','.join(str(v) for v in s8[i:i + 32].tolist()) + ',')
    pcm_lines.append('};')
    tab_p.append(f'  {{ sfx_pcm_{name}, {len(s8)}, {sr}, {int(loop)} }}, /* {name} */')
    ad = adpcm_encode(s16, loop)
    ad += bytes((-len(ad)) % 64)             # SPU DMA moves 64-byte chunks
    ad_lines.append(f'static const unsigned char sfx_adpcm_{name}[{len(ad)}] __attribute__((aligned(4))) = {{')
    for i in range(0, len(ad), 32):
        ad_lines.append('  ' + ','.join(str(v) for v in ad[i:i + 32]) + ',')
    ad_lines.append('};')
    tab_a.append(f'  {{ sfx_adpcm_{name}, {len(ad)}, {sr}, {int(loop)} }}, /* {name} */')
    total_p += len(s8); total_a += len(ad)
    print(f'{name:12s} {len(s8) / sr:5.2f}s @ {sr} Hz {"loop" if loop else ""}')

names = [s[0] for s in SOUNDS]
enum = '/* generated by tools/make_sounds.py */\n#ifndef SFX_IDS_H\n#define SFX_IDS_H\nenum {\n' + ''.join(
    f'    SFX_{n.upper()},\n' for n in names) + '    SFX_COUNT\n};\n#endif\n'
open(os.path.join(ROOT, 'assets', 'sfx_ids.h'), 'w').write(enum)
open(os.path.join(ROOT, 'assets', 'sfx_pcm.h'), 'w').write(
    f'/* generated by tools/make_sounds.py: {len(SOUNDS)} sounds, {total_p} bytes of 8-bit PCM */\n#include "sfx_ids.h"\n'
    'typedef struct { const signed char *data; int len, rate, loop; } SfxPcm;\n' + '\n'.join(pcm_lines) +
    '\nstatic const SfxPcm sfx_pcm[SFX_COUNT] = {\n' + '\n'.join(tab_p) + '\n};\n')
open(os.path.join(ROOT, 'assets', 'sfx_adpcm.h'), 'w').write(
    f'/* generated by tools/make_sounds.py: {len(SOUNDS)} sounds, {total_a} bytes of PS1 SPU ADPCM */\n#include "sfx_ids.h"\n'
    'typedef struct { const unsigned char *data; int len, rate, loop; } SfxAdpcm;\n' + '\n'.join(ad_lines) +
    '\nstatic const SfxAdpcm sfx_adpcm[SFX_COUNT] = {\n' + '\n'.join(tab_a) + '\n};\n')
print(f'PCM {total_p} bytes, ADPCM {total_a} bytes')
