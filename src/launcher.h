#pragma once

#include <filesystem>
#include <functional>

struct ImFontAtlas;

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace hcu {

class GameSettings;

struct LauncherPaths {
  std::filesystem::path game_dir;
  std::filesystem::path user_dir;
  std::filesystem::path settings_file;
};

struct LauncherCallbacks {
  std::function<void()> play;               // starts the game in this window
  std::function<bool()> window_changed;     // the saved window size, monitor or VSync needs a new window
  std::function<bool()> restart;            // starts a new process with the saved settings; false if it could not
  std::function<void()> quit;
};

// `density` is the window's display scale, so text is rasterized for its real pixel size.
void LoadLauncherFonts(ImFontAtlas* atlas, float density);

// Shown when the game files are not ready, when Shift is held, or unless the
// player turned it off. Not shown on the relaunch that follows a window change.
bool ShouldShowLauncher(const std::filesystem::path& game_dir);

// Call before the window is created when the launcher is shown: the window
// is Windowed, at the launcher's size, on the primary display.
void PrepareLauncherWindow();

// Creates the launcher dialog; it deletes itself when closed.
void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks);

}  // namespace hcu
