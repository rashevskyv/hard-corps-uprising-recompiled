#!/usr/bin/env python3
"""Switch button icons for the game's prompt images (button_glyphs=switch): writes src/button_glyphs.h.

The art is ours: the solid style of the switch-glyphs set (github checkout next to this repo,
../switch-glyphs: png/atlas-solid-64.png + atlas.json), no Nintendo file. Each icon replaces one
Gui/Img/guidebutton_<NAME>.bin by position (Xbox A = the bottom button = Switch B, ...).

The game's .bin image: a 32-byte big-endian header (width, height, palette bytes, data bytes, 7, 0, 0, 0),
256 palette entries A,R,G,B (entry 0 = FF FF FF 00 = the transparent key), then 8-bit indices, Xenos 2D
tiled, rows bottom-up, the data rounded up to 4096 bytes. A 32x32 icon file is 5152 bytes.
The header keeps the palette and each icon's 1024 index bytes, row-major and top row first;
src/button_glyphs.cpp tiles them and builds the file bytes on read.

Look: the 64 cell scaled to 24x24, centred in 32x32 (the game's icons fill about 22x22), white body, the
cut-out letter filled dark, a 1-pixel dark ring (the game draws the icons untinted on a dark bar; a paletted
file has 1-bit alpha).

    python tools/make_glyphs.py [switch-glyphs dir] [preview.png]     # needs Pillow
"""
import json
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), 'switch-glyphs')
OUT = os.path.join(ROOT, 'src', 'button_glyphs.h')
N, BOX = 32, 24            # icon size, glyph size inside it
DARK = 16                  # letter and ring grey
LEVELS = 32                # palette entries 1..32: DARK -> white

# game file -> glyph cells (several = their cut-outs combined: the d-pad with two or four arrows)
MAP = [
    ('A', ['b']), ('B', ['a']), ('X', ['y']), ('Y', ['x']),
    ('LB', ['l']), ('RB', ['r']), ('LT', ['zl']), ('RT', ['zr']),
    ('START', ['plus']), ('BACK', ['minus']),
    ('JUUJI', ['dpad']), ('LR', ['dpad-left', 'dpad-right']), ('TD', ['dpad-up', 'dpad-down']),
    ('LRTD', ['dpad-up', 'dpad-down', 'dpad-left', 'dpad-right']),
    ('LS', ['ls']), ('LS2', ['ls-press']), ('RS', ['rs']), ('RS2', ['rs-press']), ('HOME', ['home']),
]


def tiled_offset(x, y, width, log_bpp):
    """XGAddress2DTiledOffset."""
    aw = (width + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aw >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << log_bpp
    off = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return ((((off & ~0x1FF) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2)
             + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (off & 0x3F))) >> log_bpp


def palette():
    """Entries 1..LEVELS as A,R,G,B bytes."""
    out = bytearray()
    for i in range(LEVELS):
        g = DARK + (255 - DARK) * i // (LEVELS - 1)
        out += bytes((255, g, g, g))
    return bytes(out)


def icon(atlas, cells, names):
    """32x32 index image (row-major, top row first): 0 = transparent, 1..LEVELS = grey."""
    a = None
    for n in names:
        c, r = cells[n]
        g = atlas.crop((c * 64, r * 64, c * 64 + 64, r * 64 + 64)).getchannel('A')
        a = g if a is None else Image.frombytes('L', g.size, bytes(map(min, a.tobytes(), g.tobytes())))
    s = a.resize((BOX, BOX), Image.LANCZOS)
    o = (N - BOX) // 2
    al = [[0] * N for _ in range(N)]
    for y in range(BOX):
        for x in range(BOX):
            al[y + o][x + o] = s.getpixel((x, y))
    shape = [[al[y][x] >= 16 for x in range(N)] for y in range(N)]
    # outside = reachable from the border without crossing the shape; the rest (the cut-outs too) is inside
    out = [[False] * N for _ in range(N)]
    todo = [(x, y) for x in range(N) for y in (0, N - 1)] + [(x, y) for y in range(N) for x in (0, N - 1)]
    while todo:
        x, y = todo.pop()
        if 0 <= x < N and 0 <= y < N and not out[y][x] and not shape[y][x]:
            out[y][x] = True
            todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    inside = [[not out[y][x] for x in range(N)] for y in range(N)]
    idx = [[0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            ring = any(inside[j][i] for j in range(max(y - 1, 0), min(y + 2, N)) for i in range(max(x - 1, 0), min(x + 2, N)))
            if ring:
                idx[y][x] = 1 + (al[y][x] * (LEVELS - 1) + 127) // 255
    return idx


def file_bytes(idx, pal):
    """The whole .bin file, as src/button_glyphs.cpp builds it (the self-check below decodes it)."""
    data = bytearray(4096)
    for y in range(N):
        for x in range(N):
            data[tiled_offset(x, y, N, 0)] = idx[N - 1 - y][x]
    p = bytearray(bytes((0xFF, 0, 0, 0)) * 256)  # unused entries: opaque black
    p[0:4] = bytes((0xFF, 0xFF, 0xFF, 0))  # entry 0: the transparent key
    p[4:4 + len(pal)] = pal
    return struct.pack('>8I', N, N, 1024, 4096, 7, 0, 0, 0) + bytes(p) + bytes(data)


def check(b, idx):
    """Decode the file like the game: the index image must come back."""
    assert len(b) == 5152 and struct.unpack('>8I', b[:32]) == (N, N, 1024, 4096, 7, 0, 0, 0)
    for y in range(N):
        for x in range(N):
            assert b[1056 + tiled_offset(x, y, N, 0)] == idx[N - 1 - y][x]


def main():
    meta = json.load(open(os.path.join(SRC, 'atlas.json')))
    atlas = Image.open(os.path.join(SRC, 'png', 'atlas-solid-64.png')).convert('RGBA')
    icons = [(f, icon(atlas, meta['glyphs'], names)) for f, names in MAP]
    pal = palette()
    lines = ['// Generated by tools/make_glyphs.py from the switch-glyphs solid atlas (our own art). Do not edit.',
             '#pragma once', '#include <stdint.h>', '',
             'static const uint8_t k_glyph_palette[%d] = {  // entries 1..%d, A,R,G,B' % (len(pal), LEVELS)]
    lines += ['    ' + ' '.join('0x%02X,' % b for b in pal[i:i + 16]) for i in range(0, len(pal), 16)]
    lines += ['};', '', 'struct HcuGlyph {', '  const char* file;  // Gui/Img file name', '  uint8_t index[1024];  // 32x32, row-major, top row first',
              '};', '', 'static const HcuGlyph k_glyphs[] = {']
    for f, idx in icons:
        check(file_bytes(idx, pal), idx)
        t = bytes(i for row in idx for i in row)
        lines.append('    {"guidebutton_%s.bin", {' % f)
        lines += ['        ' + ' '.join('%d,' % b for b in t[i:i + 32]) for i in range(0, len(t), 32)]
        lines.append('    }},')
    lines.append('};')
    with open(OUT, 'w', newline='\n') as fh:
        fh.write('\n'.join(lines) + '\n')
    print('wrote', OUT, len(icons), 'icons')
    if len(sys.argv) > 2:  # preview: the icons x4 on the game's dark green bar
        sheet = Image.new('RGB', (len(icons) * 36 * 4, 36 * 4), (24, 48, 32))
        for k, (f, idx) in enumerate(icons):
            im = Image.new('RGBA', (N, N))
            for y in range(N):
                for x in range(N):
                    i = idx[y][x]
                    if i:
                        g = pal[(i - 1) * 4 + 1]
                        im.putpixel((x, y), (g, g, g, 255))
            sheet.paste(im.resize((N * 4, N * 4), Image.NEAREST), (k * 36 * 4 + 8, 8), im.resize((N * 4, N * 4), Image.NEAREST))
        sheet.save(sys.argv[2])
        print('preview', sys.argv[2])


if __name__ == '__main__':
    main()
