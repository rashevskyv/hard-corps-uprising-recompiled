// Aspect Ratio: formats, the game-side widths and canvas factor, the setting key and its fallback, the
// retired present_letterbox line, and the launcher, settings and render wiring. Run: hcu_aspect_ratio_tests

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "aspect_ratio.h"
#include "retired_settings.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestFormats() {
  std::printf("formats, labels and ratios\n");
  CHECK(std::size(hcu::kAspectRatios) == 6);
  const char* keys[] = {"16:9", "16:10", "21:9", "32:9", "4:3", "5:4"};
  const char* labels[] = {"16:9 Original", "16:10", "21:9 Ultrawide", "32:9 Super Ultrawide", "4:3", "5:4"};
  const double ratios[] = {16.0 / 9.0, 16.0 / 10.0, 21.0 / 9.0, 32.0 / 9.0, 4.0 / 3.0, 5.0 / 4.0};
  for (std::size_t i = 0; i < 6; ++i) {
    const hcu::AspectRatioInfo& info = hcu::kAspectRatios[i];
    CHECK(std::string(info.key) == keys[i]);
    CHECK(std::string(info.label) == labels[i]);
    CHECK(info.ratio == ratios[i]);
    CHECK(hcu::InfoFor(info.aspect).aspect == info.aspect);
    CHECK(hcu::ParseAspectRatio(info.key) == info.aspect);
  }
}

void TestParseFallback() {
  std::printf("setting value parsing and fallback\n");
  CHECK(hcu::ParseAspectRatio("16:9") == hcu::AspectRatio::k16x9);
  CHECK(hcu::ParseAspectRatio("32:9") == hcu::AspectRatio::k32x9);
  CHECK(hcu::ParseAspectRatio("") == hcu::AspectRatio::k16x9);
  CHECK(hcu::ParseAspectRatio("auto") == hcu::AspectRatio::k16x9);
  CHECK(hcu::ParseAspectRatio("21:9 ") == hcu::AspectRatio::k16x9);
  CHECK(hcu::ParseAspectRatio("2.33") == hcu::AspectRatio::k16x9);
}

void TestWindowSizes() {
  std::printf("window sizes\n");
  const hcu::WindowSize required[] = {
      {1280, 720, hcu::AspectRatio::k16x9},   {1600, 900, hcu::AspectRatio::k16x9},
      {1920, 1080, hcu::AspectRatio::k16x9},  {2560, 1440, hcu::AspectRatio::k16x9},
      {1280, 800, hcu::AspectRatio::k16x10},  {1440, 900, hcu::AspectRatio::k16x10},
      {1260, 540, hcu::AspectRatio::k21x9},   {1680, 720, hcu::AspectRatio::k21x9},
      {2100, 900, hcu::AspectRatio::k21x9},   {1600, 450, hcu::AspectRatio::k32x9},
      {1920, 540, hcu::AspectRatio::k32x9},   {1024, 768, hcu::AspectRatio::k4x3},
      {1280, 960, hcu::AspectRatio::k4x3},    {1280, 1024, hcu::AspectRatio::k5x4},
  };
  for (const hcu::WindowSize& want : required) {
    bool found = false;
    for (const hcu::WindowSize& size : hcu::kWindowSizes) {
      found = found || (size.width == want.width && size.height == want.height && size.aspect == want.aspect);
    }
    CHECK(found);
  }
  for (const hcu::WindowSize& size : hcu::kWindowSizes) {
    const double ratio = double(size.width) / double(size.height);
    CHECK(std::fabs(ratio - hcu::InfoFor(size.aspect).ratio) < 0.001);
  }
}

// Game-side widths: 720 lines at every format, with the guest video mode keeping 480 lines.
void TestGameWidths() {
  std::printf("game-side widths, video mode and canvas factor\n");
  const int guest[] = {1280, 1152, 1680, 2560, 960, 900};
  const int video[] = {853, 768, 1120, 1707, 640, 600};
  for (std::size_t i = 0; i < 6; ++i) {
    const hcu::AspectRatio aspect = hcu::kAspectRatios[i].aspect;
    const double ratio = hcu::kAspectRatios[i].ratio;
    CHECK(hcu::GuestWidth(aspect) == guest[i]);
    CHECK(hcu::VideoModeWidth(aspect) == video[i]);
    CHECK(std::fabs(double(hcu::GuestWidth(aspect)) / hcu::kGuestHeight - ratio) < 1e-3);
    CHECK(std::fabs(double(hcu::VideoModeWidth(aspect)) / hcu::kVideoModeHeight - ratio) < 2e-3);
    const float factor = hcu::CanvasFactor(aspect, float(hcu::GuestWidth(aspect)), float(hcu::kGuestHeight));
    CHECK(std::fabs(factor - 1.0f) < 1e-5f);
    CHECK(std::fabs(factor * hcu::GuestWidth(aspect) - hcu::kGuestHeight * ratio) < 1e-2);
  }
  CHECK(hcu::CanvasFactor(hcu::AspectRatio::k16x9, 1280.0f, 720.0f) == float(16.0 / 9.0) / (1280.0f / 720.0f));
}

void TestRetiredLineMigration() {
  std::printf("retired present_letterbox line\n");
  const std::string keep = "hcu_skip_intro = true\nwindow_width = 1280\n";
  CHECK(hcu::RemoveSettingLines("hcu_skip_intro = true\npresent_letterbox = false\nwindow_width = 1280\n",
                                "present_letterbox") == keep);
  CHECK(hcu::RemoveSettingLines("  present_letterbox=true\r\nhcu_aspect_ratio = \"21:9\"\r\n", "present_letterbox") ==
        "hcu_aspect_ratio = \"21:9\"\r\n");
  CHECK(hcu::RemoveSettingLines("window_width = 1\npresent_letterbox = false", "present_letterbox") ==
        "window_width = 1\n");
  CHECK(hcu::RemoveSettingLines("present_letterbox_extra = 1\n# present_letterbox = false\n", "present_letterbox") ==
        "present_letterbox_extra = 1\n# present_letterbox = false\n");
  CHECK(hcu::RemoveSettingLines("present_letterbox\n", "present_letterbox") == "present_letterbox\n");
  CHECK(hcu::RemoveSettingLines(keep, "present_letterbox") == keep);
  CHECK(hcu::RemoveSettingLines("", "present_letterbox").empty());
}

void TestWiring() {
  std::printf("setting, launcher and render wiring\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(hcu_aspect_ratio, \"16:9\""));
  CHECK(Has(settings, "    \"hcu_aspect_ratio\",\n") || Has(settings, "    \"hcu_aspect_ratio\",\r\n"));
  CHECK(!Has(settings, "    \"present_letterbox\",\n") && !Has(settings, "    \"present_letterbox\",\r\n"));
  CHECK(Has(settings, "RemoveRetiredPlayerSettings(path);"));
  CHECK(Has(settings, "ApplyAspectRatio();"));

  const std::string app = ReadSource("hard_corps_uprising_app.h");
  CHECK(Has(app, "hcu::ApplyAspectRatio();"));

  const std::string render = ReadSource("aspect_render.cpp");
  CHECK(Has(render, "ParseAspectRatio(rex::cvar::GetFlagByName(\"hcu_aspect_ratio\"))"));
  CHECK(Has(render, "REX_HOOK_RAW(sub_82564EF8)"));
  CHECK(Has(render, "REX_HOOK_RAW(sub_8282FC80)"));
  CHECK(Has(render, "REX_HOOK_RAW(sub_8282E450)"));
  CHECK(Has(render, "hcu::CanvasFactor(aspect, width, height)"));
  CHECK(Has(render, "SetFlagByName(\"present_letterbox\", \"true\")"));
  CHECK(Has(render, "SetFlagByName(\"video_mode_width\""));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Combo(\"Aspect Ratio\""));
  CHECK(Has(launcher, "\"hcu_aspect_ratio\", \"16:9\", AspectRatioOptions()"));
  CHECK(Has(launcher, "RenderResolutionOptions(SelectedAspectRatio())"));
  CHECK(!Has(launcher, "Letterbox 16:9"));
  CHECK(!Has(launcher, "\"aspect\""));
  CHECK(!Has(launcher, "\"present_letterbox\""));
}

}  // namespace

int main() {
  TestFormats();
  TestParseFallback();
  TestWindowSizes();
  TestGameWidths();
  TestRetiredLineMigration();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
