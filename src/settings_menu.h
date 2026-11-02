// In-game settings menu (Switch; the Linux test host links it for the backend's values): both stick clicks open and
// close it. While it is open the game gets no pad input; up/down pick a line, left/right or A change it, B closes it,
// and closing saves settings.txt in the debug dir. The deko3d backend draws it (hcu_menu_lines) and reads the values.
// The old marker files (fps, overlay_off, zfix, sfix, norumble) are still read and win over settings.txt (A/B tests).
#pragma once

#include <stdint.h>

extern "C" {

struct HcuSettings {
  int fps_mode;       // 0 = 30skip, 1 = 60skip, 2 = 60 (every frame), 3 = 30 cap (the game at half speed)
  int strip;          // the version / meter strip
  int rumble;
  float shadow_bias;  // shadow-pass depth bias: units, slope (0, 0 = off)
  float shadow_slope;
  float colour_bias;  // colour-pass depth bias, units (0 = off)
  int precise;        // vertex shaders with precise outputs (at start)
  int filter;         // 0 = as the game sets them, 1 = all linear, 2 = all nearest (at start; file sfix only: linear
                      // blends the 8-bit palette indices of the sprites = wrong colours at their edges, 0.243-0.246)
  float lod_bias;     // at start
  float aniso;        // at start
  int shadow_map;     // 0 = the shadow pass is not drawn (A/B of the console rings: the map keeps its clear, all lit)
  int view;           // what is shown (not saved): 0 = the game, 1 = the scene copy before the post effects, 2 = the
                      // shadow map (the depth texture the receivers sample), 3 = STEP: the image of the frame's
                      // resolve number view_step + 1 (colour and depth resolves in order), all stretched
  int view_step;      // VIEW STEP: 0 = the first resolve of the frame
  int dump;           // DUMP NEXT FRAME: 1 = the backend writes the next drawn frame's images to dump/, then sets 0
  int record;         // RECORD 5 SEC: 1 = the backend records the shown frames for 5 s (frame_recorder.cpp), then sets 0
  int dxt_cpu;        // DXT ON CPU (at start): DXT1/3/5 textures are decoded to RGBA8 on the CPU, not copied as BC blocks
  int depth32;        // DEPTH 32 BIT (at start): every depth target and depth texture is ZF32_X24S8 instead of Z24S8
  int prepass;        // DEPTH PREPASS: 0 = the game's depth-only draws of the colour pass are skipped (A/B of the console's
                      // brick hatching: the colour pass repeats them with another vertex shader under LEQUAL)
  int upload_strips;  // 1 = textures go up in strips of 64 block rows (A/B of the console hatching; at start)
};

extern HcuSettings hcu_settings;

// Once, before the first frame: settings.txt in dir, then the marker files there.
void hcu_settings_load(const char* dir);

// Menu buttons (any pad layout): one bit each.
enum { HCU_MENU_UP = 1, HCU_MENU_DOWN = 2, HCU_MENU_LEFT = 4, HCU_MENU_RIGHT = 8, HCU_MENU_A = 16, HCU_MENU_B = 32,
       HCU_MENU_LS = 64, HCU_MENU_RS = 128 };

// Every pad poll with the held buttons. Returns 1 while the menu is open: the game must get no input then.
int hcu_menu_input(uint32_t held);

// The menu text, one line per row; 0 lines = closed. The selected row is marked.
int hcu_menu_lines(char lines[][48], int max_lines);
}
