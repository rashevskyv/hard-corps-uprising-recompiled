#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace hcu::cheats {

enum class Cheat : uint8_t { Health, Lives, RapidFire, JumpHeight, KeepWeapons, MovementSpeed, SuperArmor };
inline constexpr size_t kCheatCount = 7;

// The launcher's saved value of a cheat, or the in-game menu's override of it for this session.
// Defined in cheats.cpp.
bool Enabled(Cheat cheat);
// Never writes the saved settings.
void ToggleForSession(Cheat cheat);

// What the in-game menu changed. It only records differences from the saved value, so the saved value is
// never altered and a cheat toggled back follows the saved value again.
class SessionCheats {
 public:
  bool Effective(Cheat cheat, bool saved) const {
    const uint8_t state = states_[Index(cheat)].load(std::memory_order_relaxed);
    return state == kFollowsSaved ? saved : state == kOn;
  }

  void Set(Cheat cheat, bool saved, bool enabled) {
    states_[Index(cheat)].store(enabled == saved ? kFollowsSaved : (enabled ? kOn : kOff),
                                std::memory_order_relaxed);
  }

  void Toggle(Cheat cheat, bool saved) { Set(cheat, saved, !Effective(cheat, saved)); }

 private:
  static constexpr uint8_t kFollowsSaved = 0;
  static constexpr uint8_t kOff = 1;
  static constexpr uint8_t kOn = 2;

  static size_t Index(Cheat cheat) { return static_cast<size_t>(cheat); }

  std::array<std::atomic<uint8_t>, kCheatCount> states_{};
};

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

// Super Armor holds for a living player: a hit keeps its damage but neither pushes nor starts the hit state.
// A lethal hit stays the game's own.
inline bool SuperArmorActive(bool enabled, bool victim_is_player, float victim_life) {
  return enabled && victim_is_player && victim_life > 0.0f;
}

inline bool AppliesHitKnockback(bool armored, bool game_applies) {
  return game_applies && !armored;
}

// The player's stagger threshold is 0 and the damage taken since the last stagger never falls below it. An
// armored hit reports a value below it, which makes the hit handler skip the hit state as it does for hits an
// enemy's armor absorbs.
inline constexpr double kBelowStaggerThreshold = -1.0e30;

inline double AccumulatedHitDamage(double accumulated, bool armored) {
  return armored ? kBelowStaggerThreshold : accumulated;
}

// Without the hit state nothing stops the next hit of a volley or of sustained contact, so a player that took
// an armored hit ignores further hits for this long.
inline constexpr double kHitGraceSeconds = 1.0;

class HitGrace {
 public:
  bool Active(uint32_t victim, double now) const {
    for (const Entry& entry : entries_) {
      if (entry.victim == victim && now < entry.until) {
        return true;
      }
    }
    return false;
  }

  void Start(uint32_t victim, double now) {
    Entry* slot = &entries_[0];
    for (Entry& entry : entries_) {
      if (entry.victim == victim) {
        slot = &entry;
        break;
      }
      if (entry.until < slot->until) {
        slot = &entry;
      }
    }
    *slot = {victim, now + kHitGraceSeconds};
  }

 private:
  struct Entry {
    uint32_t victim = 0;
    double until = 0.0;
  };
  std::array<Entry, 4> entries_{};
};

inline constexpr double kMovementSpeedScale = 2.0;

// The speeds the five characters' scripts set for ordinary controllable movement: walking on the ground
// (the same for all five) and steering in the air (one per character). Dashes, slides and the other special
// moves set different values.
inline constexpr double kLocomotionSpeeds[] = {0.608, 0.656, 0.704, 0.816};

// The index into kLocomotionSpeeds of `speed` (either direction), or -1.
inline int LocomotionSpeedIndex(double speed) {
  for (size_t i = 0; i < std::size(kLocomotionSpeeds); ++i) {
    if (std::fabs(std::fabs(speed) - kLocomotionSpeeds[i]) < 1e-4) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// The speeds of the Run action, which only counts in the run states of the player script layout (a slide in
// state 110 also sets 1.6).
inline constexpr double kRunSpeeds[] = {1.6, 1.76};

inline bool IsRunSpeed(double speed) {
  for (const double run : kRunSpeeds) {
    if (std::fabs(std::fabs(speed) - run) < 1e-4) {
      return true;
    }
  }
  return false;
}

inline bool IsRunState(uint32_t state) {
  return state == 99 || (state >= 101 && state <= 103) || (state >= 164 && state <= 168) ||
         (state >= 170 && state <= 173);
}

inline bool IsLocomotionSpeed(double speed, uint32_t state = 0) {
  return LocomotionSpeedIndex(speed) >= 0 || (IsRunSpeed(speed) && IsRunState(state));
}

struct Velocity2 {
  double x;
  double y;
};

// Walking doubles both parts so the path along a slope stays the same. A run doubles only the horizontal part,
// so a run jump's launch speed is not raised. Any other velocity is left alone.
inline Velocity2 ScaledMovementVelocity(Velocity2 velocity, uint32_t state = 0) {
  if (LocomotionSpeedIndex(velocity.x) >= 0) {
    return {velocity.x * kMovementSpeedScale, velocity.y * kMovementSpeedScale};
  }
  if (IsRunSpeed(velocity.x) && IsRunState(state)) {
    return {velocity.x * kMovementSpeedScale, velocity.y};
  }
  return velocity;
}

inline double ScaledMovementSpeed(double speed, uint32_t state = 0) {
  return ScaledMovementVelocity({speed, 0.0}, state).x;
}

}  // namespace hcu::cheats
