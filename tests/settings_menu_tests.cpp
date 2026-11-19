// START STAGE jump edge tests (src/settings_menu.cpp): a close jumps only when cheat_stage changed to 1..8 while the
// menu was open. Host only (unistd.h): g++ -std=c++20 -Isrc tests/settings_menu_tests.cpp src/settings_menu.cpp
#include <cstdio>

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

  std::printf("%d failures\n", g_failures);
  return g_failures != 0;
}
