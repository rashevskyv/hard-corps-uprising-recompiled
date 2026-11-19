#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>

#include "cheats.h"

namespace cheats = hcu::cheats;

#if defined(HCU_CHEATS)
// Switch, -DHCU_CHEATS=ON (flavour "-cheats"): the keys are settings.txt's (src/settings_menu.h), read live; there is
// no session menu, so the session layer only ever follows them.
#include <cstdio>

#include "settings_menu.h"

namespace {

bool SavedValue(cheats::Cheat cheat) {
  using cheats::Cheat;
  const HcuSettings& s = hcu_settings;
  switch (cheat) {
    case Cheat::Health:
      return s.cheat_health || s.cheat_god;
    case Cheat::Lives:
      return s.cheat_lives || s.cheat_god;
    case Cheat::RapidFire:
      return s.cheat_rapid_fire;
    case Cheat::KeepWeapons:
      return s.cheat_keep_weapons || s.cheat_god;
    default:
      return false;
  }
}

}  // namespace
#else
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(hcu_cheat_health, false, "HCU/Cheats", "Infinite health");
REXCVAR_DEFINE_BOOL(hcu_cheat_lives, false, "HCU/Cheats", "Infinite lives");
REXCVAR_DEFINE_BOOL(hcu_cheat_rapid_fire, false, "HCU/Cheats", "Rapid fire");
REXCVAR_DEFINE_BOOL(hcu_cheat_jump_height, false, "HCU/Cheats", "Jump height 2x");
REXCVAR_DEFINE_BOOL(hcu_cheat_keep_weapons, false, "HCU/Cheats", "Keep weapons on hit");
REXCVAR_DEFINE_BOOL(hcu_cheat_movement_speed, false, "HCU/Cheats", "Movement speed 2x");
REXCVAR_DEFINE_BOOL(hcu_cheat_super_armor, false, "HCU/Cheats", "Super armor");

namespace {

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

}  // namespace
#endif

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

#if defined(HCU_CHEATS)
// The Switch-only cheats: the locked weapon slots, the start stage, and the cheat_log diagnostics.
REX_EXTERN(__imp__sub_824738A0);
REX_EXTERN(__imp__sub_8246AEA0);
REX_EXTERN(__imp__sub_8246BD38);

namespace {

// G, the game's global record: +0 the scene change request, +24 the mode bits (bit 2: skills from the player),
// +28 the stage index (sub_824F0BF0 writes it at a new game), +136 * player the saved player records.
constexpr uint32_t kGamePointer = 0x82CB96B4;
constexpr uint32_t kSlotOffset = 4347;      // byte: the current weapon slot, 0 or 1
// word per slot: the weapon id, 0 = none, 1 MG, 2 Spread, 3 Ripple, 4 Heated Plasma, 5 Laser, 6 Crush (the muzzle
// effect table 0x823C5D78 of sub_82453A50)
constexpr uint32_t kWeaponOffset = 4352;
constexpr uint32_t kLevelOffset = 4360;     // word per slot: the weapon level
constexpr uint32_t kMaxLevelOffset = 4368;  // word: the highest level

// Inside the game's weapon set-up that the lock runs: its own calls of the hooks below are not acted on.
thread_local bool t_in_setup = false;

bool Log() {
  return hcu_settings.cheat_log != 0;
}

uint32_t Game(const uint8_t* base) {
  return Load32(base, kGamePointer);
}

// Player::HasSkill(this, n), called on a copy of the context: the caller's registers stay as they are. Only at the
// entry of a hook, where the guest stack below r1 is free.
int Skill(const PPCContext& ctx, uint8_t* base, uint32_t self, int n) {
  PPCContext c = ctx;
  c.r3.u64 = self;
  c.r4.s64 = n;
  __imp__sub_824738A0(c, base);
  return c.r3.s32;
}

// The lock of slot 0 (A) and 1 (B): weapon id 1..6 and the level key 1..3 (level 0..2), 0 = the game's.
uint32_t WantId(uint32_t slot) {
  return uint32_t(slot == 0 ? hcu_settings.cheat_weapon_a : hcu_settings.cheat_weapon_b);
}

int WantLevel(uint32_t slot) {
  return slot == 0 ? hcu_settings.cheat_level_a : hcu_settings.cheat_level_b;
}

bool Locked(uint32_t slot) {
  return WantId(slot) != 0 || WantLevel(slot) != 0;
}

bool AnyLock() {
  return Locked(0) || Locked(1);
}

// The player's current slot, or 2 for a value the game never uses.
uint32_t CurrentSlot(const uint8_t* base, uint32_t self) {
  const uint32_t slot = base[self + kSlotOffset];
  return slot <= 1 ? slot : 2;
}

// Player::HasNoWeapons (vtable +4584, sub_8246AEA0: the character HT, who carries none): the lock leaves HT alone.
// On a copy of the context, at a hook's entry or after a leaf: the guest stack below r1 is free there.
bool HasNoWeapons(const PPCContext& ctx, uint8_t* base, uint32_t self) {
  PPCContext c = ctx;
  c.r3.u64 = self;
  __imp__sub_8246AEA0(c, base);
  return c.r3.u32 != 0;
}

// A player whose current slot holds a locked weapon (or level, with `level_too`), not HT.
bool LockedPlayerSlot(const PPCContext& ctx, uint8_t* base, uint32_t self, bool level_too) {
  if (t_in_setup || !IsPlayer(base, self)) {
    return false;
  }
  const uint32_t slot = CurrentSlot(base, self);
  return slot <= 1 && (WantId(slot) != 0 || (level_too && WantLevel(slot) != 0)) && !HasNoWeapons(ctx, base, self);
}

void LogSlots(const uint8_t* base, uint32_t self) {
  std::printf(" slot=%d ids=%d,%d levels=%d,%d max=%d", int8_t(base[self + kSlotOffset]),
              int32_t(Load32(base, self + kWeaponOffset)), int32_t(Load32(base, self + kWeaponOffset + 4)),
              int32_t(Load32(base, self + kLevelOffset)), int32_t(Load32(base, self + kLevelOffset + 4)),
              int32_t(Load32(base, self + kMaxLevelOffset)));
}

void LogLife(const uint8_t* base, uint32_t object) {
  if (IsPlayer(base, object)) {
    std::printf(" player=%08X life=%g", object, LoadFloat(base, object + kLifeOffset));
    LogSlots(base, object);
  } else {
    std::printf(" object=%08X (not the player)", object);
  }
}

// The locked slots get their id and level back. True when a word changed; `new_id`: the id of the current slot did.
bool StoreLocks(uint8_t* base, uint32_t self, uint32_t current, bool& new_id) {
  bool changed = false;
  for (uint32_t slot = 0; slot < 2; ++slot) {
    if (!Locked(slot)) {
      continue;
    }
    const uint32_t id_addr = self + kWeaponOffset + 4 * slot, level_addr = self + kLevelOffset + 4 * slot;
    const uint32_t id = WantId(slot) != 0 ? WantId(slot) : Load32(base, id_addr);
    const int32_t level = id == 0 ? int32_t(Load32(base, level_addr))
                                  : cheats::LockedLevel(WantLevel(slot), int32_t(Load32(base, level_addr)),
                                                        int32_t(Load32(base, self + kMaxLevelOffset)));
    if (Load32(base, id_addr) != id) {
      Store32(base, id_addr, id);
      new_id |= slot == current;
      changed = true;
    }
    if (int32_t(Load32(base, level_addr)) != level) {
      Store32(base, level_addr, uint32_t(level));
      changed = true;
    }
  }
  return changed;
}

// A player (not HT; the caller checks): the locked slots get their id and level back. A new id in the current slot
// gets the game's level for it first (sub_8246BD38, as after a pick-up: skills 1..6 give level 1, skills 8..13 level
// 2), then a locked level is put back over it.
void AlignSlots(const PPCContext& ctx, uint8_t* base, uint32_t self) {
  const uint32_t current = CurrentSlot(base, self);
  bool new_id = false;
  if (!StoreLocks(base, self, current, new_id)) {
    return;
  }
  if (new_id) {
    PPCContext c = ctx;
    c.r3.u64 = self;
    c.r4.s64 = int8_t(Load32(base, self + kWeaponOffset + 4 * current));
    t_in_setup = true;
    __imp__sub_8246BD38(c, base);
    t_in_setup = false;
    StoreLocks(base, self, current, new_id);
  }
  if (Log()) {
    std::printf("[cheat] slots locked:");
    LogSlots(base, self);
    std::printf("\n");
  }
}

// At a hook's entry or after a call (the guest stack below r1 is free there): a player, not HT, while a key locks a
// slot.
void ApplyLocks(const PPCContext& ctx, uint8_t* base, uint32_t self) {
  if (AnyLock() && !t_in_setup && IsPlayer(base, self) && !HasNoWeapons(ctx, base, self)) {
    AlignSlots(ctx, base, self);
  }
}

}  // namespace

// Player::Update (vtable +4; it counts the shot cooldown down): the locks every frame, before the player acts.
REX_EXTERN(__imp__sub_8246A8B0);
REX_HOOK_RAW(sub_8246A8B0) {
  ApplyLocks(ctx, base, ctx.r3.u32);
  __imp__sub_8246A8B0(ctx, base);
}

// Player::ApplyWeaponSkillLevel(this, id): the current slot's level to 1 (skills 1..6) or 2 (skills 8..13) for this
// id; the pick-up and the start weapons call it. A locked level is put back after it.
REX_HOOK_RAW(sub_8246BD38) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8246BD38(ctx, base);
  ApplyLocks(ctx, base, self);
}

// Player::GiveWeapon(this, id), the pick-up: another id fills the current slot at level 0, the same id raises the
// level. Ignored for a locked current slot, except the same id while its level is the game's.
REX_EXTERN(__imp__sub_8246BC20);
REX_HOOK_RAW(sub_8246BC20) {
  const uint32_t self = ctx.r3.u32;
  if (LockedPlayerSlot(ctx, base, self, false)) {
    const uint32_t slot = CurrentSlot(base, self);
    if (ctx.r4.u32 != WantId(slot) || WantLevel(slot) != 0) {
      if (Log()) {
        std::printf("[cheat] pick-up of %d ignored (lr=%08X)\n", ctx.r4.s32, uint32_t(ctx.lr));
      }
      return;
    }
  }
  __imp__sub_8246BC20(ctx, base);
}

// Player::ClearWeapon (vtable +4824): the current slot's id to 0. Not for a locked slot.
REX_EXTERN(__imp__sub_8240DF68);
REX_HOOK_RAW(sub_8240DF68) {
  if (LockedPlayerSlot(ctx, base, ctx.r3.u32, false)) {
    return;
  }
  __imp__sub_8240DF68(ctx, base);
}

// Player::ClearLevel (vtable +4840): the current slot's level to 0. Not for a locked slot (weapon or level).
REX_EXTERN(__imp__sub_8240E0D0);
REX_HOOK_RAW(sub_8240E0D0) {
  if (LockedPlayerSlot(ctx, base, ctx.r3.u32, true)) {
    return;
  }
  __imp__sub_8240E0D0(ctx, base);
}

// Player::GiveStartWeapons(this): each empty slot gets the weapon of the first start-weapon skill (60..65: ids 1, 2,
// 5, 6, 3, 4) the player has; a slot with a weapon gets its skill level (sub_8246BD38). The locks go in first, so a
// locked slot is not empty here and the pick-up hook has no start weapon to refuse.
REX_EXTERN(__imp__sub_82473D48);
REX_HOOK_RAW(sub_82473D48) {
  const uint32_t self = ctx.r3.u32;
  ApplyLocks(ctx, base, self);
  const bool player = IsPlayer(base, self);
  if (Log()) {
    const uint32_t g = Game(base);
    std::printf("[cheat] start weapons lr=%08X G=%08X", uint32_t(ctx.lr), g);
    if (g != 0) {
      std::printf(" G0=%u G24=%08X G28=%u", Load32(base, g), Load32(base, g + 24), Load32(base, g + 28));
    }
    if (player) {
      char skills[32] = {};
      for (int n = 1; n <= 13; ++n) skills[n - 1] = Skill(ctx, base, self, n) ? '1' : '0';
      skills[13] = '/';
      for (int n = 60; n <= 65; ++n) skills[14 + n - 60] = Skill(ctx, base, self, n) ? '1' : '0';
      std::printf(" skills1-13/60-65=%s before:", skills);
      LogSlots(base, self);
    }
    std::printf("\n");
  }
  __imp__sub_82473D48(ctx, base);
  if (Log() && player) {
    std::printf("[cheat] start weapons after:");
    LogSlots(base, self);
    std::printf("\n");
  }
}

// Script commands that clear the target's current weapon (vtable +4824) and its level (+4840): logged only.
REX_EXTERN(__imp__sub_8243E648);
REX_HOOK_RAW(sub_8243E648) {
  const uint32_t lr = uint32_t(ctx.lr), owner = Load32(base, ctx.r3.u32 + 4);
  __imp__sub_8243E648(ctx, base);
  if (Log()) {
    std::printf("[cheat] clear weapon command lr=%08X", lr);
    LogLife(base, owner);
    std::printf("\n");
  }
}

REX_EXTERN(__imp__sub_8243E768);
REX_HOOK_RAW(sub_8243E768) {
  const uint32_t lr = uint32_t(ctx.lr), owner = Load32(base, ctx.r3.u32 + 4);
  __imp__sub_8243E768(ctx, base);
  if (Log()) {
    std::printf("[cheat] clear level command lr=%08X", lr);
    LogLife(base, owner);
    std::printf("\n");
  }
}

// The title / stage menu state machine. The new-game path writes G+28 (the chosen stage) and G+0 = 1.
REX_EXTERN(__imp__sub_824F0BF0);
REX_HOOK_RAW(sub_824F0BF0) {
  const uint32_t g = Game(base);
  const uint32_t g0 = g ? Load32(base, g) : 0, g28 = g ? Load32(base, g + 28) : 0;
  __imp__sub_824F0BF0(ctx, base);
  if (g == 0 || Game(base) != g) {
    return;
  }
  const uint32_t a0 = Load32(base, g), a28 = Load32(base, g + 28);
  if ((g0 == 1 || a0 != 1) && a28 == g28) {
    return;  // no scene request and no stage written in this call
  }
  const int stage = cheats::StageIndex(hcu_settings.cheat_stage);
  if (stage >= 0) {
    Store32(base, g + 28, uint32_t(stage));
  }
  if (Log()) {
    std::printf("[cheat] menu: G0 %u -> %u, G28 %u -> %u, now %u (cheat_stage=%d)\n", g0, a0, g28, a28,
                Load32(base, g + 28), hcu_settings.cheat_stage);
  }
}
#endif

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
#if defined(HCU_CHEATS)
  if (Log()) {
    std::printf("[cheat] arm weapon drop lr=%08X", uint32_t(ctx.lr));
    LogLife(base, self);
    std::printf("\n");
  }
  // A locked current slot is never dropped (the flag +4521 stays clear).
  if (LockedPlayerSlot(ctx, base, self, false)) {
    __imp__sub_8240E8E0(ctx, base);
    return;
  }
#endif
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
