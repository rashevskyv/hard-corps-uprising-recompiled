#!/usr/bin/env python3
"""Converts the Switch build's DUMP NEXT FRAME files (sdmc:/switch/hcu/dump/f<swap>_<what>_fmt<F>_<w>x<h>.raw) to PNG.

    python tools/dkdump2png.py <dump dir> [<out dir>]

F is the deko3d DkImageFormat: 1 R8, 13 RG8, 28 RGBA8, 32 RGBA16F, 44 Z24S8 (depth in the low 24 bits; written as a
16-bit grey PNG), 45 ZF32_X24S8 (float depth, 16-bit grey), 52 RGB10A2, 57-59 BC1-BC3. Needs Pillow and numpy.
"""
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

NAME = re.compile(r'_fmt(\d+)_(\d+)x(\d+)\.raw$')


def convert(raw, fmt, w, h):
    n = w * h
    if fmt == 28:
        return Image.frombytes('RGBA', (w, h), raw[:n * 4])
    if fmt == 1:
        return Image.frombytes('L', (w, h), raw[:n])
    if fmt == 13:
        a = np.frombuffer(raw[:n * 2], np.uint8).reshape(h, w, 2)
        return Image.fromarray(np.dstack([a, np.zeros((h, w), np.uint8)]), 'RGB')
    if fmt == 44:
        v = np.frombuffer(raw[:n * 4], '<u4').reshape(h, w) & 0xFFFFFF
        return Image.fromarray((v >> 8).astype(np.uint16))
    if fmt == 45:
        v = np.frombuffer(raw[:n * 8], '<u4').reshape(h, w, 2)[:, :, 0].view(np.float32)
        return Image.fromarray((np.clip(v, 0, 1) * 65535).astype(np.uint16))
    if fmt == 52:
        v = np.frombuffer(raw[:n * 4], '<u4').reshape(h, w)
        rgb = [((v >> s) & 1023) >> 2 for s in (0, 10, 20)]
        return Image.fromarray(np.dstack(rgb).astype(np.uint8), 'RGB')
    if fmt == 32:
        v = np.frombuffer(raw[:n * 8], '<f2').reshape(h, w, 4).astype(np.float32)
        return Image.fromarray((np.clip(v, 0, 1) * 255).astype(np.uint8), 'RGBA')
    if fmt in (57, 58, 59):
        return Image.frombytes('RGBA', (w, h), raw, 'bcn', fmt - 56)
    return None


def main():
    src = Path(sys.argv[1])
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else src / 'png'
    out.mkdir(parents=True, exist_ok=True)
    for p in sorted(src.glob('*.raw')):
        m = NAME.search(p.name)
        if not m:
            continue
        fmt, w, h = map(int, m.groups())
        img = convert(p.read_bytes(), fmt, w, h)
        if img is None:
            print('skipped (format %d): %s' % (fmt, p.name))
            continue
        img.save(out / (p.stem + '.png'))
        print(p.stem)


if __name__ == '__main__':
    main()
