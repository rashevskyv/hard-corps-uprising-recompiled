// In-game settings menu: see settings_menu.h. Modelled on Conker's clx_menu.c (conker-lr-xbox).
#include "settings_menu.h"

#include <atomic>
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
                            .rt16 = 1, .prepass_skip = 0, .upload_strips = 0, .tex_view = 0, .pad_applet = 0, .button_glyphs = 1, .clocks = 0, .audio_threads = 0,
                            .sfx_gain = 80, .music_gain = 100, .voice_gain = 20};

namespace {

std::string g_dir;
bool g_open = false;
int g_row = 0;
int g_page = 0;  // 0 = SETTINGS, 1 = CHEATS
uint32_t g_last = 0;
int g_stage_at_open = 0;     // cheat_stage when the menu opened
std::atomic<int> g_jump{0};  // hcu_take_stage_jump

// One menu line: its option names and how an option index maps to the settings.
struct Item {
  const char* label;
  const char* names[21];
  int count;
  bool at_start;  // applies at the next start
  int (*get)();
  void (*set)(int);
  bool clamp = false;  // LEFT stops at the first option, RIGHT / A at the last (no wraparound)
};

const Item kItems[] = {
    // Shown in rate order; the saved fps_mode keeps its old numbers (1, the old 60skip, now does what 2 does: shown as 60;
    // 3, the removed 30 HALF SPEED, loads as 0).
    {"FRAME RATE", {"30", "45", "60"}, 3, false,
     [] { const int m = hcu_settings.fps_mode; return m == 4 ? 1 : m == 1 || m == 2 ? 2 : 0; },
     [](int i) { hcu_settings.fps_mode = i == 1 ? 4 : i; }},
    {"OVERLAY", {"OFF", "ON"}, 2, false, [] { return hcu_settings.strip; }, [](int i) { hcu_settings.strip = i; }},
    {"FPS COUNTER", {"OFF", "ON"}, 2, false, [] { return hcu_settings.fps_counter; }, [](int i) { hcu_settings.fps_counter = i; }},
    {"RUMBLE", {"OFF", "ON"}, 2, false, [] { return hcu_settings.rumble; }, [](int i) { hcu_settings.rumble = i; }},
    // Live: the SDK reads the value at each decoded XMA frame (rex_hcu_xma_gain). A file value off the 10 % steps shows
    // as the step below it; a change snaps it to the steps. Above 100 % amplifies (saturating; loud passages can clip).
    {"SFX VOLUME", {"0 %", "10 %", "20 %", "30 %", "40 %", "50 %", "60 %", "70 %", "80 %", "90 %", "100 %", "110 %", "120 %",
                    "130 %", "140 %", "150 %", "160 %", "170 %", "180 %", "190 %", "200 %"}, 21, false,
     [] { return hcu_settings.sfx_gain / 10; }, [](int i) { hcu_settings.sfx_gain = i * 10; }, true},
    // The mono streams src/xma_voice_table.cpp knows as voices (the Voice_* banks and the voice_prefixes samples).
    {"VOICE VOLUME", {"0 %", "10 %", "20 %", "30 %", "40 %", "50 %", "60 %", "70 %", "80 %", "90 %", "100 %", "110 %", "120 %",
                      "130 %", "140 %", "150 %", "160 %", "170 %", "180 %", "190 %", "200 %"}, 21, false,
     [] { return hcu_settings.voice_gain / 10; }, [](int i) { hcu_settings.voice_gain = i * 10; }, true},
    {"MUSIC VOLUME", {"0 %", "10 %", "20 %", "30 %", "40 %", "50 %", "60 %", "70 %", "80 %", "90 %", "100 %", "110 %", "120 %",
                      "130 %", "140 %", "150 %", "160 %", "170 %", "180 %", "190 %", "200 %"}, 21, false,
     [] { return hcu_settings.music_gain / 10; }, [](int i) { hcu_settings.music_gain = i * 10; }, true},
    // A crutch for GPU-bound stages, not the default: fewer pixels, a softer picture.
    {"RESOLUTION", {"NATIVE 1280X720", "1120X630", "960X540", "800X450"}, 4, true, [] { return hcu_settings.resolution; },
     [](int i) { hcu_settings.resolution = i; }},
    {"BUTTON ICONS", {"XBOX", "SWITCH"}, 2, true, [] { return hcu_settings.button_glyphs; }, [](int i) { hcu_settings.button_glyphs = i; }},
    // An action, not a value: the console's controller applet (1 or 2 players, full pads), for local 2-player.
    {"CONTROLLERS", {"PRESS A"}, 1, false, [] { return 0; }, [](int) { hcu_settings.pad_applet = 1; }},
#if defined(HCU_CHEATS)
    // Opens the CHEATS page (kCheats): A, left or right.
    {"CHEATS", {">"}, 1, false, [] { return 0; }, [](int) { g_page = 1, g_row = 0; }},
#endif
    // The diagnostic items (biases, shadow map, prepass, gamma, 16F targets, views, dumps, precise, strips, DXT, depth
    // 32, texture view) left the menu for the release (git history before 2026-11-03); settings.txt still sets them.
};
constexpr int kCount = sizeof(kItems) / sizeof(kItems[0]);

#if defined(HCU_CHEATS)
// The CHEATS page: the keys of src/cheats.cpp. This file only sets them, the hooks read them (live). B goes back.
const Item kCheats[] = {
    {"GOD MODE", {"OFF", "ON"}, 2, false, [] { return hcu_settings.cheat_god; }, [](int i) { hcu_settings.cheat_god = i; }},
    {"INFINITE LIVES", {"OFF", "ON"}, 2, false, [] { return hcu_settings.cheat_lives; }, [](int i) { hcu_settings.cheat_lives = i; }},
    {"INFINITE HEALTH", {"OFF", "ON"}, 2, false, [] { return hcu_settings.cheat_health; }, [](int i) { hcu_settings.cheat_health = i; }},
    {"KEEP WEAPONS", {"OFF", "ON"}, 2, false, [] { return hcu_settings.cheat_keep_weapons; }, [](int i) { hcu_settings.cheat_keep_weapons = i; }},
    {"RAPID FIRE", {"OFF", "ON"}, 2, false, [] { return hcu_settings.cheat_rapid_fire; }, [](int i) { hcu_settings.cheat_rapid_fire = i; }},
    // Option i is the key's value i: weapon ids 1..6 (src/cheats.cpp, kWeaponOffset), level keys 1..3 = level 0..2.
    {"WEAPON SLOT A", {"GAME", "MG", "SPREAD", "RIPPLE", "HEATED PLASMA", "LASER", "CRUSH"}, 7, false,
     [] { return hcu_settings.cheat_weapon_a; }, [](int i) { hcu_settings.cheat_weapon_a = i; }},
    {"LEVEL SLOT A", {"GAME", "0", "1", "2"}, 4, false, [] { return hcu_settings.cheat_level_a; }, [](int i) { hcu_settings.cheat_level_a = i; }},
    {"WEAPON SLOT B", {"GAME", "MG", "SPREAD", "RIPPLE", "HEATED PLASMA", "LASER", "CRUSH"}, 7, false,
     [] { return hcu_settings.cheat_weapon_b; }, [](int i) { hcu_settings.cheat_weapon_b = i; }},
    {"LEVEL SLOT B", {"GAME", "0", "1", "2"}, 4, false, [] { return hcu_settings.cheat_level_b; }, [](int i) { hcu_settings.cheat_level_b = i; }},
    {"START STAGE", {"GAME", "1", "2", "3", "4", "5", "6", "7", "8"}, 9, false, [] { return hcu_settings.cheat_stage; },
     [](int i) { hcu_settings.cheat_stage = i; }},
};
constexpr int kCheatCount = sizeof(kCheats) / sizeof(kCheats[0]);
#else
const Item* const kCheats = nullptr;
constexpr int kCheatCount = 0;
#endif

// The lines of the page shown: 0 = SETTINGS, 1 = CHEATS.
const Item* Page(int& count) {
  count = g_page ? kCheatCount : kCount;
  return g_page ? kCheats : kItems;
}

void Save() {
  if (g_dir.empty()) return;
  // The clocks_* lines (clocks_auto, clocks_cpu, clocks_gpu, clocks_ladder*) and voice_prefixes are read by the NRO's
  // start-up code, not by this struct: every such line is copied as it is, in its order (the reader takes the last line
  // of a key).
  std::string clock_lines;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "r")) {
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
      if (std::strncmp(line, "clocks_", 7) && std::strncmp(line, "voice_prefixes=", 15)) continue;
      clock_lines += line;
      if (clock_lines.back() != '\n') clock_lines += '\n';
    }
    std::fclose(f);
  }
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "w")) {
    const HcuSettings& s = hcu_settings;
    std::fprintf(f, "fps_mode=%d\nearly_submit=%d\ngpu_frames=%d\nswap_images=%d\nclear_skip=%d\nshadow_direct=%d\nresolution=%d\nstrip=%d\nfps_counter=%d\nrumble=%d\nshadow_bias=%g\nshadow_slope=%g\ncolour_bias=%g\nprecise=%d\nshadow_map=%d\nupload_strips=%d\ndxt_cpu=%d\ndepth32=%d\nprepass_skip=%d\ngamma=%d\nrt16=%d\nbutton_glyphs=%s\nclocks=%d\naudio_threads=%d\n",
                 s.fps_mode, s.early_submit, s.gpu_frames, s.swap_images, s.clear_skip, s.shadow_direct, s.resolution, s.strip, s.fps_counter, s.rumble, s.shadow_bias, s.shadow_slope, s.colour_bias, s.precise, s.shadow_map, s.upload_strips, s.dxt_cpu, s.depth32, s.prepass_skip, s.gamma, s.rt16, s.button_glyphs ? "switch" : "xbox", s.clocks, s.audio_threads);
    std::fprintf(f, "sfx_gain=%g\nvoice_gain=%g\nmusic_gain=%g\n", s.sfx_gain / 100.0, s.voice_gain / 100.0,
                 s.music_gain / 100.0);  // always written
    // The settings.txt-only keys: written only when on, so a menu save keeps them and an old file stays as it was.
    if (s.resolve_clear_key) std::fprintf(f, "resolve_clear_key=%d\n", s.resolve_clear_key);
    if (s.trail_rearm) std::fprintf(f, "trail_rearm=%d\n", s.trail_rearm);
    if (s.cull) std::fprintf(f, "cull=%d\n", s.cull);
    if (s.alpha_variants) std::fprintf(f, "alpha_variants=%d\n", s.alpha_variants);
    if (s.shadow_target) std::fprintf(f, "shadow_target=%d\n", s.shadow_target);
    if (s.tiled_cache) std::fprintf(f, "tiled_cache=%d\n", s.tiled_cache);
    if (s.input_record) std::fprintf(f, "input_record=%d\n", s.input_record);
    if (s.input_replay) std::fprintf(f, "input_replay=%d\n", s.input_replay);
    if (s.bench_seconds) std::fprintf(f, "bench_seconds=%d\n", s.bench_seconds);
    if (s.cheat_lives) std::fprintf(f, "cheat_lives=%d\n", s.cheat_lives);
    if (s.cheat_health) std::fprintf(f, "cheat_health=%d\n", s.cheat_health);
    if (s.cheat_keep_weapons) std::fprintf(f, "cheat_keep_weapons=%d\n", s.cheat_keep_weapons);
    if (s.cheat_rapid_fire) std::fprintf(f, "cheat_rapid_fire=%d\n", s.cheat_rapid_fire);
    if (s.cheat_weapon_a) std::fprintf(f, "cheat_weapon_a=%d\n", s.cheat_weapon_a);
    if (s.cheat_weapon_b) std::fprintf(f, "cheat_weapon_b=%d\n", s.cheat_weapon_b);
    if (s.cheat_level_a) std::fprintf(f, "cheat_level_a=%d\n", s.cheat_level_a);
    if (s.cheat_level_b) std::fprintf(f, "cheat_level_b=%d\n", s.cheat_level_b);
    if (s.cheat_stage) std::fprintf(f, "cheat_stage=%d\n", s.cheat_stage);
    if (s.cheat_god) std::fprintf(f, "cheat_god=%d\n", s.cheat_god);
    if (s.cheat_log) std::fprintf(f, "cheat_log=%d\n", s.cheat_log);
    std::fputs(clock_lines.c_str(), f);
    std::fclose(f);
  }
}

bool Exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }

// sfx_gain / voice_gain / music_gain: 0..2 to whole percent; above 2 is 200, below 0 or NaN is 0.
int GainPercent(float v) { return v >= 2.f ? 200 : v > 0.f ? int(v * 100.f + 0.5f) : 0; }

// Every close: settings.txt, and a jump when START STAGE changed to a stage while the menu was open (an edge: a
// close without a change jumps nowhere, even with cheat_stage set).
void Close() {
  g_open = false;
  Save();
  const int stage = hcu_settings.cheat_stage;
  if (stage != g_stage_at_open && stage != 0) g_jump.store(stage, std::memory_order_release);
}

}  // namespace

void hcu_settings_load(const char* dir) {
  g_dir = dir ? dir : "";
  HcuSettings& s = hcu_settings;
  bool saved_fps = false;
  if (FILE* f = std::fopen((g_dir + "settings.txt").c_str(), "r")) {
    char line[1024];  // (a long voice_prefixes line, read by src/xma_voice_table.cpp, in one piece)
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
      else if (!std::strcmp(key, "clocks")) s.clocks = int(v) ? 1 : 0;  // acted on at start (hcu_clocks_key)
      else if (!std::strcmp(key, "audio_threads")) s.audio_threads = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "sfx_gain")) s.sfx_gain = GainPercent(v);
      else if (!std::strcmp(key, "music_gain")) s.music_gain = GainPercent(v);
      else if (!std::strcmp(key, "voice_gain")) s.voice_gain = GainPercent(v);
      else if (!std::strcmp(key, "resolve_clear_key")) s.resolve_clear_key = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "trail_rearm")) s.trail_rearm = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cull")) s.cull = int(v) == 1 || int(v) == 2 ? int(v) : 0;
      else if (!std::strcmp(key, "alpha_variants")) s.alpha_variants = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "shadow_target")) s.shadow_target = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "tiled_cache")) s.tiled_cache = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "input_record")) s.input_record = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "input_replay")) s.input_replay = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "bench_seconds")) s.bench_seconds = int(v) > 0 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_lives")) s.cheat_lives = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cheat_health")) s.cheat_health = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cheat_keep_weapons")) s.cheat_keep_weapons = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cheat_rapid_fire")) s.cheat_rapid_fire = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cheat_weapon_a")) s.cheat_weapon_a = int(v) >= 0 && int(v) <= 6 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_weapon_b")) s.cheat_weapon_b = int(v) >= 0 && int(v) <= 6 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_level_a")) s.cheat_level_a = int(v) >= 0 && int(v) <= 3 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_level_b")) s.cheat_level_b = int(v) >= 0 && int(v) <= 3 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_stage")) s.cheat_stage = int(v) >= 0 && int(v) <= 8 ? int(v) : 0;
      else if (!std::strcmp(key, "cheat_god")) s.cheat_god = int(v) ? 1 : 0;
      else if (!std::strcmp(key, "cheat_log")) s.cheat_log = int(v) ? 1 : 0;
    }
    std::fclose(f);
  }
  if (s.fps_mode < 0 || s.fps_mode > 4 || s.fps_mode == 3) s.fps_mode = 0;  // 3: the removed 30 HALF SPEED
  // The marker files of the console A/B tests win, but fps only while settings.txt holds no fps_mode (the menu's
  // choice, saved when it closes): "30skip" / "30" (30), "45", "60skip" / "60" (60).
  if (FILE* f = std::fopen((g_dir + "fps").c_str(), "r")) {
    char t[16] = {};
    if (std::fgets(t, sizeof(t), f)) {
      t[std::strcspn(t, "\r\n")] = 0;
      const int m = !std::strncmp(t, "30skip", 6) ? 0 : !std::strncmp(t, "60skip", 6) ? 1 : std::atoi(t) == 30 ? 0 : std::atoi(t) == 45 ? 4 : 2;
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

int hcu_clocks_key(const char* path) {
  int on = 0;
  if (FILE* f = std::fopen(path, "r")) {
    char line[64];
    int v = 0;
    while (std::fgets(line, sizeof(line), f))
      if (std::sscanf(line, "clocks=%d", &v) == 1) on = v != 0;
    std::fclose(f);
  }
  return on;
}

int hcu_menu_input(uint32_t held) {
  const uint32_t down = held & ~g_last;  // newly pressed
  g_last = held;
  const uint32_t combo = HCU_MENU_LS | HCU_MENU_RS;
  if ((held & combo) == combo && (down & combo)) {
    if (g_page) g_page = 0, g_row = kCount - 1;  // the menu opens again on SETTINGS, at the CHEATS line
    if (g_open) Close();
    else g_open = true, g_stage_at_open = hcu_settings.cheat_stage;
    return 1;
  }
  if (!g_open) return 0;
  if (down & HCU_MENU_B) {
    if (g_page) {
      g_page = 0, g_row = kCount - 1;  // back to the CHEATS line (the last of kItems)
      return 1;
    }
    Close();
    return 1;
  }
  int count;
  const Item* items = Page(count);
  if (down & HCU_MENU_UP) g_row = (g_row + count - 1) % count;
  if (down & HCU_MENU_DOWN) g_row = (g_row + 1) % count;
  const Item& it = items[g_row];
  const int was = it.get();
  if (down & (HCU_MENU_RIGHT | HCU_MENU_A)) it.set(it.clamp ? (was + 1 < it.count ? was + 1 : was) : (was + 1) % it.count);
  if (down & HCU_MENU_LEFT) it.set(it.clamp ? (was > 0 ? was - 1 : 0) : (was + it.count - 1) % it.count);
  if (it.get() != was) {
    std::printf("[nx] menu: %s %s -> %s%s\n", it.label, it.names[was], it.names[it.get()], it.at_start ? " (saved; applies at the next start)" : "");
    std::fflush(stdout);
  }
  return 1;
}

int hcu_menu_lines(char lines[][48], int max_lines) {
  int count;
  const Item* items = Page(count);
  if (!g_open || max_lines < count + 2) return 0;  // the title, the items, the footer
  int n = 0;
  std::snprintf(lines[n++], 48, g_page ? "  CHEATS" : "  SETTINGS");
  for (int i = 0; i < count; i++) {
    const Item& it = items[i];
    std::snprintf(lines[n++], 48, "%c %-18s %s%s", i == g_row ? '>' : ' ', it.label, it.names[it.get()], it.at_start ? " (RESTART)" : "");
  }
  std::snprintf(lines[n++], 48, g_page ? "  B BACK    LEFT / RIGHT / A CHANGE" : "  B CLOSE   LEFT / RIGHT / A CHANGE");
  return n;
}

int hcu_take_stage_jump(void) {
  return g_jump.load(std::memory_order_relaxed) ? g_jump.exchange(0, std::memory_order_acquire) : 0;
}

// The ultimate base's SDK reads settings.txt audio_threads through this (switch/sdk-edits-base.py, src/audio/audio_system.cpp).
extern "C" int rex_hcu_audio_threads(void) { return hcu_settings.audio_threads; }

// The SDK's XMA decoder asks this at each frame (switch/sdk-edits-base.py, src/audio/xma_context.cpp): 0..2 for the
// stream's class (src/xma_voice_table.h): 0 = sfx_gain (mono), 1 = music_gain (stereo), 2 = voice_gain (a mono sound
// the voice table knows as a voice). The menu thread writes the int; the audio thread reads it here.
extern "C" float rex_hcu_xma_gain(int cls) {
  const int* g = cls == 1 ? &hcu_settings.music_gain : cls == 2 ? &hcu_settings.voice_gain : &hcu_settings.sfx_gain;
  return __atomic_load_n(g, __ATOMIC_RELAXED) / 100.f;
}
