#include "window_host.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>
#include <rex/ui/windowed_app_context.h>

#include "settings.h"

namespace {

constexpr double kSampleSeconds = 1.0;
// Above the app's own input listener (z-order 0), so F11 never reaches the game.
constexpr size_t kWindowHostZOrder = 1000;

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_active{false};
rex::ui::Window* g_window = nullptr;
rex::ui::WindowedAppContext* g_app_context = nullptr;

std::mutex g_sample_mutex;
std::optional<Clock::time_point> g_sample_start;
uint64_t g_sample_frames = 0;

void ShowFps(double fps) {
  if (g_active.load(std::memory_order_relaxed)) {
    g_window->SetTitle(REXCVAR_GET(hcu_show_fps) ? fmt::format("{} | FPS: {:.2f}", kWindowTitle, fps)
                                                 : std::string(kWindowTitle));
  }
}

void CountGuestFrame() {
  if (!g_active.load(std::memory_order_relaxed)) {
    return;
  }
  std::lock_guard lock(g_sample_mutex);
  const auto now = Clock::now();
  if (!g_sample_start) {
    g_sample_start = now;
    return;
  }
  ++g_sample_frames;
  const double elapsed = std::chrono::duration<double>(now - *g_sample_start).count();
  if (elapsed < kSampleSeconds) {
    return;
  }
  const double fps = g_sample_frames / elapsed;
  g_sample_start = now;
  g_sample_frames = 0;
  g_app_context->CallInUIThread([fps] { ShowFps(fps); });
}

class WindowHost final : public rex::ui::WindowListener, public rex::ui::WindowInputListener {
 public:
  void OnClosing(rex::ui::UIEvent&) override { g_active.store(false, std::memory_order_relaxed); }

  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.virtual_key() != rex::ui::VirtualKey::kF11 || e.target() != g_window) {
      return;
    }
    e.set_handled(true);
    if (e.prev_state()) {
      return;
    }
    // SetFullscreen(true) is SDL's default fullscreen mode, which is borderless desktop.
    const bool fullscreen = !g_window->IsFullscreen();
    g_window->SetFullscreen(fullscreen);
    REXLOG_INFO("Window mode: {}", fullscreen ? "borderless fullscreen" : "windowed");
  }
};

WindowHost window_host;

}  // namespace

void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context) {
  g_window = window;
  g_app_context = &app_context;
  g_active.store(true, std::memory_order_relaxed);
  window->SetTitle(kWindowTitle);
  window->AddListener(&window_host);
  window->AddInputListener(&window_host, kWindowHostZOrder);
}

REX_EXTERN(__imp__sub_825520F8);

// Swap routine. VdSwap is gated by obj+21748, so calls can exceed presented frames.
REX_HOOK_RAW(sub_825520F8) {
  __imp__sub_825520F8(ctx, base);
  CountGuestFrame();
}
