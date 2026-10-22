// Switch entry: the plain ReXApp, without the PC launcher, window host, cheats and offline Live
// services (src/hard_corps_uprising_app.h). The cvars of config/hard_corps_uprising.toml that the
// game needs come from switch/host/ui/windowed_app_main_nx.cpp.
#include "generated/default/hard_corps_uprising_init.h"

#include <rex/rex_app.h>

class HcuNxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<HcuNxApp>(new HcuNxApp(ctx, "hard_corps_uprising", PPCImageConfig));
  }
};

REX_DEFINE_APP(hard_corps_uprising, HcuNxApp::Create)
