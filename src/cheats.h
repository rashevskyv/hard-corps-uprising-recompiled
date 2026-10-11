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

}  // namespace hcu::cheats
