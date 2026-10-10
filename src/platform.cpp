#include "platform.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include <windows.h>
#include <shellscalingapi.h>
#include <xinput.h>

#include <imgui.h>

#include <rex/logging.h>

namespace hcu {
namespace {

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }
  MonitorInfo found;
  found.left = info.rcMonitor.left;
  found.top = info.rcMonitor.top;
  found.width = info.rcMonitor.right - info.rcMonitor.left;
  found.height = info.rcMonitor.bottom - info.rcMonitor.top;
  found.work_width = info.rcWork.right - info.rcWork.left;
  found.work_height = info.rcWork.bottom - info.rcWork.top;
  found.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  UINT dpi_x = 0;
  UINT dpi_y = 0;
  if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) && dpi_x) {
    found.scale = float(dpi_x) / 96.0f;
  }
  reinterpret_cast<std::vector<MonitorInfo>*>(user)->push_back(found);
  return TRUE;
}

using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

XInputGetStateFn LoadXInput() {
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    if (HMODULE dll = LoadLibraryW(name)) {
      if (FARPROC proc = GetProcAddress(dll, "XInputGetState")) {
        return reinterpret_cast<XInputGetStateFn>(reinterpret_cast<void*>(proc));
      }
    }
  }
  return nullptr;
}

DWORD GetPadState(DWORD pad, XINPUT_STATE* state) {
  static const XInputGetStateFn get_state = LoadXInput();
  return get_state ? get_state(pad, state) : ERROR_DEVICE_NOT_CONNECTED;
}

struct ButtonKey {
  ImGuiKey key;
  WORD mask;
};
constexpr ButtonKey kButtonKeys[] = {
    {ImGuiKey_GamepadStart, XINPUT_GAMEPAD_START},
    {ImGuiKey_GamepadBack, XINPUT_GAMEPAD_BACK},
    {ImGuiKey_GamepadFaceDown, XINPUT_GAMEPAD_A},
    {ImGuiKey_GamepadFaceRight, XINPUT_GAMEPAD_B},
    {ImGuiKey_GamepadFaceLeft, XINPUT_GAMEPAD_X},
    {ImGuiKey_GamepadFaceUp, XINPUT_GAMEPAD_Y},
    {ImGuiKey_GamepadDpadLeft, XINPUT_GAMEPAD_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, XINPUT_GAMEPAD_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, XINPUT_GAMEPAD_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, XINPUT_GAMEPAD_DPAD_DOWN},
};

struct StickKey {
  ImGuiKey key;
  bool horizontal;
  int dead_zone;
  int full;
};
constexpr StickKey kStickKeys[] = {
    {ImGuiKey_GamepadLStickLeft, true, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768},
    {ImGuiKey_GamepadLStickRight, true, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767},
    {ImGuiKey_GamepadLStickUp, false, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, 32767},
    {ImGuiKey_GamepadLStickDown, false, -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE, -32768},
};

}  // namespace

std::string Utf8(const std::filesystem::path& path) {
  const std::u8string s = path.u8string();
  return std::string(s.begin(), s.end());
}

std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::filesystem::path SystemFontsFolder() {
  wchar_t windows[MAX_PATH];
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  return std::filesystem::path(length ? std::wstring(windows, length) : std::wstring(L"C:\\Windows")) / L"Fonts";
}

// SDL builds its display list the same way: the primary display, then EnumDisplayMonitors order.
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

MonitorInfo MonitorForSetting(int monitor) {
  const std::vector<MonitorInfo> monitors = ListMonitors();
  if (monitors.empty()) {
    return {};
  }
  if (monitor <= 1 || monitor > int(monitors.size())) {
    return monitors.front();
  }
  return monitors[size_t(monitor - 1)];
}

void CheckWindowDisplay(void* native_window, int monitor) {
  const HWND hwnd = static_cast<HWND>(native_window);
  const MonitorInfo expected = MonitorForSetting(monitor);
  if (!hwnd || expected.width == 0) {
    return;
  }
  MONITORINFO actual{sizeof(actual)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &actual)) {
    return;
  }
  if (actual.rcMonitor.left != expected.left || actual.rcMonitor.top != expected.top) {
    REXLOG_WARN("Window opened on the display at ({}, {}), but monitor setting {} names ({}, {})",
                actual.rcMonitor.left, actual.rcMonitor.top, monitor, expected.left, expected.top);
  }
}

bool RelaunchSelf(std::wstring_view extra_args) {
  std::wstring command_line = GetCommandLineW();
  command_line += L' ';
  command_line += extra_args;
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                      &process)) {
    REXLOG_ERROR("Relaunch failed ({})", GetLastError());
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

void LauncherGamepad::Update(ImGuiIO& io) {
  XINPUT_STATE state{};
  if (pad_ < 0 || GetPadState(DWORD(pad_), &state) != ERROR_SUCCESS) {
    pad_ = -1;
    // Polling an empty slot is slow, so look for a controller once a second.
    if (ImGui::GetTime() < next_scan_) {
      return;
    }
    next_scan_ = ImGui::GetTime() + 1.0;
    for (DWORD i = 0; i < XUSER_MAX_COUNT && pad_ < 0; ++i) {
      if (GetPadState(i, &state) == ERROR_SUCCESS) {
        pad_ = int(i);
      }
    }
    if (pad_ < 0) {
      return;
    }
  }
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  const XINPUT_GAMEPAD& pad = state.Gamepad;
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, (pad.wButtons & b.mask) != 0);
  }
  for (const StickKey& s : kStickKeys) {
    const int value = s.horizontal ? pad.sThumbLX : pad.sThumbLY;
    const float amount = std::clamp(float(value - s.dead_zone) / float(s.full - s.dead_zone), 0.0f, 1.0f);
    io.AddKeyAnalogEvent(s.key, amount > 0.1f, amount);
  }
}

void LauncherGamepad::Release(ImGuiIO& io) {
  for (const ButtonKey& b : kButtonKeys) {
    io.AddKeyEvent(b.key, false);
  }
  for (const StickKey& s : kStickKeys) {
    io.AddKeyAnalogEvent(s.key, false, 0.0f);
  }
  pad_ = -1;
}

}  // namespace hcu
