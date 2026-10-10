// DEBUG_SAULO: temporary input diagnostics, remove during the final code review.

#include "debug_saulo_input.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

namespace {

constexpr int32_t kStickDeadzone = 7849;  // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
// Above MnK (z-order 0). Never sets handled, so the key still reaches MnK.
constexpr size_t kKeyboardLogZOrder = 1000;

struct NamedBit {
  uint16_t bit;
  const char* name;
};

constexpr NamedBit kButtons[] = {
    {0x0001, "DPAD_UP"},  {0x0002, "DPAD_DOWN"}, {0x0004, "DPAD_LEFT"}, {0x0008, "DPAD_RIGHT"},
    {0x0010, "START"},    {0x0020, "BACK"},      {0x0040, "LS_CLICK"},  {0x0080, "RS_CLICK"},
    {0x0100, "LB"},       {0x0200, "RB"},        {0x1000, "A"},         {0x2000, "B"},
    {0x4000, "X"},        {0x8000, "Y"},
};

struct PadState {
  uint32_t result = 0;
  uint16_t buttons = 0;
  bool left_trigger = false;
  bool right_trigger = false;
  int8_t lx = 0, ly = 0, rx = 0, ry = 0;
};

template <typename T>
T LoadBE(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof(v));
  return std::byteswap(v);
}

int8_t Digital(int16_t axis) {
  if (axis > kStickDeadzone) return 1;
  if (axis < -kStickDeadzone) return -1;
  return 0;
}

PadState ReadPad(uint32_t result, const uint8_t* state) {
  PadState pad{.result = result};
  if (result != 0 || !state) return pad;
  pad.buttons = LoadBE<uint16_t>(state + 4);
  pad.left_trigger = state[6] != 0;
  pad.right_trigger = state[7] != 0;
  pad.lx = Digital(LoadBE<int16_t>(state + 8));
  pad.ly = Digital(LoadBE<int16_t>(state + 10));
  pad.rx = Digital(LoadBE<int16_t>(state + 12));
  pad.ry = Digital(LoadBE<int16_t>(state + 14));
  return pad;
}

void LogTrigger(uint32_t user, const char* name, bool before, bool now) {
  if (before != now) {
    REXLOG_INFO("[DEBUG_SAULO][Input] user {} {} {}", user, name, now ? "pressed" : "released");
  }
}

void LogStick(uint32_t user, const char* name, int8_t before_x, int8_t before_y, int8_t x,
              int8_t y) {
  if (before_x != x || before_y != y) {
    REXLOG_INFO("[DEBUG_SAULO][Input] user {} {} ({},{})", user, name, static_cast<int>(x),
                static_cast<int>(y));
  }
}

void LogPadChanges(uint32_t user, const PadState& before, const PadState& now) {
  for (const NamedBit& button : kButtons) {
    if ((before.buttons ^ now.buttons) & button.bit) {
      REXLOG_INFO("[DEBUG_SAULO][Input] user {} {} {} buttons 0x{:04X}", user, button.name,
                  (now.buttons & button.bit) ? "pressed" : "released", now.buttons);
    }
  }
  LogTrigger(user, "LT", before.left_trigger, now.left_trigger);
  LogTrigger(user, "RT", before.right_trigger, now.right_trigger);
  LogStick(user, "LS", before.lx, before.ly, now.lx, now.ly);
  LogStick(user, "RS", before.rx, before.ry, now.rx, now.ry);
}

std::mutex pads_mutex;
std::map<uint32_t, std::optional<PadState>> pads_by_user;

void Report(uint32_t user, const PadState& now) {
  std::lock_guard lock(pads_mutex);
  auto& last = pads_by_user[user];
  const bool connected = now.result == 0;
  if (!last || (last->result == 0) != connected) {
    if (connected) {
      REXLOG_INFO("[DEBUG_SAULO][Input] user {} connected", user);
    } else {
      REXLOG_INFO("[DEBUG_SAULO][Input] user {} not connected, result 0x{:X}", user, now.result);
    }
  }
  if (connected) {
    LogPadChanges(user, last && last->result == 0 ? *last : PadState{}, now);
  }
  last = now;
}

class KeyboardLog final : public rex::ui::WindowInputListener {
 public:
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (!e.prev_state()) {  // skips auto-repeat
      REXLOG_INFO("[DEBUG_SAULO][Input] keyboard {} down",
                  rex::ui::VirtualKeyToString(e.virtual_key()));
    }
  }
  void OnKeyUp(rex::ui::KeyEvent& e) override {
    REXLOG_INFO("[DEBUG_SAULO][Input] keyboard {} up", rex::ui::VirtualKeyToString(e.virtual_key()));
  }
};

KeyboardLog keyboard_log;

}  // namespace

void DebugSauloAttachKeyboardLog(rex::ui::Window* window) {
  if (window) {
    window->AddInputListener(&keyboard_log, kKeyboardLogZOrder);
  }
}

REX_EXTERN(__imp__sub_82B71170);

// XDK XInputGetState(user, state) wrapper; runs the original and only inspects its output.
REX_HOOK_RAW(sub_82B71170) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t state_ptr = ctx.r4.u32;
  __imp__sub_82B71170(ctx, base);
  Report(user, ReadPad(ctx.r3.u32, state_ptr ? base + state_ptr : nullptr));
}
