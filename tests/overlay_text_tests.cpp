// The deko3d strip text (src/gpu/overlay_text.h): its format and that the longest text fits the strip.
// Run: hcu_overlay_text_tests

#include <cstdio>
#include <string>

#include "gpu/overlay_text.h"

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

std::string Text(bool strip, bool fps, const char* version, uint32_t fps_x10, double t, uint32_t swap, const char* meter) {
  char out[112];
  hcu::OverlayText(out, sizeof(out), strip, fps, version, fps_x10, t, swap, meter);
  return out;
}

}  // namespace

int main() {
  const char* meter = "m 12.3:15.6 c 4.5:8.9 g 10.2:14.4";
  CHECK(Text(true, true, "0.394.582823f-meter", 300, 1234.56, 37037, meter) ==
        "HCU 0.394.582823f-meter  30.0 fps  t 1234.6 #37037  m 12.3:15.6 c 4.5:8.9 g 10.2:14.4");
  CHECK(Text(false, true, "0.394.582823f", 599, 0.04, 1, "") == "59.9 fps  t 0.0 #1");
  CHECK(Text(true, false, "0.394.582823f", 300, 5.0, 2, "") == "HCU 0.394.582823f  ");
  // The longest text (a dirty meter version, 100+ fps, 27 h, the meter's 47 characters) fits the 105 glyphs of the
  // 1264-pixel strip (src/gpu/deko3d_backend.cpp kOverlayW: (1264 - 4) / 12).
  const std::string longest = Text(true, true, "0.9999.582823f+-meter", 9999, 99999.9, 9999999, std::string(47, 'm').c_str());
  CHECK(longest.size() <= 105);
  std::printf("%s\n%d checks, %d failures\n", longest.c_str(), g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
