#include <atomic>
#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_memory.h"

#if defined(_WIN32)

namespace {

// sub_82556728 waits for the GPU fence word of the device (block at device+10896) with
// begin (82564148) / poll (82564258) / end (82564178). Other callers of the helper wait on other
// words and keep the guest's own polling.
constexpr uint32_t kBeginSite = 0x825567B4;
constexpr uint32_t kPollSite = 0x825567C0;
constexpr uint32_t kDeviceFenceOffset = 10896;
constexpr uint32_t kDeviceFlagsOffset = 10941;
constexpr uint8_t kDeviceStoppedFlag = 2;
// Upper bound on one block, for fence writers that do not wake the waiter.
constexpr DWORD kBlockTimeoutMs = 2;
// Blocks that ended by timeout with the word already changed before blocking is given up.
constexpr uint32_t kMissedWakeLimit = 8;

enum SetupFailure : uint32_t {
  kNoDevice,
  kDeviceStopped,
  kBadFence,
  kNoMemory,
  kViewsDiffer,
  kFailureCount
};

struct WaitState {
  uint32_t object = 0;
  uint32_t device = 0;
  uint32_t fence_address = 0;
  const uint32_t* word = nullptr;
  const uint32_t* guest_word = nullptr;
  uint32_t seen = 0;
  // Set once the guest has re-evaluated its condition after `seen` was read.
  bool armed = false;
};
thread_local WaitState t_wait;

std::atomic<bool> g_blocking_enabled{true};
std::atomic<uint32_t> g_missed_wakes{0};

// DEBUG_SAULO
struct DebugSauloStats {
  std::atomic<uint64_t> begin_hits{0};
  std::atomic<uint64_t> begin_rejected{0};
  std::atomic<uint64_t> poll_hits{0};
  std::atomic<uint64_t> accepted{0};
  std::atomic<uint64_t> failures[kFailureCount]{};
  std::atomic<uint64_t> fallbacks{0};
  std::atomic<uint64_t> rearmed{0};
  std::atomic<uint64_t> recheck_avoided{0};
  std::atomic<uint64_t> blocks{0};
  std::atomic<uint64_t> woken{0};
  std::atomic<uint64_t> spurious{0};
  std::atomic<uint64_t> timeouts{0};
  std::atomic<uint64_t> missed{0};
  std::atomic<uint64_t> view_mismatches{0};
  std::atomic<uint64_t> blocked_ns{0};
  std::atomic<uint64_t> max_blocked_ns{0};
  std::atomic<int64_t> window_start_ns{0};
  std::atomic<uint32_t> caller_count{0};
  std::atomic<uint32_t> callers[8]{};
};
DebugSauloStats g_debug_saulo;

// The recompiled loads add 0x1000 for the 4 KB physical view, whose host pages sit 4 KB further on.
constexpr uint32_t HostOffset(uint32_t address) {
  return address >= 0xE0000000u ? 0x1000u : 0u;
}

inline uint32_t LoadGuest32(const uint8_t* base, uint32_t address) {
  return hcu::LoadBE32(base, address + HostOffset(address));
}

inline uint32_t LoadWord(const uint32_t* word) {
  return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

bool DeviceStopped(const uint8_t* base, uint32_t device) {
  const uint32_t address = device + kDeviceFlagsOffset;
  return hcu::LoadBE<uint8_t>(base, address + HostOffset(address)) & kDeviceStoppedFlag;
}

// Both views are the same pages; a difference that outlasts one re-read means the physical
// address is not the one the guest reads.
bool ViewsAgree(const uint32_t* physical_word, const uint32_t* guest_word) {
  const uint32_t physical = LoadWord(physical_word);
  return LoadWord(guest_word) == physical || LoadWord(physical_word) != physical;
}

// The command processor stores to and wakes physical_membase + (address & 0x1FFFFFFF), where address
// is the hardware form of the guest address that the guest emits into the packet.
bool ResolveFenceWord(const uint8_t* base, uint32_t device, WaitState& wait,
                      SetupFailure& failure) {
  if (!device) {
    failure = kNoDevice;
    return false;
  }
  if (DeviceStopped(base, device)) {
    failure = kDeviceStopped;
    return false;
  }
  const uint32_t fence = LoadGuest32(base, device + kDeviceFenceOffset);
  if (!fence || (fence & 3)) {
    failure = kBadFence;
    return false;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  rex::memory::Memory* memory = kernel ? kernel->memory() : nullptr;
  if (!memory) {
    failure = kNoMemory;
    return false;
  }
  const uint32_t physical = (fence & 0x1FFFFFFF) + ((((fence >> 20) & 0xFFF) + 512) & 0x1000);
  wait.word = memory->TranslatePhysical<const uint32_t*>(physical);
  wait.guest_word = reinterpret_cast<const uint32_t*>(base + fence + HostOffset(fence));
  wait.device = device;
  wait.fence_address = fence;
  if (!ViewsAgree(wait.word, wait.guest_word)) {
    failure = kViewsDiffer;
    return false;
  }
  // DEBUG_SAULO
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true)) {
    REXLOG_INFO(
        "[DEBUG_SAULO][CPU] GPU fence word: guest 0x{:08X}, physical 0x{:08X}, wait pointer {}, "
        "physical base {}",
        fence, physical & 0x1FFFFFFF, static_cast<const void*>(wait.word),
        static_cast<const void*>(memory->physical_membase()));
  }
  return true;
}

// DEBUG_SAULO
void DebugSauloNoteCaller(uint32_t lr, bool begin) {
  const uint32_t tagged = begin ? lr : lr ^ 1;
  const uint32_t count = g_debug_saulo.caller_count.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < count && i < 8; ++i) {
    if (g_debug_saulo.callers[i].load(std::memory_order_relaxed) == tagged) {
      return;
    }
  }
  const uint32_t slot = g_debug_saulo.caller_count.fetch_add(1, std::memory_order_relaxed);
  if (slot < 8) {
    g_debug_saulo.callers[slot].store(tagged, std::memory_order_relaxed);
    REXLOG_INFO("[DEBUG_SAULO][CPU] GPU fence {} hook caller #{}: LR=0x{:08X}",
                begin ? "begin" : "poll", slot + 1, lr);
  }
}

// DEBUG_SAULO
void DebugSauloReport() {
  constexpr int64_t kPeriodNs = 5'000'000'000;
  const int64_t now_ns = std::chrono::steady_clock::now().time_since_epoch().count();
  int64_t start = g_debug_saulo.window_start_ns.load(std::memory_order_relaxed);
  if (!start) {
    g_debug_saulo.window_start_ns.compare_exchange_strong(start, now_ns);
    return;
  }
  if (now_ns - start < kPeriodNs ||
      !g_debug_saulo.window_start_ns.compare_exchange_strong(start, now_ns)) {
    return;
  }
  const double seconds = double(now_ns - start) * 1e-9;
  auto per_second = [&](std::atomic<uint64_t>& counter) {
    return double(counter.exchange(0)) / seconds;
  };
  const uint64_t blocks = g_debug_saulo.blocks.exchange(0);
  const uint64_t blocked_ns = g_debug_saulo.blocked_ns.exchange(0);
  REXLOG_INFO(
      "[DEBUG_SAULO][CPU] GPU fence hooks active, per second: begin hits {:.0f} (rejected {:.0f}), "
      "poll hits {:.0f}, accepted {:.0f}, setup failures (no device {:.0f}, stopped {:.0f}, bad "
      "fence {:.0f}, no memory {:.0f}, views differ {:.0f}), {:.0f} fallback, {:.0f} rearmed, "
      "{:.0f} recheck-avoided",
      per_second(g_debug_saulo.begin_hits), per_second(g_debug_saulo.begin_rejected),
      per_second(g_debug_saulo.poll_hits), per_second(g_debug_saulo.accepted),
      per_second(g_debug_saulo.failures[kNoDevice]),
      per_second(g_debug_saulo.failures[kDeviceStopped]),
      per_second(g_debug_saulo.failures[kBadFence]), per_second(g_debug_saulo.failures[kNoMemory]),
      per_second(g_debug_saulo.failures[kViewsDiffer]), per_second(g_debug_saulo.fallbacks),
      per_second(g_debug_saulo.rearmed), per_second(g_debug_saulo.recheck_avoided));
  REXLOG_INFO(
      "[DEBUG_SAULO][CPU] GPU fence wait, per second: {:.0f} WaitOnAddress calls ({:.0f} woken, "
      "{:.0f} spurious, {:.0f} timeouts, {:.0f} missed wakes), {:.0f} view mismatches, {:.1f} ms "
      "blocked (avg {:.1f} us, max {:.1f} us), spin 0 ms, blocking {}",
      double(blocks) / seconds, per_second(g_debug_saulo.woken),
      per_second(g_debug_saulo.spurious), per_second(g_debug_saulo.timeouts),
      per_second(g_debug_saulo.missed), per_second(g_debug_saulo.view_mismatches),
      double(blocked_ns) * 1e-6 / seconds,
      blocks ? double(blocked_ns) * 1e-3 / double(blocks) : 0.0,
      double(g_debug_saulo.max_blocked_ns.exchange(0)) * 1e-3,
      g_blocking_enabled.load() ? "on" : "disabled");
}

// DEBUG_SAULO
void DebugSauloRecordBlock(int64_t blocked_ns, bool woken, bool timed_out, bool missed) {
  g_debug_saulo.blocks.fetch_add(1, std::memory_order_relaxed);
  g_debug_saulo.blocked_ns.fetch_add(blocked_ns, std::memory_order_relaxed);
  (woken ? g_debug_saulo.woken : timed_out ? g_debug_saulo.timeouts : g_debug_saulo.spurious)
      .fetch_add(1, std::memory_order_relaxed);
  if (missed) {
    g_debug_saulo.missed.fetch_add(1, std::memory_order_relaxed);
  }
  uint64_t previous_max = g_debug_saulo.max_blocked_ns.load(std::memory_order_relaxed);
  while (uint64_t(blocked_ns) > previous_max &&
         !g_debug_saulo.max_blocked_ns.compare_exchange_weak(previous_max, blocked_ns)) {
  }
}

// Blocks while the word still holds the value read before the guest last re-checked its condition,
// so a write after that read is never slept through.
void BlockUntilFenceWritten(WaitState& wait) {
  const auto begin = std::chrono::steady_clock::now();  // DEBUG_SAULO
  uint32_t undesired = wait.seen;
  const BOOL signaled = WaitOnAddress(const_cast<uint32_t*>(wait.word), &undesired,
                                      sizeof(undesired), kBlockTimeoutMs);
  const bool timed_out = !signaled && GetLastError() == ERROR_TIMEOUT;
  const uint32_t current = LoadWord(wait.word);
  const bool missed_wake = timed_out && current != wait.seen;
  if (!ViewsAgree(wait.word, wait.guest_word)) {
    g_blocking_enabled.store(false, std::memory_order_relaxed);
    g_debug_saulo.view_mismatches.fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
  }
  if (missed_wake) {
    if (g_missed_wakes.fetch_add(1, std::memory_order_relaxed) + 1 >= kMissedWakeLimit) {
      g_blocking_enabled.store(false, std::memory_order_relaxed);
    }
  } else if (signaled && current != wait.seen) {
    g_missed_wakes.store(0, std::memory_order_relaxed);
  }
  const auto end = std::chrono::steady_clock::now();  // DEBUG_SAULO
  // DEBUG_SAULO
  DebugSauloRecordBlock((end - begin).count(), signaled && current != wait.seen, timed_out,
                        missed_wake);
  wait.seen = current;
}

}  // namespace

REX_EXTERN(__imp__sub_82564148);
REX_HOOK_RAW(sub_82564148) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t device = ctx.r4.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_82564148(ctx, base);
  g_debug_saulo.begin_hits.fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
  DebugSauloNoteCaller(caller, true);                                  // DEBUG_SAULO
  WaitState& wait = t_wait;
  wait = {};
  if (caller != kBeginSite || !g_blocking_enabled.load(std::memory_order_relaxed)) {
    g_debug_saulo.begin_rejected.fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
    return;
  }
  SetupFailure failure = kNoDevice;
  if (ResolveFenceWord(base, device, wait, failure)) {
    wait.object = object;
    wait.seen = LoadWord(wait.word);
  } else {
    wait = {};
    g_debug_saulo.failures[failure].fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
  }
}

REX_EXTERN(__imp__sub_82564258);
REX_HOOK_RAW(sub_82564258) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_82564258(ctx, base);
  // DEBUG_SAULO
  const uint64_t hits = g_debug_saulo.poll_hits.fetch_add(1, std::memory_order_relaxed) + 1;
  DebugSauloNoteCaller(caller, false);
  if (!(hits & 0xFF)) {
    DebugSauloReport();
  }
  if (caller != kPollSite || !ctx.r3.u32) {
    return;
  }
  g_debug_saulo.accepted.fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
  WaitState& wait = t_wait;
  if (!wait.word || wait.object != object || !g_blocking_enabled.load(std::memory_order_relaxed) ||
      DeviceStopped(base, wait.device) ||
      LoadGuest32(base, wait.device + kDeviceFenceOffset) != wait.fence_address) {
    wait = {};
    g_debug_saulo.fallbacks.fetch_add(1, std::memory_order_relaxed);  // DEBUG_SAULO
    return;
  }
  const uint32_t current = LoadWord(wait.word);
  if (!wait.armed || current != wait.seen) {
    // DEBUG_SAULO
    (wait.armed ? g_debug_saulo.recheck_avoided : g_debug_saulo.rearmed)
        .fetch_add(1, std::memory_order_relaxed);
    wait.seen = current;
    wait.armed = true;
    return;
  }
  BlockUntilFenceWritten(wait);
}

#endif  // _WIN32
