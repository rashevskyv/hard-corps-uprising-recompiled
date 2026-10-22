// The Switch presentation surface: VK_NN_vi_surface on libnx's default nwindow.
#pragma once

#include <switch.h>

#include <rex/ui/surface.h>

namespace rex::ui {

class NnViSurface final : public Surface {
 public:
  NnViSurface() = default;
  TypeIndex GetType() const override { return kTypeIndex_NnVi; }
  NWindow* window() const { return nwindowGetDefault(); }

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override {
    u32 width = 0, height = 0;
    if (R_FAILED(nwindowGetDimensions(nwindowGetDefault(), &width, &height)) || !width || !height) {
      width = 1280;
      height = 720;
    }
    width_out = width;
    height_out = height;
    return true;
  }
};

}  // namespace rex::ui
