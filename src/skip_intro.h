#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace hcu {

// Boot Director step that completes the boot with no presentation.
inline constexpr uint32_t kBootCompleteStep = 11;

// Set until the first boot ends; the startup presentation plays only while it is set.
inline bool SkipsBootIntro(bool skip_setting, uint8_t intro_flag) {
  return skip_setting && intro_flag != 0;
}

inline std::optional<uint32_t> BootCompleteIndex(const std::vector<uint32_t>& steps) {
  for (uint32_t i = 0; i < steps.size(); ++i) {
    if (steps[i] == kBootCompleteStep) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace hcu
