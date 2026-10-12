#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiIO;

namespace hcu {

// TRG Launcher takes UTF-8 paths.
std::string Utf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& utf8);

// The folder the player sees the port in: the executable's, or the AppImage's.
std::filesystem::path PortFolder();

// The next free <name>_NNN.log in `folder`, which is created. Empty when it cannot be created.
std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name);

struct UiFontFiles {
  std::filesystem::path regular, semibold, bold;
};
UiFontFiles FindUiFonts();

// Gives the window the application icon. Windows takes it from the executable's resources already; on Linux SDL
// does not, so the AppImage's icon is loaded.
void ApplyWindowIcon();

// Names the application to the Linux desktop: SDL uses it as the Wayland app_id and the X11 WM_CLASS class. It has to
// be set before the window is created. A no-op on Windows.
void SetApplicationId();

// True while Shift is held. On Linux SDL only knows the keyboard once a window has focus.
bool ShiftHeld();

struct MonitorInfo {
  int left = 0, top = 0;                // physical pixels
  int width = 0, height = 0;
  int work_width = 0, work_height = 0;  // without the taskbar
  float scale = 1.0f;                   // display scaling
  bool primary = false;
};

// In the order ReXGlue's `monitor` setting numbers them: SDL's display order.
std::vector<MonitorInfo> ListMonitors();

// The display a `monitor` setting names. 0 (Default) and 1 are the primary display.
MonitorInfo MonitorForSetting(int monitor);

// Logs when the window did not open on the display the monitor setting names.
void CheckWindowDisplay(void* native_window, int monitor);

// Starts a new instance with this run's command line plus `extra_args`.
bool RelaunchSelf(std::wstring_view extra_args);

// Feeds an Xbox controller to Dear ImGui while the launcher or the in-game cheat menu is open. The runtime's
// own input system only starts with the game, and the menu keeps the game from reading it.
class LauncherGamepad {
 public:
  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  int pad_ = -1;
  double next_scan_ = 0.0;
#if !defined(_WIN32)
  bool subsystem_ = false;
#endif
};

}  // namespace hcu
