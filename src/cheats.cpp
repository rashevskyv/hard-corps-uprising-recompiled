#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "cheats.h"

REXCVAR_DEFINE_BOOL(hcu_cheat_health, false, "HCU/Cheats", "Infinite health");
REXCVAR_DEFINE_BOOL(hcu_cheat_lives, false, "HCU/Cheats", "Infinite lives");

namespace {

constexpr uint32_t kStockOffset = 4320;

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
