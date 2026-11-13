#!/usr/bin/env python3
"""After re-running an authoring script (build_demon.py, build_soldier.py, build_chainsaw.py,
build_pistol.py, make_ui.py, make_font.py), bring its output into assets/src/ - the sources that
tools/pack_assets.py compresses into assets/packed.h:

  <name>.tim (8bpp, 256-texel sheet)  ->  assets/src/<name>_tex.png   (characters: full size as is;
                                          weapons: RGBA, half size, de-dithered + sharpened)
  <name>_mesh.h with vertex data      ->  assets/src/<name>_mesh.json, and the header keeps only
                                          its constants and bone table
  ui_tim.h / font_tim.h               ->  assets/src/ui_tex.png / font.png (full size)

usage: tools/import_authored.py demon soldier chainsaw pistol ui font   (then tools/pack_assets.py)
"""
import os, re, sys, json, struct, numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'src')


def find(*names):
    for d in [os.path.join(ROOT, 'tools', 'out', n.split('.')[0].split('_')[0]) for n in names] + [os.path.join(ROOT, 'tools', 'out'), os.path.join(ROOT, 'assets')]:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return p
    sys.exit('not found: ' + ' / '.join(names))


def tim_from_header(path, var):
    s = open(path).read()
    m = re.search(var + r'\[\d+\][^=]*= \{([^}]*)\}', s)
    return bytes(int(x) for x in re.findall(r'\d+', m.group(1)))


def decode(b):
    mode = struct.unpack('<I', b[4:8])[0] & 3
    cl = struct.unpack('<I', b[8:12])[0]
    cw, ch = struct.unpack('<HH', b[16:20])
    clut = np.frombuffer(b[20:20 + cw * ch * 2], '<u2')
    o = 8 + cl
    pw, ph = struct.unpack('<HH', b[o + 8:o + 12])
    d = np.frombuffer(b[o + 12:o + 12 + pw * ph * 2], np.uint8)
    if mode == 1:
        idx = d.reshape(ph, pw * 2)
    else:
        d = d.reshape(ph, pw * 2)
        idx = np.zeros((ph, pw * 4), np.uint8); idx[:, 0::2] = d & 15; idx[:, 1::2] = d >> 4
    return idx, clut


def rgb_of(idx, clut):
    c = clut[idx].astype(int)
    rgb = np.stack([c & 31, (c >> 5) & 31, (c >> 10) & 31], -1) * 8.0
    return rgb + rgb / 32, idx != 0


def skin(name):
    p = find(name + '.tim')
    idx, clut = decode(open(p, 'rb').read())
    rgb, a = rgb_of(idx, clut)
    if name in ('demon', 'soldier'):
        # the characters keep their full 256 x 256 sheet as is (a texture page each, see pack_assets.py);
        # alpha marks the painted texels, grown by one so a UV on a texel's edge still lands inside
        from scipy.ndimage import binary_dilation
        m = binary_dilation(a > 0, structure=np.ones((3, 3), bool))
        Image.fromarray(np.dstack([rgb.clip(0, 255).astype(np.uint8), m.astype(np.uint8) * 255]), 'RGBA').save(
            os.path.join(SRC, name + '_tex.png'))
        return
    from scipy.ndimage import uniform_filter
    m = a.astype(float)
    s = np.stack([uniform_filter(rgb[..., k] * m, 3) for k in range(3)], -1)
    n = uniform_filter(m, 3)
    sm = np.where(n[..., None] > 0, s / np.maximum(n, 1e-6)[..., None], 0)
    sm = np.where(a[..., None], 0.5 * sm + 0.5 * rgb, sm)            # undo the dither
    h, w = a.shape
    im = Image.fromarray(sm.clip(0, 255).astype(np.uint8)).resize((w // 2, h // 2), Image.LANCZOS)
    im = im.filter(ImageFilter.UnsharpMask(1.2, 90, 2))
    mm = a.reshape(h // 2, 2, w // 2, 2).sum((1, 3)) >= 2
    Image.fromarray(np.dstack([np.array(im), mm.astype(np.uint8) * 255]), 'RGBA').save(os.path.join(SRC, name + '_tex.png'))


def mesh(name):
    path = find(name + '_mesh.h')
    s = open(path).read()
    vm = re.search(r'static const DVERT \w+\[\] = \{(.*?)\n\};', s, re.S)
    pm = re.search(r'static const DPRIM \w+\[\] = \{(.*?)\n\};', s, re.S)
    if not vm:
        sys.exit(path + ' has no vertex data (already imported?)')
    verts = [[int(x) for x in re.findall(r'-?\d+', l.split('/*')[0])[:3]] for l in vm.group(1).strip().split('\n')]
    prims = [[int(x) for x in re.findall(r'-?\d+', l)][:13] for l in pm.group(1).strip().split('\n')]
    json.dump({'verts': verts, 'prims': prims}, open(os.path.join(SRC, name + '_mesh.json'), 'w'))
    open(os.path.join(ROOT, 'assets', name + '_mesh.h'), 'w').write(s[:vm.start()] + s[pm.end():])


for what in sys.argv[1:]:
    if what == 'ui':
        idx, clut = decode(tim_from_header(find('ui_tim.h'), 'ui_tim'))
        rgb, a = rgb_of(idx, clut)
        Image.fromarray(np.dstack([rgb.clip(0, 255).astype(np.uint8), a.astype(np.uint8) * 255]), 'RGBA').save(os.path.join(SRC, 'ui_tex.png'))
    elif what == 'font':
        idx, clut = decode(tim_from_header(find('font_tim.h'), 'font_tim'))
        p = Image.fromarray(idx, 'P')
        pal = []
        for w in clut.astype(int):
            pal += [(w & 31) * 8, ((w >> 5) & 31) * 8, ((w >> 10) & 31) * 8]
        p.putpalette(pal + [0] * (768 - len(pal)))
        p.save(os.path.join(SRC, 'font.png'))
    else:
        skin(what)
        mesh(what)
    print('imported', what)
