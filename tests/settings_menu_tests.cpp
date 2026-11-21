// START STAGE jump edge tests (src/settings_menu.cpp): a close jumps only when cheat_stage changed to 1..8 while the
// menu was open. And the clocks key: hcu_clocks_key (read at start by switch/hcu_nx_base.cpp) and a menu save keeps
// clocks=1. Host only (unistd.h): g++ -std=c++20 -Isrc tests/settings_menu_tests.cpp src/settings_menu.cpp
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

#include "settings_menu.h"
#include "../switch/sysclk_owner.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) ++g_failures, std::printf("FAIL: %s\n", what);
}

void Press(uint32_t buttons) {
  hcu_menu_input(buttons);
  hcu_menu_input(0);
}

void Toggle() { Press(HCU_MENU_LS | HCU_MENU_RS); }

void WriteFile(const std::string& path, const char* text) {
  FILE* f = std::fopen(path.c_str(), "w");
  std::fputs(text, f);
  std::fclose(f);
}

}  // namespace

int main() {
  hcu_settings_load(nullptr);  // no dir: Save() writes nothing
  Check(hcu_take_stage_jump() == 0, "no jump at start");

  Toggle();
  hcu_settings.cheat_stage = 3;
  Toggle();
  Check(hcu_take_stage_jump() == 3, "a change to 3 then the combo close jumps to 3");
  Check(hcu_take_stage_jump() == 0, "the jump is taken once");

  Toggle();
  Toggle();
  Check(hcu_take_stage_jump() == 0, "a close without a change does not jump (cheat_stage 3 stays set)");

  Toggle();
  hcu_settings.cheat_stage = 5;
  Press(HCU_MENU_B);
  Check(hcu_take_stage_jump() == 5, "a change then the B close jumps");

  Toggle();
  hcu_settings.cheat_stage = 0;
  Press(HCU_MENU_B);
  Check(hcu_take_stage_jump() == 0, "a change to 0 does not jump");

  Toggle();
  hcu_settings.cheat_stage = 2;
  hcu_settings.cheat_stage = 0;
  Toggle();
  Check(hcu_take_stage_jump() == 0, "a change and back does not jump");

  // The clocks key, as the start-up reads it.
  char tmpl[] = "/tmp/hcu_settings_XXXXXX";
  const std::string dir = std::string(mkdtemp(tmpl)) + "/";
  const std::string file = dir + "settings.txt";
  Check(hcu_clocks_key(file.c_str()) == 0, "no file: clocks 0");
  WriteFile(file, "fps_mode=0\nrumble=1\n");
  Check(hcu_clocks_key(file.c_str()) == 0, "no clocks key: 0");
  WriteFile(file, "fps_mode=0\nclocks=1\r\nrumble=1\r\n");
  Check(hcu_clocks_key(file.c_str()) == 1, "clocks=1 with CRLF: 1");
  WriteFile(file, "clocks=0\n");
  Check(hcu_clocks_key(file.c_str()) == 0, "clocks=0: 0");
  WriteFile(file, "clocks=0\nclocks=1\n");
  Check(hcu_clocks_key(file.c_str()) == 1, "the last clocks line wins");
  // A menu close saves settings.txt: clocks=1 must stay (not re-saved as 0).
  WriteFile(file, "fps_mode=0\nclocks=1\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.clocks == 1, "the settings load reads clocks=1");
  Toggle();
  Toggle();
  Check(hcu_clocks_key(file.c_str()) == 1, "a menu save keeps clocks=1");
  hcu_settings.clocks = 0;
  Toggle();
  Toggle();
  Check(hcu_clocks_key(file.c_str()) == 0, "a menu save with clocks 0 writes clocks=0");
  // A menu save keeps the clocks_* lines (read by the NRO's start-up code, not by the struct).
  WriteFile(file, "clocks=1\nclocks_auto=0\nclocks_cpu=1428\nclocks_ladder=1020/307.2,1581/384\r\nfps_mode=0\n");
  hcu_settings_load(dir.c_str());
  Toggle();
  Toggle();
  {
    std::string text;
    char buf[512];
    FILE* f = std::fopen(file.c_str(), "r");
    while (f && std::fgets(buf, sizeof(buf), f)) text += buf;
    if (f) std::fclose(f);
    Check(text.find("clocks_auto=0\n") != std::string::npos, "a menu save keeps clocks_auto");
    Check(text.find("clocks_cpu=1428\n") != std::string::npos, "a menu save keeps clocks_cpu");
    Check(text.find("clocks_ladder=1020/307.2,1581/384") != std::string::npos, "a menu save keeps clocks_ladder");
  }
  std::remove(file.c_str());
  rmdir(dir.c_str());

  // sys-clk's rate per module (switch/sysclk_owner.h), as its ClockManager::Tick picks it.
  {
    using namespace hcu_sysclk;
    Context c{};
    Profiles title{}, global{};
    c.enabled = 1;
    c.profile = 0;  // Handheld
    Check(target_hz(c, title, global, 0) == 0, "sys-clk: no value anywhere = Do not override");
    c.overrideFreqs[1] = 921600000;
    Check(target_hz(c, title, global, 1) == 921600000, "sys-clk: the override wins");
    Check(target_hz(c, title, global, 0) == 0, "sys-clk: an override of the GPU leaves the CPU");
    title.mhz[0][1] = 460;
    Check(target_hz(c, title, global, 1) == 921600000, "sys-clk: the override wins over the profile");
    title.mhz[0][0] = 1224;
    global.mhz[0][0] = 1020;
    Check(target_hz(c, title, global, 0) == 1224000000u, "sys-clk: the title profile wins over the global one");
    global.mhz[0][2] = 1600;
    Check(target_hz(c, title, global, 2) == 1600000000u, "sys-clk: the global profile when the title has none");
    c.profile = 4;  // Docked: no fallback to Handheld
    Check(target_hz(c, title, global, 0) == 0, "sys-clk: docked does not take the handheld rate");
    c.profile = 3;  // Official charger: Official, Charging, Handheld
    Check(target_hz(c, title, global, 0) == 1224000000u, "sys-clk: the official charger falls back to handheld");
    title.mhz[1][0] = 1581;
    Check(target_hz(c, title, global, 0) == 1581000000u, "sys-clk: the official charger takes charging first");
    c.profile = 2;  // USB charger: USB, Charging, Handheld
    title.mhz[2][0] = 1785;
    Check(target_hz(c, title, global, 0) == 1785000000u, "sys-clk: the USB charger takes its own rate first");
    c.profile = 9;
    Check(target_hz(c, title, global, 0) == 0, "sys-clk: an unknown profile has no profile rate");
    Check(target_hz(c, title, global, 1) == 921600000, "sys-clk: an unknown profile keeps the override");
    c.enabled = 0;
    Check(target_hz(c, title, global, 1) == 0, "sys-clk disabled sets nothing");
    Check(api_trusted(4) && api_trusted(5) && !api_trusted(2) && !api_trusted(6), "sys-clk: only IPC API 4 and 5");
  }

  std::printf("%d failures\n", g_failures);
  return g_failures != 0;
}
