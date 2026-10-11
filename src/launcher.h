#pragma once

#include <filesystem>
#include <functional>

struct ImFont;
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

// The launcher's UI fonts, also used by the in-game cheat menu. Null when the system has none.
ImFont* LauncherRegularFont();
ImFont* LauncherSemiboldFont();

// Game files that are not ready always show the launcher: it is the only way to install them, so neither
// the player's "off" choice nor the relaunch flag can hide it.
inline bool LauncherWanted(bool game_ready, bool skip_once, bool shift_held, bool enabled) {
  if (!game_ready) {
    return true;
  }
  return !skip_once && (shift_held || enabled);
}

// Shown when the game files are not ready, when Shift is held, or unless the
// player turned it off. Not shown on the relaunch that follows a window change.
bool ShouldShowLauncher(const std::filesystem::path& game_dir);

// Linux: SDL sees Shift only once the window has focus, so it is checked again then.
// Always false on Windows, where ShouldShowLauncher already saw it.
bool ShiftHeldAfterWindowOpened();

// Call before the window is created when the launcher is shown: the window
// is Windowed, at the launcher's size, on the primary display.
void PrepareLauncherWindow();

// Creates the launcher dialog; it deletes itself when closed.
void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks);

}  // namespace hcu
