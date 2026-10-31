#include <cstdlib>

#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/window_nx.h>

extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why);  // switch/host/switch_nx.cpp

namespace rex::ui {

NXWindowedAppContext::NXWindowedAppContext() { ueventCreate(&wake_event_, true); }

NXWindowedAppContext::~NXWindowedAppContext() {}

bool NXWindowedAppContext::Initialize() { return true; }

void NXWindowedAppContext::NotifyUILoopOfPendingFunctions() { ueventSignal(&wake_event_); }

void NXWindowedAppContext::PlatformQuitFromUIThread() { ueventSignal(&wake_event_); }

int NXWindowedAppContext::RunMainMessageLoop() {
  while (!HasQuitFromUIThread()) {
    if (!appletMainLoop()) rex_nx_quick_exit("the system asked the application to exit (HOME -> Close)");
    // The game's own quit (XamLoaderTerminateTitle / LaunchTitle to the dashboard): the title stops, the program ends.
    if (auto* ks = REX_KERNEL_STATE(); ks && ks->is_terminating_title()) rex_nx_quick_exit("the game ended the title");
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
