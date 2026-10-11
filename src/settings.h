#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "trg/settings.h"

REXCVAR_DECLARE(bool, hcu_launcher);
REXCVAR_DECLARE(bool, hcu_skip_launcher);
REXCVAR_DECLARE(bool, hcu_show_fps);

namespace hcu {

// Allowing tearing is VSync Off. Each platform's presenter has its own setting for it.
#if defined(_WIN32)
inline constexpr char kTearingSetting[] = "d3d12_allow_variable_refresh_rate_and_tearing";
#else
inline constexpr char kTearingSetting[] = "vulkan_allow_present_mode_immediate";
#endif

// The player's settings file, loaded over the shipped hard_corps_uprising.toml.
std::filesystem::path PlayerSettingsPath(const std::filesystem::path& user_data_root);
void LoadPlayerSettings(const std::filesystem::path& path);

// Edits stay pending until Save() or Apply(), so nothing reaches the window while the launcher is open.
class GameSettings final : public trg::SettingsStore {
 public:
  // The settings file is TOML, so toggles are written as true and false.
  GameSettings() {
    true_value = "true";
    false_value = "false";
  }

  // Takes the settings in effect now as the saved state.
  void Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config);
  // Takes the settings that were not registered at Capture (the Xenos graphics cvars).
  // Call once after SetupPresentation and before the launcher is shown.
  void CaptureLateRegistered();

  std::string Get(std::string_view key, std::string_view def = {}) const override;
  void Set(std::string_view key, std::string value) override;
  bool Save() override;
  void ResetToDefaults() override;

  // Sets the running game's cvars that differ from the pending values.
  void Apply() const;

  // Records the window settings this process creates its window with.
  void MarkWindowCreated();
  // Whether the pending window size, monitor or VSync needs a new window.
  bool WindowChanged() const;

 private:
  std::filesystem::path player_file_;
  std::filesystem::path shipped_config_;
  std::map<std::string, std::string> saved_;
  std::map<std::string, std::string> pending_;
  std::map<std::string, std::string> window_created_;
  std::set<std::string> unregistered_;
};

}  // namespace hcu
