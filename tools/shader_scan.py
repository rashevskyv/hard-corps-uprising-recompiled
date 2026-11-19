#!/usr/bin/env python3
"""The game's Xenos shaders offline: every D3D shader blob (magic 0x102A1100 pixel, 0x102A1101 vertex) in the game
files (DataXENON/Shader/aspShaders.pkg holds 74 + 74, Shader/*.bin 10 more) and in the XEX's loaded image (the codegen's
REXGLUE_DUMP_IMAGE, switch/build-base.sh writes it), disassembled by the SDK (tools/ucode_disasm.cpp) into the same
shader_<hash>.ucode.<vert|frag> files as the deko3d backend's missing/ dumps, the input of tools/xenos2glsl.py.

    python3 -I tools/shader_scan.py --game <extracted dir> [--image hcu_image.bin] --sdk <SDK dir> --out <dir>
        [--known <dump dir>]...   (how many of these runtime hashes the scan gives)

Blob layout (from aspShaders.pkg): +4 header size H (the ucode area starts at H), +8 ucode area size N; the header
holds the pair (program offset in the area, program size) whose sum is N (a pixel shader with literal constants has
them as 0x40 bytes in front of the program). The program bytes are what the GPU gets, so the hash is the runtime one.

Pixel shaders: exact (20 of the 27 the console and the trace backend dumped by 2026-11-19 are in these files; the other
7 are nowhere in the files or the image: probably built at run time, unverified). Vertex shaders are skipped: in aspShaders.pkg and Shader/*.bin
their vfetch instructions are unbound (stride 0) and the D3D runtime rewrites them for the vertex declaration of each draw
(format, offset, stride, order, destination register), so one pkg shader runs under several hashes, none of them in the
files (the 64 dumped ones come from 32 of the 62 distinct pkg programs); the XEX image's 90 vertex blobs are the D3D
library's own (memexport, vertex texture fetch): none is a dumped one and xenos2glsl.py translates none. Vertex shaders
come only from the dumps (switch/fetch-missing-shaders.sh).
"""
import argparse
import os
import re
import resource
import struct
import subprocess
import sys


def blobs(data):
    """(offset, stage, program bytes big-endian) of every plausible shader blob in data."""
    for m in re.finditer(rb'\x10\x2a\x11[\x00\x01]', data):
        o = m.start()
        if o % 4:
            continue
        hsize, asize = struct.unpack('>2I', data[o + 4:o + 12])
        if not (0x20 <= hsize < 0x4000 and 0 < asize < 0x4000 and asize % 4 == 0 and o + hsize + asize <= len(data)):
            continue
        hw = struct.unpack('>%dI' % (hsize // 4), data[o:o + hsize - hsize % 4])
        pairs = [(hw[i], hw[i + 1]) for i in range(3, len(hw) - 1)
                 if hw[i] + hw[i + 1] == asize and hw[i] % 4 == 0 and hw[i] < asize]
        start, size = pairs[-1] if pairs else (0, asize)
        yield o, 'vert' if data[o + 3] == 1 else 'frag', data[o + hsize + start:o + hsize + start + size]


def build_tool(sdk, out):
    here = os.path.dirname(os.path.abspath(__file__))
    tool = os.path.join(out, 'ucode_disasm')
    lib = os.path.join(sdk, 'out/linux-amd64/Release')
    shader = os.path.join(sdk, 'src/graphics')
    cmd = ['g++', '-std=c++23', '-O1', '-mssse3', '-msse4.1', '-I' + os.path.join(sdk, 'include'),
           '-I' + os.path.join(sdk, 'out/build/linux-amd64/include'), '-I' + os.path.join(sdk, 'thirdparty/fmt/include'),
           '-I' + os.path.join(sdk, 'thirdparty/spdlog/include'), '-I' + os.path.join(sdk, 'thirdparty/simde'),
           '-I' + os.path.join(here, '../src/gpu'), os.path.join(here, 'ucode_disasm.cpp'),
           os.path.join(shader, 'pipeline/shader/shader.cpp'), os.path.join(shader, 'pipeline/shader/translator.cpp'),
           os.path.join(shader, 'pipeline/shader/translator_disasm.cpp'), os.path.join(shader, 'format/ucode.cpp'),
           '-L' + lib, '-lrexruntime', '-lfmt', '-Wl,-rpath,' + lib, '-o', tool]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('building ucode_disasm failed:\n' + r.stderr[-3000:])
    return tool


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--image')
    ap.add_argument('--sdk', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--known', action='append', default=[])
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for f in os.listdir(args.out):
        if f.startswith('shader_'):
            os.remove(os.path.join(args.out, f))
    tmp = os.path.join(args.out, 'tmp')
    os.makedirs(tmp, exist_ok=True)
    tool = build_tool(args.sdk, tmp)
    files = [os.path.join(dp, f) for dp, _, fs in os.walk(args.game) for f in fs]
    if args.image:
        files.append(args.image)
    no_core = lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    got = {}       # hash -> stage
    blob_count = {}  # (source, stage) -> blobs
    unbound = {}   # source -> blobs the SDK cannot analyse (not a shader)
    for p in sorted(files):
        data = open(p, 'rb').read()
        if b'\x10\x2a\x11' not in data:
            continue
        src = os.path.relpath(p, args.game) if p != args.image else 'XEX image'
        for o, stage, prog in blobs(data):
            blob_count[(src, stage)] = blob_count.get((src, stage), 0) + 1
            if stage == 'vert':
                continue  # see the module comment
            be = os.path.join(tmp, 'blob.bin')
            open(be, 'wb').write(prog)
            r = subprocess.run([tool, be, stage, 'big', args.out], capture_output=True, text=True, preexec_fn=no_core)
            if r.returncode != 0:
                unbound[src] = unbound.get(src, 0) + 1
                continue
            got[int(r.stdout.strip(), 16)] = stage
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    for (src, stage), n in sorted(blob_count.items()):
        print('%-40s %s %3d blobs' % (src, stage, n))
    for src, n in sorted(unbound.items()):
        print('%-40s %3d pixel shader blobs not analysable (not a shader)' % (src, n))
    print('%d distinct pixel shaders in %s (vertex shaders: none, they come from dumps only)' % (len(got), args.out))
    for k in args.known:
        known = {}
        for f in os.listdir(k):
            m = re.match(r'shader_([0-9A-F]{16})\.ucode\.(vert|frag)$', f)
            if m:
                known[int(m.group(1), 16)] = m.group(2)
        for stage in ('frag', 'vert'):
            ks = [h for h, s in known.items() if s == stage]
            print('%s: %d of %d %s shaders found offline' % (k, sum(h in got for h in ks), len(ks), stage))


if __name__ == '__main__':
    main()
