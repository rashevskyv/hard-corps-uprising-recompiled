#pragma once
// The deko3d backend's on-screen strip text (src/gpu/deko3d_backend.cpp), tests/overlay_text_tests.cpp.
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace hcu {

// OVERLAY on:          "HCU <version>  <fps> fps  t <s> #<swap>  <meter>" (no fps part with FPS COUNTER off)
// FPS COUNTER alone:   "<fps> fps  t <s> #<swap>"
// t and # are the [nx] load line's clock (seconds since the first drawn swap) and swap number: a screenshot of the
// strip finds its place in the log.
inline void OverlayText(char* out, size_t n, bool strip, bool fps, const char* version, uint32_t fps_x10, double t,
                        uint32_t swap, const char* meter) {
  char f[48] = "";
  if (fps) std::snprintf(f, sizeof(f), "%u.%u fps  t %.1f #%u", fps_x10 / 10, fps_x10 % 10, t, swap);
  if (strip)
    std::snprintf(out, n, "HCU %s  %s%s%s", version, f, fps ? "  " : "", meter);
  else
    std::snprintf(out, n, "%s", f);
}

}  // namespace hcu
