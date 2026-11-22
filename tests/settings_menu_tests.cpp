// START STAGE jump edge tests (src/settings_menu.cpp): a close jumps only when cheat_stage changed to 1..8 while the
// menu was open. And the clocks key: hcu_clocks_key (read at start by switch/hcu_nx_base.cpp) and a menu save keeps
// clocks=1. Host only (unistd.h): g++ -std=c++20 -Isrc tests/settings_menu_tests.cpp src/settings_menu.cpp
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

#include "settings_menu.h"
#include "../switch/clock_ladders.h"
#include "../switch/sysclk_owner.h"

extern "C" float rex_hcu_xma_gain(int cls);  // src/settings_menu.cpp (the SDK's declaration): 0 sfx, 1 music, 2 voice

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
  // fps_mode=3 (the removed 30 HALF SPEED) loads as 30, and the menu shows 30; RIGHT goes 30 -> 45 -> 60 -> 30.
  WriteFile(file, "fps_mode=3\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.fps_mode == 0, "fps_mode=3 loads as 0 (30)");
  Toggle();
  {
    char lines[40][48] = {};
    const int n = hcu_menu_lines(lines, 40);
    Check(n > 1 && std::string(lines[1]).find("FRAME RATE") != std::string::npos && std::string(lines[1]).substr(21) == "30",
          "the menu shows FRAME RATE 30 for fps_mode=3");
    int seen[3] = {};
    for (int i = 0; i < 3; i++) Press(HCU_MENU_RIGHT), seen[i] = hcu_settings.fps_mode;
    Check(seen[0] == 4 && seen[1] == 2 && seen[2] == 0, "RIGHT cycles 30 (0), 45 (4), 60 (2), never 3");
  }
  Press(HCU_MENU_B);
  WriteFile(dir + "fps", "30\n");
  WriteFile(file, "rumble=1\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.fps_mode == 0, "the fps file 30 sets 0 (30)");
  unlink((dir + "fps").c_str());
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
  // sfx_gain / music_gain: 0..2 in the file, percent in the struct (clamped), saved by a menu close, the SDK's getter.
  Check(hcu_settings.sfx_gain == 80 && hcu_settings.music_gain == 100 && hcu_settings.voice_gain == 20,
        "no gain keys: sfx 80 %, music 100 %, voice 20 %");
  Check(rex_hcu_xma_gain(0) == 0.8f && rex_hcu_xma_gain(1) == 1.f && rex_hcu_xma_gain(2) == 0.2f,
        "the getter: sfx 0.8, music 1, voice 0.2 (the defaults)");
  // voice_gain: the same 0..2 range, clamp and save; a menu save keeps the voice_prefixes line as it is.
  WriteFile(file, "voice_gain=0.7\nvoice_prefixes=NR_,TB_, ZOMBIE\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.voice_gain == 70 && rex_hcu_xma_gain(2) == 0.7f, "voice_gain=0.7 is 70 %, the getter 0.7");
  WriteFile(file, "voice_gain=9\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.voice_gain == 200, "voice_gain=9 clamps to 200 %");
  WriteFile(file, "voice_gain=-3\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.voice_gain == 0, "voice_gain=-3 clamps to 0");
  {
    std::string longp = "voice_prefixes=NR_";
    for (int i = 0; i < 60; i++) longp += ",LONG_PREFIX_" + std::to_string(i);  // past the old 256-byte line buffer
    WriteFile(file, (longp + "\nvoice_gain=0.3\n").c_str());
    hcu_settings_load(dir.c_str());
    Check(hcu_settings.voice_gain == 30, "voice_gain after a long voice_prefixes line");
    hcu_settings.voice_gain = 120;
    Toggle();
    Toggle();
    hcu_settings.voice_gain = 100;
    hcu_settings_load(dir.c_str());
    Check(hcu_settings.voice_gain == 120, "a menu save and a load keep voice_gain 1.2");
    std::string text;
    char buf[2048];
    FILE* f = std::fopen(file.c_str(), "r");
    while (f && std::fgets(buf, sizeof(buf), f)) text += buf;
    if (f) std::fclose(f);
    Check(text.find(longp + "\n") != std::string::npos, "a menu save keeps the whole voice_prefixes line");
    Check(text.find("voice_gain=1.2\n") != std::string::npos, "a menu save writes voice_gain=1.2");
  }
  hcu_settings.sfx_gain = 40;
  hcu_settings.voice_gain = 40;
  WriteFile(file, "fps_mode=0\n");
  WriteFile(file, "sfx_gain=0.3\nmusic_gain=7\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 30 && hcu_settings.music_gain == 200, "sfx_gain=0.3 is 30 %, music_gain=7 clamps to 200 %");
  Check(rex_hcu_xma_gain(0) == 0.3f && rex_hcu_xma_gain(1) == 2.f, "the getter: mono 0.3, stereo 2");
  WriteFile(file, "sfx_gain=1.7\nmusic_gain=2\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 170 && hcu_settings.music_gain == 200, "sfx_gain=1.7 is 170 %, music_gain=2 is 200 %");
  // A save and a load of 1.5 / 0.4 (the file has %g: "1.5", "0.4").
  hcu_settings.sfx_gain = 150;
  hcu_settings.music_gain = 40;
  Toggle();
  Toggle();
  hcu_settings.sfx_gain = hcu_settings.music_gain = 100;
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 150 && hcu_settings.music_gain == 40, "a save and a load keep 1.5 and 0.4");
  Check(rex_hcu_xma_gain(0) == 1.5f && rex_hcu_xma_gain(1) == 0.4f, "the getter after the load: 1.5 and 0.4");
  WriteFile(file, "sfx_gain=-1\n");
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 0, "sfx_gain=-1 clamps to 0");
  hcu_settings.sfx_gain = 50;
  hcu_settings.music_gain = 80;
  Toggle();
  Toggle();
  hcu_settings.sfx_gain = hcu_settings.music_gain = 100;
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 50 && hcu_settings.music_gain == 80, "a menu save keeps sfx_gain and music_gain");
  // The SFX VOLUME line: LEFT is one 10 % step down, and the close saves it.
  hcu_settings.sfx_gain = 100;
  Toggle();
  bool on_line = false;
  for (int i = 0; i < 20 && !on_line; i++) {
    char lines[20][48];
    const int n = hcu_menu_lines(lines, 20);
    for (int k = 0; k < n; k++) on_line |= std::string(lines[k]).rfind("> SFX VOLUME", 0) == 0;
    if (!on_line) Press(HCU_MENU_DOWN);
  }
  Check(on_line, "the menu has an SFX VOLUME line");
  Press(HCU_MENU_LEFT);
  Check(hcu_settings.sfx_gain == 90, "LEFT on SFX VOLUME: 100 % -> 90 %");
  Toggle();
  hcu_settings.sfx_gain = 100;
  hcu_settings_load(dir.c_str());
  Check(hcu_settings.sfx_gain == 90, "the menu close saved sfx_gain 90 %");
  // The volume lines go to 200 % and clamp at 0 and 200 % (no wraparound). The menu opens again on the SFX VOLUME line.
  hcu_settings.sfx_gain = 100;
  Toggle();
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.sfx_gain == 110, "RIGHT on SFX VOLUME: 100 % -> 110 %");
  {
    char lines[20][48];
    const int n = hcu_menu_lines(lines, 20);
    bool shown = false;
    for (int k = 0; k < n; k++) shown |= std::string(lines[k]).find("SFX VOLUME") != std::string::npos &&
                                         std::string(lines[k]).find("110 %") != std::string::npos;
    Check(shown, "the SFX VOLUME line shows 110 %");
  }
  hcu_settings.sfx_gain = 190;
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.sfx_gain == 200, "RIGHT on SFX VOLUME: 190 % -> 200 %");
  Press(HCU_MENU_RIGHT);
  Press(HCU_MENU_A);
  Check(hcu_settings.sfx_gain == 200, "RIGHT / A on SFX VOLUME at 200 % stays 200 %");
  hcu_settings.sfx_gain = 0;
  Press(HCU_MENU_LEFT);
  Check(hcu_settings.sfx_gain == 0, "LEFT on SFX VOLUME at 0 % stays 0 %");
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.sfx_gain == 10, "RIGHT on SFX VOLUME at 0 %: 10 %");
  Press(HCU_MENU_DOWN);  // VOICE VOLUME
  {
    char lines[20][48];
    const int n = hcu_menu_lines(lines, 20);
    bool on_voice = false;
    for (int k = 0; k < n; k++) on_voice |= std::string(lines[k]).rfind("> VOICE VOLUME", 0) == 0;
    Check(on_voice, "the line below SFX VOLUME is VOICE VOLUME");
  }
  hcu_settings.voice_gain = 40;
  Press(HCU_MENU_LEFT);
  Check(hcu_settings.voice_gain == 30, "LEFT on VOICE VOLUME: 40 % -> 30 %");
  hcu_settings.voice_gain = 200;
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.voice_gain == 200, "RIGHT on VOICE VOLUME at 200 % stays 200 %");
  hcu_settings.voice_gain = 0;
  Press(HCU_MENU_LEFT);
  Check(hcu_settings.voice_gain == 0, "LEFT on VOICE VOLUME at 0 % stays 0 %");
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.voice_gain == 10, "RIGHT on VOICE VOLUME at 0 %: 10 %");
  Press(HCU_MENU_DOWN);  // MUSIC VOLUME
  hcu_settings.music_gain = 200;
  Press(HCU_MENU_RIGHT);
  Check(hcu_settings.music_gain == 200, "RIGHT on MUSIC VOLUME at 200 % stays 200 %");
  hcu_settings.music_gain = 0;
  Press(HCU_MENU_LEFT);
  Check(hcu_settings.music_gain == 0, "LEFT on MUSIC VOLUME at 0 % stays 0 %");
  Toggle();
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

  // The governor's ladder per mode and its speed (switch/clock_ladders.h).
  {
    using namespace hcu_clock;
    const auto first = [](int mode, bool docked, uint32_t cpu, uint32_t gpu) {
      const Ladder l = ladder_for(mode, docked);
      return l.rungs[0].cpu == cpu && l.rungs[0].gpu == gpu;
    };
    Check(first(0, false, 1122000, 384000), "clock: handheld 30 starts at 1122/384");
    Check(first(4, false, 1326000, 460800), "clock: handheld 45 starts at 1326/460.8");
    Check(first(1, false, 1785000, 844800) && first(2, false, 1785000, 844800), "clock: handheld 60 starts at 1785/844.8");
    Check(first(0, true, 1683000, 844800) && first(4, true, 1683000, 844800) && first(2, true, 1683000, 844800),
          "clock: docked starts at 1683/844.8 in every mode");
    for (int mode = 0; mode <= 4; mode++) {
      for (bool docked : {false, true}) {
        const Ladder l = ladder_for(mode, docked);
        bool up = l.count >= 2;
        for (int i = 1; i < l.count; i++) up = up && l.rungs[i].cpu >= l.rungs[i - 1].cpu && l.rungs[i].gpu >= l.rungs[i - 1].gpu;
        Check(up && l.rungs[l.count - 1].cpu == 1785000, "clock: every ladder climbs to CPU 1785");
      }
    }
    // Erista: GPU 460.8 on every rung of every ladder, the CPU unchanged; Mariko: the rung as it is.
    bool erista_ok = true;
    for (int mode : {0, 1, 2, 4}) {
      for (bool docked : {false, true}) {
        const Ladder l = ladder_for(mode, docked);
        for (int i = 0; i < l.count; i++) {
          const Rung e = for_soc(l.rungs[i], true), m = for_soc(l.rungs[i], false);
          erista_ok = erista_ok && e.gpu == 460800 && e.cpu == l.rungs[i].cpu && m.gpu == l.rungs[i].gpu && m.cpu == l.rungs[i].cpu;
        }
      }
    }
    Check(erista_ok, "clock: erista holds GPU 460.8 on every rung, the CPU and Mariko unchanged");
    Check(gov_speed(0, 120, 60, 2.0) == 100.0, "clock: 30 fps (every second frame drawn) is 100 %");
    Check(gov_speed(2, 120, 108, 2.0) == 90.0, "clock: 60 with busy skips counts the shown frames");
    Check(gov_speed(4, 120, 90, 2.0) == 100.0, "clock: 45 shows 45 of 60");
    Check(gov_lost(2, 100.0 * 58 / 60) > 1.99 && gov_lost(2, 100.0 * 58 / 60) < 2.01, "clock: 58 of 60 frames is 2 lost");
    Check(gov_lost(4, 100.0 * 42 / 45) > 2.99 && gov_lost(4, 100.0 * 42 / 45) < 3.01, "clock: 42 of 45 frames is 3 lost");
    Check(gov_speed(1, 108, 108, 2.0) == 90.0, "clock: 60 with a slow game counts the game speed");
  }

  std::printf("%d failures\n", g_failures);
  return g_failures != 0;
}
