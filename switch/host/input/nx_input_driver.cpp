#include <rex/input/nx/nx_input_driver.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rex/input/flags.h>
#include <rex/logging.h>

#include "../../../src/settings_menu.h"

namespace rex::input::nx {

namespace {

constexpr DeviceId kPad = static_cast<DeviceId>(0x4E580000);  // "NX"

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
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad_);
  REXLOG_INFO("[nx] input: libnx pad (handheld / Joy-Con pair / Pro Controller) as gamepad 0");
  return X_STATUS_SUCCESS;
}

void NXInputDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  DeviceInfo info;
  info.id = kPad;
  info.name = "Switch pad";
  info.synthetic = false;
  out.push_back(info);
}

X_RESULT NXInputDriver::GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  (void)flags;
  if (id != kPad) {
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

X_RESULT NXInputDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  if (id != kPad) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!schedule_loaded_) LoadSchedule();
  padUpdate(&pad_);
  uint64_t held = padGetButtons(&pad_);
  uint16_t scheduled = 0;
  bool scheduled_lt = false, scheduled_rt = false;
  if (!schedule_.empty()) {
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
  // The settings menu (src/settings_menu.cpp) takes the pad while it is open: the game sees nothing held.
  {
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
  if (id != kPad) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (!vibration) return X_ERROR_SUCCESS;
  X_INPUT_VIBRATION zero{};
  if (!hcu_settings.rumble) vibration = &zero;  // also stops a rumble that was on when it was turned off
  const uint32_t styles = padGetStyleSet(&pad_);
  const bool handheld = padIsHandheld(&pad_);
  const uint32_t style = handheld                              ? HidNpadStyleTag_NpadHandheld
                         : (styles & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey
                         : (styles & HidNpadStyleTag_NpadJoyDual) ? HidNpadStyleTag_NpadJoyDual
                                                                 : 0;
  if (!style) return X_ERROR_SUCCESS;
  if (style != vib_style_) {
    if (R_FAILED(hidInitializeVibrationDevices(vib_handles_, 2, handheld ? HidNpadIdType_Handheld : HidNpadIdType_No1,
                                               static_cast<HidNpadStyleTag>(style))))
      return X_ERROR_SUCCESS;
    vib_style_ = style;
  }
  HidVibrationValue v[2];
  for (HidVibrationValue& x : v) {
    x.amp_low = float(uint16_t(vibration->left_motor_speed)) / 65535.0f;
    x.freq_low = 160.0f;
    x.amp_high = float(uint16_t(vibration->right_motor_speed)) / 65535.0f;
    x.freq_high = 320.0f;
  }
  hidSendVibrationValues(vib_handles_, v, 2);
  return X_ERROR_SUCCESS;
}

X_RESULT NXInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  (void)out_keystroke;
  return id == kPad ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace rex::input::nx
