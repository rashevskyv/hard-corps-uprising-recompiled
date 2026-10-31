// In-game settings menu: see settings_menu.h. Modelled on Conker's clx_menu.c (conker-lr-xbox).
#include "settings_menu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

HcuSettings hcu_settings = {0, 1, 1, 32.f, 1.5f, -16.f, 1, 0, 0.f, 1.f, 1};

namespace {

std::string g_dir;
bool g_open = false;
int g_row = 0;
uint32_t g_last = 0;

// One menu line: its option names and how an option index maps to the settings.
struct Item {
  const char* label;
  const char* names[5];
  int count;
  bool at_start;  // applies at the next start
  int (*get)();
  void (*set)(int);
};

int NearestF(float v, const float* opts, int n) {
  int best = 0;
  for (int i = 1; i < n; i++)
    if (std::abs(opts[i] - v) < std::abs(opts[best] - v)) best = i;
  return best;
}
const float kShadow[] = {0.f, 32.f, 256.f, 1024.f};
const float kColour[] = {-16.f, 0.f};

const Item kItems[] = {
    {"FRAME RATE", {"30", "60 SKIP WHEN BUSY", "60", "30 HALF SPEED"}, 4, false,
     [] { return hcu_settings.fps_mode; }, [](int i) { hcu_settings.fps_mode = i; }},
    {"PERFORMANCE STRIP", {"OFF", "ON"}, 2, false, [] { return hcu_settings.strip; }, [](int i) { hcu_settings.strip = i; }},
    {"RUMBLE", {"OFF", "ON"}, 2, false, [] { return hcu_settings.rumble; }, [](int i) { hcu_settings.rumble = i; }},
    {"SHADOW BIAS", {"OFF", "32", "256", "1024"}, 4, false,
     [] { return hcu_settings.shadow_bias == 0.f && hcu_settings.shadow_slope == 0.f ? 0 : NearestF(hcu_settings.shadow_bias, kShadow, 4); },
     [](int i) {
       hcu_settings.shadow_bias = kShadow[i];
       hcu_settings.shadow_slope = i ? 1.5f : 0.f;
     }},
    {"COLOUR BIAS", {"-16", "OFF"}, 2, false, [] { return NearestF(hcu_settings.colour_bias, kColour, 2); },
     [](int i) { hcu_settings.colour_bias = kColour[i]; }},
    {"SHADOW MAP", {"OFF", "ON"}, 2, false, [] { return hcu_settings.shadow_map; }, [](int i) { hcu_settings.shadow_map = i; }},
    {"PRECISE SHADERS", {"OFF", "ON"}, 2, true, [] { return hcu_settings.precise; }, [](int i) { hcu_settings.precise = i; }},
    {"TEXTURE FILTER", {"GAME", "LINEAR", "NEAREST"}, 3, true, [] { return hcu_settings.filter; },
     [](int i) { hcu_settings.filter = i; }},
};
constexpr int kCount = sizeof(kItems) / sizeof(kItems[0]);

void Save() {
  if (g_dir.empty()) return;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "w")) {
    const HcuSettings& s = hcu_settings;
    std::fprintf(f, "fps_mode=%d\nstrip=%d\nrumble=%d\nshadow_bias=%g\nshadow_slope=%g\ncolour_bias=%g\nprecise=%d\nfilter=%d\nshadow_map=%d\n",
                 s.fps_mode, s.strip, s.rumble, s.shadow_bias, s.shadow_slope, s.colour_bias, s.precise, s.filter, s.shadow_map);
    std::fclose(f);
  }
}

bool Exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

}  // namespace

void hcu_settings_load(const char* dir) {
  g_dir = dir ? dir : "";
  HcuSettings& s = hcu_settings;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), f)) {
      char key[32];
      float v;
      if (std::sscanf(line, "%31[^=]=%f", key, &v) != 2) continue;
      if (!std::strcmp(key, "fps_mode")) s.fps_mode = int(v);
      else if (!std::strcmp(key, "strip")) s.strip = int(v);
      else if (!std::strcmp(key, "rumble")) s.rumble = int(v);
      else if (!std::strcmp(key, "shadow_bias")) s.shadow_bias = v;
      else if (!std::strcmp(key, "shadow_slope")) s.shadow_slope = v;
      else if (!std::strcmp(key, "colour_bias")) s.colour_bias = v;
      else if (!std::strcmp(key, "precise")) s.precise = int(v);
      else if (!std::strcmp(key, "filter")) s.filter = int(v);
      else if (!std::strcmp(key, "shadow_map")) s.shadow_map = int(v);
    }
    std::fclose(f);
  }
  // The marker files of the console A/B tests win.
  if (FILE* f = std::fopen((g_dir + "fps").c_str(), "r")) {
    char t[16] = {};
    if (std::fgets(t, sizeof(t), f)) {
      s.fps_mode = !std::strncmp(t, "30skip", 6) ? 0 : !std::strncmp(t, "60skip", 6) ? 1 : std::atoi(t) == 30 ? 3 : 2;
    }
    std::fclose(f);
  }
  if (Exists(g_dir + "overlay_off")) s.strip = 0;
  if (Exists(g_dir + "norumble")) s.rumble = 0;
  if (FILE* f = std::fopen((g_dir + "zfix").c_str(), "r")) {  // "noprecise", "sb=<units>,<slope>", "cb=<units>"
    char w[32];
    while (std::fscanf(f, "%31s", w) == 1) {
      if (!std::strcmp(w, "noprecise")) s.precise = 0;
      else if (!std::strncmp(w, "sb=", 3)) std::sscanf(w + 3, "%f,%f", &s.shadow_bias, &s.shadow_slope);
      else if (!std::strncmp(w, "cb=", 3)) s.colour_bias = float(std::atof(w + 3));
    }
    std::fclose(f);
  }
  if (FILE* f = std::fopen((g_dir + "sfix").c_str(), "r")) {  // "linear", "nearest", "lodbias=<x>", "aniso=<n>"
    char w[32];
    while (std::fscanf(f, "%31s", w) == 1) {
      if (!std::strcmp(w, "linear")) s.filter = 1;
      else if (!std::strcmp(w, "nearest")) s.filter = 2;
      else if (!std::strncmp(w, "lodbias=", 8)) s.lod_bias = float(std::atof(w + 8));
      else if (!std::strncmp(w, "aniso=", 6)) s.aniso = float(std::atof(w + 6)) < 1.f ? 1.f : float(std::atof(w + 6));
    }
    std::fclose(f);
  }
}

int hcu_menu_input(uint32_t held) {
  const uint32_t down = held & ~g_last;  // newly pressed
  g_last = held;
  const uint32_t combo = HCU_MENU_LS | HCU_MENU_RS;
  if ((held & combo) == combo && (down & combo)) {
    g_open = !g_open;
    if (!g_open) Save();
    return 1;
  }
  if (!g_open) return 0;
  if (down & HCU_MENU_B) {
    g_open = false;
    Save();
    return 1;
  }
  if (down & HCU_MENU_UP) g_row = (g_row + kCount - 1) % kCount;
  if (down & HCU_MENU_DOWN) g_row = (g_row + 1) % kCount;
  const Item& it = kItems[g_row];
  const int was = it.get();
  if (down & (HCU_MENU_RIGHT | HCU_MENU_A)) it.set((was + 1) % it.count);
  if (down & HCU_MENU_LEFT) it.set((was + it.count - 1) % it.count);
  if (it.get() != was) {
    std::printf("[nx] menu: %s %s -> %s%s\n", it.label, it.names[was], it.names[it.get()], it.at_start ? " (at the next start)" : "");
    std::fflush(stdout);
  }
  return 1;
}

int hcu_menu_lines(char lines[][48], int max_lines) {
  if (!g_open || max_lines < kCount + 3) return 0;
  int n = 0;
  std::snprintf(lines[n++], 48, "  SETTINGS");
  for (int i = 0; i < kCount; i++) {
    const Item& it = kItems[i];
    std::snprintf(lines[n++], 48, "%c %-18s %s%s", i == g_row ? '>' : ' ', it.label, it.names[it.get()], it.at_start ? " (R)" : "");
  }
  std::snprintf(lines[n++], 48, "  B CLOSE  (R) = AT NEXT START");
  return n;
}
