#!/usr/bin/env python3
# Variant B (branch renut-ultimate): the sections of switch/sdk-edits.py that HCU still needs on the ultimate base's SDK.
#   python3 switch/sdk-edits-base.py SDK [--print]
# The base has its own Horizon layer (REX_PLATFORM_SWITCH: CMake, memory mirrors, threads, clock, input, audout), so
# most of sdk-edits.py must not run there. Its 31 sections, by the evidence of the anchor check on the base:
#   base has it, or NVK only (14): CMake, Headers, Sources, Guest memory fold, Added later (threading), Corrections
#     after the advisor review (fold), libnx pad driver, VkPipelineCache, sdmc absolute, GuestPtr fold, Host clock,
#     Too few processor cores, vkDestroyInstance, audio worker.
#   diagnostic, dropped (10): the step marks (3), vblank marks, FPS perf counters, WAIT_REG_MEM statistics, CP phase
#     timers, IssueDraw phases, pre sub-timers, GPU work generators.
#   behaviour (7): the sections in SECTIONS below, except "GPU write tracking": its store marks call the fold
#     (rex_nx_host_offset), which the base does not have, and miss the base's D-form stores. On the base the
#     marks are its store cards (tools/store_cards.py, one card per 4 KB guest page at ea >> 12); the deko3d
#     back end must read the cards of a texture's physical-mirror pages at its bind instead of rex_nx_dirty.
#     Not done yet. 2 anchors of "Directory opens" (host_path_entry.cpp, host_path_file.cpp) do not match: the base
#     already opens a directory without a host handle (host_path_entry.cpp) and guards ReadSync and the others
#     against it (host_path_file.cpp). The script warns for them; that is expected.
# The sections are taken from sdk-edits.py by their header line, so both files stay one source of truth.
# --print writes the composed script to stdout instead of running it (for the anchor check).
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SECTIONS = [
    '# --- Null GPU backend',
    '# --- Extra GPU backends from the game tree',
    '# --- Missing-file opens',
    '# --- Directory opens',
    '# --- Guest DLL modules',
    '# --- Function tables out of the way',
]
src = open(os.path.join(HERE, 'sdk-edits.py'), encoding='utf-8').read().split('\n')
starts = [i for i, l in enumerate(src) if l.startswith('# --- ')]
parts = ['\n'.join(src[:starts[0]])]
for want in SECTIONS:
    hit = [i for i in starts if src[i].startswith(want)]
    assert len(hit) == 1, want
    end = next((j for j in starts if j > hit[0]), len(src))
    parts.append('\n'.join(src[hit[0]:end]))
# From switch/sdk-edits-hcu.py: HCU's patch 0009 (guest_wake.cpp) takes the Linux futex branch on the Switch too,
# where <linux/futex.h> does not exist (the rexgpu-xenos build failed on it). Of its other 14 edits, 5 are in the
# base or not needed there (threading_switch wait, critical-section spin, nop audio -> audout, fold table, vector
# registers in ctx = non_volatile_as_local_keep), 5 are diagnostics (WAIT_REG_MEM statistics, hook counters (2),
# swap histogram, CP wait meter) and 4 still have to be ported (audio client threads, the button-glyph swap in
# host_path_file.cpp and virtual_file_system.cpp, the FPCR counter; FPCR one mode is the base's BLUR_FPCR_ONE_MODE).
parts.append('''
edit('src/graphics/guest_wake.cpp', [
    ('#elif REX_PLATFORM_LINUX' + NL + '#include <climits>' + NL,
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '#include <climits>' + NL),
    ('#elif REX_PLATFORM_LINUX' + NL + '  std::atomic_thread_fence',
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '  std::atomic_thread_fence'),
])
''')
code = '\n'.join(parts) + '\n'
if '--print' in sys.argv:
    print(code)
else:
    sys.argv = [os.path.join(HERE, 'sdk-edits.py')] + [a for a in sys.argv[1:] if a != '--print']
    exec(compile(code, sys.argv[0], 'exec'), {'__name__': '__main__', '__file__': sys.argv[0]})
