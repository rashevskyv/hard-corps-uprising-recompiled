#include "launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/version.h>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"

#include "game_files.h"
#include "platform.h"
#include "settings.h"

namespace hcu {
namespace {

// Logical pixels.
constexpr std::pair<int, int> kLauncherWindowSize = {1280, 720};
// Share of the primary display's work area the launcher may take.
constexpr double kLauncherMaxShare = 0.9;
// How long the saved message stays on screen before the game starts.
constexpr double kPlayFeedbackSeconds = 0.4;

constexpr std::pair<int, int> kWindowSizes[] = {{853, 480}, {1280, 720}, {1600, 900}, {1920, 1080}};

constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

// Hard Corps HUD look: neon green over the Midnight background.
const ImVec4 kAccent = ImVec4(0.36f, 0.88f, 0.44f, 1.0f);
const ImVec4 kEmerald = ImVec4(0.09f, 0.36f, 0.22f, 1.0f);

trg::Theme LauncherTheme() {
  trg::Theme theme = trg::Theme::Midnight().WithAccent(kAccent);
  theme.good = kEmerald;
  theme.good_dot = kAccent;
  return theme;
}

trg::HeaderPainter LauncherHeader() {
  return trg::headers::Gradient(IM_COL32(14, 18, 30, 255), IM_COL32(10, 52, 32, 255), IM_COL32(8, 28, 18, 255),
                                IM_COL32(8, 10, 18, 255));
}

trg::Fonts g_fonts;

GameFiles CheckAndLogGameFiles(const std::filesystem::path& game_dir) {
  const GameCheck check = CheckGameFiles(game_dir);
  if (check.state == GameFiles::kUnsupported && check.xex_crc32 != 0) {
    REXLOG_WARN("default.xex is not the supported version (CRC32 {:08X})", check.xex_crc32);
  }
  return check.state;
}

// Written by the install job, read once the Task has finished.
struct InstallOutcome {
  InstallResult result;
  GameFiles files = GameFiles::kMissing;
};

std::string Size(int width, int height) {
  return std::to_string(width) + " \xC3\x97 " + std::to_string(height);
}

// Measured presented output at resolution_scale 1. The guest video mode stays 853x480.
constexpr int kPresentedWidth = 1280;
constexpr int kPresentedHeight = 720;

std::vector<trg::Option> RenderResolutionOptions() {
  std::vector<trg::Option> options;
  for (int scale = 1; scale <= 4; ++scale) {
    std::string label = scale == 1 ? "Original" : std::to_string(scale) + "x";
    label += " \xE2\x80\x94 " + std::to_string(kPresentedWidth * scale) + "x" + std::to_string(kPresentedHeight * scale);
    if (scale == 2) {
      label += " \xE2\x80\x94 Recommended for 1440p";
    }
    options.push_back({std::to_string(scale), label});
  }
  return options;
}

std::string KeyList(std::string keys) {
  if (keys.empty()) {
    return "None";
  }
  for (size_t i = keys.find(','); i != std::string::npos; i = keys.find(',', i)) {
    keys.replace(i, 1, " or ");
  }
  return keys;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        cb_(std::move(callbacks)),
        settings_(settings),
        game_files_(CheckAndLogGameFiles(paths_.game_dir)),
        monitors_(ListMonitors()) {
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) {
      return;
    }
    PollInstall();
    if (!nav_taken_) {
      saved_nav_flags_ = io.ConfigFlags & kNavFlags;
      saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
      io.ConfigFlags |= kNavFlags;
      nav_taken_ = true;
    }
    gamepad_.Update(io);
    if (starting_ && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds) {
      StartGame();
      return;
    }
    if (starting_) {
      // TRG's hotkeys read the same key state, so release it before Frame() runs.
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
    ImGui::BeginDisabled(starting_);
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (starting_) {
      DrawStartingMessage();
    }
    switch (result) {
      case trg::Result::kPlay:
        RequestPlay();
        break;
      case trg::Result::kQuit:
        if (!starting_) {
          Finish(cb_.quit);
        }
        break;
      case trg::Result::kNone:
        break;
    }
  }

  // Every PLAY (mouse, Enter, Xbox Start) ends here. The one save happens here; StartGame never saves.
  void RequestPlay() {
    if (starting_ || install_.running() || game_files_ != GameFiles::kReady) {
      return;
    }
    if (!launcher_->Save()) {
      return;
    }
    starting_ = true;
    play_started_ = ImGui::GetTime();
    launcher_->SetStatus("", 0.0);
  }

  void StartGame() {
    if (cb_.window_changed && cb_.window_changed()) {
      if (cb_.restart && cb_.restart()) {
        Finish(nullptr);
        return;
      }
      starting_ = false;
      launcher_->SetStatus("Could not start the game in a new window. Press Play to try again.", 8);
      return;
    }
    Finish(cb_.play);
  }

  // Drawn outside the disabled frame so the message keeps full brightness.
  void DrawStartingMessage() {
    constexpr const char* kText = "Settings saved. Starting game...";
    ImFont* font = g_fonts.semibold ? g_fonts.semibold : ImGui::GetFont();
    const float size = g_fonts.size * 1.25f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kText);
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - text_size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - text_size.y) * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(kAccent), kText);
  }

  // Runs inside the last frame; the in-game overlays share this ImGui context.
  void OnClose() override {
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    Close();
    if (action) {
      action();
    }
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    // PLAY saves in RequestPlay, so a failed save keeps the launcher open.
    c.save_on_play = false;
    c.theme = LauncherTheme();
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.fonts = g_fonts;
    c.branding.title = "HARD CORPS: UPRISING";
    c.branding.background = LauncherHeader();
    c.pages = {
        {"Play", "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
         [this] { return game_files_ != GameFiles::kReady; }},
        {"Display", "Window, monitor and how the picture fits your screen.",
         [this](trg::Ui& ui) { PageDisplay(ui); }},
        {"Graphics", "Render resolution, anti-aliasing and texture filtering.",
         [](trg::Ui& ui) { PageGraphics(ui); }},
        {"Gameplay", "What the window shows while you play.", [](trg::Ui& ui) { PageGameplay(ui); }},
        {"Controls", "Controller and keyboard.", [](trg::Ui& ui) { PageControls(ui); }},
        {"About", "About this port, and where your files and settings live.",
         [this](trg::Ui& ui) { PageAbout(ui); }},
    };
    c.can_play = [this] {
      if (install_.running()) {
        return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      }
      if (game_files_ == GameFiles::kReady) {
        return trg::PlayCheck{};
      }
      return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
    };
    return c;
  }

  void PagePlay(trg::Ui& ui) {
    const std::string dir = Utf8(paths_.game_dir);
    if (install_.running()) {
      ui.StatusCard(trg::Status::kBusy, "Installing game files", nullptr, std::max(0.0f, install_.fraction()));
    } else {
      switch (game_files_) {
        case GameFiles::kReady:
          ui.StatusCard(trg::Status::kReady, "Ready to play", dir.c_str());
          break;
        case GameFiles::kUnsupported:
          ui.StatusCard(trg::Status::kAttention, "Different version of the game",
                        DescribeInstall(InstallStatus::kWrongVersion).text);
          break;
        case GameFiles::kMissing:
          ui.StatusCard(trg::Status::kAttention, "Game files needed",
                        "This port requires your own Hard Corps: Uprising Xbox Live Arcade package.");
          break;
      }
    }
    if (!install_failure_.empty()) {
      DrawInstallFailure(ui);
    }
    if (paths_.game_dir.empty()) {
      ui.Help("No game folder was given: start the game with --game_data_root.");
    } else {
      ui.Row("Game package",
             "Title ID: 58410A18. No game files are included with this port; the package's files (about 1 GB) are "
             "copied next to the program.");
      if (!ui.TaskProgress(install_)) {
        const bool clicked = game_files_ == GameFiles::kReady
                                 ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                                 : ui.AccentButton("Install from package...");
        if (clicked) {
          BrowseAndInstall();
        }
      }
    }
    ui.LauncherVisibilityRow("hcu_launcher");
    ui.EndRows();
  }

  void DrawInstallFailure(trg::Ui& ui) {
    ui.Spacer(4);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
    ImGui::TextUnformatted(install_failure_.c_str());
    ImGui::PopStyleColor();
    ui.Help(install_failure_text_.c_str());
    ui.Spacer(4);
  }

  void BrowseAndInstall() {
    PollInstall();
    if (install_.running()) {
      return;
    }
    const std::string picked = trg::BrowseForFile("Select your Hard Corps: Uprising Xbox Live Arcade package",
                                                  {{"Xbox Live Arcade package", "*"}});
    if (picked.empty()) {
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    outcome_ = std::make_shared<InstallOutcome>();
    const bool started = install_.Start(
        "Reading package...",
        [outcome = outcome_, package = PathFromUtf8(picked), game_dir = paths_.game_dir](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.progress = [&task](uint64_t done, uint64_t total) {
            task.Progress(static_cast<long long>(done), static_cast<long long>(total));
            return !task.cancelled();
          };
          outcome->result = InstallFromPackage(package, game_dir, hooks);
          if (outcome->result.ok()) {
            outcome->files = CheckGameFiles(game_dir).state;
          }
          return outcome->result.ok() ? std::string() : std::string(DescribeInstall(outcome->result.status).title);
        });
    if (started) {
      REXLOG_INFO("Installing game files from {}", picked);
    }
  }

  // Runs every frame so a finished install is picked up whichever page is open.
  void PollInstall() {
    std::string error;
    if (install_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    const std::shared_ptr<InstallOutcome> outcome = std::move(outcome_);
    if (!outcome) {
      return;
    }
    const InstallResult& result = outcome->result;
    const InstallMessage message = DescribeInstall(result.status);
    if (result.ok() || result.status == InstallStatus::kCancelled) {
      if (result.ok()) {
        game_files_ = outcome->files;
        REXLOG_INFO("Game files installed: {} files, {} bytes", result.files, result.bytes);
      } else {
        REXLOG_INFO("Game install cancelled");
      }
      launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("Game install failed: {} ({})", message.title, result.detail);
    install_failure_ = message.title;
    install_failure_text_ = message.text;
    launcher_->SetStatus("Install failed: " + install_failure_, 8.0);
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode",
              "How the game starts. Borderless Fullscreen fills the monitor. F11 switches while playing.", "fullscreen",
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", "The game window's size in Windowed mode.");
    WindowSizeCombo();
    ui.Row("Monitor", "The display the game opens on.");
    MonitorCombo();
    ui.Row("VSync", "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.");
    {
      const bool tearing = settings_.GetBool(kTearingSetting, true);
      if (int i = ui.Segmented("vsync", {"Off", "On"}, tearing ? 0 : 1); i >= 0) {
        settings_.SetBool(kTearingSetting, i == 0);
      }
    }
    ui.Row("Aspect ratio", "Letterbox keeps the game's 16:9 picture on any window shape; Stretch fills the window.");
    {
      const bool letterbox = settings_.GetBool("present_letterbox", true);
      if (int i = ui.Segmented("aspect", {"Letterbox 16:9", "Stretch"}, letterbox ? 0 : 1); i >= 0) {
        settings_.SetBool("present_letterbox", i == 0);
      }
    }
  }

  const MonitorInfo* SelectedMonitor() const {
    const int index = settings_.GetInt("monitor", 0);
    if (index >= 1 && index <= int(monitors_.size())) {
      return &monitors_[size_t(index - 1)];
    }
    for (const MonitorInfo& m : monitors_) {
      if (m.primary) {
        return &m;
      }
    }
    return monitors_.empty() ? nullptr : &monitors_.front();
  }

  // window_width/height are logical pixels; display scaling enlarges them.
  void WindowSizeCombo() {
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    const MonitorInfo* monitor = SelectedMonitor();
    const float scale = monitor ? monitor->scale : 1.0f;
    auto label = [scale](int w, int h) {
      if (w <= 0 || h <= 0) {
        return std::string("Default");
      }
      std::string text = Size(w, h);
      if (scale != 1.0f) {
        text += "   (" + Size(int(std::lround(w * scale)), int(std::lround(h * scale))) + " on screen)";
      }
      return text;
    };
    if (ImGui::BeginCombo("##window_size", label(width, height).c_str())) {
      for (const auto& [w, h] : kWindowSizes) {
        const bool current = w == width && h == height;
        const bool fits = !monitor || (w * scale < monitor->width && h * scale < monitor->height);
        if (!current && !fits) {
          continue;
        }
        if (ImGui::Selectable(label(w, h).c_str(), current) && !current) {
          settings_.SetInt("window_width", w);
          settings_.SetInt("window_height", h);
        }
      }
      ImGui::EndCombo();
    }
  }

  void MonitorCombo() {
    const int current = settings_.GetInt("monitor", 0);
    auto label = [this](int index) {
      if (index <= 0) {
        return std::string("Default");
      }
      if (index > int(monitors_.size())) {
        return "Display " + std::to_string(index) + " (not connected)";
      }
      const MonitorInfo& m = monitors_[size_t(index - 1)];
      return "Display " + std::to_string(index) + (m.primary ? " (primary)" : "") + "   " + Size(m.width, m.height);
    };
    if (ImGui::BeginCombo("##monitor", label(current).c_str())) {
      for (int i = 1; i <= int(monitors_.size()); ++i) {
        if (ImGui::Selectable(label(i).c_str(), i == current) && i != current) {
          settings_.SetInt("monitor", i);
        }
      }
      ImGui::EndCombo();
    }
  }

  static void PageGraphics(trg::Ui& ui) {
    ui.Combo("Render resolution",
             "Draws the game at a multiple of its own picture, then scales it to the window. Higher is sharper and "
             "costs more GPU time. Window size is separate.",
             "resolution_scale", "1", RenderResolutionOptions());
    ui.Choice("Anti-aliasing", "Smooths jagged edges after the frame is drawn. Extreme is softer but cleaner.",
              "swap_post_effect", "none", {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}});
    ui.Toggle("Multisampling", "Native 2\xC3\x97 MSAA wherever the game asks the Xbox 360 GPU for it.",
              "native_2x_msaa", true, "Off", "2\xC3\x97 MSAA");
    ui.Choice("Texture filtering", "Anisotropic filtering keeps textures sharp at oblique angles. Game leaves it to the game.",
              "anisotropic_override", "3",
              {{"-1", "Game"},
               {"0", "Off"},
               {"2", "2\xC3\x97"},
               {"3", "4\xC3\x97"},
               {"4", "8\xC3\x97"},
               {"5", "16\xC3\x97"}},
              6);
  }

  static void PageGameplay(trg::Ui& ui) {
    ui.Toggle("Frame rate in title", "Shows the game's frame rate in the window title: Hard Corps: Uprising | FPS: 60.00",
              "hcu_show_fps", true, "Off", "On");
  }

  static void PageControls(trg::Ui& ui) {
    ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
    ui.Toggle("Keyboard", "Plays as an Xbox 360 controller, together with any connected controller.", "mnk_mode",
              true, "Disabled", "Enabled");
    if (ui.Section("Keyboard keys")) {
      ui.Help("Plain arrow keys are the right stick. Menus use W A S D or Shift + arrow keys.");
      for (const rex::cvar::FlagEntry& e : rex::cvar::GetRegistry()) {
        if (e.category == "Input/Keybinds/Controller") {
          ui.Info(e.description.c_str(), KeyList(e.getter()).c_str());
        }
      }
    }
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Hard Corps: Uprising for Xbox Live Arcade, statically recompiled to run natively on PC with the ReXGlue "
        "SDK. No game code or assets are included: the game runs from your own game files.");
    ui.Spacer(4);
    if (!paths_.game_dir.empty()) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(paths_.game_dir));
    }
    ui.FolderRow("Saves and settings", "Your saves, the shader cache and settings.toml.", "Open data folder",
                 Utf8(paths_.user_dir));
    if (ui.ButtonRow("Settings file", "Your launcher choices, as plain text. Loaded after hard_corps_uprising.toml.",
                     "Show settings file") &&
        launcher_->Save()) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(paths_.settings_file, ec);
      trg::OpenInFileManager(Utf8(exists ? paths_.settings_file : paths_.user_dir));
    }
    ui.ResetAllRow();
    ui.Info("Port version", HCU_PORT_VERSION);
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "   \xC2\xB7   TRG Launcher " HCU_TRG_VERSION);
    ui.Info("Game", "Xbox Live Arcade, title 58410A18, game version 0.0.0.3");
    ui.Help("github.com/saulob/hard-corps-uprising-recompiled");
  }

  LauncherPaths paths_;
  LauncherCallbacks cb_;
  GameSettings& settings_;
  GameFiles game_files_;
  std::vector<MonitorInfo> monitors_;
  std::unique_ptr<trg::Launcher> launcher_;
  LauncherGamepad gamepad_;
  bool nav_taken_ = false;
  bool starting_ = false;
  double play_started_ = 0.0;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  bool done_ = false;
  std::string install_failure_;
  std::string install_failure_text_;
  std::shared_ptr<InstallOutcome> outcome_;
  trg::Task install_;
};

}  // namespace

void LoadLauncherFonts(ImFontAtlas* atlas, float density) {
  // Latin-1 plus General Punctuation.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};
  ImFontConfig config;
  config.RasterizerDensity = density;
  auto load = [&](const std::filesystem::path& path) -> ImFont* {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
      return nullptr;
    }
    return atlas->AddFontFromFileTTF(Utf8(path).c_str(), g_fonts.size, &config, kRanges);
  };
  const UiFontFiles fonts = FindUiFonts();
  g_fonts.regular = load(fonts.regular);
  g_fonts.semibold = load(fonts.semibold);
  g_fonts.bold = load(fonts.bold);
}

namespace {
bool g_skipped_once = false;
}  // namespace

bool ShouldShowLauncher(const std::filesystem::path& game_dir) {
  g_skipped_once = REXCVAR_GET(hcu_skip_launcher);
  rex::cvar::ResetToDefault("hcu_skip_launcher");
  return LauncherWanted(CheckAndLogGameFiles(game_dir) == GameFiles::kReady, g_skipped_once, ShiftHeld(),
                        REXCVAR_GET(hcu_launcher));
}

bool ShiftHeldAfterWindowOpened() {
#if defined(_WIN32)
  return false;
#else
  return !g_skipped_once && ShiftHeld();
#endif
}

namespace {

std::pair<int, int> LauncherWindowSize() {
  const MonitorInfo primary = MonitorForSetting(1);
  if (primary.width == 0 || primary.scale <= 0.0f) {
    return kLauncherWindowSize;
  }
  const int max_width = int(primary.work_width * kLauncherMaxShare / primary.scale);
  const int max_height = int(primary.work_height * kLauncherMaxShare / primary.scale);
  return {std::min(kLauncherWindowSize.first, max_width), std::min(kLauncherWindowSize.second, max_height)};
}

}  // namespace

void PrepareLauncherWindow() {
  const auto [width, height] = LauncherWindowSize();
  rex::cvar::SetFlagByName("fullscreen", "false");
  rex::cvar::SetFlagByName("window_width", std::to_string(width));
  rex::cvar::SetFlagByName("window_height", std::to_string(height));
  // Monitor 0 lets SDL centre the window on the primary display, whatever the display order.
  rex::cvar::SetFlagByName("monitor", "0");
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), settings, std::move(callbacks));
}

}  // namespace hcu
