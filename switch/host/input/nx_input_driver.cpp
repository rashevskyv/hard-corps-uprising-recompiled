#include <rex/input/nx/nx_input_driver.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rex/input/flags.h>
#include <rex/logging.h>

#include "../../../src/settings_menu.h"

namespace rex::input::nx {

namespace {

constexpr DeviceId kPad = static_cast<DeviceId>(0x4E580000);  // "NX": pad 1; kPad + 1 = pad 2
// The controllers a player can hold (sources_): the handheld console, wireless controllers 1 and 2.
constexpr HidNpadIdType kSourceIds[NXInputDriver::kSources] = {HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2};
constexpr const char* kSourceNames[NXInputDriver::kSources] = {"the handheld console", "controller 1", "controller 2"};

// One run of the console's controller applet: 1 or 2 players, full pads (a Joy-Con pair counts). It is libnx's
// hidLaShowControllerSupport opened up for the log: libnx folds a bad applet exit and the applet's own error into
// one code (0x5D59 on firmware 23.0.1 with build 0.300: the screen flashed and no applet came). `system` = the
// variant the HOME menu uses (it always shows its screen). False = it ended at once with an error, so no screen
// was shown.
bool RunControllerApplet(bool system) {
  HidLaControllerSupportArg arg;
  hidLaCreateControllerSupportArg(&arg);
  arg.hdr.player_count_min = 1;
  arg.hdr.player_count_max = NXInputDriver::kPads;
  const char* name = system ? "system variant" : "application variant";
  if (hosversionBefore(8, 0, 0)) {  // the older argument layout: libnx converts it
    HidLaControllerSupportResultInfo info = {};
    const Result rc = system ? hidLaShowControllerSupportForSystem(&info, &arg, true) : hidLaShowControllerSupport(&info, &arg);
    std::printf("[nx] pad: controller applet (%s): rc 0x%X, %d player(s)\n", name, rc, int(info.player_count));
    std::fflush(stdout);
    return true;
  }
  HidLaControllerSupportArgPrivate priv = {};
  priv.private_size = sizeof(priv);
  priv.arg_size = sizeof(arg);
  priv.flag0 = system;
  priv.flag1 = system;
  priv.mode = HidLaControllerSupportMode_ShowControllerSupport;
  u32 styles = 0;
  HidNpadJoyHoldType hold = HidNpadJoyHoldType_Vertical;
  hidGetSupportedNpadStyleSet(&styles);
  hidGetNpadJoyHoldType(&hold);
  priv.npad_style_set = styles;
  priv.npad_joy_hold_type = hold;
  AppletHolder holder;
  Result rc = appletCreateLibraryApplet(&holder, AppletId_LibraryAppletController, LibAppletMode_AllForeground);
  if (R_FAILED(rc)) {
    std::printf("[nx] pad: controller applet (%s): not created, rc 0x%X\n", name, rc);
    std::fflush(stdout);
    return false;
  }
  LibAppletArgs common;
  libappletArgsCreate(&common, hosversionAtLeast(11, 0, 0) ? 0x8 : 0x7);
  libappletArgsSetPlayStartupSound(&common, system);
  rc = libappletArgsPush(&common, &holder);
  if (R_SUCCEEDED(rc)) rc = libappletPushInData(&holder, &priv, sizeof(priv));
  if (R_SUCCEEDED(rc)) rc = libappletPushInData(&holder, &arg, sizeof(arg));
  const uint64_t t0 = armGetSystemTick();
  if (R_SUCCEEDED(rc)) rc = appletHolderStart(&holder);
  int reason = -1;
  HidLaControllerSupportResultInfoInternal out = {};
  size_t got = 0;
  Result pop = 0;
  if (R_SUCCEEDED(rc)) {
    appletHolderJoin(&holder);
    reason = int(appletHolderGetExitReason(&holder));
    pop = libappletPopOutData(&holder, &out, sizeof(out), &got);
  }
  appletHolderClose(&holder);
  const uint32_t ms = uint32_t(armTicksToNs(armGetSystemTick() - t0) / 1000000ull);
  const bool ok = R_SUCCEEDED(rc) && reason == LibAppletExitReason_Normal && R_SUCCEEDED(pop) && out.res == 0;
  std::printf("[nx] pad: controller applet (%s): %s after %u ms; styles 0x%X, hold %d, start rc 0x%X, exit reason %d (0 normal, "
              "1 cancelled, 2 abnormal, 10 unexpected), reply %zu bytes rc 0x%X, result %u (0 ok, 2 style not supported, "
              "other cancelled), %d player(s), selected id %u\n",
              name, ok ? "ok" : "failed", ms, styles, int(hold), rc, reason, got, pop, out.res, int(out.info.player_count),
              out.info.selected_id);
  std::fflush(stdout);
  return ok || ms >= 1500;  // a failure after that long is the player's own cancel
}

// The pad index of a device id, or -1.
int PadIndex(DeviceId id) {
  const uint64_t n = static_cast<uint64_t>(id) - static_cast<uint64_t>(kPad);
  return n < uint64_t(NXInputDriver::kPads) ? int(n) : -1;
}

// Positional mapping: the Switch's B/A/Y/X sit where the 360's A/B/X/Y sit.
struct ButtonMap {
  uint64_t nx;
  uint16_t x360;
};
constexpr ButtonMap kButtons[] = {
    {HidNpadButton_B, X_INPUT_GAMEPAD_A},
    {HidNpadButton_A, X_INPUT_GAMEPAD_B},
    {HidNpadButton_Y, X_INPUT_GAMEPAD_X},
    {HidNpadButton_X, X_INPUT_GAMEPAD_Y},
    {HidNpadButton_Plus, X_INPUT_GAMEPAD_START},
    {HidNpadButton_Minus, X_INPUT_GAMEPAD_BACK},
    {HidNpadButton_L, X_INPUT_GAMEPAD_LEFT_SHOULDER},
    {HidNpadButton_R, X_INPUT_GAMEPAD_RIGHT_SHOULDER},
    {HidNpadButton_StickL, X_INPUT_GAMEPAD_LEFT_THUMB},
    {HidNpadButton_StickR, X_INPUT_GAMEPAD_RIGHT_THUMB},
    {HidNpadButton_Up, X_INPUT_GAMEPAD_DPAD_UP},
    {HidNpadButton_Down, X_INPUT_GAMEPAD_DPAD_DOWN},
    {HidNpadButton_Left, X_INPUT_GAMEPAD_DPAD_LEFT},
    {HidNpadButton_Right, X_INPUT_GAMEPAD_DPAD_RIGHT},
};

int16_t ClampStick(int32_t v) {
  if (v > 32767) v = 32767;
  if (v < -32767) v = -32767;
  return static_cast<int16_t>(v);
}

}  // namespace

NXInputDriver::NXInputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

NXInputDriver::~NXInputDriver() = default;

X_STATUS NXInputDriver::Setup() {
  // Two players, full pads only (NpadFullCtrl = Pro Controller, Joy-Con pair, handheld): the game uses every
  // button, so a single Joy-Con is not offered (NpadStandard, used up to build 0.300, also let single Joy-Cons in).
  padConfigureInput(kPads, HidNpadStyleSet_NpadFullCtrl);
  for (int i = 0; i < kSources; i++) padInitialize(&sources_[i], kSourceIds[i]);
  REXLOG_INFO("[nx] input: the handheld console and controllers 1 and 2 (Joy-Con pair / Pro Controller) as gamepads 0 and 1");
  return X_STATUS_SUCCESS;
}

// The source that is player `pad`'s controller now. The handheld console is player 1 and the first wireless
// controller is then player 2 (up to build 0.300 one pad read both, so a Pro Controller beside the handheld console
// was a copy of player 1). Without the handheld console (docked, Joy-Cons detached), or with two wireless
// controllers, the players are controllers 1 and 2.
int NXInputDriver::SourceOf(int pad) const {
  const bool handheld = padIsConnected(&sources_[0]) && !(padIsConnected(&sources_[1]) && padIsConnected(&sources_[2]));
  return pad + (handheld ? 0 : 1);
}

// Both pads, pad 1 first: the input system numbers them in this order, and SlotAssignment gives pad n to user n.
void NXInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  for (int i = 0; i < kPads; i++) {
    DeviceInfo info;
    info.id = static_cast<DeviceId>(static_cast<uint64_t>(kPad) + i);
    info.name = i == 0 ? "Switch pad 1" : "Switch pad 2";
    info.synthetic = false;
    out.push_back(info);
  }
}

X_RESULT NXInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  (void)flags;
  const int pad = PadIndex(id);
  // Pad 2 is a device only while a controller holds it (the last GetDeviceState poll), as on the console.
  if (pad < 0 || (pad == 1 && !padIsConnected(&sources_[SourceOf(pad)]))) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
    out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

// Scheduled presses from sdmc:/switch/hcu/pad.txt, one per line: "<seconds>:<BUTTON>[+BUTTON][:<hold ms>]"
// (START BACK A B X Y LB RB LT RT UP DOWN LEFT RIGHT LS RS; the hold defaults to 200 ms), seconds
// from the driver's start. Test automation in Eden and on the console: the keyboard in Eden needs
// focus, this does not. A "#" starts a comment.
void NXInputDriver::LoadSchedule() {
  schedule_loaded_ = true;
  t0_tick_ = armGetSystemTick();
  FILE* fp = std::fopen("sdmc:/switch/hcu/pad.txt", "r");
  if (!fp) return;
  struct Name {
    const char* name;
    uint16_t bit;
  };
  static const Name names[] = {{"START", X_INPUT_GAMEPAD_START}, {"BACK", X_INPUT_GAMEPAD_BACK},
                               {"A", X_INPUT_GAMEPAD_A},         {"B", X_INPUT_GAMEPAD_B},
                               {"X", X_INPUT_GAMEPAD_X},         {"Y", X_INPUT_GAMEPAD_Y},
                               {"LB", X_INPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", X_INPUT_GAMEPAD_RIGHT_SHOULDER},
                               {"UP", X_INPUT_GAMEPAD_DPAD_UP},  {"DOWN", X_INPUT_GAMEPAD_DPAD_DOWN},
                               {"LEFT", X_INPUT_GAMEPAD_DPAD_LEFT}, {"RIGHT", X_INPUT_GAMEPAD_DPAD_RIGHT},
                               {"LS", X_INPUT_GAMEPAD_LEFT_THUMB}, {"RS", X_INPUT_GAMEPAD_RIGHT_THUMB}};
  char line[128];
  while (std::fgets(line, sizeof(line), fp)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    char* p = line;
    double seconds = std::strtod(p, &p);
    if (*p != ':') continue;
    p++;
    Scheduled s{};
    s.start_tick = t0_tick_ + armNsToTicks(uint64_t(seconds * 1e9));
    uint32_t hold_ms = 200;
    char* buttons = p;
    char* colon = std::strchr(p, ':');
    if (colon) {
      *colon = 0;
      hold_ms = uint32_t(std::strtoul(colon + 1, nullptr, 10));
    }
    for (char* tok = std::strtok(buttons, "+ \r\n"); tok; tok = std::strtok(nullptr, "+ \r\n")) {
      for (const Name& n : names) {
        if (std::strcmp(tok, n.name) == 0) s.buttons |= n.bit;
      }
      if (std::strcmp(tok, "LT") == 0) s.left_trigger = true;
      if (std::strcmp(tok, "RT") == 0) s.right_trigger = true;
    }
    s.end_tick = s.start_tick + armNsToTicks(uint64_t(hold_ms) * 1000000ull);
    schedule_.push_back(s);
  }
  std::fclose(fp);
  std::printf("[nx] pad: %zu scheduled presses from pad.txt\n", schedule_.size());
  std::fflush(stdout);
}

// The console's controller applet, opened from the settings menu (CONTROLLERS) on the next pad poll: a second
// player can pair a controller there. The application variant first; the system variant when that ends at once.
void NXInputDriver::ShowControllerApplet() {
  if (!RunControllerApplet(false)) RunControllerApplet(true);
}

X_RESULT NXInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  const int pad = PadIndex(id);
  if (pad < 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  uint32_t& packet_number_ = this->packet_number_[pad];
  uint64_t& last_buttons_ = this->last_buttons_[pad];
  if (!schedule_loaded_) LoadSchedule();
  if (pad == 0 && hcu_settings.pad_applet) {
    hcu_settings.pad_applet = 0;
    ShowControllerApplet();
  }
  for (PadState& source : sources_) padUpdate(&source);
  const int src = SourceOf(pad);
  PadState& pad_ = sources_[src];
  if (pad == 0) {  // the log: who holds which player, at each change
    const int now = 1 + src + (padIsConnected(&sources_[SourceOf(1)]) ? 8 : 0);
    if (now != logged_players_) {
      logged_players_ = now;
      std::printf("[nx] pad: connected: handheld %d, controller 1 %d, controller 2 %d; player 1 = %s, player 2 = %s\n",
                  int(padIsConnected(&sources_[0])), int(padIsConnected(&sources_[1])), int(padIsConnected(&sources_[2])),
                  kSourceNames[src], (now & 8) ? kSourceNames[SourceOf(1)] : "nobody");
      std::fflush(stdout);
    }
  }
  // Pad 2 only while a controller holds it: the game then sees player 2's pad as disconnected, as on the console.
  if (pad == 1 && !padIsConnected(&pad_)) return X_ERROR_DEVICE_NOT_CONNECTED;
  uint64_t held = padGetButtons(&pad_);
  uint16_t scheduled = 0;
  bool scheduled_lt = false, scheduled_rt = false;
  if (pad == 0 && !schedule_.empty()) {
    const uint64_t now = armGetSystemTick();
    for (Scheduled& s : schedule_) {
      if (now >= s.start_tick && now < s.end_tick) {
        scheduled |= s.buttons;
        scheduled_lt |= s.left_trigger;
        scheduled_rt |= s.right_trigger;
        if (!s.logged) {
          s.logged = true;
          std::printf("[nx] pad: scheduled press buttons 0x%04X at %llu s\n", s.buttons,
                      (unsigned long long)(armTicksToNs(now - t0_tick_) / 1000000000ull));
          std::fflush(stdout);
        }
      }
    }
    if (scheduled || scheduled_lt || scheduled_rt) held ^= 0x8000000000000000ull;  // a change: a new packet
  }
  // The settings menu (src/settings_menu.cpp) takes pad 1 while it is open: the game sees nothing held on it.
  if (pad == 0) {
    uint32_t menu = 0;
    if (held & HidNpadButton_AnyUp) menu |= HCU_MENU_UP;
    if (held & HidNpadButton_AnyDown) menu |= HCU_MENU_DOWN;
    if (held & HidNpadButton_AnyLeft) menu |= HCU_MENU_LEFT;
    if (held & HidNpadButton_AnyRight) menu |= HCU_MENU_RIGHT;
    if (held & HidNpadButton_A) menu |= HCU_MENU_A;
    if (held & HidNpadButton_B) menu |= HCU_MENU_B;
    if (held & HidNpadButton_StickL) menu |= HCU_MENU_LS;
    if (held & HidNpadButton_StickR) menu |= HCU_MENU_RS;
    // pad.txt presses (Xbox bits, by position: its A is the bottom button = Switch B).
    static const struct { uint16_t x360; uint32_t menu; } kScheduled[] = {
        {X_INPUT_GAMEPAD_DPAD_UP, HCU_MENU_UP},     {X_INPUT_GAMEPAD_DPAD_DOWN, HCU_MENU_DOWN},
        {X_INPUT_GAMEPAD_DPAD_LEFT, HCU_MENU_LEFT}, {X_INPUT_GAMEPAD_DPAD_RIGHT, HCU_MENU_RIGHT},
        {X_INPUT_GAMEPAD_B, HCU_MENU_A},            {X_INPUT_GAMEPAD_A, HCU_MENU_B},
        {X_INPUT_GAMEPAD_LEFT_THUMB, HCU_MENU_LS},  {X_INPUT_GAMEPAD_RIGHT_THUMB, HCU_MENU_RS}};
    for (const auto& m : kScheduled)
      if (scheduled & m.x360) menu |= m.menu;
    if (hcu_menu_input(menu)) {
      if (out_state) {
        std::memset(out_state, 0, sizeof(*out_state));
        if (last_buttons_ != 0) packet_number_++;
        last_buttons_ = 0;
        out_state->packet_number = packet_number_;
      }
      return X_ERROR_SUCCESS;
    }
  }
  const HidAnalogStickState left = padGetStickPos(&pad_, 0);
  const HidAnalogStickState right = padGetStickPos(&pad_, 1);
  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    uint16_t buttons = 0;
    for (const ButtonMap& m : kButtons) {
      if (held & m.nx) buttons |= m.x360;
    }
    out_state->gamepad.buttons = buttons | scheduled;
    out_state->gamepad.left_trigger = (held & HidNpadButton_ZL) || scheduled_lt ? 0xFF : 0;
    out_state->gamepad.right_trigger = (held & HidNpadButton_ZR) || scheduled_rt ? 0xFF : 0;
    out_state->gamepad.thumb_lx = ClampStick(left.x);
    out_state->gamepad.thumb_ly = ClampStick(left.y);
    out_state->gamepad.thumb_rx = ClampStick(right.x);
    out_state->gamepad.thumb_ry = ClampStick(right.y);
    if (held != last_buttons_) {
      packet_number_++;
      last_buttons_ = held;
    }
    out_state->packet_number = packet_number_;
  }
  return X_ERROR_SUCCESS;
}

// The 360's two motors on HD rumble: the left (heavy) one as the low band, the right (light) one as the high band, on
// both sides of the pad. The settings menu's RUMBLE (or the file sdmc:/switch/hcu/norumble) turns it off.
X_RESULT NXInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  const int pad = PadIndex(id);
  if (pad < 0) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (!vibration) return X_ERROR_SUCCESS;
  const int src = SourceOf(pad);
  PadState& pad_ = sources_[src];
  HidVibrationDeviceHandle* vib_handles_ = this->vib_handles_[pad];
  uint32_t& vib_key_ = this->vib_key_[pad];
  X_INPUT_VIBRATION zero{};
  if (!hcu_settings.rumble) vibration = &zero;  // also stops a rumble that was on when it was turned off
  const uint32_t styles = padGetStyleSet(&pad_);
  const bool handheld = src == 0 && padIsHandheld(&pad_);
  const uint32_t style = handheld                              ? HidNpadStyleTag_NpadHandheld
                         : (styles & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey
                         : (styles & HidNpadStyleTag_NpadJoyDual) ? HidNpadStyleTag_NpadJoyDual
                                                                 : 0;
  // The log: the first calls, every 200th, each vibration-device set-up and the first send error.
  static uint32_t calls = 0, nonzero = 0;
  static bool send_error_logged = false;
  calls++;
  if (vibration->left_motor_speed || vibration->right_motor_speed) nonzero++;
  if (calls <= 8 || calls % 200 == 0) {
    std::printf("[nx] rumble: call %u (%u with a motor on): left %u right %u, menu RUMBLE %d, pad styles 0x%X handheld %d\n", calls, nonzero,
                uint32_t(uint16_t(vibration->left_motor_speed)), uint32_t(uint16_t(vibration->right_motor_speed)), hcu_settings.rumble,
                styles, int(handheld));
  }
  if (!style) return X_ERROR_SUCCESS;
  const uint32_t key = style | uint32_t(src + 1) << 28;  // the player's controller changed: new devices
  if (key != vib_key_) {
    if (vib_key_) {  // stop the controller the player held before
      HidVibrationValue stop[2] = {{0.0f, 160.0f, 0.0f, 320.0f}, {0.0f, 160.0f, 0.0f, 320.0f}};
      hidSendVibrationValues(vib_handles_, stop, 2);
      vib_key_ = 0;
    }
    const Result rc = hidInitializeVibrationDevices(vib_handles_, 2, kSourceIds[src], static_cast<HidNpadStyleTag>(style));
    std::printf("[nx] rumble: player %d vibration devices for style 0x%X (%s): rc 0x%X\n", pad + 1, style, kSourceNames[src], rc);
    if (R_FAILED(rc)) return X_ERROR_SUCCESS;
    vib_key_ = key;
  }
  HidVibrationValue v[2];
  for (HidVibrationValue& x : v) {
    x.amp_low = float(uint16_t(vibration->left_motor_speed)) / 65535.0f;
    x.freq_low = 160.0f;
    x.amp_high = float(uint16_t(vibration->right_motor_speed)) / 65535.0f;
    x.freq_high = 320.0f;
  }
  const Result rc = hidSendVibrationValues(vib_handles_, v, 2);
  if (R_FAILED(rc) && !send_error_logged) {
    std::printf("[nx] rumble: hidSendVibrationValues rc 0x%X\n", rc);
    send_error_logged = true;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT NXInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  (void)out_keystroke;
  return PadIndex(id) >= 0 ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace rex::input::nx
