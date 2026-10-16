#include <bit>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "aspect_ratio.h"
#include "guest_memory.h"
#include "settings.h"

namespace hcu {
namespace {

AspectRatio RenderAspect() {
  return ParseAspectRatio(rex::cvar::GetFlagByName("hcu_aspect_ratio"));
}

float LoadF(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(LoadBE32(base, addr));
}

void StoreF(uint8_t* base, uint32_t addr, float value) {
  StoreBE32(base, addr, std::bit_cast<uint32_t>(value));
}

}  // namespace

void ApplyAspectRatio() {
  const AspectRatio aspect = RenderAspect();
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(VideoModeWidth(aspect)));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(kVideoModeHeight));
  rex::cvar::SetFlagByName("present_letterbox", "true");
}

}  // namespace hcu

// Display mode struct copied into the device: width at +0.
REX_EXTERN(__imp__sub_82564EF8);
REX_HOOK_RAW(sub_82564EF8) {
  const hcu::AspectRatio aspect = hcu::RenderAspect();
  if (aspect != hcu::AspectRatio::k16x9) {
    hcu::StoreBE32(base, ctx.r4.u32, uint32_t(hcu::GuestWidth(aspect)));
  }
  __imp__sub_82564EF8(ctx, base);
}

// Display mode argument read by sub_8282FC80: width at +56, height at +60. Sets the render surfaces.
REX_EXTERN(__imp__sub_8282FC80);
REX_HOOK_RAW(sub_8282FC80) {
  const uint32_t mode = ctx.r4.u32;
  const hcu::AspectRatio aspect = hcu::RenderAspect();
  if (aspect != hcu::AspectRatio::k16x9) {
    hcu::StoreBE32(base, mode + 56, uint32_t(hcu::GuestWidth(aspect)));
    hcu::StoreBE32(base, mode + 60, uint32_t(hcu::kGuestHeight));
  }
  __imp__sub_8282FC80(ctx, base);
}

// Canvas factor: target ratio over the device's width/height, as the original computes it.
REX_EXTERN(__imp__sub_8282E450);
REX_HOOK_RAW(sub_8282E450) {
  const uint32_t obj = ctx.r3.u32;
  __imp__sub_8282E450(ctx, base);
  const hcu::AspectRatio aspect = hcu::RenderAspect();
  if (aspect != hcu::AspectRatio::k16x9) {
    const float width = hcu::LoadF(base, obj + 132);
    const float height = hcu::LoadF(base, obj + 136);
    if (width > 0.0f && height > 0.0f) {
      hcu::StoreF(base, obj + 140, hcu::CanvasFactor(aspect, width, height));
    }
  }
}
