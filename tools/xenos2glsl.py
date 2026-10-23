#!/usr/bin/env python3
"""Xenos shader microcode -> GLSL for uam (deko3d), from the ReXGlue SDK's ucode disassembly
(the `shader_<hash>.ucode.vert|frag` files its --dump_shaders writes; the trace backend writes
them too). The semantics follow the SDK's shader interpreter (BSD-3); the text grammar follows
its translator_disasm.cpp. Only what the game's shaders use is implemented; anything else stops
with a clear message so the gap is visible (docs/native-renderer-plan.md, M2).

Interface of the generated GLSL (the deko3d backend relies on it):
  vertex:   layout(location = a) in vec4 in_a      one per vfetch, in instruction order; the
            sidecar JSON lists (slot, offset, format, stride, signed, integer) per attribute;
            binding 0 = uniform Consts { vec4 c[256]; } (the ALU constants c0..c255);
            binding 1 = uniform Sys (ndc_scale, ndc_offset, alpha test, bool constants);
            layout(location = k) out vec4 o_k      the interpolators the shader exports.
  fragment: layout(location = k) in vec4 i_k       the interpolators it reads (r_k at start);
            layout(binding = n) uniform sampler2D tf_n (1D/3D/Cube by the fetch's dimension);
            layout(location = k) out vec4 oC_k; the alpha test is applied from Sys.

    python3 tools/xenos2glsl.py in.ucode.vert [-o out.vert] [--json out.json] [--check]
    python3 tools/xenos2glsl.py --all <dump dir> --out <dir> [--check]   (every dump; --check runs uam)
"""
import argparse
import json
import os
import re
import subprocess
import sys

VECTOR_OPS = {'add', 'mul', 'max', 'min', 'seq', 'sgt', 'sge', 'sne', 'frc', 'trunc', 'floor', 'mad',
              'cndeq', 'cndge', 'cndgt', 'dp4', 'dp3', 'dp2add', 'max4', 'setp_eq_push', 'setp_ne_push',
              'setp_gt_push', 'setp_ge_push', 'kill_eq', 'kill_gt', 'kill_ge', 'kill_ne', 'dst', 'cube', 'maxa'}
SCALAR_OPS = {'adds', 'adds_prev', 'muls', 'muls_prev', 'muls_prev2', 'maxs', 'mins', 'seqs', 'sgts', 'sges',
              'snes', 'frcs', 'truncs', 'floors', 'exp', 'logc', 'log', 'rcpc', 'rcpf', 'rcp', 'rsqc', 'rsqf',
              'rsq', 'maxas', 'maxasf', 'subs', 'subs_prev', 'setp_eq', 'setp_ne', 'setp_gt', 'setp_ge',
              'setp_inv', 'setp_pop', 'setp_clr', 'setp_rstr', 'kills_eq', 'kills_gt', 'kills_ge', 'kills_ne',
              'kills_one', 'sqrt', 'mulsc', 'addsc', 'subsc', 'sin', 'cos', 'retain_prev'}
TWO_COMPONENT_SCALAR = {'adds', 'muls', 'maxs', 'mins', 'subs', 'maxas', 'maxasf'}  # one operand, two components
TWO_OPERAND_SCALAR = {'mulsc', 'addsc', 'subsc'}  # a constant and a register
REPLICATED = {'dp4', 'dp3', 'dp2add', 'max4', 'setp_eq_push', 'setp_ne_push', 'setp_gt_push', 'setp_ge_push',
              'kill_eq', 'kill_gt', 'kill_ge', 'kill_ne'}


class Translator:
    def __init__(self, text, stage, name):
        self.stage = stage  # 'vert' or 'frag'
        self.name = name
        self.lines = text.splitlines()
        self.body = []        # GLSL statements of the current block
        self.blocks = []      # (cf_index, kind, statements, extra) in order
        self.attrs = []       # vertex attributes: dict(slot, offset, format, stride, signed, integer)
        self.textures = {}    # slot -> dimension ('2D', '1D', '3D', 'Cube')
        self.max_reg = -1
        self.reads_before_write = set()
        self.written = {}     # register index -> components written so far (per component: `r0.z = ...`
                              # before `tfetch2D r0, r0.xy` must not hide the interpolator in r0.xy)
        self.exports = set()  # 'oPos', 'o0'.., 'oC0'.., 'oPts', 'oDepth'
        self.has_jump = False
        self.has_kill = False
        self.uses_bools = False
        self.stride_words = 0
        self.pending_serialize = False
        self.indent = '  '

    # ----- helpers ---------------------------------------------------------------------------
    def err(self, msg, line=''):
        raise SystemExit('%s: %s%s' % (self.name, msg, ('\n  ' + line) if line else ''))

    def note_reg(self, idx):
        self.max_reg = max(self.max_reg, idx)

    def reg_read(self, idx, comps):
        self.note_reg(idx)
        if not set(comps) <= self.written.get(idx, set()):
            self.reads_before_write.add(idx)

    def reg_write(self, idx, comps):
        self.note_reg(idx)
        self.written.setdefault(idx, set()).update(comps)

    def parse_result(self, tok):
        """'r0.__z_' -> (target, [per-component source char or None]). Target: 'r3', 'o1', 'oPos', 'oC0'."""
        m = re.match(r'^(r|o|oPos|oPts|oC|oDepth|eA|eM)(\d*)(?:\.([xyzw01_]+))?$', tok)
        if not m:
            self.err('bad result operand %r' % tok)
        kind, idx, mask = m.groups()
        target = kind + idx
        if mask is None:
            comps = ['x', 'y', 'z', 'w']
        elif mask == '_':
            comps = [None] * 4
        else:
            if len(mask) != 4:
                self.err('bad result mask %r' % tok)
            comps = [None if ch == '_' else ch for ch in mask]
        if kind == 'r':
            self.reg_write(int(idx), ['xyzw'[i] for i, ch in enumerate(comps) if ch is not None])
        elif kind in ('eA', 'eM'):
            self.err('memexport is not supported')
        else:
            self.exports.add(target)
        return target, comps

    def parse_source(self, tok, count):
        """Source operand -> GLSL expression with exactly `count` components (1, 2 or 4)."""
        m = re.match(r'^(-?)(r|c)(_abs)?(?:\[(\d+)\]|(\d+))(?:\.([xyzw]+))?$', tok)
        if not m:
            self.err('bad source operand %r' % tok)
        neg, kind, is_abs, idx_abs, idx_plain, swz = m.groups()
        idx = int(idx_abs if idx_abs is not None else idx_plain)
        if swz is None:
            swz = 'xyzw'
        if len(swz) == 1:
            swz = swz * 4  # the disassembler prints one letter for a scalar operand: that component
        elif len(swz) == 2:
            swz = swz  # two-component scalar operand
        if kind == 'r':
            self.reg_read(idx, swz[:count] if count in (1, 2) else swz[:4])
            base = 'r%d' % idx
        else:
            base = 'c[%d]' % idx
        if count == 1:
            expr = '%s.%s' % (base, swz[0])
        elif count == 2:
            expr = '%s.%s' % (base, swz[:2])
        else:
            expr = '%s.%s' % (base, swz[:4]) if swz != 'xyzw' else base
        if is_abs:
            expr = 'abs(%s)' % expr
        if neg:
            expr = '(-%s)' % expr
        return expr

    def write_masked(self, target, comps, value_var, is_export):
        """Assign value_var (vec4) into target by the result mask."""
        out = []
        if comps == ['x', 'y', 'z', 'w']:
            out.append('%s = %s;' % (target, value_var))
            return out
        for i, ch in enumerate(comps):
            dst = 'xyzw'[i]
            if ch is None:
                continue  # exports start at zero: an unwritten component stays 0
            if ch == '0':
                out.append('%s.%s = 0.0;' % (target, dst))
            elif ch == '1':
                out.append('%s.%s = 1.0;' % (target, dst))
            else:
                out.append('%s.%s = %s.%s;' % (target, dst, value_var, ch))
        return out

    # ----- instructions ----------------------------------------------------------------------
    def emit_fetch_vertex(self, op, rest):
        # vfetch_full r1.xyz1, r0.x, vf95, DataFormat=..., Stride=7, Signed=true, NumFormat=integer, PrefetchCount=7
        # vfetch_mini r0, Offset=3, DataFormat=..., Signed=true, NumFormat=integer
        parts = [p.strip() for p in rest.split(',')]
        kv = {}
        slot = None
        pos = 1
        if op == 'vfetch_full':
            self.parse_source(parts[1], 1)  # the index register (r0.x): the host supplies vertex ids
        target, comps = self.parse_result(parts[0])
        if op == 'vfetch_full':
            if not parts[2].startswith('vf'):
                self.err('vfetch_full without a fetch constant', rest)
            slot = int(parts[2][2:])
            pos = 3
        for p in parts[pos:]:
            if '=' in p:
                k, v = p.split('=', 1)
                kv[k] = v
            else:
                self.err('unexpected vfetch attribute %r' % p, rest)
        if op == 'vfetch_full':
            self.cur_slot = slot
            self.stride_words = int(kv.get('Stride', '0'))
        attr = {
            'index': len(self.attrs),
            'slot': self.cur_slot,
            'offset_words': int(kv.get('Offset', '0')),
            'format': kv.get('DataFormat', 'FMT_UNDEFINED'),
            'stride_words': self.stride_words,
            'signed': kv.get('Signed') == 'true',
            'integer': kv.get('NumFormat') == 'integer',
            'exp_adjust': int(kv.get('ExpAdjust', '0')),
        }
        self.attrs.append(attr)
        var = 'in_%d' % attr['index']
        self.body.extend(self.write_masked(target, comps, var, False))

    def emit_fetch_texture(self, op, rest):
        # tfetch2D r0, r0.xy, tf0[, FetchValidOnly=false, ...]
        parts = [p.strip() for p in rest.split(',')]
        dim = {'tfetch1D': '1D', 'tfetch2D': '2D', 'tfetch3D': '3D', 'tfetchCube': 'Cube'}.get(op)
        if dim is None:
            self.err('unsupported fetch %s' % op, rest)
        ncomp = {'1D': 1, '2D': 2, '3D': 3, 'Cube': 3}[dim]
        # Sources before the result: `tfetch2D r0, r0.xy` reads r0 (an interpolator) before writing it.
        coord = self.parse_source(parts[1], ncomp if ncomp != 3 else 4)
        target, comps = self.parse_result(parts[0])
        if ncomp == 3:
            coord = '(%s).xyz' % coord
        if dim == '1D':
            # The Xenos has no 1D textures: a 1D fetch reads a 2D texture of height 1 (Xenia does the
            # same), so the sampler is 2D and the row is 0.
            coord = 'vec2(%s, 0.5)' % coord
            dim = '2D'
        if not parts[2].startswith('tf'):
            self.err('tfetch without a fetch constant', rest)
        slot = int(parts[2][2:])
        prev = self.textures.get(slot)
        if prev and prev != dim:
            self.err('texture slot %d used as %s and %s' % (slot, prev, dim))
        self.textures[slot] = dim
        for p in parts[3:]:
            k = p.split('=')[0]
            if k not in ('FetchValidOnly', 'UseComputedLOD', 'MagFilter', 'MinFilter', 'MipFilter', 'AnisoFilter'):
                self.err('unsupported tfetch attribute %r' % p, rest)
        tmp = 'tex_%d' % self.tmp_counter()
        self.body.append('vec4 %s = texture(tf_%d, %s);' % (tmp, slot, coord))
        self.body.extend(self.write_masked(target, comps, tmp, False))

    _tmp = 0

    def tmp_counter(self):
        Translator._tmp += 1
        return Translator._tmp

    def vector_expr(self, op, a):
        """GLSL expression (vec4) of a vector op with operand expressions a[0..2]."""
        if op == 'add': return '(%s + %s)' % (a[0], a[1])
        if op == 'mul': return '(%s * %s)' % (a[0], a[1])
        if op == 'max': return 'max(%s, %s)' % (a[0], a[1])
        if op == 'min': return 'min(%s, %s)' % (a[0], a[1])
        if op == 'seq': return 'vec4(equal(%s, %s))' % (a[0], a[1])
        if op == 'sgt': return 'vec4(greaterThan(%s, %s))' % (a[0], a[1])
        if op == 'sge': return 'vec4(greaterThanEqual(%s, %s))' % (a[0], a[1])
        if op == 'sne': return 'vec4(notEqual(%s, %s))' % (a[0], a[1])
        if op == 'frc': return 'fract(%s)' % a[0]
        if op == 'trunc': return 'trunc(%s)' % a[0]
        if op == 'floor': return 'floor(%s)' % a[0]
        if op == 'mad': return '(%s * %s + %s)' % (a[0], a[1], a[2])
        if op == 'cndeq': return 'mix(%s, %s, vec4(notEqual(%s, vec4(0.0))))' % (a[1], a[2], a[0])
        if op == 'cndge': return 'mix(%s, %s, vec4(lessThan(%s, vec4(0.0))))' % (a[1], a[2], a[0])
        if op == 'cndgt': return 'mix(%s, %s, vec4(lessThanEqual(%s, vec4(0.0))))' % (a[1], a[2], a[0])
        if op == 'dp4': return 'vec4(dot(%s, %s))' % (a[0], a[1])
        if op == 'dp3': return 'vec4(dot((%s).xyz, (%s).xyz))' % (a[0], a[1])
        if op == 'dp2add': return 'vec4(dot((%s).xy, (%s).xy) + (%s).x)' % (a[0], a[1], a[2])
        if op == 'max4':
            return 'vec4(max(max((%s).x, (%s).y), max((%s).z, (%s).w)))' % (a[0], a[0], a[0], a[0])
        if op in ('setp_eq_push', 'setp_ne_push', 'setp_gt_push', 'setp_ge_push'):
            cmp = {'setp_eq_push': '== 0.0', 'setp_ne_push': '!= 0.0', 'setp_gt_push': '> 0.0',
                   'setp_ge_push': '>= 0.0'}[op]
            self.extra_pred = 'p0 = ((%s).w == 0.0 && (%s).w %s);' % (a[0], a[1], cmp)
            return 'vec4(((%s).x == 0.0 && (%s).x %s) ? 0.0 : (%s).x + 1.0)' % (a[0], a[1], cmp, a[0])
        if op in ('kill_eq', 'kill_gt', 'kill_ge', 'kill_ne'):
            fn = {'kill_eq': 'equal', 'kill_gt': 'greaterThan', 'kill_ge': 'greaterThanEqual',
                  'kill_ne': 'notEqual'}[op]
            self.has_kill = True
            self.extra_kill = 'if (any(%s(%s, %s))) discard;' % (fn, a[0], a[1])
            return 'vec4(float(any(%s(%s, %s))))' % (fn, a[0], a[1])
        if op == 'dst':
            return 'vec4(1.0, (%s).y * (%s).y, (%s).z, (%s).w)' % (a[0], a[1], a[0], a[1])
        self.err('vector op %s is not implemented' % op)

    def scalar_expr(self, op, a):
        """GLSL float expression of a scalar op; a = operand expressions (1 or 2 floats); sets self.extra_pred."""
        if op in ('adds', 'addsc'): return '(%s + %s)' % (a[0], a[1])
        if op == 'adds_prev': return '(%s + ps)' % a[0]
        if op in ('muls', 'mulsc'): return '(%s * %s)' % (a[0], a[1])
        if op == 'muls_prev': return '(%s * ps)' % a[0]
        if op == 'maxs': return 'max(%s, %s)' % (a[0], a[1])
        if op == 'mins': return 'min(%s, %s)' % (a[0], a[1])
        if op in ('subs', 'subsc'): return '(%s - %s)' % (a[0], a[1])
        if op == 'subs_prev': return '(%s - ps)' % a[0]
        if op == 'seqs': return 'float(%s == 0.0)' % a[0]
        if op == 'sgts': return 'float(%s > 0.0)' % a[0]
        if op == 'sges': return 'float(%s >= 0.0)' % a[0]
        if op == 'snes': return 'float(%s != 0.0)' % a[0]
        if op == 'frcs': return 'fract(%s)' % a[0]
        if op == 'truncs': return 'trunc(%s)' % a[0]
        if op == 'floors': return 'floor(%s)' % a[0]
        if op == 'exp': return 'exp2(%s)' % a[0]
        if op in ('log', 'logc'): return 'log2(%s)' % a[0]
        if op in ('rcp', 'rcpc', 'rcpf'): return '(1.0 / %s)' % a[0]
        if op in ('rsq', 'rsqc', 'rsqf'): return 'inversesqrt(%s)' % a[0]
        if op == 'sqrt': return 'sqrt(%s)' % a[0]
        if op == 'sin': return 'sin(%s)' % a[0]
        if op == 'cos': return 'cos(%s)' % a[0]
        if op in ('setp_eq', 'setp_ne', 'setp_gt', 'setp_ge'):
            cmp = {'setp_eq': '== 0.0', 'setp_ne': '!= 0.0', 'setp_gt': '> 0.0', 'setp_ge': '>= 0.0'}[op]
            self.extra_pred = 'p0 = (%s %s);' % (a[0], cmp)
            return 'float(!p0)'
        if op == 'setp_inv':
            self.extra_pred = 'p0 = (%s == 1.0);' % a[0]
            return '(p0 ? 0.0 : ((%s == 0.0) ? 1.0 : %s))' % (a[0], a[0])
        if op == 'setp_pop':
            self.extra_pred = 'p0 = ((%s - 1.0) <= 0.0);' % a[0]
            return '(p0 ? 0.0 : (%s - 1.0))' % a[0]
        if op == 'setp_clr':
            self.extra_pred = 'p0 = false;'
            return '3.402823466e+38'
        if op == 'setp_rstr':
            self.extra_pred = 'p0 = (%s == 0.0);' % a[0]
            return '(p0 ? 0.0 : %s)' % a[0]
        if op in ('kills_eq', 'kills_gt', 'kills_ge', 'kills_ne', 'kills_one'):
            cmp = {'kills_eq': '== 0.0', 'kills_gt': '> 0.0', 'kills_ge': '>= 0.0', 'kills_ne': '!= 0.0',
                   'kills_one': '== 1.0'}[op]
            self.has_kill = True
            self.extra_kill = 'if (%s %s) discard;' % (a[0], cmp)
            return 'float(%s %s)' % (a[0], cmp)
        if op == 'retain_prev': return 'ps'
        self.err('scalar op %s is not implemented' % op)

    def emit_alu(self, vec, sca, pred):
        """vec/sca: (opname, saturate, result token, [operand tokens]) or None; pred: None/True/False."""
        self.extra_pred = None
        self.extra_kill = None
        stmts = []
        vres = None
        sres = None
        if vec:
            op, sat, res, ops = vec
            a = [self.parse_source(o, 4) for o in ops]  # sources before the result (read-before-write analysis)
            target, comps = self.parse_result(res)
            expr = self.vector_expr(op, a)
            if sat:
                expr = 'clamp(%s, 0.0, 1.0)' % expr
            vres = (target, comps, expr)
        if sca:
            op, sat, res, ops = sca
            if op in TWO_OPERAND_SCALAR:
                a = [self.parse_source(ops[0], 1), self.parse_source(ops[1], 1)]
            elif op in TWO_COMPONENT_SCALAR:
                two = self.parse_source(ops[0], 2)
                a = ['(%s).x' % two, '(%s).y' % two]
            elif ops:
                a = [self.parse_source(ops[0], 1)]
            else:
                a = []
            target, comps = self.parse_result(res)
            expr = self.scalar_expr(op, a)
            if sat:
                expr = 'clamp(%s, 0.0, 1.0)' % expr
            sres = (target, comps, expr)
        # Both parts read their operands before either writes (one instruction).
        n = self.tmp_counter()
        if vres:
            stmts.append('vec4 v%d = %s;' % (n, vres[2]))
        if sres:
            stmts.append('float s%d = %s;' % (n, sres[2]))
        if self.extra_pred:
            stmts.append(self.extra_pred)
        if sres:
            stmts.append('ps = s%d;' % n)
        if vres:
            stmts.extend(self.write_masked(vres[0], vres[1], 'v%d' % n, False))
        if sres:
            stmts.extend(self.write_masked(sres[0], sres[1], 'vec4(s%d)' % n, False))
        if self.extra_kill:
            stmts.append(self.extra_kill)
        if pred is not None:
            stmts = ['if (%sp0) {' % ('' if pred else '!')] + ['  ' + s for s in stmts] + ['}']
        self.body.extend(stmts)

    # ----- parsing ---------------------------------------------------------------------------
    def parse_alu_part(self, text):
        """'mad_sat oPos.xy__, r0.yzzz, c32.xyyy' -> (op, sat, result, [operands])"""
        m = re.match(r'^([a-z0-9_]+?)(_sat)?\s+(\S+?)(?:,\s*(.*))?$', text.strip())
        if not m:
            self.err('bad ALU text %r' % text)
        op, sat, res, rest = m.groups()
        ops = [o.strip() for o in rest.split(',')] if rest else []
        return op, bool(sat), res, ops

    def strip_pred(self, text):
        text = text.strip()
        if text.startswith('(p0)'):
            return True, text[4:].strip()
        if text.startswith('(!p0)'):
            return False, text[5:].strip()
        return None, text

    def translate(self):
        cf = []  # (index, text)
        instr_lines = []
        pending_alu = None  # (vec, sca, pred)
        serialize_next = False

        def flush_alu():
            nonlocal pending_alu
            if pending_alu:
                self.emit_alu(*pending_alu)
                pending_alu = None

        blocks = []  # list of dict(index, kind, pred, cond, target, stmts)
        cur = None

        def end_block():
            nonlocal cur
            flush_alu()
            if cur is not None:
                cur['stmts'] = self.body
                blocks.append(cur)
            self.body = []
            cur = None

        for raw in self.lines:
            line = raw.rstrip()
            if not line.strip():
                continue
            m = re.match(r'^/\*\s*(\d+)\.(\d+)\s*\*/\s*(.*)$', line)
            if m:  # control-flow instruction
                end_block()
                idx = int(m.group(1)) * 2 + int(m.group(2))
                text = m.group(3).split('//')[0].strip()
                pred, text = self.strip_pred(text)
                op = text.split()[0] if text else ''
                if op in ('exec', 'exece'):
                    cond = None
                    rest = text[len(op):].strip()
                    if rest.startswith('b') or rest.startswith('!b'):
                        self.uses_bools = True
                        cond = rest.split(',')[0].strip()
                    cur = {'index': idx, 'kind': 'exec', 'end': op == 'exece', 'pred': pred, 'cond': cond}
                elif op == 'jmp' or op == 'cjmp':
                    self.has_jump = True
                    m2 = re.match(r'^c?jmp\s+(?:(!?b\d+),\s*)?L(\d+)$', text)
                    if not m2:
                        self.err('bad jump', line)
                    if m2.group(1):
                        self.uses_bools = True
                    blocks.append({'index': idx, 'kind': 'jmp', 'pred': pred, 'cond': m2.group(1),
                                   'target': int(m2.group(2)), 'stmts': []})
                elif op in ('alloc', 'cnop', 'nop'):
                    blocks.append({'index': idx, 'kind': 'nop', 'stmts': []})
                elif op in ('loop', 'endloop', 'call', 'ccall', 'ret'):
                    self.err('control flow %s is not supported' % op, line)
                else:
                    self.err('unknown control flow %r' % text, line)
                continue
            m = re.match(r'^/\*\s*(\d+)\s*\*/\s*(.*)$', line)
            if m:
                text = m.group(2).strip()
            else:
                text = line.strip()
                if text.startswith('label L'):
                    # A jump target: the next control-flow instruction has this index already.
                    end_block()
                    continue
            if text == 'serialize':
                continue
            if text.startswith('+'):  # the scalar co-issue of the pending ALU instruction
                if not pending_alu:
                    self.err('scalar co-issue without a vector part', line)
                pred, body = self.strip_pred(text[1:])
                pending_alu = (pending_alu[0], self.parse_alu_part(body), pending_alu[2])
                continue
            flush_alu()
            pred, body = self.strip_pred(text)
            if cur is None:
                self.err('instruction outside an exec block', line)
            op = body.split()[0] if body else ''
            if op in ('vfetch_full', 'vfetch_mini'):
                self.emit_fetch_vertex(op, body[len(op):].strip())
            elif op.startswith('tfetch') or op in ('getTextureBorderColorFrac', 'getTextureComputedLod',
                                                     'getTextureGradients', 'getTextureWeights', 'setTextureLod',
                                                     'setTextureGradientsHorz', 'setTextureGradientsVert'):
                if not op.startswith('tfetch'):
                    self.err('texture instruction %s is not supported' % op, line)
                self.emit_fetch_texture(op, body[len(op):].strip())
            elif op == 'nop':
                continue
            elif op in VECTOR_OPS or (op.endswith('_sat') and op[:-4] in VECTOR_OPS):
                pending_alu = (self.parse_alu_part(body), None, pred)
            elif op in SCALAR_OPS or (op.endswith('_sat') and op[:-4] in SCALAR_OPS):
                pending_alu = (None, self.parse_alu_part(body), pred)
            else:
                self.err('unknown instruction %r' % body, line)
        end_block()
        return self.generate(blocks)

    # ----- output ----------------------------------------------------------------------------
    def generate(self, blocks):
        out = ['#version 460', '// hcu xenos2glsl: %s (%s)' % (self.name, self.stage)]
        ind = '  '
        if self.stage == 'vert':
            for a in self.attrs:
                out.append('layout(location = %d) in vec4 in_%d;  // vf%d offset %d %s' % (
                    a['index'], a['index'], a['slot'], a['offset_words'], a['format']))
        else:
            for k in sorted(self.reads_before_write):
                out.append('layout(location = %d) in vec4 i_%d;' % (k, k))
            for slot in sorted(self.textures):
                out.append('layout(binding = %d) uniform sampler%s tf_%d;' % (slot, self.textures[slot], slot))
        out.append('layout(std140, binding = 0) uniform Consts { vec4 c[256]; };')
        out.append('layout(std140, binding = 1) uniform Sys { vec4 ndc_scale; vec4 ndc_offset; vec4 alpha; uvec4 bools[2]; };')
        interps = sorted(int(e[1:]) for e in self.exports if re.match(r'^o\d+$', e))
        colors = sorted(int(e[2:]) for e in self.exports if e.startswith('oC'))
        if self.stage == 'vert':
            for k in interps:
                out.append('layout(location = %d) out vec4 o_%d;' % (k, k))
        else:
            for k in colors:
                out.append('layout(location = %d) out vec4 oC_%d;' % (k, k))
        out.append('void main() {')
        for i in range(self.max_reg + 1):
            if self.stage == 'frag' and i in self.reads_before_write:
                out.append(ind + 'vec4 r%d = i_%d;' % (i, i))
            else:
                out.append(ind + 'vec4 r%d = vec4(0.0);' % i)
        for e in sorted(self.exports):
            out.append(ind + 'vec4 %s = vec4(0.0);' % e)
        out.append(ind + 'bool p0 = false;')
        out.append(ind + 'float ps = 0.0;')

        def block_cond(b):
            conds = []
            if b.get('pred') is not None:
                conds.append('p0' if b['pred'] else '!p0')
            if b.get('cond'):
                c = b['cond']
                neg = c.startswith('!')
                n = int(c.lstrip('!b'))
                conds.append('%s((bools[%d][%d] >> %du) & 1u) != 0u' % ('!' if neg else '', n // 128, (n % 128) // 32, n % 32))
            return ' && '.join(conds)

        if not self.has_jump:
            for b in blocks:
                if b['kind'] != 'exec':
                    continue
                cond = block_cond(b)
                if cond:
                    out.append(ind + 'if (%s) {' % cond)
                    out.extend(ind + '  ' + s for s in b['stmts'])
                    out.append(ind + '}')
                else:
                    out.extend(ind + s for s in b['stmts'])
        else:
            out.append(ind + 'int pc = 0;')
            out.append(ind + 'while (pc >= 0) {')
            out.append(ind + '  switch (pc) {')
            for i, b in enumerate(blocks):
                out.append(ind + '  case %d: {' % b['index'])
                nxt = blocks[i + 1]['index'] if i + 1 < len(blocks) else -1
                if b['kind'] == 'exec':
                    cond = block_cond(b)
                    if cond:
                        out.append(ind + '    if (%s) {' % cond)
                        out.extend(ind + '      ' + s for s in b['stmts'])
                        out.append(ind + '    }')
                    else:
                        out.extend(ind + '    ' + s for s in b['stmts'])
                    out.append(ind + '    pc = %d;' % (-1 if b['end'] else nxt))
                elif b['kind'] == 'jmp':
                    cond = block_cond(b)
                    if cond:
                        out.append(ind + '    pc = (%s) ? %d : %d;' % (cond, b['target'], nxt))
                    else:
                        out.append(ind + '    pc = %d;' % b['target'])
                else:
                    out.append(ind + '    pc = %d;' % nxt)
                out.append(ind + '  } break;')
            out.append(ind + '  default: pc = -1; break;')
            out.append(ind + '  }')
            out.append(ind + '}')
        if self.stage == 'vert':
            out.append(ind + 'gl_Position = vec4(oPos.xy * ndc_scale.xy + ndc_offset.xy * oPos.w, '
                             'oPos.z * ndc_scale.z + ndc_offset.z * oPos.w, oPos.w);')
            for k in interps:
                out.append(ind + 'o_%d = o%d;' % (k, k))
        else:
            if colors:
                out.append(ind + '// Alpha test (RB_COLORCONTROL): alpha.x enable, alpha.y function, alpha.z reference.')
                out.append(ind + 'if (alpha.x > 0.5) {')
                out.append(ind + '  uint f = uint(alpha.y); float a = oC0.w; bool pass = f == 7u || (f == 1u && a < alpha.z) || '
                                 '(f == 2u && a == alpha.z) || (f == 3u && a <= alpha.z) || (f == 4u && a > alpha.z) || '
                                 '(f == 5u && a != alpha.z) || (f == 6u && a >= alpha.z);')
                out.append(ind + '  if (!pass) discard;')
                out.append(ind + '}')
            for k in colors:
                out.append(ind + 'oC_%d = oC%d;' % (k, k))
        out.append('}')
        meta = {
            'name': self.name, 'stage': self.stage,
            'attributes': self.attrs if self.stage == 'vert' else [],
            'textures': {str(k): v for k, v in sorted(self.textures.items())},
            'interpolators': interps, 'inputs': sorted(self.reads_before_write) if self.stage == 'frag' else [],
            'colors': colors, 'has_jump': self.has_jump, 'uses_bools': self.uses_bools, 'kill': self.has_kill,
        }
        return '\n'.join(out) + '\n', meta


def geometry_glsl(name, interps):
    """A pass-through geometry shader for Xenos rectangle lists: 3 vertices per rectangle, the
    4th = v1 + v2 - v0 (positions and every interpolator extrapolated). deko3d's fill-rectangle
    mode does the same on the console; Eden ignores that mode, so the backend binds this shader
    for rectangle-list draws (cvar deko3d_fill_rectangle selects the hardware mode)."""
    out = ['#version 460', '// hcu xenos2glsl: %s (geom: rectangle lists)' % name,
           'layout(triangles) in;', 'layout(triangle_strip, max_vertices = 4) out;']
    for k in interps:
        out.append('layout(location = %d) in vec4 g_%d[];' % (k, k))
        out.append('layout(location = %d) out vec4 o_%d;' % (k, k))
    out.append('void main() {')
    out.append('  for (int i = 0; i < 3; i++) {')
    out.append('    gl_Position = gl_in[i].gl_Position;')
    for k in interps:
        out.append('    o_%d = g_%d[i];' % (k, k))
    out.append('    EmitVertex();')
    out.append('  }')
    out.append('  gl_Position = gl_in[1].gl_Position + gl_in[2].gl_Position - gl_in[0].gl_Position;')
    for k in interps:
        out.append('  o_%d = g_%d[1] + g_%d[2] - g_%d[0];' % (k, k, k, k))
    out.append('  EmitVertex();')
    out.append('  EndPrimitive();')
    out.append('}')
    return '\n'.join(out) + '\n'


def translate_file(path):
    stage = 'vert' if path.endswith('.vert') else 'frag' if path.endswith('.frag') else None
    if stage is None:
        raise SystemExit('%s: name must end in .vert or .frag' % path)
    name = os.path.basename(path).replace('.ucode.', '.')
    text = open(path, encoding='utf-8').read()
    return Translator(text, stage, name).translate()


def uam_check(glsl_path, stage):
    uam = os.environ.get('UAM', '/opt/devkitpro/tools/bin/uam')
    out = glsl_path + '.dksh'
    r = subprocess.run([uam, '-s', stage, '-o', out, glsl_path], capture_output=True, text=True)
    if r.returncode != 0:
        return False, (r.stdout + r.stderr).strip()
    return True, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input', nargs='?')
    ap.add_argument('-o', '--out')
    ap.add_argument('--json')
    ap.add_argument('--all', help='translate every *.ucode.vert/frag in this directory')
    ap.add_argument('--out-dir', help='output directory for --all')
    ap.add_argument('--check', action='store_true', help='compile the output with uam')
    args = ap.parse_args()
    if args.all:
        files = sorted(f for f in os.listdir(args.all) if f.endswith('.ucode.vert') or f.endswith('.ucode.frag'))
        os.makedirs(args.out_dir or args.all, exist_ok=True)
        ok = 0
        for f in files:
            src = os.path.join(args.all, f)
            try:
                glsl, meta = translate_file(src)
            except SystemExit as e:
                print('FAIL %s: %s' % (f, e))
                continue
            stem = f.replace('.ucode.', '.')
            dst = os.path.join(args.out_dir or args.all, stem + '.glsl')
            open(dst, 'w', newline='\n').write(glsl)
            open(dst.replace('.glsl', '.json'), 'w', newline='\n').write(json.dumps(meta, indent=1))
            if args.check:
                good, msg = uam_check(dst, meta['stage'])
                print('%s %s%s' % ('OK  ' if good else 'FAIL', stem, '' if good else ': ' + msg))
                ok += good
            else:
                ok += 1
            if meta['stage'] == 'vert':
                # The rectangle-list geometry shader of this vertex shader (same hash, .geom).
                gdst = os.path.join(args.out_dir or args.all, stem.replace('.vert', '.geom') + '.glsl')
                open(gdst, 'w', newline='\n').write(geometry_glsl(meta['name'], meta['interpolators']))
                if args.check:
                    good, msg = uam_check(gdst, 'geom')
                    if not good:
                        print('FAIL %s: %s' % (os.path.basename(gdst), msg))
        print('%d of %d translated' % (ok, len(files)))
        return
    glsl, meta = translate_file(args.input)
    if args.out:
        open(args.out, 'w', newline='\n').write(glsl)
        if args.check:
            good, msg = uam_check(args.out, meta['stage'])
            print('OK' if good else 'FAIL: ' + msg)
    else:
        sys.stdout.write(glsl)
    if args.json:
        open(args.json, 'w', newline='\n').write(json.dumps(meta, indent=1))


if __name__ == '__main__':
    main()
