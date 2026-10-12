#include "settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/logging.h>

#include "trg/settings.h"

#include "legacy_settings.h"

#include "platform.h"

REXCVAR_DEFINE_BOOL(hcu_launcher, true, "Hard Corps",
                    "Show the launcher before the game starts (hold Shift at start to force it)");
REXCVAR_DEFINE_BOOL(hcu_skip_launcher, false, "Hard Corps",
                    "Skip the launcher once (set when the launcher restarts the game)");
REXCVAR_DEFINE_BOOL(hcu_show_fps, true, "Hard Corps", "Show the frame rate in the window title");
REXCVAR_DEFINE_BOOL(hcu_show_frame_counter, false, "Hard Corps",
                    "Show the frame counter in the corner (F2 toggles it)");
REXCVAR_DEFINE_INT32(hcu_launcher_x, 0, "Hard Corps", "Launcher window left edge, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_y, 0, "Hard Corps", "Launcher window top edge, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_width, 0, "Hard Corps", "Launcher window restored width, written by the launcher");
REXCVAR_DEFINE_INT32(hcu_launcher_height, 0, "Hard Corps",
                     "Launcher window restored height, written by the launcher");
REXCVAR_DEFINE_BOOL(hcu_launcher_maximized, false, "Hard Corps", "Launcher window was maximized when closed");
REXCVAR_DEFINE_BOOL(hcu_launcher_fullscreen, false, "Hard Corps",
                    "Launcher was left in its own F11 fullscreen, written by the launcher");

namespace hcu {
namespace {

const std::vector<std::string> kGameSettingKeys = {
    "hcu_launcher",
    "fullscreen",
    "window_width",
    "window_height",
    "monitor",
    kTearingSetting,
    "present_letterbox",
    "resolution_scale",
    "swap_post_effect",
    "native_2x_msaa",
    "anisotropic_override",
    "hcu_show_fps",
    "hcu_show_frame_counter",
    "mnk_mode",
    "hcu_cheat_health",
    "hcu_cheat_lives",
    "hcu_cheat_rapid_fire",
    "hcu_cheat_jump_height",
    "hcu_cheat_keep_weapons",
    "hcu_cheat_movement_speed",
    "hcu_cheat_super_armor",
};

const std::vector<std::string> kWindowKeys = {
    "window_width",
    "window_height",
    "monitor",
    kTearingSetting,
};

bool IsWindowKey(std::string_view key) {
  return std::find(kWindowKeys.begin(), kWindowKeys.end(), key) != kWindowKeys.end();
}

// Monitor 0 (Default) is the primary display, as monitor 1 is.
std::string WindowValue(std::string_view key, const std::string& value) {
  if (key == "monitor") {
    return std::to_string(std::max(1, std::atoi(value.c_str())));
  }
  return value;
}

std::string TomlValue(std::string_view key, const std::string& value) {
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
  if (!info || info->type != rex::cvar::FlagType::String) {
    return value;
  }
  std::string quoted = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
    }
    quoted += c;
  }
  return quoted + '"';
}

std::optional<std::string> ConfigValue(const toml::table& table, std::string_view key) {
  const toml::node* node = table.get(key);
  if (!node) {
    return std::nullopt;
  }
  if (auto v = node->value_exact<bool>()) {
    return *v ? "true" : "false";
  }
  if (auto v = node->value_exact<int64_t>()) {
    return std::to_string(*v);
  }
  if (auto v = node->value_exact<double>()) {
    return std::to_string(*v);
  }
  return node->value_exact<std::string>();
}

trg::TextFileSettings OpenPlayerFile(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (!std::filesystem::exists(path, ec)) {
    std::ofstream(path) << "# Hard Corps: Uprising player settings, written by the launcher.\n"
                           "# Loaded after hard_corps_uprising.toml. Remove a line to use the shipped value.\n";
  }
  return trg::TextFileSettings(Utf8(path));
}

}  // namespace

// Runs before the file is parsed: ReXGlue rejects a whole TOML file that contains one bad value.
void RepairLegacyPlayerSettings(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::size_t repaired = 0;
  const std::string fixed = RepairLegacyToggles(text, repaired);
  if (repaired == 0) {
    return;
  }

  std::filesystem::path backup = path;
  backup += ".bak";
  std::error_code ec;
  std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    REXLOG_ERROR("Not repairing {}: cannot write the backup", Utf8(path));
    return;
  }
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary);
    out << fixed;
    if (!out) {
      REXLOG_ERROR("Cannot write the repaired settings to {}", Utf8(temp));
      return;
    }
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    REXLOG_ERROR("Cannot replace {} with the repaired settings", Utf8(path));
    return;
  }
  REXLOG_INFO("Repaired {} legacy on/off value(s) in {} (original kept as {})", repaired, Utf8(path), Utf8(backup));
}

void LoadPlayerSettings(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return;
  }
  RepairLegacyPlayerSettings(path);
  rex::cvar::LoadConfig(path);
  REXLOG_INFO("Player settings: {}", Utf8(path));
}

WindowPlacement SavedLauncherPlacement() {
  struct Cvars {
    int GetInt(const char* key, int) const { return std::atoi(rex::cvar::GetFlagByName(key).c_str()); }
    bool GetBool(const char* key, bool) const { return rex::cvar::GetFlagByName(key) == "true"; }
  };
  return ReadPlacement(Cvars{});
}

bool SavedLauncherFullscreen() {
  return REXCVAR_GET(hcu_launcher_fullscreen);
}

void GameSettings::Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config) {
  player_file_ = player_file;
  shipped_config_ = shipped_config;
  saved_.clear();
  unregistered_.clear();
  for (const std::string& key : kGameSettingKeys) {
    saved_[key] = rex::cvar::GetFlagByName(key);
    if (!rex::cvar::GetFlagInfo(key)) {
      unregistered_.insert(key);
    }
  }
  pending_ = saved_;
}

void GameSettings::CaptureLateRegistered() {
  for (auto it = unregistered_.begin(); it != unregistered_.end();) {
    const std::string& key = *it;
    if (!rex::cvar::GetFlagInfo(key)) {
      ++it;
      continue;
    }
    if (pending_[key].empty()) {
      saved_[key] = pending_[key] = rex::cvar::GetFlagByName(key);
    }
    it = unregistered_.erase(it);
  }
}

std::string GameSettings::Get(std::string_view key, std::string_view def) const {
  const auto it = pending_.find(std::string(key));
  return it == pending_.end() || it->second.empty() ? std::string(def) : it->second;
}

void GameSettings::Set(std::string_view key, std::string value) {
  pending_[std::string(key)] = std::move(value);
}

bool GameSettings::Save() {
  std::vector<std::pair<std::string, std::string>> changes;
  for (const auto& [key, value] : pending_) {
    const auto saved = saved_.find(key);
    if (saved == saved_.end() || saved->second != value) {
      changes.emplace_back(key, value);
    }
  }
  if (changes.empty()) {
    return true;
  }
  for (const auto& [key, value] : changes) {
    if (!rex::cvar::GetFlagInfo(key)) {
      REXLOG_ERROR("Cannot save {}: the setting is not registered", key);
      return false;
    }
  }

  trg::TextFileSettings file = OpenPlayerFile(player_file_);
  for (const auto& [key, value] : changes) {
    file.Set(key, TomlValue(key, value));
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_));
    return false;
  }
  saved_ = pending_;
  return true;
}

bool GameSettings::SaveNow(const SettingValues& values) {
  trg::TextFileSettings file = OpenPlayerFile(player_file_);
  for (const auto& [key, value] : values) {
    file.Set(key, value);
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_));
    return false;
  }
  return true;
}

void GameSettings::ResetToDefaults() {
  toml::table shipped;
  try {
    shipped = toml::parse_file(Utf8(shipped_config_));
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("Cannot read {}: {}", Utf8(shipped_config_), e.what());
  }
  for (const std::string& key : kGameSettingKeys) {
    const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
    pending_[key] = ConfigValue(shipped, key).value_or(info ? info->default_value : std::string());
  }
}

void GameSettings::Apply() const {
  for (const auto& [key, value] : pending_) {
    if (IsWindowKey(key) || rex::cvar::GetFlagByName(key) == value) {
      continue;
    }
    rex::cvar::SetFlagByName(key, value);
  }
}

void GameSettings::MarkWindowCreated() {
  window_created_.clear();
  for (const std::string& key : kWindowKeys) {
    window_created_[key] = rex::cvar::GetFlagByName(key);
  }
}

bool GameSettings::WindowChanged() const {
  for (const std::string& key : kWindowKeys) {
    const auto created = window_created_.find(key);
    const std::string created_value = created == window_created_.end() ? std::string() : created->second;
    if (WindowValue(key, Get(key)) != WindowValue(key, created_value)) {
      return true;
    }
  }
  return false;
}

}  // namespace hcu
