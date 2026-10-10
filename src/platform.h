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

std::filesystem::path SystemFontsFolder();

struct MonitorInfo {
  int left = 0, top = 0;                // physical pixels
  int width = 0, height = 0;
  int work_width = 0, work_height = 0;  // without the taskbar
  float scale = 1.0f;                   // Windows display scaling
  bool primary = false;
};

// The primary display first, then the others: the order ReXGlue's `monitor` setting numbers them.
std::vector<MonitorInfo> ListMonitors();

// The display a `monitor` setting names. 0 (Default) and 1 are the primary display.
MonitorInfo MonitorForSetting(int monitor);

// Logs when the window did not open on the display the monitor setting names.
void CheckWindowDisplay(void* native_window, int monitor);

// Starts a new instance with this run's command line plus `extra_args`.
bool RelaunchSelf(std::wstring_view extra_args);

// Feeds an Xbox controller to Dear ImGui while the launcher is open: the
// runtime's own input system only starts with the game.
class LauncherGamepad {
 public:
  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  int pad_ = -1;
  double next_scan_ = 0.0;
};

}  // namespace hcu
