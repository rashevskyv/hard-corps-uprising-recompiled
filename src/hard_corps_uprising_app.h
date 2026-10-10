#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/flags.h>

#include "launcher.h"
#include "platform.h"
#include "settings.h"
#include "window_host.h"

class HardCorpsUprisingApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<HardCorpsUprisingApp>(
        new HardCorpsUprisingApp(ctx, "hard_corps_uprising", PPCImageConfig));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override { shipped_config_ = paths.config_path; }

  void OnPostInitLogging() override { hcu::LoadPlayerSettings(hcu::PlayerSettingsPath(user_data_root())); }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    hcu::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    game_settings_.Capture(hcu::PlayerSettingsPath(user_data_root()), shipped_config_);
    show_launcher_ = hcu::ShouldShowLauncher(game_data_root());
    if (show_launcher_) {
      hcu::PrepareLauncherWindow();
    }
    game_settings_.MarkWindowCreated();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    game_settings_.CaptureLateRegistered();
    if (!show_launcher_) {
      return defaults;
    }
    if (!imgui_drawer()) {
      game_settings_.Apply();
      return defaults;
    }
    REXLOG_INFO("Launcher shown");
    hcu::LauncherCallbacks callbacks;
    callbacks.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        game_settings_.Apply();
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!hcu::RelaunchSelf(L"--hcu_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); });
      return true;
    };
    callbacks.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    hcu::ShowLauncher(imgui_drawer(),
                      {defaults.game_data_root, defaults.user_data_root,
                       hcu::PlayerSettingsPath(defaults.user_data_root)},
                      game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void OnPostSetup() override {
    AttachWindowHost(window(), app_context());
    if (window()) {
      hcu::CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
    }
  }

 private:
  hcu::GameSettings game_settings_;
  std::filesystem::path shipped_config_;
  bool show_launcher_ = false;
};
