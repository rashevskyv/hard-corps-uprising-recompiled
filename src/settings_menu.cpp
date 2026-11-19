// In-game settings menu: see settings_menu.h. Modelled on Conker's clx_menu.c (conker-lr-xbox).
#include "settings_menu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <unistd.h>

// The defaults are the configuration the user verified on the console (run14, build 0.294): the hidden settings are still
// read from settings.txt (A/B tests), the menu shows only the user settings.
// clear_skip and shadow_direct are 0 until a console run shows the same picture with them (none has yet). CLEAR SKIP is
// right only while every write to a target resets its clean_key (draws and resolve clears do; a write path added
// later and missed would leave a stale picture). SHADOW DIRECT is right only while no draw samples a cascade's own address
// as a texture and the larger depth image found under it is the shadow texture (a wrong match writes into another image).
HcuSettings hcu_settings = {.fps_mode = 0, .early_submit = 1, .gpu_frames = 3, .swap_images = 3, .clear_skip = 0, .shadow_direct = 0, .resolution = 0, .strip = 1, .fps_counter = 1, .rumble = 1, .shadow_bias = 0.f, .shadow_slope = 0.f,
                            .colour_bias = 0.f, .precise = 0, .filter = 0, .lod_bias = 0.f, .aniso = 1.f, .shadow_map = 1,
                            .view = 0, .view_step = 0, .dump = 0, .record = 0, .dxt_cpu = 1, .depth32 = 0, .gamma = 0,
                            .rt16 = 1, .prepass_skip = 0, .upload_strips = 0, .tex_view = 0, .pad_applet = 0, .button_glyphs = 1, .clocks = 0, .audio_threads = 0};

namespace {

std::string g_dir;
bool g_open = false;
int g_row = 0;
uint32_t g_last = 0;

// One menu line: its option names and how an option index maps to the settings.
struct Item {
  const char* label;
  const char* names[16];
  int count;
  bool at_start;  // applies at the next start
  int (*get)();
  void (*set)(int);
};

const Item kItems[] = {
    // Shown in rate order; the saved fps_mode keeps its old numbers (1, the old 60skip, now does what 2 does: shown as 60).
    {"FRAME RATE", {"30", "45", "60", "30 HALF SPEED"}, 4, false,
     [] { const int m = hcu_settings.fps_mode; return m == 4 ? 1 : m == 1 ? 2 : m; },
     [](int i) { hcu_settings.fps_mode = i == 1 ? 4 : i; }},
    {"OVERLAY", {"OFF", "ON"}, 2, false, [] { return hcu_settings.strip; }, [](int i) { hcu_settings.strip = i; }},
    {"FPS COUNTER", {"OFF", "ON"}, 2, false, [] { return hcu_settings.fps_counter; }, [](int i) { hcu_settings.fps_counter = i; }},
    {"RUMBLE", {"OFF", "ON"}, 2, false, [] { return hcu_settings.rumble; }, [](int i) { hcu_settings.rumble = i; }},
    // A crutch for GPU-bound stages, not the default: fewer pixels, a softer picture.
    {"RESOLUTION", {"NATIVE 1280X720", "1120X630", "960X540", "800X450"}, 4, true, [] { return hcu_settings.resolution; },
     [](int i) { hcu_settings.resolution = i; }},
    {"BUTTON ICONS", {"XBOX", "SWITCH"}, 2, true, [] { return hcu_settings.button_glyphs; }, [](int i) { hcu_settings.button_glyphs = i; }},
    // An action, not a value: the console's controller applet (1 or 2 players, full pads), for local 2-player.
    {"CONTROLLERS", {"PRESS A"}, 1, false, [] { return 0; }, [](int) { hcu_settings.pad_applet = 1; }},
    // The diagnostic items (biases, shadow map, prepass, gamma, 16F targets, views, dumps, precise, strips, DXT, depth
    // 32, texture view) left the menu for the release (git history before 2026-11-03); settings.txt still sets them.
};
constexpr int kCount = sizeof(kItems) / sizeof(kItems[0]);

void Save() {
  if (g_dir.empty()) return;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "w")) {
    const HcuSettings& s = hcu_settings;
    std::fprintf(f, "fps_mode=%d\nearly_submit=%d\ngpu_frames=%d\nswap_images=%d\nclear_skip=%d\nshadow_direct=%d\nresolution=%d\nstrip=%d\nfps_counter=%d\nrumble=%d\nshadow_bias=%g\nshadow_slope=%g\ncolour_bias=%g\nprecise=%d\nshadow_map=%d\nupload_strips=%d\ndxt_cpu=%d\ndepth32=%d\nprepass_skip=%d\ngamma=%d\nrt16=%d\nbutton_glyphs=%s\nclocks=%d\naudio_threads=%d\n",
                 s.fps_mode, s.early_submit, s.gpu_frames, s.swap_images, s.clear_skip, s.shadow_direct, s.resolution, s.strip, s.fps_counter, s.rumble, s.shadow_bias, s.shadow_slope, s.colour_bias, s.precise, s.shadow_map, s.upload_strips, s.dxt_cpu, s.depth32, s.prepass_skip, s.gamma, s.rt16, s.button_glyphs ? "switch" : "xbox", s.clocks, s.audio_threads);
    // The settings.txt-only keys: written only when on, so a menu save keeps them and an old file stays as it was.
    if (s.resolve_clear_key) std::fprintf(f, "resolve_clear_key=%d\n", s.resolve_clear_key);
    std::fclose(f);
  }
}

bool Exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

}  // namespace

void hcu_settings_load(const char* dir) {
  g_dir = dir ? dir : "";
  HcuSettings& s = hcu_settings;
  bool saved_fps = false;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), f)) {
      char key[32], word[16];
      float v;
      if (std::sscanf(line, "button_glyphs=%15s", word) == 1 && (!std::strcmp(word, "switch") || !std::strcmp(word, "xbox"))) {
        s.button_glyphs = word[0] == 's';
        continue;
      }
      if (std::sscanf(line, "%31[^=]=%f", key, &v) != 2) continue;
      if (!std::strcmp(key, "fps_mode")) s.fps_mode = int(v), saved_fps = true;
      else if (!std::strcmp(key, "early_submit")) s.early_submit = int(v);
      else if (!std::strcmp(key, "gpu_frames")) s.gpu_frames = int(v) == 2 ? 2 : 3;
      else if (!std::strcmp(key, "swap_images")) s.swap_images = int(v) == 2 ? 2 : 3;
      else if (!std::strcmp(key, "clear_skip")) s.clear_skip = int(v);
      else if (!std::strcmp(key, "shadow_direct")) s.shadow_direct = int(v);
      else if (!std::strcmp(key, "resolution")) s.resolution = int(v) >= 0 && int(v) <= 3 ? int(v) : 0;
      else if (!std::strcmp(key, "strip")) s.strip = int(v);
      else if (!std::strcmp(key, "fps_counter")) s.fps_counter = int(v);
      else if (!std::strcmp(key, "rumble")) s.rumble = int(v);
      else if (!std::strcmp(key, "shadow_bias")) s.shadow_bias = v;
      else if (!std::strcmp(key, "shadow_slope")) s.shadow_slope = v;
      else if (!std::strcmp(key, "colour_bias")) s.colour_bias = v;
      else if (!std::strcmp(key, "precise")) s.precise = int(v);
      else if (!std::strcmp(key, "shadow_map")) s.shadow_map = int(v);
      else if (!std::strcmp(key, "upload_strips")) s.upload_strips = int(v);
      else if (!std::strcmp(key, "dxt_cpu")) s.dxt_cpu = int(v);
      else if (!std::strcmp(key, "depth32")) s.depth32 = int(v);
      else if (!std::strcmp(key, "prepass_skip")) s.prepass_skip = int(v);  // the old key prepass did nothing: ignored
      else if (!std::strcmp(key, "gamma")) s.gamma = int(v);
      else if (!std::strcmp(key, "rt16")) s.rt16 = int(v);
      else if (!std::strcmp(key, "button_glyphs")) s.button_glyphs = int(v);
      else if (!std::strcmp(key, "tex_view")) s.tex_view = int(v);  // read only (the Linux host has no pad)
      else if (!std::strcmp(key, "clocks")) s.clocks = int(v) ? 1 : 0;  // acted on in switch_nx.cpp at start
      else if (!std::strcmp(key, "audio_threads")) s.audio_threads = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "resolve_clear_key")) s.resolve_clear_key = int(v) ? 1 : 0;
    }
    std::fclose(f);
  }
  if (s.fps_mode < 0 || s.fps_mode > 4) s.fps_mode = 0;
  // The marker files of the console A/B tests win, but fps only while settings.txt holds no fps_mode (the menu's
  // choice, saved when it closes): "30skip" (30), "45", "60skip" / "60" (60), "30" (the half-speed cap, as before).
  if (FILE* f = std::fopen((g_dir + "fps").c_str(), "r")) {
    char t[16] = {};
    if (std::fgets(t, sizeof(t), f)) {
      t[std::strcspn(t, "\r\n")] = 0;
      const int m = !std::strncmp(t, "30skip", 6) ? 0 : !std::strncmp(t, "60skip", 6) ? 1 : std::atoi(t) == 30 ? 3 : std::atoi(t) == 45 ? 4 : 2;
      if (saved_fps) std::printf("[nx] settings: the fps file (%s) is ignored: settings.txt fps_mode=%d (the menu's choice) wins\n", t, s.fps_mode);
      else {
        s.fps_mode = m;
        std::printf("[nx] settings: the fps file (%s) sets fps_mode=%d\n", t, m);
      }
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
    std::printf("[nx] menu: %s %s -> %s%s\n", it.label, it.names[was], it.names[it.get()], it.at_start ? " (saved; applies at the next start)" : "");
    std::fflush(stdout);
  }
  return 1;
}

int hcu_menu_lines(char lines[][48], int max_lines) {
  if (!g_open || max_lines < kCount + 2) return 0;  // the title, the items, the footer
  int n = 0;
  std::snprintf(lines[n++], 48, "  SETTINGS");
  for (int i = 0; i < kCount; i++) {
    const Item& it = kItems[i];
    std::snprintf(lines[n++], 48, "%c %-18s %s%s", i == g_row ? '>' : ' ', it.label, it.names[it.get()], it.at_start ? " (RESTART)" : "");
  }
  std::snprintf(lines[n++], 48, "  B CLOSE   LEFT / RIGHT / A CHANGE");
  return n;
}

// The ultimate base's SDK reads settings.txt audio_threads through this (switch/sdk-edits-base.py, src/audio/audio_system.cpp).
extern "C" int rex_hcu_audio_threads(void) { return hcu_settings.audio_threads; }
