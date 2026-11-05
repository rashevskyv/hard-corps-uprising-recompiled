// libnx pad input for the ReXGlue input system: two Xbox 360 gamepads from the console's pads 1 and 2
// (handheld, a Joy-Con pair, or a Pro Controller each; a single Joy-Con is not accepted, the game
// uses every button). The settings menu's CONTROLLERS item opens the console's controller applet.
#pragma once

#include <switch.h>

#include <vector>

#include <rex/input/input_driver.h>

namespace rex::input::nx {

class NXInputDriver final : public InputDriver {
 public:
  static constexpr int kPads = 2;

  explicit NXInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~NXInputDriver() override;

  X_STATUS Setup() override;
  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
  // Scheduled presses (sdmc:/switch/hcu/pad.txt): test automation without a keyboard or a hand.
  struct Scheduled {
    uint64_t start_tick, end_tick;
    uint16_t buttons;
    bool left_trigger, right_trigger;
    bool logged;
  };
  void LoadSchedule();
  void ShowControllerApplet();
  // Rumble: the two HD rumble devices of each pad's style in use (handheld, Joy-Con pair, Pro Controller).
  HidVibrationDeviceHandle vib_handles_[kPads][2] = {};
  uint32_t vib_style_[kPads] = {};  // the style the handles were made for (0 = none yet)

  PadState pads_[kPads] = {};
  uint32_t packet_number_[kPads] = {};
  uint64_t last_buttons_[kPads] = {};
  std::vector<Scheduled> schedule_;
  uint64_t t0_tick_ = 0;
  bool schedule_loaded_ = false;
};

}  // namespace rex::input::nx
