// START STAGE jump edge tests (src/settings_menu.cpp): a close jumps only when cheat_stage changed to 1..8 while the
// menu was open. And the clocks key: hcu_clocks_key (read at start by switch/hcu_nx_base.cpp) and a menu save keeps
// clocks=1. Host only (unistd.h): g++ -std=c++20 -Isrc tests/settings_menu_tests.cpp src/settings_menu.cpp
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

#include "settings_menu.h"

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
  std::remove(file.c_str());
  rmdir(dir.c_str());

  std::printf("%d failures\n", g_failures);
  return g_failures != 0;
}
