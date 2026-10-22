// libnx pad input for the ReXGlue input system: one Xbox 360 gamepad from the console's default pad
// (handheld, a Joy-Con pair, or a Pro Controller).
#pragma once

#include <switch.h>

#include <rex/input/input_driver.h>

namespace rex::input::nx {

class NXInputDriver final : public InputDriver {
 public:
  explicit NXInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~NXInputDriver() override;

  X_STATUS Setup() override;
  void EnumerateDevices(std::vector<DeviceInfo>& out) override;
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
  PadState pad_{};
  uint32_t packet_number_ = 0;
  uint64_t last_buttons_ = 0;
};

}  // namespace rex::input::nx
