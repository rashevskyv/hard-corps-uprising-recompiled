#include <bit>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

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

// DEBUG_SAULO: temporary canvas trace.
void LogCanvas(const char* tag, const uint8_t* base, uint32_t obj) {
  const uint32_t device = LoadBE32(base, obj + 56);
  REXLOG_INFO(
      "[DEBUG_SAULO][Aspect] {} obj={:08X} win=({},{}) canvas_w={:.4f} canvas_h={:.4f} factor={:.5f} "
      "dev={:08X} dev13756={} dev13760={} vp96={} vp100={} vp104={} vp108={}",
      tag, obj, LoadBE32(base, obj + 124), LoadBE32(base, obj + 128), LoadF(base, obj + 132),
      LoadF(base, obj + 136), LoadF(base, obj + 140), device, LoadBE32(base, device + 13756),
      LoadBE32(base, device + 13760), LoadBE32(base, obj + 96), LoadBE32(base, obj + 100),
      LoadBE32(base, obj + 104), LoadBE32(base, obj + 108));
}

}  // namespace

void ApplyAspectRatio() {
  const AspectRatio aspect = RenderAspect();
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(VideoModeWidth(aspect)));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(kVideoModeHeight));
  rex::cvar::SetFlagByName("present_letterbox", "true");
  REXLOG_INFO("[DEBUG_SAULO][Aspect] apply {} video {}x{} guest {}x{}", InfoFor(aspect).key,
              VideoModeWidth(aspect), kVideoModeHeight, GuestWidth(aspect), kGuestHeight);
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
  const uint32_t obj = ctx.r3.u32;
  const uint32_t mode = ctx.r4.u32;
  const hcu::AspectRatio aspect = hcu::RenderAspect();
  if (aspect != hcu::AspectRatio::k16x9) {
    hcu::StoreBE32(base, mode + 56, uint32_t(hcu::GuestWidth(aspect)));
    hcu::StoreBE32(base, mode + 60, uint32_t(hcu::kGuestHeight));
  }
  __imp__sub_8282FC80(ctx, base);
  hcu::LogCanvas("FC80 after", base, obj);
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
    hcu::StoreF(base, obj + 140, hcu::CanvasFactor(aspect, width, height));
  }
  hcu::LogCanvas("E450", base, obj);
}

REX_EXTERN(__imp__sub_8282E9A8);
REX_HOOK_RAW(sub_8282E9A8) {
  const uint32_t obj = ctx.r3.u32;
  __imp__sub_8282E9A8(ctx, base);
  hcu::LogCanvas("E9A8 after", base, obj);
}
