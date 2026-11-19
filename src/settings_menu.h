// In-game settings menu (Switch; the Linux test host links it for the backend's values): both stick clicks open and
// close it. While it is open the game gets no pad input; up/down pick a line, left/right or A change it, B closes it,
// and closing saves settings.txt in the debug dir. The deko3d backend draws it (hcu_menu_lines) and reads the values.
// The old marker files (fps, overlay_off, zfix, sfix, norumble) are still read and win over settings.txt (A/B tests).
#pragma once

#include <stdint.h>

extern "C" {

struct HcuSettings {
  int fps_mode;       // 0 = 30, 4 = 45, 1 and 2 = 60 (all skip a frame the GPU is busy for: the game keeps 100 %),
                      // 3 = 30 cap (the game at half speed)
  int early_submit;   // 1 = the scene goes to the GPU before the swapchain image is acquired (its wait is only for the
                      // present pass); 0 = the old order: the whole frame waited for vblank (18 ms frames shown at 30)
  int gpu_frames;     // frames in flight, 2 or 3 (at start): 3 = an 18 ms GPU frame is not skipped every second frame
  int swap_images;   // swapchain images, 2 or 3 (at start): 3 = a drawn frame's acquire seldom waits (up to one more
                      // frame of display latency); 2 = the build 0.331 setup
  int clear_skip;    // 1 = a target clear to the value and rect of its last clear, with no draw since, is not done (live)
  int shadow_direct;  // 1 = a depth resolve inside a larger depth image (the shadow cascades) is copied straight into it,
                      // not into its own image and then merged (live)
  int resolution;     // RESOLUTION (at start): 0 = native 1280x720, 1 = 1120x630, 2 = 960x540, 3 = 800x450; the game's
                      // 1280-wide targets are drawn at that size, the present pass scales the picture up (the HUD too)
  int strip;         // OVERLAY: the version / meter strip
  int fps_counter;    // FPS COUNTER: the fps, with the strip or alone (independent of OVERLAY)
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
  int gamma;          // GAMMA RAMP: 1 = the game's display gamma ramp in the present pass, 0 = identity (live)
  int rt16;           // SCENE RT 16F (at start): the game's 8_8_8_8 colour targets are RGBA16_Float images instead of RGBA8_Unorm
  int prepass_skip;   // DEPTH PREPASS (prepass_skip=1 in settings.txt): the game's depth-only draws of the colour pass are
                      // skipped (A/B of the console's brick hatching and GPU time: the colour pass repeats them with another
                      // vertex shader under LEQUAL). 0 = drawn, the verified picture.
  int upload_strips;  // 1 = textures go up in strips of 64 block rows (A/B of the console hatching; at start)
  int tex_view;       // TEXTURE VIEW (live, not saved; Sys alpha.w of every pixel shader): 1 = every texture fetch
                      // returns white, 2/3 = the colour is fract(first fetch coordinate x 16 / x 256): the console's
                      // hatching is in the texels (gone at 1, stripes smooth) or in the interpolated UV (stripes jagged)
  int pad_applet;     // CONTROLLERS: 1 = the pad driver opens the console's controller applet on its next poll, then sets 0
  int button_glyphs;  // BUTTON ICONS (button_glyphs=switch|xbox, or 1|0): 1 = Switch icons (our art, src/button_glyphs.cpp), 0 = the game's Xbox icons with
                      // A<->B and X<->Y swapped (read when the game opens a screen's images)
  int clocks;         // clocks=1: the game raises CPU 1785 / GPU 768 / memory 1600 MHz and keeps them (the old default);
                      // 0 (default): the clocks are not touched (stock, or what sys-clk sets), only logged. Read by
                      // switch_nx.cpp at start, before this struct is loaded; kept here so the menu's save keeps it
  int audio_threads;  // audio_threads=1 (the ultimate base's SDK only): one host thread per guest audio client
                      // (switch/sdk-edits-base.py); 0 (default): the base's one audio worker runs every client
  // The keys below are not in the initializer (0 = off) and not in the menu: settings.txt only (A/B tests).
  int resolve_clear_key;  // 1 = a resolve clear (colour, and depth with stencil 0) leaves the clean_key of a clear of
                          // that rect to that value, so CLEAR SKIP (clear_skip=1) can skip the next same clear (live)
  int trail_rearm;        // 1 = each close of the settings menu starts the GPU step trail again: frames 20 and 60 after
                          // it go to dk_gpu_times_2.txt and _3.txt (a boss scene; no 150-draw threshold)
  int cull;               // CULL (at each draw): 0 = no culling (the verified picture); 1 = the guest's cull_front /
                          // cull_back with face 0 = CCW front; 2 = the same with the other winding (A/B of the Y flip)
  int alpha_variants;     // 1 = at start, the pixel shaders without the alpha test (shader_<hash>.frag.na.dksh) are
                          // loaded and bound for the draws with alpha_test_enable 0 (no discard there)
  int shadow_target;      // 1 = SHADOW TARGET: the shadow cascades are cleared and drawn straight into their resolve
                          // destination (last frame's resolves predict it); the matching resolve copies nothing
  // Cheats (src/cheats.cpp, every NRO of switch/switch-base.cmake; the menu's CHEATS page). All 0 = off.
  int cheat_lives;         // the stock never falls
  int cheat_health;        // no damage
  int cheat_keep_weapons;  // a hit that leaves the player alive does not take the weapon
  int cheat_rapid_fire;    // the shot cooldown of the player's script is halved
  int cheat_weapon_a;      // 1..6: slot A (+4352) holds this weapon id, locked: no pick-up, drop or reset changes it;
                           // 1 MG, 2 Spread, 3 Ripple, 4 Heated Plasma, 5 Laser, 6 Crush
  int cheat_weapon_b;      // 1..6: the same for slot B (+4356)
  int cheat_level_a;       // 1..3: slot A's level (+4360) is 0..2 (key - 1), locked (the game's maximum caps it);
                           // 0 = the game's
  int cheat_level_b;       // 1..3: the same for slot B (+4364)
  int cheat_stage;         // 1..8: a new game starts at this stage
  int cheat_god;           // lives + health + keep weapons
  int cheat_log;           // 1 = the cheat hooks log what they see ([cheat] lines; diagnostic)
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
