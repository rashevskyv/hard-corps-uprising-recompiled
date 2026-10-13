#include <atomic>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <rex/hook.h>
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
bool ResolveFenceWord(const uint8_t* base, uint32_t device, WaitState& wait) {
  if (!device || DeviceStopped(base, device)) {
    return false;
  }
  const uint32_t fence = LoadGuest32(base, device + kDeviceFenceOffset);
  if (!fence || (fence & 3)) {
    return false;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  rex::memory::Memory* memory = kernel ? kernel->memory() : nullptr;
  if (!memory) {
    return false;
  }
  const uint32_t physical = (fence & 0x1FFFFFFF) + ((((fence >> 20) & 0xFFF) + 512) & 0x1000);
  wait.word = memory->TranslatePhysical<const uint32_t*>(physical);
  wait.guest_word = reinterpret_cast<const uint32_t*>(base + fence + HostOffset(fence));
  wait.device = device;
  wait.fence_address = fence;
  return ViewsAgree(wait.word, wait.guest_word);
}

// Blocks while the word still holds the value read before the guest last re-checked its condition,
// so a write after that read is never slept through.
void BlockUntilFenceWritten(WaitState& wait) {
  uint32_t undesired = wait.seen;
  const BOOL signaled = WaitOnAddress(const_cast<uint32_t*>(wait.word), &undesired,
                                      sizeof(undesired), kBlockTimeoutMs);
  const bool timed_out = !signaled && GetLastError() == ERROR_TIMEOUT;
  const uint32_t current = LoadWord(wait.word);
  const bool missed_wake = timed_out && current != wait.seen;
  if (!ViewsAgree(wait.word, wait.guest_word)) {
    g_blocking_enabled.store(false, std::memory_order_relaxed);
  }
  if (missed_wake) {
    if (g_missed_wakes.fetch_add(1, std::memory_order_relaxed) + 1 >= kMissedWakeLimit) {
      g_blocking_enabled.store(false, std::memory_order_relaxed);
    }
  } else if (signaled && current != wait.seen) {
    g_missed_wakes.store(0, std::memory_order_relaxed);
  }
  wait.seen = current;
}

}  // namespace

REX_EXTERN(__imp__sub_82564148);
REX_HOOK_RAW(sub_82564148) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t device = ctx.r4.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_82564148(ctx, base);
  WaitState& wait = t_wait;
  wait = {};
  if (caller != kBeginSite || !g_blocking_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  if (ResolveFenceWord(base, device, wait)) {
    wait.object = object;
    wait.seen = LoadWord(wait.word);
  } else {
    wait = {};
  }
}

REX_EXTERN(__imp__sub_82564258);
REX_HOOK_RAW(sub_82564258) {
  const uint32_t object = ctx.r3.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_82564258(ctx, base);
  if (caller != kPollSite || !ctx.r3.u32) {
    return;
  }
  WaitState& wait = t_wait;
  if (!wait.word || wait.object != object || !g_blocking_enabled.load(std::memory_order_relaxed) ||
      DeviceStopped(base, wait.device) ||
      LoadGuest32(base, wait.device + kDeviceFenceOffset) != wait.fence_address) {
    wait = {};
    return;
  }
  const uint32_t current = LoadWord(wait.word);
  if (!wait.armed || current != wait.seen) {
    wait.seen = current;
    wait.armed = true;
    return;
  }
  BlockUntilFenceWritten(wait);
}

#endif  // _WIN32
