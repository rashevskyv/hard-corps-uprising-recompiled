// The clock governor's ladders (switch/hcu_nx_base.cpp), one per frame-rate mode, and the speed it acts on. Pure
// C++: the host test (tests/settings_menu_tests.cpp) includes it too. The rates are steps of the clkrst/pcv tables
// (CPU 1020, 1122, 1224, 1326, 1428, 1581, 1683, 1785; GPU 307.2, 384, 460.8, 537.6, 614.4, 691.2, 768, 844.8,
// 921.6 MHz; sys-clk's list, D:\git\dev\sys-clk-vanilla-kefir\manager\src\ipc\pc_shim\client.cpp).
#pragma once

#include <cstdint>

namespace hcu_clock {

struct Rung { uint32_t cpu, gpu; };  // kHz

// The first rung is the start and the floor: the governor never goes below it. Handheld: one ladder per mode.
// 30 (fps_mode 0) and 30 HALF SPEED (3): 1122/384 is the user's guess, not measured.
constexpr Rung k30[] = {{1122000, 384000}, {1224000, 384000}, {1428000, 384000},
                        {1581000, 384000}, {1683000, 460800}, {1785000, 768000}};
// 45 (fps_mode 4): 1326/460.8 is the user's guess, not measured.
constexpr Rung k45[] = {{1326000, 460800}, {1428000, 460800}, {1581000, 460800}, {1683000, 460800}, {1785000, 768000}};
// 60 (fps_mode 1 and 2): 1683/691.2 is the user's choice, not measured as a pair (by hand with sys-clk: stock CPU with
// GPU 690, 1200/800+ and 1785/844 held 60 fps).
constexpr Rung k60[] = {{1683000, 691200}, {1785000, 768000}, {1785000, 844800}, {1785000, 921600}};
// Docked, every mode: the user's guess "1700 and 800", not measured; 1683 is the CPU step nearest 1700, 844.8 the GPU
// step above 800 (768 is below it).
constexpr Rung kDocked[] = {{1683000, 844800}, {1785000, 844800}, {1785000, 921600}};
// The memory is not on a ladder: clocks=1 sets it to 1600 MHz once (hcu_nx_base.cpp), in every mode and on every rung.

struct Ladder {
  const char* name;
  const Rung* rungs;
  int count;
};

constexpr Ladder ladder_for(int fps_mode, bool docked) {
  if (docked) return {"docked", kDocked, int(sizeof(kDocked) / sizeof(kDocked[0]))};
  if (fps_mode == 4) return {"45", k45, int(sizeof(k45) / sizeof(k45[0]))};
  if (fps_mode == 1 || fps_mode == 2) return {"60", k60, int(sizeof(k60) / sizeof(k60[0]))};
  return {"30", k30, int(sizeof(k30) / sizeof(k30[0]))};
}

constexpr const char* mode_name(int fps_mode) {
  return fps_mode == 3 ? "30 fps half speed" : fps_mode == 4 ? "45 fps" : fps_mode == 1 || fps_mode == 2 ? "60 fps" : "30 fps";
}

// The speed in % from the level swaps (all guest frames, skipped ones too) and the drawn ones of `secs` seconds.
// Full game speed is 60 guest frames a second, 30 in 30 HALF SPEED (it runs the game at half speed). 45 and 60 skip a
// frame the GPU is busy for, so the game keeps its speed while fewer frames are shown: there the shown frames against
// 45 or 60 count too (the lower of the two). 30 keeps the game speed only (its 30 Hz pacing draws about 30).
inline double gov_speed(int fps_mode, uint32_t swaps, uint32_t drawn, double secs) {
  const double game = swaps / secs / (fps_mode == 3 ? 30.0 : 60.0) * 100.0;
  const double shown_fps = fps_mode == 4 ? 45.0 : fps_mode == 1 || fps_mode == 2 ? 60.0 : 0.0;
  if (shown_fps == 0.0) return game;
  const double shown = drawn / secs / shown_fps * 100.0;
  return shown < game ? shown : game;
}

}  // namespace hcu_clock
