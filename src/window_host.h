#pragma once

#include <cstdint>

inline constexpr char kWindowTitle[] = "Hard Corps: Uprising";

// The latest one-second sample of the game's swaps; all zero until the first one is measured.
struct GuestFrameStats {
  double fps = 0;
  double frame_time_ms = 0;
  uint64_t frame_count = 0;
};

namespace rex::ui {
class Window;
class WindowedAppContext;
}  // namespace rex::ui

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context);
void DetachWindowHost();
GuestFrameStats GetGuestFrameStats();
