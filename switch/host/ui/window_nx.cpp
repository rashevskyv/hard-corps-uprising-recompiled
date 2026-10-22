#include <rex/ui/window_nx.h>

#include <rex/logging.h>
#include <rex/ui/surface_nx.h>

namespace rex::ui {

std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context, const std::string_view title,
                                       uint32_t desired_logical_width, uint32_t desired_logical_height) {
  return std::make_unique<WindowNX>(app_context, title, desired_logical_width, desired_logical_height);
}

WindowNX::WindowNX(WindowedAppContext& app_context, const std::string_view title,
                   uint32_t desired_logical_width, uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width, desired_logical_height) {}

WindowNX::~WindowNX() {
  EnterDestructor();
  nx_app_context().UnregisterWindow(this);
}

bool WindowNX::OpenImpl() {
  nx_app_context().RegisterWindow(this);
  u32 width = 0, height = 0;
  if (R_FAILED(nwindowGetDimensions(nwindowGetDefault(), &width, &height)) || !width || !height) {
    width = 1280;
    height = 720;
  }
  REXLOG_INFO("[nx] window {}x{} on the default nwindow", width, height);
  WindowDestructionReceiver destruction_receiver(this);
  OnActualSizeUpdate(width, height, destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return true;
  }
  OnFocusUpdate(true, destruction_receiver);
  return true;
}

void WindowNX::RequestCloseImpl() { PerformClose(); }

void WindowNX::PerformClose() {
  WindowDestructionReceiver destruction_receiver(this);
  OnBeforeClose(destruction_receiver);
  if (destruction_receiver.IsWindowDestroyed()) {
    return;
  }
  nx_app_context().UnregisterWindow(this);
  OnAfterClose();
}

std::unique_ptr<Surface> WindowNX::CreateSurfaceImpl(Surface::TypeFlags allowed_types) {
  REXLOG_INFO("[nx] creating the VI surface (allowed types 0x{:X})", allowed_types);
  if (allowed_types & Surface::kTypeFlag_NnVi) {
    return std::make_unique<NnViSurface>();
  }
  REXLOG_ERROR("[nx] the graphics provider does not accept a VI surface");
  return nullptr;
}

void WindowNX::RequestPaintImpl() {
  if (paint_pending_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  nx_app_context().NotifyUILoopOfPendingFunctions();
}

void WindowNX::HandlePaintEvent() {
  paint_pending_.store(false, std::memory_order_release);
  OnPaint();
}

}  // namespace rex::ui
