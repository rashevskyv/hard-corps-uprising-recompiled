#include "skip_intro.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <rex/hook.h>

#include "guest_memory.h"
#include "settings.h"

namespace {

constexpr uint32_t kBootIntroFlag = 0x82C1EEE2;
constexpr uint32_t kBootStepListOffset = 28;
constexpr uint32_t kBootStepIndexOffset = 32;
constexpr uint32_t kMaxBootSteps = 64;

std::vector<uint32_t> BootSteps(const uint8_t* base, uint32_t list) {
  std::vector<uint32_t> steps;
  steps.reserve(kMaxBootSteps);
  for (uint32_t i = 0; i < kMaxBootSteps; ++i) {
    steps.push_back(hcu::LoadBE32(base, list + 4 * i));
  }
  return steps;
}

}  // namespace

// The director starts at its completion step, so no presentation item is entered.
REX_EXTERN(__imp__sub_824E52C8);
REX_HOOK_RAW(sub_824E52C8) {
  const uint32_t boot = ctx.r3.u32;
  __imp__sub_824E52C8(ctx, base);
  if (!hcu::SkipsBootIntro(REXCVAR_GET(hcu_skip_intro), base[kBootIntroFlag])) {
    return;
  }
  const uint32_t list = hcu::LoadBE32(base, boot + kBootStepListOffset);
  if (list == 0) {
    return;
  }
  const std::optional<uint32_t> step = hcu::BootCompleteIndex(BootSteps(base, list));
  if (step) {
    hcu::StoreBE32(base, boot + kBootStepIndexOffset, *step);
  }
}
