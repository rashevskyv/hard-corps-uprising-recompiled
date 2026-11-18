// Switch entry: the plain ReXApp, without the PC launcher, window host, cheats and offline Live
// services (src/hard_corps_uprising_app.h). The cvars of config/hard_corps_uprising.toml that the
// game needs come from switch/host/ui/windowed_app_main_nx.cpp (old SDK) or switch/hcu_nx_base.cpp
// (the ultimate base's SDK).
#include "generated/default/hard_corps_uprising_init.h"

#include <rex/cvar.h>
#include <rex/rex_app.h>
#if defined(HCU_BASE_SDK)
#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/nx/nx_input_driver.h>
#include <rex/runtime.h>

#include "hcu_pgo.h"
#else
REXCVAR_DECLARE(int32_t, audio_maxqframes);
#endif

class HcuNxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<HcuNxApp>(new HcuNxApp(ctx, "hard_corps_uprising", PPCImageConfig));
  }

 protected:
#if defined(HCU_BASE_SDK)
  // Our pad driver (switch/host/input/nx_input_driver.cpp: the settings menu, the controller applet, pad.txt, rumble
  // by settings.txt) in place of the base's: players 1 and 2, then the base's no-op pads.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.input_factory = [](bool tool_mode) {
      auto input = std::make_unique<rex::input::InputSystem>(nullptr);
      if (!tool_mode) {
        auto pads = std::make_unique<rex::input::nx::NXInputDriver>(nullptr, 0);
        if (XSUCCEEDED(pads->Setup())) input->AddDriver(std::move(pads));
      }
      input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, tool_mode ? 0 : 1));
      input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
      return input;
    };
  }
  // The PGO-instrumented build (HCU_PGO=generate, switch/hcu_pgo.cpp) saves its counts every 2 minutes, on focus loss
  // and at the end; in every other build these do nothing.
  void OnPostSetup() override { hcu::pgo::Start(); }
  void OnWindowFocusChanged(bool focused) override {
    if (!focused) hcu::pgo::DumpNow();
  }
  void OnShutdown() override { hcu::pgo::Stop(); }
#else
  // A short audio queue for the audout driver (src/nx_audout_driver.h, from gh-recomp): 6 frames x 5.33 ms.
  void OnConfigurePaths(rex::PathConfig&) override { REXCVAR_SET(audio_maxqframes, 6); }
#endif
};

REX_DEFINE_APP(hard_corps_uprising, HcuNxApp::Create)
