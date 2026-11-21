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
#     marks are its store cards (tools/store_cards.py of the base, one card per 4 KB guest page at ea >> 12), which
#     the deko3d back end reads at each texture bind and vertex copy (src/gpu/deko3d_backend.cpp, PullCards).
#     2 anchors of "Directory opens" (host_path_entry.cpp, host_path_file.cpp) do not match: the base already opens a
#     directory without a host handle (host_path_entry.cpp) and guards ReadSync and the others against it
#     (host_path_file.cpp). The script warns for them; that is expected.
#     "Null GPU backend" without its rex_app.cpp edit: the base defines the gpu_backend cvar and passes it to
#     LoadGpuPlugin itself (a second definition was a duplicate symbol).
# The sections are taken from sdk-edits.py by their header line, so both files stay one source of truth. The HCU edits
# of switch/sdk-edits-hcu.py follow below, re-anchored on the base (its text differs: the hook macro has the export
# census, the audio worker waits for a running callback at unregistration, ReadSync has three more success paths).
# --print writes the composed sdk-edits.py part to stdout instead of running it (for the anchor check).
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
code = '\n'.join(parts) + '\n'
# The base has the gpu_backend cvar: the Null section's rex_app.cpp edit goes.
cut = code.index("edit('src/ui/rex_app.cpp', [")
code = code[:cut] + code[code.index('\n])\n', cut) + len('\n])\n'):]
if '--print' in sys.argv:
    print(code)
    sys.exit(0)
sys.argv = [os.path.join(HERE, 'sdk-edits.py')] + sys.argv[1:]
g = {'__name__': '__main__', '__file__': sys.argv[0]}
exec(compile(code, sys.argv[0], 'exec'), g)
edit, write, NL, changed = g['edit'], g['write'], g['NL'], g['changed']

# From switch/sdk-edits-hcu.py: HCU's patch 0009 (guest_wake.cpp) takes the Linux futex branch on the Switch too,
# where <linux/futex.h> does not exist (the rexgpu-xenos build failed on it). Of its other 14 edits, 5 are in the
# base or not needed there (threading_switch wait, critical-section spin = the base's kernel_critsec_spin cvar, nop
# audio -> audout, fold table, vector registers in ctx = non_volatile_as_local_keep). The FPCR switch counter is not
# ported: it is a diagnostic, and the base's FPSCRRegister has its own census hooks (REX_NX_CENSUS, REX_SYS_CENSUS)
# where the same counts go. The WAIT_REG_MEM statistics and the swap histogram are dropped (diagnostics). Ported below:
# the hook counters and the CP wait meter (src/frame_meter.cpp's frame mark and waits; CMake HCU_NX_METER), the
# audio client threads (settings.txt audio_threads=1; off = the base's worker), the button-glyph swap.
edit('src/graphics/guest_wake.cpp', [
    ('#elif REX_PLATFORM_LINUX' + NL + '#include <climits>' + NL,
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '#include <climits>' + NL),
    ('#elif REX_PLATFORM_LINUX' + NL + '  std::atomic_thread_fence',
     '#elif REX_PLATFORM_LINUX && !REX_PLATFORM_SWITCH' + NL + '  std::atomic_thread_fence'),
])

# --- The CPU-work meter (src/frame_meter.cpp): VdSwap marks the main thread's frame, the waiting exports its waits.
write('include/rex/nx_hook_counter.h', r'''#pragma once
#include <cstdint>
#include <cstring>
// The CPU-work meter (hcu src/frame_meter.cpp): time in waiting exports, and VdSwap as the frame mark.
// Weak defaults for binaries without the meter; frame_meter.cpp defines the real ones.
#ifndef REX_NX_METER_IMPL
extern "C" __attribute__((weak)) uint64_t rex_nx_meter_enter() { return 0; }
extern "C" __attribute__((weak)) void rex_nx_meter_leave(uint64_t, int, uint64_t*) {}
#else
extern "C" uint64_t rex_nx_meter_enter();
// main_wait: the export's HookCounter::main_wait (the main thread's time in it) or null.
extern "C" void rex_nx_meter_leave(uint64_t t0, int kind, uint64_t* main_wait);
#endif
namespace rex_nx {
inline int HookKind(const char* s) {
  static const char* const waits[] = {"__imp__KeWaitForSingleObject", "__imp__KeWaitForMultipleObjects",
                                      "__imp__NtWaitForSingleObjectEx", "__imp__NtWaitForMultipleObjectsEx",
                                      "__imp__KeDelayExecutionThread", "__imp__NtYieldExecution",
                                      "__imp__RtlEnterCriticalSection"};
  if (std::strcmp(s, "__imp__VdSwap") == 0) return 2;
  for (const char* w : waits) if (std::strcmp(s, w) == 0) return 1;
  return 0;
}
struct HookCounter {
  const char* name;
  uint32_t n = 0;
  int kind;  // 0 plain, 1 a waiting export, 2 VdSwap
  uint64_t main_wait = 0;  // host ticks the main thread waited in it (written and reset by that thread only)
  HookCounter* next;
  static HookCounter*& head() { static HookCounter* h = nullptr; return h; }
  explicit HookCounter(const char* s) : name(s), kind(HookKind(s)), next(head()) { head() = this; }
};
}  // namespace rex_nx
''')
edit('include/rex/hook.h', [
    ('#else' + NL + '#define REX_HOOK(subroutine, function)                  \\' + NL +
     '  extern "C" REX_FUNC(subroutine) {                     \\' + NL +
     '    REX_EXPORT_CENSUS(#subroutine);                     \\' + NL,
     r'''#elif defined(__SWITCH__) && defined(HCU_NX_METER)
// hcu (switch/sdk-edits-base.py, CMake HCU_NX_METER): call counts, and the time in the waiting exports and VdSwap for
// the CPU-work meter (src/frame_meter.cpp).
#include <rex/nx_hook_counter.h>
#define REX_HOOK(subroutine, function)                           \
  static rex_nx::HookCounter rex_nx_hc_##subroutine{#subroutine}; \
  extern "C" REX_FUNC(subroutine) {                              \
    ++rex_nx_hc_##subroutine.n;                                  \
    if (!rex_nx_hc_##subroutine.kind) {                          \
      rex::ppc::HostToGuestFunction<function>(ctx, base);        \
      return;                                                    \
    }                                                            \
    const uint64_t rex_nx_t0 = rex_nx_meter_enter();             \
    rex::ppc::HostToGuestFunction<function>(ctx, base);          \
    rex_nx_meter_leave(rex_nx_t0, rex_nx_hc_##subroutine.kind,   \
                       &rex_nx_hc_##subroutine.main_wait);       \
  }
''' + '#else' + NL + '#define REX_HOOK(subroutine, function)                  \\' + NL +
     '  extern "C" REX_FUNC(subroutine) {                     \\' + NL +
     '    REX_EXPORT_CENSUS(#subroutine);                     \\' + NL),
])
# The command processor's waits (its idle wait for the write pointer, WAIT_REG_MEM sleeps) as one tick count.
edit('src/graphics/command_processor.cpp', [
    ('#include <rex/graphics/guest_wake.h>' + NL,
     '#include <rex/graphics/guest_wake.h>' + NL + r'''#if defined(__SWITCH__) && defined(HCU_NX_METER)
extern "C" volatile uint64_t rex_nx_cp_wait_ticks;  // hcu src/frame_meter.cpp (switch/sdk-edits-base.py)
#define HCU_CP_WAIT_T0(v) const uint64_t v = rex::chrono::Clock::QueryHostTickCount()
#define HCU_CP_WAIT_ADD(v) (rex_nx_cp_wait_ticks += rex::chrono::Clock::QueryHostTickCount() - (v))
#else
#define HCU_CP_WAIT_T0(v) ((void)0)
#define HCU_CP_WAIT_ADD(v) ((void)0)
#endif
'''),
    ('      PrepareForWait();' + NL + '      bool poll_background_work = PollBackgroundWork();' + NL,
     '      HCU_CP_WAIT_T0(hcu_idle0);' + NL +
     '      PrepareForWait();' + NL + '      bool poll_background_work = PollBackgroundWork();' + NL),
    ('      ReturnFromWait();' + NL + '      if (!worker_running_ || !pending_fns_.empty()) {' + NL,
     '      ReturnFromWait();' + NL + '      HCU_CP_WAIT_ADD(hcu_idle0);' + NL +
     '      if (!worker_running_ || !pending_fns_.empty()) {' + NL),
    ('        PrepareForWait();' + NL + '        if (unmatched_poll_count < kYieldPollCount',
     '        HCU_CP_WAIT_T0(hcu_wait0);' + NL + '        PrepareForWait();' + NL +
     '        if (unmatched_poll_count < kYieldPollCount'),
    ('        rex::thread::SyncMemory();' + NL + '        ReturnFromWait();' + NL,
     '        rex::thread::SyncMemory();' + NL + '        ReturnFromWait();' + NL + '        HCU_CP_WAIT_ADD(hcu_wait0);' + NL),
])
print('changed (hcu cpu meter):', *changed, sep=NL + '  ')

# --- Audio client threads (from gh-recomp, switch/audio_client_threads.inc): one host thread per guest audio client
# instead of the one worker that runs every client's callback in turn. settings.txt audio_threads=1 (src/settings_menu.cpp);
# 0, the default, keeps the base's worker. On HCU's old SDK the worker's WaitAny polled at 1 ms and spun at 75-80 % of
# a core (0.191); whether the base's does is the A/B. ponytail: the client threads do not set the base's
# dispatching_client_, so UnregisterClient does not wait for a callback still running on one (the base's fix of a
# start-up crash in Banjo); HCU registers its clients once.
_inc = open(os.path.join(HERE, 'audio_client_threads.inc'), encoding='utf-8').read()
edit('src/audio/audio_system.cpp', [
    ('#include <rex/audio/audio_system.h>' + NL + '#include <rex/audio/flags.h>' + NL,
     '#include <rex/audio/audio_system.h>' + NL + '#include <chrono>' + NL + '#include <cstdio>' + NL + '#include <memory>' + NL +
     '#include <string>' + NL + '#include <rex/audio/flags.h>' + NL),
    ('namespace rex::audio {' + NL,
     'namespace rex::audio {' + NL + '#if REX_PLATFORM_SWITCH  // hcu (switch/sdk-edits-base.py): the audio client threads' + NL +
     'static system::KernelState* hcu_kernel_state = nullptr;' + NL + 'static uint32_t hcu_client_gen[8];' + NL +
     'extern "C" volatile uint64_t rex_nx_audio_cb_us;  // hcu src/frame_meter.cpp' + NL +
     'extern "C" int rex_hcu_audio_threads(void);  // hcu src/settings_menu.cpp: settings.txt audio_threads' + NL + '#endif' + NL),
    ('X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {' + NL,
     'X_STATUS AudioSystem::Setup(system::KernelState* kernel_state) {' + NL +
     '#if REX_PLATFORM_SWITCH' + NL + '  hcu_kernel_state = kernel_state;' + NL + '#endif' + NL),
    ('  clients_[index] = {driver, callback, callback_arg, ptr, true};' + NL,
     '  clients_[index] = {driver, callback, callback_arg, ptr, true};' + NL +
     '#if REX_PLATFORM_SWITCH' + NL + '  if (rex_hcu_audio_threads())' + NL + '#endif' + NL + _inc),
    ('  DestroyDriver(clients_[index].driver);' + NL + '  memory()->SystemHeapFree(clients_[index].wrapped_callback_arg);' + NL +
     '  clients_[index] = {nullptr, 0, 0, 0, false};' + NL,
     '  DestroyDriver(clients_[index].driver);' + NL + '  memory()->SystemHeapFree(clients_[index].wrapped_callback_arg);' + NL +
     '  clients_[index] = {nullptr, 0, 0, 0, false};' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     '  ++hcu_client_gen[index];  // its client thread leaves' + NL + '#endif' + NL),
    ('  while (worker_running_) {' + NL + '    // These handles signify',
     '  while (worker_running_) {' + NL + r'''#if REX_PLATFORM_SWITCH
    // hcu: with the client threads the worker only waits for the shutdown (and the pause) event.
    if (rex_hcu_audio_threads()) {
      if (rex::thread::Wait(shutdown_event_.get(), true, std::chrono::milliseconds(500)) ==
              rex::thread::WaitResult::kSuccess &&
          paused_) {
        pause_fence_.Signal();
        thread::Wait(resume_event_.get(), false);
      }
      continue;
    }
#endif
''' + '    // These handles signify'),
])
print('changed (hcu audio client threads):', *changed, sep=NL + '  ')

# --- The button prompts on the Switch: the pad maps by position (Xbox A = the bottom button = Switch B), so a prompt
# must show the Switch label of the button in that place. button_glyphs=switch (src/button_glyphs.cpp): every
# successful HostPathFile::ReadSync hands its bytes to rex_hcu_glyph_read, which serves our Switch icon in place of
# Gui/Img/guidebutton_<NAME>.bin (same 5152-byte file). The base's ReadSync has four success paths (startup trace,
# read-ahead window, fill, plain read): the old body becomes ReadSyncBase and ReadSync wraps it.
# button_glyphs=xbox: the game's Xbox icons, A/B/X/Y.bin opened swapped (A<->B, X<->Y). The game opens them relative to
# the Gui\Img directory handle, so the swap is in OpenFile; the base's path parameter is const, so the swapped name is
# opened by a second OpenFile call.
edit('include/rex/filesystem/devices/host_path_file.h', [
    ('  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override;' + NL,
     '  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override;' + NL +
     '  // hcu (switch/sdk-edits-base.py): the read itself; ReadSync adds the button glyphs.' + NL +
     '  X_STATUS ReadSyncBase(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read);' + NL),
])
edit('src/filesystem/devices/host_path_file.cpp', [
    ('#include <rex/string.h>' + NL,
     '#include <rex/string.h>' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     'extern "C" void rex_hcu_glyph_read(const char* name, uint64_t offset, uint8_t* dst, size_t len);  // hcu src/button_glyphs.cpp' + NL +
     '#endif' + NL),
    ('X_STATUS HostPathFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,' + NL +
     '                                size_t* out_bytes_read) {' + NL,
     r'''X_STATUS HostPathFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) {
  const X_STATUS status = ReadSyncBase(buffer, byte_offset, out_bytes_read);
#if REX_PLATFORM_SWITCH
  if (status == X_STATUS_SUCCESS) rex_hcu_glyph_read(entry()->name().c_str(), byte_offset, buffer.data(), *out_bytes_read);
#endif
  return status;
}

X_STATUS HostPathFile::ReadSyncBase(std::span<uint8_t> buffer, size_t byte_offset,
                                    size_t* out_bytes_read) {
'''),
])
edit('src/filesystem/virtual_file_system.cpp', [
    ('#include <rex/string.h>' + NL,
     '#include <rex/string.h>' + NL + '#include <cctype>' + NL + '#include <cstdio>' + NL + '#include <cstring>' + NL +
     '#if REX_PLATFORM_SWITCH' + NL + 'extern "C" int rex_hcu_button_glyphs(void);  // hcu src/button_glyphs.cpp: 1 = Switch icons' + NL +
     '#endif' + NL),
    ("  // TODO(gibbed): should 'is_directory' remain as a bool or should it be" + NL,
     r'''#if REX_PLATFORM_SWITCH
  // hcu (switch/sdk-edits-base.py): Switch labels on the button prompts with the Xbox icons (button_glyphs=xbox).
  static thread_local bool hcu_swapping = false;
  if (!hcu_swapping && !rex_hcu_button_glyphs()) {
    static const char kSwap[] = "ABYX";  // A<->B, X<->Y
    std::string low(path);
    for (char& ch : low) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    const size_t at = low.find("guidebutton_");
    if (at != std::string::npos && at + 13 < low.size() && low[at + 13] == '.') {
      const char c = char(std::toupper(static_cast<unsigned char>(path[at + 12])));
      if (const char* k = c ? std::strchr(kSwap, c) : nullptr) {
        std::string nx_path(path);
        nx_path[at + 12] = kSwap[(k - kSwap) ^ 1];
        static int logged = 0;
        if (logged++ < 8) std::printf("[nx] vfs: prompt image %.*s -> %s\n", int(path.size()), path.data(), nx_path.c_str());
        hcu_swapping = true;
        const X_STATUS swapped = OpenFile(root_entry, nx_path, creation_disposition, desired_access, is_directory,
                                          is_non_directory, out_file, out_action);
        hcu_swapping = false;
        return swapped;
      }
    }
  }
#endif
''' + "  // TODO(gibbed): should 'is_directory' remain as a bool or should it be" + NL),
])
print('changed (hcu button glyphs):', *changed, sep=NL + '  ')

# --- RtlFillMemoryUlong marks its store cards (the deko3d backend reads them; the base's CRT hooks and file reads
# mark theirs, this export did not: a texture it filled was never uploaded again).
edit('src/kernel/xboxkrnl/xboxkrnl_rtl.cpp', [
    ('#include <rex/system/xthread.h>' + NL,
     '#include <rex/system/xthread.h>' + NL + '#include <rex/system/xmemory.h>' + NL),
    ('  for (uint32_t n = 0; n < count; n++, p++) {' + NL + '    *p = swapped_pattern;' + NL + '  }' + NL,
     '  for (uint32_t n = 0; n < count; n++, p++) {' + NL + '    *p = swapped_pattern;' + NL + '  }' + NL +
     '  rex::memory::MarkStoreCards(destination.guest_address(), count * 4);  // hcu: after the stores' + NL),
])
print('changed (hcu fill cards):', *changed, sep=NL + '  ')

# --- Own process handle without a loader (Eden). The base maps the guest views with svcMapProcessMemory on its own
# process. A console's loader passes the process handle; Eden passes none, so the base used the current-process
# pseudo-handle, and in Eden 0.348 every view stayed unmapped (878,667 "Unmapped Write" lines from the first guest
# write). Horizon's svcMapProcessMemory takes no pseudo-handle (hypothesis for Eden: the same rule). So without a
# loader handle a real one is taken once, as nx-hbloader's getOwnProcessHandle does: CUR_PROCESS_HANDLE sent to
# ourselves through a session as a copy handle. A console has the loader's handle and never runs it. The first
# failure of each map call goes to stdout, so an emulator run shows which call refused.
edit('src/core/guest_memory_switch.cpp', [
    ('''Handle Proc() {
  const Handle h = envGetOwnProcessHandle();
  return h != INVALID_HANDLE ? h : CUR_PROCESS_HANDLE;
}
''',
     r'''Handle g_hcu_own_handle = INVALID_HANDLE;  // hcu (switch/sdk-edits-base.py)

void HcuOwnHandleThread(void* arg) {
  const Handle server = static_cast<Handle>(reinterpret_cast<uintptr_t>(arg));
  void* tls = armGetTls();
  memset(tls, 0, 0x100);
  s32 index = 0;
  if (R_SUCCEEDED(svcReplyAndReceive(&index, &server, 1, INVALID_HANDLE, UINT64_MAX))) {
    const HipcParsedRequest r = hipcParseRequest(tls);
    if (r.meta.num_copy_handles == 1) g_hcu_own_handle = r.data.copy_handles[0];
  }
  svcCloseHandle(server);
}

Handle HcuOwnHandle() {
  printf("[gm] no loader: own process handle through a session (nx-hbloader's way)\n");
  fflush(stdout);
  Handle server = INVALID_HANDLE, client = INVALID_HANDLE;
  Result rc = svcCreateSession(&server, &client, 0, 0);
  if (R_SUCCEEDED(rc)) {
    Thread t;
    rc = threadCreate(&t, HcuOwnHandleThread, reinterpret_cast<void*>(static_cast<uintptr_t>(server)), nullptr, 0x4000,
                      0x20, -2);
    if (R_SUCCEEDED(rc)) {
      rc = threadStart(&t);
      if (R_SUCCEEDED(rc)) {
        HipcRequest req = hipcMakeRequestInline(armGetTls(), .type = CmifCommandType_Request, .num_copy_handles = 1);
        req.copy_handles[0] = CUR_PROCESS_HANDLE;
        svcSendSyncRequest(client);  // no reply: the server closes its end
        threadWaitForExit(&t);
      } else {
        svcCloseHandle(server);
      }
      threadClose(&t);
    } else {
      svcCloseHandle(server);
    }
    svcCloseHandle(client);
  }
  printf("[gm] own process handle 0x%X (rc 0x%X)\n", g_hcu_own_handle, rc);
  fflush(stdout);
  return g_hcu_own_handle;
}

Handle Proc() {
  const Handle h = envGetOwnProcessHandle();
  if (h != INVALID_HANDLE) return h;
  static const Handle own = HcuOwnHandle();  // hcu: first call from RexGmInit, before any lock is held
  return own != INVALID_HANDLE ? own : CUR_PROCESS_HANDLE;
}
'''),
    ('''uint8_t* RexGmInit(size_t size) {
    State& s = S();
''',
     '''uint8_t* RexGmInit(size_t size) {
    (void)Proc();  // hcu: the own handle before the mutex and virtmemLock (its thread needs virtmem)
    State& s = S();
'''),
    ('''        if (R_FAILED(rc)) {
            free(backing);
            s.last_result = rc;
''',
     r'''        if (R_FAILED(rc)) {
            static bool logged = false;  // hcu: the first refusal
            if (!logged) { logged = true; printf("[gm] code alias failed: 0x%X\n", rc); fflush(stdout); }
            free(backing);
            s.last_result = rc;
'''),
    ('''                                      reinterpret_cast<u64>(backing), sz);
            free(backing);
''',
     r'''                                      reinterpret_cast<u64>(backing), sz);
            static bool logged = false;  // hcu: the first refusal
            if (!logged) { logged = true; printf("[gm] alias permission failed: 0x%X\n", rc); fflush(stdout); }
            free(backing);
'''),
    ('''                if (Intersect(c.offset, c.length, s.views[vi].offset, s.views[vi].length, &lo, &hi))
                    MapChunkIntoView(s, vi, c);
''',
     r'''                if (Intersect(c.offset, c.length, s.views[vi].offset, s.views[vi].length, &lo, &hi) &&
                    !MapChunkIntoView(s, vi, c)) {
                    static bool logged = false;  // hcu: the first refusal
                    if (!logged) { logged = true; printf("[gm] view map failed: 0x%X (view %zu)\n", s.last_result, vi); fflush(stdout); }
                }
'''),
])
print('changed (hcu own process handle):', *changed, sep=NL + '  ')

# --- The game's own quit (EXIT GAME: XamLoaderLaunchTitle -> TerminateTitle -> StopTitleThreads). When a guest thread
# is still running after 2 s, std::_Exit ran __libnx_exit with __nx_applet_exit_mode 0: the NRO went back to hbloader
# with every other thread alive and hbloader aborted (2347-0024, console 0.400). The base's own exit with live threads
# (ReXApp::OnClosing) asks the system to end the process instead; rex_nx_quick_exit (switch/hcu_nx_base.cpp) does that
# after the log and the clock restore.
edit('src/system/xthread.cpp', [
    ('''  rex::FlushLogging();
  std::_Exit(EXIT_FAILURE);
}
''',
     '''  rex::FlushLogging();
#if REX_PLATFORM_SWITCH
  rex_nx_quick_exit("guest threads still running 2 s after the title ended");  // hcu (switch/sdk-edits-base.py)
#else
  std::_Exit(EXIT_FAILURE);
#endif
}
'''),
    ('#include <rex/vec128.h>' + NL,
     '#include <rex/vec128.h>' + NL + '#if REX_PLATFORM_SWITCH' + NL +
     'extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why);  // hcu switch/hcu_nx_base.cpp' + NL + '#endif' + NL),
])
print('changed (hcu title-end exit):', *changed, sep=NL + '  ')

# --- SFX / music balance (the mob death sounds were much louder than the music). FMOD in the guest mixes every volume;
# the host only sees the XMA streams: BGM_xenon.bnk is stereo XMA, the SE_* and Voice_* banks mono. Each decoded frame
# is scaled after the frame cache (the cache keeps the unscaled samples, so a menu change acts at the next frame): mono
# by sfx_gain, stereo by music_gain (src/settings_menu.cpp, rex_hcu_xma_gain: 0..1, attenuation only; 1 leaves the
# samples as they are). And the levels of the [audio] line (switch/hcu_nx_base.cpp, once a second): the XMA sources
# after the gain, and each render client after the 5.1 fold, before the sum and the clamp.
write('include/rex/hcu_audio_levels.h', r'''#pragma once
// hcu (switch/sdk-edits-base.py): the audio levels of the [audio] line (switch/hcu_nx_base.cpp, once a second).
// Slot 0 = mono XMA, 1 = stereo XMA (src/audio/xma_context.cpp, after the gain); 2..5 = render clients 0..3 after the
// 5.1 fold, before the sum and the clamp (src/audio/switch/switch_audio_system.cpp). Peak and the sum of squares in
// int16 units (32767 = full scale), n = samples. Relaxed atomics, no lock: a racing peak can be lost (a diagnostic).
#include <atomic>
#include <cstdint>
struct HcuAudioLevel {
  std::atomic<uint32_t> peak{0};
  std::atomic<uint64_t> sumsq{0};
  std::atomic<uint64_t> n{0};
  void Add(uint32_t p, uint64_t s, uint32_t count) {
    if (p > peak.load(std::memory_order_relaxed)) peak.store(p, std::memory_order_relaxed);
    sumsq.fetch_add(s, std::memory_order_relaxed);
    n.fetch_add(count, std::memory_order_relaxed);
  }
};
extern "C" HcuAudioLevel rex_hcu_audio_levels[6];
''')
edit('src/audio/xma_context.cpp', [
    ('#include <rex/filesystem.h>' + NL,
     '#include <rex/filesystem.h>' + NL + r'''#if REX_PLATFORM_SWITCH
// hcu (switch/sdk-edits-base.py): the SFX / music balance and the source levels of the [audio] line.
#include <rex/hcu_audio_levels.h>
extern "C" float rex_hcu_xma_gain(int stereo);  // hcu src/settings_menu.cpp: sfx_gain (mono), music_gain (stereo), 0..1
HcuAudioLevel rex_hcu_audio_levels[6];
// A decoded frame's big-endian int16 samples times the gain (Q15; 1 leaves them as they are), then its level.
static void HcuGainAndLevel(uint8_t* be, size_t samples, bool stereo) {
  const float gain = rex_hcu_xma_gain(stereo);
  const int32_t g = gain >= 1.f ? 32768 : gain > 0.f ? int32_t(gain * 32768.f) : 0;
  auto* p = reinterpret_cast<uint16_t*>(be);
  uint32_t peak = 0;
  uint64_t sumsq = 0;
  for (size_t i = 0; i < samples; ++i) {
    int32_t v = int16_t(__builtin_bswap16(p[i]));
    if (g != 32768) {
      v = (v * g) >> 15;
      p[i] = __builtin_bswap16(uint16_t(int16_t(v)));
    }
    const uint32_t a = uint32_t(v < 0 ? -v : v);
    peak = a > peak ? a : peak;
    sumsq += uint64_t(a) * a;
  }
  rex_hcu_audio_levels[stereo ? 1 : 0].Add(peak, sumsq, uint32_t(samples));
}
#endif
'''),
    ('''              size_t(kBytesPerFrameChannel) << data->is_stereo);
''',
     '''              size_t(kBytesPerFrameChannel) << data->is_stereo);
#if REX_PLATFORM_SWITCH
    // hcu: after the frame cache and the dump, before the guest reads it.
    HcuGainAndLevel(raw_frame_.data(), (size_t(kBytesPerFrameChannel) << data->is_stereo) / 2, bool(data->is_stereo));
#endif
'''),
])
edit('src/audio/switch/switch_audio_system.cpp', [
    ('extern "C" void RexSwitchPerfMax(unsigned id, u64 value);' + NL,
     'extern "C" void RexSwitchPerfMax(unsigned id, u64 value);' + NL + NL +
     '// hcu (switch/sdk-edits-base.py): the render clients\' levels of the [audio] line. Only the output thread mixes:' + NL +
     '// FillAndAppend sets the ordinal of the client MixFrameInto mixes.' + NL +
     '#include <rex/hcu_audio_levels.h>' + NL + 'static uint32_t hcu_mix_client = 0;' + NL),
    ('''  for (size_t i = 0; i < folded.size(); ++i) {
    stereo_out[i] += folded[i];
  }
''',
     '''  float hcu_peak = 0.f, hcu_sumsq = 0.f;  // hcu: this client's level (NaN and inf left out)
  for (size_t i = 0; i < folded.size(); ++i) {
    stereo_out[i] += folded[i];
    const float a = std::fabs(folded[i]);
    if (a <= 64.f) hcu_peak = std::max(hcu_peak, a), hcu_sumsq += a * a;
  }
  if (hcu_mix_client < 4)
    rex_hcu_audio_levels[2 + hcu_mix_client].Add(uint32_t(hcu_peak * 32767.f),
                                                 uint64_t(double(hcu_sumsq) * (32767.0 * 32767.0)),
                                                 uint32_t(folded.size()));
'''),
    ('''      for (SwitchAudioDriver* driver : drivers_) {
        // A client with nothing queued contributes silence and gets no
''',
     '''      hcu_mix_client = 0;  // hcu: the ordinal of the client MixFrameInto mixes
      for (SwitchAudioDriver* driver : drivers_) {
        // A client with nothing queued contributes silence and gets no
'''),
    ('''          ++frames_missing;
        }
''',
     '''          ++frames_missing;
        }
        ++hcu_mix_client;
'''),
])
print('changed (hcu audio balance):', *changed, sep=NL + '  ')
