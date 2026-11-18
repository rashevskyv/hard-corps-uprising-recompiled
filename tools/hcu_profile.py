#!/usr/bin/env python3
"""HCU's console profile (rex_profile.log) by thread and kind of code: the base's tools/profile_breakdown.py, adapted.

The base's tool classifies a sample by the object its function came from (renut_recomp.dir = generated code). HCU's
objects are hcu_recomp.dir, and with LTO the link map names every object *.ltrans.o, so all samples came out as
"runtime" and no guest function was listed. Here the kind comes from the symbol name: sub_XXXXXXXX (with an LTO
suffix such as .part.0) is generated game code, rex::graphics is the renderer. The functions come from the ELF's
symbol table (nm -S) instead of the map: every function, not only those in their own .text.* section.

  python3 tools/hcu_profile.py rex_profile.log --map <the ELF of the same build> [--from-report 3] [--min-cpu 0]
  (--map also takes the link map hcu.map; the ELF is better.) HCU_BASE_DIR = the base's clone (default below).
Eden reports CPU 0 % for every thread: pass --min-cpu 0 there.
"""
import os
import re
import shutil
import subprocess
import sys

BASE = os.environ.get('HCU_BASE_DIR', '/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base')
sys.path.insert(0, os.path.join(BASE, 'tools'))
import profile_breakdown as pb  # noqa: E402
import switch_hot_functions as shf  # noqa: E402

GUEST = re.compile(r'^(__imp__)?sub_[0-9A-F]{8}($|\.)')
RENDERER = re.compile(r'3rex8graphics|texture_conversion|deko3d')
NM = shutil.which('aarch64-none-elf-nm') or '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm'

base_kind = pb.kind
base_load_map = shf.load_map


def kind(name, obj, machine, hand):
    if GUEST.match(name):
        return 'generated'
    if RENDERER.search(name):
        return 'renderer'
    return base_kind(name, obj, machine, hand)


def load(path):
    with open(path, 'rb') as f:
        if f.read(4) != b'\x7fELF':
            return base_load_map(path)
    out = subprocess.run([NM, '-S', '--defined-only', path], capture_output=True, text=True, check=True).stdout
    best = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in 'tTwW':
            start, size = int(p[0], 16), int(p[1], 16)
            # sub_X and __imp__sub_X share an address: one entry per address (the breakdown drops "__imp__").
            if size and start not in best:
                best[start] = (start, start + size, p[3], 'elf')
    return sorted(best.values())


pb.kind = kind
shf.load_map = load

if __name__ == '__main__':
    pb.main()
