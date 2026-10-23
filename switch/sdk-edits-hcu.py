#!/usr/bin/env python3
"""HCU-only SDK edits, run by switch/build.sh after the shared switch/sdk-edits.py: GCC/Horizon fixes for
HCU's patches/rexglue-sdk (not shared with cvhd-recomp, whose SDK has no such patches).

    python3 switch/sdk-edits-hcu.py /root/hcu/rexglue-sdk

Idempotent like sdk-edits.py (same edit() helper).
"""
import os, re, sys

SDK = sys.argv[1]
changed = []
NL = chr(10)


def edit(rel, pairs, must=True):
    path = os.path.join(SDK, rel)
    s = open(path, encoding='utf-8').read()
    orig = s
    for old, new in pairs:
        if new in s:
            continue
        if old not in s:
            # A later corrective edit may have rewritten this spot already (the base text is gone,
            # the corrected text differs from `new`). Warn; STRICT=1 (fresh SDK checkout) makes it fatal.
            if must and os.environ.get('STRICT') == '1':
                raise SystemExit(f'{rel}: anchor not found:\n{old}')
            if must:
                print(f'warning: {rel}: anchor not found (already edited?): {old[:60]!r}')
            continue
        s = s.replace(old, new, 1)
    if s != orig:
        open(path, 'w', encoding='utf-8', newline='\n').write(s)
        changed.append(rel)


def write(rel, text):
    path = os.path.join(SDK, rel)
    if os.path.exists(path) and open(path, encoding='utf-8').read() == text:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w', encoding='utf-8', newline='\n').write(text)
    changed.append(rel)


# ===== hcu-switch: edits for HCU's patches/rexglue-sdk on top of the cvhd-recomp edits above =====

# 0009 guest_wake.cpp: REX_PLATFORM_LINUX is also set on the Switch, where <linux/futex.h> does not
# exist. The Switch has no game-side fence waiter yet (src/gpu_fence_wait.cpp is Windows/Linux), so the
# wake stays a no-op there.
edit('src/graphics/guest_wake.cpp', [
    ('#elif REX_PLATFORM_LINUX' + NL + '#include <climits>' + NL,
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '#include <climits>' + NL),
    ('#elif REX_PLATFORM_LINUX' + NL + '  std::atomic_thread_fence',
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '  std::atomic_thread_fence'),
])

# WAIT_REG_MEM statistics (the `[nx] gpu waits` line of switch_nx.cpp) on HCU's 0003/0025 wait: the
# 16 yield re-polls, then a 1 ms wait on the vblank event. Edit 21 above anchors on the stock sleep
# (its extern declarations still apply).
edit('src/graphics/command_processor.cpp', [
    ('          rex::thread::Wait(guest_progress_event_.get(), false, std::chrono::milliseconds(1));' + NL,
     '#if REX_PLATFORM_SWITCH' + NL +
     '          rex_nx_wait_polls++;' + NL + '          rex_nx_wait_sleep_ms += wait / 0x100;' + NL +
     '          const uint64_t nx_t0 = rex::chrono::Clock::QueryHostTickCount();' + NL +
     '#endif' + NL +
     '          rex::thread::Wait(guest_progress_event_.get(), false, std::chrono::milliseconds(1));' + NL +
     '#if REX_PLATFORM_SWITCH' + NL +
     '          rex_nx_wait_slept_ticks += rex::chrono::Clock::QueryHostTickCount() - nx_t0;' + NL +
     '#endif' + NL),
    ('          rex::thread::MaybeYield();' + NL + '          ++unmatched_poll_count;' + NL,
     '#if REX_PLATFORM_SWITCH' + NL + '          rex_nx_wait_yields++;' + NL + '#endif' + NL +
     '          rex::thread::MaybeYield();' + NL + '          ++unmatched_poll_count;' + NL),
])
print('changed (hcu):', *changed, sep=NL + '  ')

# 0006 RtlEnterCriticalSection spin: __builtin_arm_yield is clang's; GCC (devkitA64) takes the instruction.
edit('src/kernel/xboxkrnl/xboxkrnl_rtl.cpp', [
    ('#elif defined(__aarch64__)' + NL + '  __builtin_arm_yield();' + NL,
     '#elif defined(__aarch64__)' + NL + '  __asm__ volatile("yield");' + NL),
])
print('changed (hcu 0006):', *changed, sep=NL + '  ')
