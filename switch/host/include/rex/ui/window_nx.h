// rex::ui::Window and WindowedAppContext for Horizon (no SDL): one full-screen window on the VI
// layer, a UI loop driven by appletMainLoop, and paint requests from the presenter.
#pragma once

#include <atomic>
#include <memory>
#include <string_view>

#include <switch.h>

#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace rex::ui {

class WindowNX;

class NXWindowedAppContext final : public WindowedAppContext {
 public:
  NXWindowedAppContext();
  ~NXWindowedAppContext() override;

  bool Initialize();
  int RunMainMessageLoop();

  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

  void RegisterWindow(WindowNX* window) { window_ = window; }
  void UnregisterWindow(WindowNX* window) {
    if (window_ == window) window_ = nullptr;
  }

 private:
  UEvent wake_event_;
  WindowNX* window_ = nullptr;
};

class WindowNX final : public Window {
 public:
  WindowNX(WindowedAppContext& app_context, std::string_view title, uint32_t desired_logical_width,
           uint32_t desired_logical_height);
  ~WindowNX() override;

  // Called by the UI loop when a paint was requested.
  void HandlePaintEvent();
  bool TakePaintPending() { return paint_pending_.exchange(false, std::memory_order_acq_rel); }

 protected:
  uint32_t GetLatestDpiImpl() const override { return GetMediumDpi(); }
  bool OpenImpl() override;
  void RequestCloseImpl() override;
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  NXWindowedAppContext& nx_app_context() const {
    return static_cast<NXWindowedAppContext&>(app_context());
  }
  void PerformClose();

  std::atomic<bool> paint_pending_{false};
};

}  // namespace rex::ui
