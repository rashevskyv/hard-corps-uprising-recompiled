#pragma once

#include <memory>

#include <rex/rex_app.h>

#include "debug_saulo_input.h"

class HardCorpsUprisingApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<HardCorpsUprisingApp>(
        new HardCorpsUprisingApp(ctx, "hard_corps_uprising", PPCImageConfig));
  }

  void OnPostSetup() override {
    DebugSauloAttachKeyboardLog(window());  // DEBUG_SAULO
  }
};
