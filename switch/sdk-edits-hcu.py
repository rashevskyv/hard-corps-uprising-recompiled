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
# 16 yield re-polls, then (Switch) 100 us sleeps instead of a 1 ms wait on the vblank event. Edit 21 above anchors on the stock sleep
# (its extern declarations still apply).
edit('src/graphics/command_processor.cpp', [
    ('          rex::thread::Wait(guest_progress_event_.get(), false, std::chrono::milliseconds(1));' + NL,
     '#if REX_PLATFORM_SWITCH' + NL +
     '          rex_nx_wait_polls++;' + NL + '          rex_nx_wait_sleep_ms += wait / 0x100;' + NL +
     '          const uint64_t nx_t0 = rex::chrono::Clock::QueryHostTickCount();' + NL +
     '          // 100 us, not 1 ms on the event: only vblank sets it, not the guest store the packet waits for,' + NL +
     '          // so the command processor slept ~470 ms a second in 1 ms slices (hcu 0.201 console: about' + NL +
     '          // 1000 sleeping polls per 2 s) while the main thread waited for it in the ring.' + NL +
     '          rex::thread::Sleep(std::chrono::microseconds(100));' + NL +
     '          rex_nx_wait_slept_ticks += rex::chrono::Clock::QueryHostTickCount() - nx_t0;' + NL +
     '#else' + NL +
     '          rex::thread::Wait(guest_progress_event_.get(), false, std::chrono::milliseconds(1));' + NL +
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

# Switch audio (from gh-recomp): the guest frames go to libnx audout (src/nx_audout_driver.h) instead of the
# silent nop driver. The header is copied next to nop_audio_system.cpp, which creates it on __SWITCH__.
def nx_audio(s):
    if 'hcu-edits: audout' in s:
        return s
    anchor = '#include <rex/thread.h>' + NL
    assert anchor in s, 'nop_audio_system.cpp include anchor'
    s = s.replace(anchor, anchor + '#ifdef __SWITCH__  // hcu-edits: audout' + NL +
                  '#include "nx_audout_driver.h"' + NL + '#endif' + NL, 1)
    old = '  *out_driver = new NopAudioDriver(memory(), semaphore);' + NL
    assert old in s, 'nop CreateDriver'
    return s.replace(old, '#ifdef __SWITCH__' + NL + '  *out_driver = new NxAudoutDriver(memory(), semaphore);' + NL +
                     '#else' + NL + old + '#endif' + NL, 1)


import shutil
_nop = os.path.join(SDK, 'src/audio/nop/nop_audio_system.cpp')
_hdr_src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'nx_audout_driver.h')
_hdr_dst = os.path.join(SDK, 'src/audio/nop/nx_audout_driver.h')
if not os.path.exists(_hdr_dst) or open(_hdr_src, 'rb').read() != open(_hdr_dst, 'rb').read():
    shutil.copyfile(_hdr_src, _hdr_dst)
    changed.append('src/audio/nop/nx_audout_driver.h')
_s = open(_nop, encoding='utf-8').read()
_t = nx_audio(_s)
if _t != _s:
    open(_nop, 'w', encoding='utf-8', newline=NL).write(_t)
    changed.append('src/audio/nop/nop_audio_system.cpp')
print('changed (hcu audio):', *changed, sep=NL + '  ')

# The generated code's address fold (rex_nx_host_offset) was a chain of four compares per guest load or
# store, about 17 instructions for one `ldr` (seen in hcu-0.172's disassembly). Every boundary of the
# rules is a multiple of 16 MB, so one 256-entry table indexed by the top byte gives the same offset.
edit('resources/templates/codegen/pch_h.inja', [
    ('static inline __attribute__((always_inline)) int64_t rex_nx_host_offset(u32 a) {' + NL +
     '  if (a >= 0xE0000000u) return -0x40000000ll + 0x1000;' + NL +
     '  if (a >= 0xC0000000u) return -0x20000000ll;' + NL +
     '  if (a >= 0x90000000u && a < 0xA0000000u) return -0x10000000ll;' + NL +
     '  if (a >= 0x7F000000u && a < 0x80000000u) return 0x21000000ll;  // GPU writeback window: physical 0-16 MB' + NL +
     '  return 0;' + NL + '}' + NL,
     'struct RexNxOffsets {' + NL +
     '  int32_t v[256];' + NL +
     '  constexpr RexNxOffsets() : v{} {' + NL +
     '    for (u32 i = 0; i < 256; i++) {' + NL +
     '      int64_t o = 0;' + NL +
     '      if (i >= 0xE0u) o = -0x40000000ll + 0x1000;' + NL +
     '      else if (i >= 0xC0u) o = -0x20000000ll;' + NL +
     '      else if (i >= 0x90u && i < 0xA0u) o = -0x10000000ll;' + NL +
     '      else if (i == 0x7Fu) o = 0x21000000ll;  // GPU writeback window: physical 0-16 MB' + NL +
     '      v[i] = (int32_t)o;' + NL +
     '    }' + NL +
     '  }' + NL + '};' + NL +
     'static constexpr RexNxOffsets rex_nx_offsets{};' + NL +
     'static inline __attribute__((always_inline)) int64_t rex_nx_host_offset(u32 a) { return rex_nx_offsets.v[a >> 24]; }' + NL),
])
print('changed (hcu fold):', *changed, sep=NL + '  ')

# Call counters of the hooked guest functions and kernel exports (REX_HOOK is the one wrapper they all
# go through): the Switch host prints the top ones with the 30 s profile (`[hooks]` lines), to see which
# kernel calls the main thread makes per second.
write('include/rex/nx_hook_counter.h',
      '#pragma once' + NL + '#include <cstdint>' + NL + '#include <cstring>' + NL +
      '// The CPU-work meter (hcu src/frame_meter.cpp): time in waiting exports, and VdSwap as the frame mark.' + NL +
      '// Weak defaults for binaries without the meter (the SDK tool rexglue); frame_meter.cpp defines the real ones.' + NL +
      'extern "C" __attribute__((weak)) uint64_t rex_nx_meter_enter() { return 0; }' + NL +
      'extern "C" __attribute__((weak)) void rex_nx_meter_leave(uint64_t, int) {}' + NL +
      'namespace rex_nx {' + NL +
      'inline int HookKind(const char* s) {' + NL +
      '  static const char* const waits[] = {"__imp__KeWaitForSingleObject", "__imp__KeWaitForMultipleObjects",' + NL +
      '                                      "__imp__NtWaitForSingleObjectEx", "__imp__NtWaitForMultipleObjectsEx",' + NL +
      '                                      "__imp__KeDelayExecutionThread", "__imp__NtYieldExecution",' + NL +
      '                                      "__imp__RtlEnterCriticalSection"};' + NL +
      '  if (std::strcmp(s, "__imp__VdSwap") == 0) return 2;' + NL +
      '  for (const char* w : waits) if (std::strcmp(s, w) == 0) return 1;' + NL +
      '  return 0;' + NL + '}' + NL +
      'struct HookCounter {' + NL +
      '  const char* name;' + NL +
      '  uint32_t n = 0;' + NL +
      '  int kind;  // 0 plain, 1 a waiting export, 2 VdSwap' + NL +
      '  HookCounter* next;' + NL +
      '  static HookCounter*& head() { static HookCounter* h = nullptr; return h; }' + NL +
      '  explicit HookCounter(const char* s) : name(s), kind(HookKind(s)), next(head()) { head() = this; }' + NL +
      '};' + NL + '}  // namespace rex_nx' + NL)
edit('include/rex/hook.h', [
    ('#else' + NL + '#define REX_HOOK(subroutine, function)                  \\' + NL +
     '  extern "C" REX_FUNC(subroutine) {                     \\' + NL +
     '    rex::ppc::HostToGuestFunction<function>(ctx, base); \\' + NL + '  }' + NL,
     '#elif defined(__SWITCH__) || defined(HCU_LINUX_TESTHOST)' + NL +
     '#include <rex/nx_hook_counter.h>' + NL +
     '#define REX_HOOK(subroutine, function)                           \\' + NL +
     '  static rex_nx::HookCounter rex_nx_hc_##subroutine{#subroutine}; \\' + NL +
     '  extern "C" REX_FUNC(subroutine) {                              \\' + NL +
     '    ++rex_nx_hc_##subroutine.n;                                  \\' + NL +
     '    if (!rex_nx_hc_##subroutine.kind) {                          \\' + NL +
     '      rex::ppc::HostToGuestFunction<function>(ctx, base);        \\' + NL +
     '      return;                                                    \\' + NL +
     '    }                                                            \\' + NL +
     '    const uint64_t rex_nx_t0 = rex_nx_meter_enter();             \\' + NL +
     '    rex::ppc::HostToGuestFunction<function>(ctx, base);          \\' + NL +
     '    rex_nx_meter_leave(rex_nx_t0, rex_nx_hc_##subroutine.kind);  \\' + NL + '  }' + NL +
     '#else' + NL + '#define REX_HOOK(subroutine, function)                  \\' + NL +
     '  extern "C" REX_FUNC(subroutine) {                     \\' + NL +
     '    rex::ppc::HostToGuestFunction<function>(ctx, base); \\' + NL + '  }' + NL),
])
print('changed (hcu hook counters):', *changed, sep=NL + '  ')

# non_volatile_as_local keeps the vector registers in ctx: the XMV movie decoder swaps all 128 of them
# (sub_82B8AEA0 / sub_82B8B1E8: store to one buffer, load v64-v127 from a constant table) and its
# routines read those constants as live-ins. As locals they were zero: the intro movie broke into
# colour stripes (0.191). With v14-v31 and v64-v127 in ctx, the __savevmx/__restvmx calls must stay.
edit('src/codegen/builders/context.cpp', [
    ('  if ((cfg.nonArgumentRegistersAsLocalVariables && (index >= 32 && index <= 63)) ||' + NL +
     '      (localizeNonVolatiles() && ((index >= 14 && index <= 31) || (index >= 64 && index <= 127)))) {' + NL,
     '  if (cfg.nonArgumentRegistersAsLocalVariables && (index >= 32 && index <= 63)) {' + NL),
    ('      if (cfg.nonVolatileRegistersAsLocalVariables &&' + NL +
     '          (name.find("__rest") == 0 || name.find("__save") == 0)) {' + NL,
     '      if (cfg.nonVolatileRegistersAsLocalVariables &&' + NL +
     '          (name.find("__rest") == 0 || name.find("__save") == 0) &&' + NL +
     '          name.find("vmx") == std::string::npos) {' + NL),
])
print('changed (hcu vector registers in ctx):', *changed, sep=NL + '  ')


# Switch audio (from gh-recomp, docs/SWITCH-AUDIO.md there): one host thread per guest audio client
# (switch/audio_client_threads.inc) instead of the single audio worker running every guest callback in turn;
# the worker itself only waits for the shutdown event.
_inc = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'audio_client_threads.inc'), encoding='utf-8').read()
edit('src/audio/audio_system.cpp', [
    ('namespace rex::audio {' + NL,
     'namespace rex::audio {' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     'static system::KernelState* hcu_kernel_state = nullptr;' + NL + 'static uint32_t hcu_client_gen[8];' + NL + 'extern "C" volatile uint64_t rex_nx_audio_cb_us;  // hcu src/frame_meter.cpp' + NL + '#endif' + NL),
    ('X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {' + NL,
     'X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {' + NL +
     '#if REX_PLATFORM_SWITCH' + NL + '  hcu_kernel_state = kernel_state;' + NL + '#endif' + NL),
    ('  clients_[index] = {driver, callback, callback_arg, ptr, true};' + NL,
     '  clients_[index] = {driver, callback, callback_arg, ptr, true};' + NL + _inc),
    ('  clients_[index] = {nullptr, 0, 0, 0, false};' + NL,
     '  clients_[index] = {nullptr, 0, 0, 0, false};' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     '  ++hcu_client_gen[index];' + NL + '#endif' + NL),
    ('  while (worker_running_) {' + NL,
     '  while (worker_running_) {' + NL + '#if REX_PLATFORM_SWITCH  // hcu-edits: the clients have their own threads' + NL +
     '    rex::thread::Wait(shutdown_event_.get(), true, std::chrono::milliseconds(500));' + NL +
     '    continue;' + NL + '#endif' + NL),
    ('#include <rex/audio/audio_system.h>' + NL + '#include <rex/audio/flags.h>' + NL,
     '#include <rex/audio/audio_system.h>' + NL + '#include <chrono>' + NL + '#include <memory>' + NL +
     '#include <string>' + NL + '#include <rex/audio/flags.h>' + NL),
])
print('changed (hcu audio client threads):', *changed, sep=NL + '  ')

# Frame-time spread every 30 s next to the 1 s fps line (the target is a steady 60 fps, and a mean hides
# the long frames): swap-to-swap time in 0.25 ms bins up to 100 ms, p50/p95/p99 and frames over 17/20/25/33 ms.
edit('src/graphics/command_processor.cpp', [
    ('      nx_frames++;' + NL,
     '      nx_frames++;' + NL +
     '      {' + NL +
     '        static uint32_t nx_bins[401] = {};' + NL +
     '        static uint64_t nx_spread_tick = 0;' + NL +
     '        if (!nx_spread_tick) nx_spread_tick = now;' + NL +
     '        nx_bins[std::min<uint64_t>(uint64_t(dt_us) / 250, 400)]++;' + NL +
     '        if (now - nx_spread_tick >= 30 * freq) {' + NL +
     '          uint64_t n = 0;' + NL +
     '          for (uint32_t b : nx_bins) n += b;' + NL +
     '          auto pct = [&](double p) {' + NL +
     '            uint64_t want = uint64_t(p * double(n)), acc = 0;' + NL +
     '            for (int i = 0; i <= 400; i++) { acc += nx_bins[i]; if (acc > want) return (i + 1) * 0.25; }' + NL +
     '            return 100.0;' + NL +
     '          };' + NL +
     '          auto over = [&](int ms) { uint64_t c = 0; for (int i = ms * 4; i <= 400; i++) c += nx_bins[i]; return (unsigned long long)c; };' + NL +
     '          std::printf("[nx] frame times (30 s, %llu frames): p50 %.2f p95 %.2f p99 %.2f ms; over 17 ms %llu, 20 ms %llu, 25 ms %llu, 33 ms %llu" "' + chr(92) + 'n",' + NL +
     '                      (unsigned long long)n, pct(0.50), pct(0.95), pct(0.99), over(17), over(20), over(25), over(33));' + NL +
     '          std::memset(nx_bins, 0, sizeof(nx_bins));' + NL +
     '          nx_spread_tick = now;' + NL +
     '        }' + NL +
     '      }' + NL),
])
print('changed (hcu frame-time spread):', *changed, sep=NL + '  ')

# The CPU-work meter (src/frame_meter.cpp): the command processor's waits as one monotonic tick count
# (its idle wait for the write pointer and its WAIT_REG_MEM polls), on the Switch and the Linux test host.
edit('src/graphics/command_processor.cpp', [
    ('CpStats cp_stats;' + NL,
     'CpStats cp_stats;' + NL + 'extern "C" volatile uint64_t rex_nx_cp_wait_ticks;  // hcu src/frame_meter.cpp' + NL),
    ('    if (freq) cp_stats.wait_poll_ns += (rex::chrono::Clock::QueryHostTickCount() - nx_wait_start) * 1000000000ull / freq;' + NL,
     '    if (freq) cp_stats.wait_poll_ns += (rex::chrono::Clock::QueryHostTickCount() - nx_wait_start) * 1000000000ull / freq;' + NL +
     '    rex_nx_cp_wait_ticks += rex::chrono::Clock::QueryHostTickCount() - nx_wait_start;' + NL),
    ('      PrepareForWait();' + NL + '      bool poll_background_work = PollBackgroundWork();' + NL,
     '      const uint64_t nx_idle0 = rex::chrono::Clock::QueryHostTickCount();' + NL +
     '      PrepareForWait();' + NL + '      bool poll_background_work = PollBackgroundWork();' + NL),
    ('      ReturnFromWait();' + NL + '      if (!worker_running_ || !pending_fns_.empty()) {' + NL,
     '      ReturnFromWait();' + NL + '      rex_nx_cp_wait_ticks += rex::chrono::Clock::QueryHostTickCount() - nx_idle0;' + NL +
     '      if (!worker_running_ || !pending_fns_.empty()) {' + NL),
])
print('changed (hcu cpu meter):', *changed, sep=NL + '  ')
