#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "cheats.h"

REXCVAR_DEFINE_BOOL(hcu_cheat_health, false, "HCU/Cheats", "Infinite health");
REXCVAR_DEFINE_BOOL(hcu_cheat_lives, false, "HCU/Cheats", "Infinite lives");
REXCVAR_DEFINE_BOOL(hcu_cheat_rapid_fire, false, "HCU/Cheats", "Rapid fire");
REXCVAR_DEFINE_BOOL(hcu_cheat_jump_height, false, "HCU/Cheats", "Jump height 2x");
REXCVAR_DEFINE_BOOL(hcu_cheat_keep_weapons, false, "HCU/Cheats", "Keep weapons on hit");

namespace {

constexpr uint32_t kLifeOffset = 4152;
constexpr uint32_t kStockOffset = 4320;

thread_local bool t_in_shot_cooldown_command = false;
uint32_t g_rapid_fire_carry = 0;

uint32_t Load32(const uint8_t* base, uint32_t addr) {
  uint32_t value;
  std::memcpy(&value, base + addr, sizeof(value));
  return std::byteswap(value);
}

bool IsPlayer(const uint8_t* base, uint32_t object) {
  return hcu::cheats::IsPlayer([base](uint32_t addr) { return Load32(base, addr); }, object);
}

}  // namespace

// Player::AddLife(this, float delta). Hits reach it from the shared hit handler.
REX_EXTERN(__imp__sub_8240D8D0);
REX_HOOK_RAW(sub_8240D8D0) {
  if (REXCVAR_GET(hcu_cheat_health) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = hcu::cheats::ProtectedLifeDelta(ctx.f1.f64);
  }
  __imp__sub_8240D8D0(ctx, base);
}

// Player::AddLife for script-driven changes. The kill path SetLife(0) is not hooked, so scripted deaths remain.
REX_EXTERN(__imp__sub_8240DB28);
REX_HOOK_RAW(sub_8240DB28) {
  if (REXCVAR_GET(hcu_cheat_health) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = hcu::cheats::ProtectedLifeDelta(ctx.f1.f64);
  }
  __imp__sub_8240DB28(ctx, base);
}

// Player::DecStock(this): the life taken on respawn.
REX_EXTERN(__imp__sub_8240D7D0);
REX_HOOK_RAW(sub_8240D7D0) {
  if (REXCVAR_GET(hcu_cheat_lives) && IsPlayer(base, ctx.r3.u32)) {
    return;
  }
  __imp__sub_8240D7D0(ctx, base);
}

// Player::SetStock(this, count). Co-op continue lowers the partner's stock through it.
REX_EXTERN(__imp__sub_8240E5F0);
REX_HOOK_RAW(sub_8240E5F0) {
  if (REXCVAR_GET(hcu_cheat_lives) && IsPlayer(base, ctx.r3.u32)) {
    ctx.r4.u64 = hcu::cheats::ProtectedStockValue(Load32(base, ctx.r3.u32 + kStockOffset), ctx.r4.u32);
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
  if (t_in_shot_cooldown_command && REXCVAR_GET(hcu_cheat_rapid_fire) && IsPlayer(base, ctx.r3.u32)) {
    ctx.r4.s64 = hcu::cheats::RapidFireCooldown(ctx.r4.s16, g_rapid_fire_carry);
  }
  __imp__sub_8240D778(ctx, base);
}

// Obj::Jump(this, float speed, float alternate speed, float gravity), shared by every object class.
REX_EXTERN(__imp__sub_824884C8);
REX_HOOK_RAW(sub_824884C8) {
  if (REXCVAR_GET(hcu_cheat_jump_height) && IsPlayer(base, ctx.r3.u32)) {
    ctx.f1.f64 = hcu::cheats::ScaledJumpSpeed(ctx.f1.f64);
    ctx.f2.f64 = hcu::cheats::ScaledJumpSpeed(ctx.f2.f64);
  }
  __imp__sub_824884C8(ctx, base);
}

// Player::ArmWeaponDrop(this): the hit handler arms it, and the player script then drops the weapon.
// Disarming it keeps the weapon and also stops the dropped item from appearing.
REX_EXTERN(__imp__sub_8240E8D0);
REX_EXTERN(__imp__sub_8240E8E0);
REX_HOOK_RAW(sub_8240E8D0) {
  const uint32_t self = ctx.r3.u32;
  if (REXCVAR_GET(hcu_cheat_keep_weapons) && IsPlayer(base, self) &&
      hcu::cheats::KeepsWeaponOnHit(std::bit_cast<float>(Load32(base, self + kLifeOffset)))) {
    __imp__sub_8240E8E0(ctx, base);
    return;
  }
  __imp__sub_8240E8D0(ctx, base);
}
