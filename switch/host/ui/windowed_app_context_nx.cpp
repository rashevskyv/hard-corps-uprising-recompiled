#include <cstdlib>

#include <rex/logging.h>
#include <rex/ui/window_nx.h>

namespace rex::ui {

NXWindowedAppContext::NXWindowedAppContext() { ueventCreate(&wake_event_, true); }

NXWindowedAppContext::~NXWindowedAppContext() {}

bool NXWindowedAppContext::Initialize() { return true; }

void NXWindowedAppContext::NotifyUILoopOfPendingFunctions() { ueventSignal(&wake_event_); }

void NXWindowedAppContext::PlatformQuitFromUIThread() { ueventSignal(&wake_event_); }

int NXWindowedAppContext::RunMainMessageLoop() {
  while (!HasQuitFromUIThread()) {
    if (!appletMainLoop()) {
      REXLOG_INFO("[nx] applet asked to exit");
      QuitFromUIThread();
      break;
    }
    // Wake on a request (paint, deferred function, quit) or every 16 ms for the applet pump.
    waitSingle(waiterForUEvent(&wake_event_), 16'000'000ULL);
    ExecutePendingFunctionsFromUIThread();
    if (HasQuitFromUIThread()) {
      break;
    }
    if (window_ && window_->TakePaintPending()) {
      window_->HandlePaintEvent();
    }
  }
  return EXIT_SUCCESS;
}

}  // namespace rex::ui
