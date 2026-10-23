#include <rex/input/nx/nx_input_driver.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rex/input/flags.h>
#include <rex/logging.h>

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

X_RESULT NXInputDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) {
  (void)vibration;
  return id == kPad ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT NXInputDriver::GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  (void)out_keystroke;
  return id == kPad ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace rex::input::nx
