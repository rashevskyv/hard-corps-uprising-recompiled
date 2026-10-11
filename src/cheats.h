#pragma once

#include <cstdint>

namespace hcu::cheats {

// Vtable of the game's single player class, used by all five characters in both player slots.
inline constexpr uint32_t kPlayerVtable = 0x823B3688;
// The game clamps the stock counter to this.
inline constexpr uint32_t kMaxStock = 99;

template <typename ReadGuest32>
bool IsPlayer(ReadGuest32&& read, uint32_t object) {
  return object != 0 && read(object) == kPlayerVtable;
}

// Damage reaches the player as a negative life delta; healing is positive.
inline double ProtectedLifeDelta(double delta) {
  return delta < 0.0 ? 0.0 : delta;
}

// The stock may rise but not fall. `requested` is compared as the game stores it.
inline uint32_t ProtectedStockValue(uint32_t current, uint32_t requested) {
  const uint32_t stored = requested > kMaxStock ? kMaxStock : requested;
  return stored < current ? current : requested;
}

// The game's own cooldown, in frames, is halved. `carry` keeps the odd frame so the average is exactly half.
inline int32_t RapidFireCooldown(int32_t frames, uint32_t& carry) {
  if (frames <= 1) {
    return frames;
  }
  const uint32_t total = static_cast<uint32_t>(frames) + carry;
  carry = total % 2;
  const int32_t halved = static_cast<int32_t>(total / 2);
  return halved < 1 ? 1 : halved;
}

// Gravity is constant, so the apex grows with the square of the launch speed: sqrt(2) doubles the height.
inline constexpr double kJumpSpeedScale = 1.4142135623730951;

inline double ScaledJumpSpeed(double speed) {
  return speed * kJumpSpeedScale;
}

// A hit that leaves the player alive does not take the weapon; a lethal one still does.
inline bool KeepsWeaponOnHit(float life) {
  return life > 0.0f;
}

}  // namespace hcu::cheats
