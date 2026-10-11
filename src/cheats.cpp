#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "cheats.h"

namespace cheats = hcu::cheats;

REXCVAR_DEFINE_BOOL(hcu_cheat_health, false, "HCU/Cheats", "Infinite health");
REXCVAR_DEFINE_BOOL(hcu_cheat_lives, false, "HCU/Cheats", "Infinite lives");
REXCVAR_DEFINE_BOOL(hcu_cheat_rapid_fire, false, "HCU/Cheats", "Rapid fire");
REXCVAR_DEFINE_BOOL(hcu_cheat_jump_height, false, "HCU/Cheats", "Jump height 2x");
REXCVAR_DEFINE_BOOL(hcu_cheat_keep_weapons, false, "HCU/Cheats", "Keep weapons on hit");
REXCVAR_DEFINE_BOOL(hcu_cheat_movement_speed, false, "HCU/Cheats", "Movement speed 2x");
REXCVAR_DEFINE_BOOL(hcu_cheat_super_armor, false, "HCU/Cheats", "Super armor");

namespace {

constexpr uint32_t kLifeOffset = 4152;
constexpr uint32_t kStockOffset = 4320;
constexpr uint32_t kStateOffset = 284;

thread_local bool t_in_shot_cooldown_command = false;
thread_local uint32_t t_locomotion_target = 0;
thread_local uint32_t t_locomotion_state = 0;
thread_local uint32_t t_armored_victim = 0;
uint32_t g_rapid_fire_carry = 0;

cheats::SessionCheats g_session;
cheats::HitGrace g_hit_grace;

bool SavedValue(cheats::Cheat cheat) {
  using cheats::Cheat;
  switch (cheat) {
    case Cheat::Health:
      return REXCVAR_GET(hcu_cheat_health);
    case Cheat::Lives:
      return REXCVAR_GET(hcu_cheat_lives);
    case Cheat::RapidFire:
      return REXCVAR_GET(hcu_cheat_rapid_fire);
    case Cheat::JumpHeight:
      return REXCVAR_GET(hcu_cheat_jump_height);
    case Cheat::KeepWeapons:
      return REXCVAR_GET(hcu_cheat_keep_weapons);
    case Cheat::MovementSpeed:
      return REXCVAR_GET(hcu_cheat_movement_speed);
    case Cheat::SuperArmor:
      return REXCVAR_GET(hcu_cheat_super_armor);
  }
  return false;
}

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  uint32_t value;
  std::memcpy(&value, base + addr, sizeof(value));
  return std::byteswap(value);
}

void Store32(uint8_t* base, uint32_t addr, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(base + addr, &value, sizeof(value));
}

float LoadFloat(const uint8_t* base, uint32_t addr) {
  return std::bit_cast<float>(Load32(base, addr));
}

bool IsPlayer(const uint8_t* base, uint32_t object) {
  return cheats::IsPlayer([base](uint32_t addr) { return Load32(base, addr); }, object);
}

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct ArmorScope {
  explicit ArmorScope(uint32_t victim) : previous(t_armored_victim) { t_armored_victim = victim; }
  ~ArmorScope() { t_armored_victim = previous; }
  uint32_t previous;
};

// Inside the hit handler of an armored player, and still alive after the damage.
bool ArmoredAfterDamage(const uint8_t* base, uint32_t victim) {
  return victim != 0 && victim == t_armored_victim &&
         cheats::SuperArmorActive(true, true, LoadFloat(base, victim + kLifeOffset));
}

}  // namespace

namespace hcu::cheats {

bool Enabled(Cheat cheat) {
  return g_session.Effective(cheat, SavedValue(cheat));
}

void ToggleForSession(Cheat cheat) {
  g_session.Toggle(cheat, SavedValue(cheat));
}

}  // namespace hcu::cheats

// Player::AddLife(this, float delta). Hits reach it from the shared hit handler.
REX_EXTERN(__imp__sub_8240D8D0);
REX_HOOK_RAW(sub_8240D8D0) {
  if (cheats::Enabled(cheats::Cheat::Health) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = cheats::ProtectedLifeDelta(ctx.f1.f64);
  }
  __imp__sub_8240D8D0(ctx, base);
}

// Player::AddLife for script-driven changes. The kill path SetLife(0) is not hooked, so scripted deaths remain.
REX_EXTERN(__imp__sub_8240DB28);
REX_HOOK_RAW(sub_8240DB28) {
  if (cheats::Enabled(cheats::Cheat::Health) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = cheats::ProtectedLifeDelta(ctx.f1.f64);
  }
  __imp__sub_8240DB28(ctx, base);
}

// Player::DecStock(this): the life taken on respawn.
REX_EXTERN(__imp__sub_8240D7D0);
REX_HOOK_RAW(sub_8240D7D0) {
  if (cheats::Enabled(cheats::Cheat::Lives) && IsPlayer(base, ctx.r3.u32)) {
    return;
  }
  __imp__sub_8240D7D0(ctx, base);
}

// Player::SetStock(this, count). Co-op continue lowers the partner's stock through it.
REX_EXTERN(__imp__sub_8240E5F0);
REX_HOOK_RAW(sub_8240E5F0) {
  if (cheats::Enabled(cheats::Cheat::Lives) && IsPlayer(base, ctx.r3.u32)) {
    ctx.r4.u64 = cheats::ProtectedStockValue(Load32(base, ctx.r3.u32 + kStockOffset), ctx.r4.u32);
  }
  __imp__sub_8240E5F0(ctx, base);
}

// Script command that sets the player's shot cooldown. Only the value it sets is halved: the per-frame
// countdown reaches the same setter.
REX_EXTERN(__imp__sub_82436740);
REX_HOOK_RAW(sub_82436740) {
  t_in_shot_cooldown_command = true;
  __imp__sub_82436740(ctx, base);
  t_in_shot_cooldown_command = false;
}

// Player::SetShotCooldown(this, frames).
REX_EXTERN(__imp__sub_8240D778);
REX_HOOK_RAW(sub_8240D778) {
  if (t_in_shot_cooldown_command && cheats::Enabled(cheats::Cheat::RapidFire) &&
      IsPlayer(base, ctx.r3.u32)) {
    ctx.r4.s64 = cheats::RapidFireCooldown(ctx.r4.s16, g_rapid_fire_carry);
  }
  __imp__sub_8240D778(ctx, base);
}

// Obj::Jump(this, float speed, float alternate speed, float gravity), shared by every object class.
REX_EXTERN(__imp__sub_824884C8);
REX_HOOK_RAW(sub_824884C8) {
  if (cheats::Enabled(cheats::Cheat::JumpHeight) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = cheats::ScaledJumpSpeed(ctx.f1.f64);
    ctx.f2.f64 = cheats::ScaledJumpSpeed(ctx.f2.f64);
  }
  __imp__sub_824884C8(ctx, base);
}

// Player::ArmWeaponDrop(this): the hit handler arms it, and the player script then drops the weapon.
// Disarming it keeps the weapon and also stops the dropped item from appearing.
REX_EXTERN(__imp__sub_8240E8D0);
REX_EXTERN(__imp__sub_8240E8E0);
REX_HOOK_RAW(sub_8240E8D0) {
  const uint32_t self = ctx.r3.u32;
  if (cheats::Enabled(cheats::Cheat::KeepWeapons) && IsPlayer(base, self) &&
      cheats::KeepsWeaponOnHit(std::bit_cast<float>(Load32(base, self + kLifeOffset)))) {
    __imp__sub_8240E8E0(ctx, base);
    return;
  }
  __imp__sub_8240E8D0(ctx, base);
}

// Script commands 4504 (set horizontal velocity) and 4503 (set velocity). Their params are the target
// selector (0 = the script's own object) and the speed.
namespace {

// The player whose own script is setting an ordinary movement speed, or 0.
uint32_t LocomotionTarget(const uint8_t* base, uint32_t command_context) {
  if (!cheats::Enabled(cheats::Cheat::MovementSpeed)) {
    return 0;
  }
  const uint32_t params = Load32(base, command_context);
  const uint32_t owner = Load32(base, command_context + 4);
  if (Load32(base, params) != 0 || !IsPlayer(base, owner)) {
    return 0;
  }
  const float speed = LoadFloat(base, params + 4);
  const uint32_t state = Load32(base, owner + kStateOffset);
  if (!cheats::IsLocomotionSpeed(speed, state)) {
    return 0;
  }
  t_locomotion_state = state;
  return owner;
}

}  // namespace

REX_EXTERN(__imp__sub_82446A00);
REX_HOOK_RAW(sub_82446A00) {
  t_locomotion_target = LocomotionTarget(base, ctx.r3.u32);
  __imp__sub_82446A00(ctx, base);
  t_locomotion_target = 0;
}

REX_EXTERN(__imp__sub_82446960);
REX_HOOK_RAW(sub_82446960) {
  t_locomotion_target = LocomotionTarget(base, ctx.r3.u32);
  __imp__sub_82446960(ctx, base);
  t_locomotion_target = 0;
}

// Obj::SetVelocityX(this, float): only the call a locomotion command makes is doubled.
REX_EXTERN(__imp__sub_82409FF0);
REX_HOOK_RAW(sub_82409FF0) {
  if (t_locomotion_target != 0 && ctx.r3.u32 == t_locomotion_target) {
    ctx.f1.f64 = cheats::ScaledMovementSpeed(ctx.f1.f64, t_locomotion_state);
  }
  __imp__sub_82409FF0(ctx, base);
}

// Obj::SetVelocity(this, float[3]): the vector is the command's own temporary.
REX_EXTERN(__imp__sub_82409FD0);
REX_HOOK_RAW(sub_82409FD0) {
  if (t_locomotion_target != 0 && ctx.r3.u32 == t_locomotion_target) {
    const uint32_t velocity = ctx.r4.u32;
    const cheats::Velocity2 scaled = cheats::ScaledMovementVelocity(
        {LoadFloat(base, velocity), LoadFloat(base, velocity + 4)}, t_locomotion_state);
    Store32(base, velocity, std::bit_cast<uint32_t>(static_cast<float>(scaled.x)));
    Store32(base, velocity + 4, std::bit_cast<uint32_t>(static_cast<float>(scaled.y)));
  }
  __imp__sub_82409FD0(ctx, base);
}

// Hit handler (source, victim). An armored player takes the damage; the hooks below keep the handler from
// starting the hit state and from pushing, and the hits that follow right after are ignored.
REX_EXTERN(__imp__sub_824B2728);
REX_HOOK_RAW(sub_824B2728) {
  const uint32_t victim = ctx.r4.u32;
  const bool enabled = cheats::Enabled(cheats::Cheat::SuperArmor);
  const bool player = enabled && IsPlayer(base, victim);
  const float life = player ? LoadFloat(base, victim + kLifeOffset) : 0.0f;
  if (!cheats::SuperArmorActive(enabled, player, life)) {
    __imp__sub_824B2728(ctx, base);
    return;
  }
  const double now = NowSeconds();
  if (g_hit_grace.Active(victim, now)) {
    ctx.r3.u64 = 0;
    return;
  }
  {
    ArmorScope scope(victim);
    __imp__sub_824B2728(ctx, base);
  }
  if (ctx.r3.u32 != 0 && LoadFloat(base, victim + kLifeOffset) > 0.0f) {
    g_hit_grace.Start(victim, now);
  }
}

// Damage taken since the last stagger, which the handler compares with the stagger threshold.
REX_EXTERN(__imp__sub_8240DD00);
REX_HOOK_RAW(sub_8240DD00) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8240DD00(ctx, base);
  ctx.f1.f64 = cheats::AccumulatedHitDamage(ctx.f1.f64, ArmoredAfterDamage(base, self));
}

// Hit handler predicate: whether the victim gets the hit's velocity, decay and gravity.
REX_EXTERN(__imp__sub_824B4398);
REX_HOOK_RAW(sub_824B4398) {
  const uint32_t victim = ctx.r4.u32;
  __imp__sub_824B4398(ctx, base);
  if (!cheats::AppliesHitKnockback(ArmoredAfterDamage(base, victim), ctx.r3.u32 != 0)) {
    ctx.r3.u64 = 0;
  }
}
