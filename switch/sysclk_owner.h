// Which clocks sys-clk sets, from its IPC (service "sys:clk"). Pure C++: the host test includes it too.
// Layout and rules from D:\git\dev\sys-clk-vanilla-kefir (Kefir's sys-clk, IPC API 5; vanilla API 4, commit 05d6da2,
// has the same SysClkContext and SysClkTitleProfileList). Other API versions are not trusted.
#pragma once

#include <cstdint>

namespace hcu_sysclk {

constexpr int kModules = 3;   // SysClkModule: CPU, GPU, MEM
constexpr int kProfiles = 5;  // SysClkProfile: Handheld, HandheldCharging, ..USB, ..Official, Docked
constexpr uint64_t kGlobalTid = 0xA111111111111111ULL;  // Kefir: SYSCLK_GLOBAL_PROFILE_TID

enum Cmd : uint32_t { kGetApiVersion = 0, kGetCurrentContext = 2, kGetProfiles = 5 };

struct Context {  // SysClkContext
  uint8_t enabled;
  uint64_t applicationId;
  uint32_t profile;
  uint32_t freqs[kModules];  // the rates as sys-clk last read them (clkrst), Hz
  uint32_t realFreqs[kModules];
  uint32_t overrideFreqs[kModules];  // the overlay's temporary override, 0 = none
  uint32_t temps[3];
  int32_t power[2];
  uint32_t ramLoad[2];
};
static_assert(sizeof(Context) == 88, "SysClkContext of IPC API 4/5");

struct Profiles {  // SysClkTitleProfileList: MHz per [profile][module], 0 = "Do not override"
  uint32_t mhz[kProfiles][kModules];
};
static_assert(sizeof(Profiles) == 60, "SysClkTitleProfileList");

inline bool api_trusted(uint32_t api) { return api == 4 || api == 5; }

// The rate sys-clk's ClockManager::Tick aims `module` at, in Hz; 0 = it leaves the module to the system and the game.
// Tick: the override, else the title's profile, else the global profile, each through Config::GetAutoClockHz's
// fallback chain; it sets only while enabled. `title` and `global` are GetProfiles of the title and of kGlobalTid.
inline uint32_t target_hz(const Context& c, const Profiles& title, const Profiles& global, int module) {
  if (!c.enabled) return 0;
  if (c.overrideFreqs[module]) return c.overrideFreqs[module];
  static const int chains[kProfiles][4] = {
      {0, -1},        // Handheld
      {2, 1, 0, -1},  // HandheldCharging: USB, Charging, Handheld
      {2, 1, 0, -1},  // HandheldChargingUSB
      {3, 1, 0, -1},  // HandheldChargingOfficial: Official, Charging, Handheld
      {4, -1},        // Docked
  };
  if (c.profile >= kProfiles) return 0;  // sys-clk itself throws on it
  for (const Profiles* list : {&title, &global}) {
    for (const int* p = chains[c.profile]; *p >= 0; p++) {
      if (list->mhz[*p][module]) return list->mhz[*p][module] * 1000000u;
    }
  }
  return 0;
}

}  // namespace hcu_sysclk
