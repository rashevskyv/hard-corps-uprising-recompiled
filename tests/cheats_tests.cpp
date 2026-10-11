// Tests for the cheat guards: player classification, life, stock, shot cooldown, jump speed, weapon drop, movement
// speed, super armor and the in-game session overrides.
// Run: hcu_cheats_tests

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

#include "cheats.h"

using namespace hcu::cheats;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

// Guest words by address; unmapped addresses read as zero.
struct GuestWords {
  std::map<uint32_t, uint32_t> words;
  uint32_t operator()(uint32_t address) const {
    const auto it = words.find(address);
    return it == words.end() ? 0 : it->second;
  }
};

void TestPlayerClassification() {
  std::printf("player classification\n");
  GuestWords guest;
  guest.words[0x40001000] = kPlayerVtable;
  guest.words[0x40002000] = 0x823B1C50;  // another object class
  CHECK(IsPlayer(guest, 0x40001000));
  CHECK(!IsPlayer(guest, 0x40002000));
  CHECK(!IsPlayer(guest, 0x40003000));
  CHECK(!IsPlayer(guest, 0));
}

void TestLifeDelta() {
  std::printf("life delta\n");
  CHECK(ProtectedLifeDelta(-12500.0) == 0.0);
  CHECK(ProtectedLifeDelta(-0.001) == 0.0);
  CHECK(ProtectedLifeDelta(0.0) == 0.0);
  CHECK(ProtectedLifeDelta(12500.0) == 12500.0);
  CHECK(ProtectedLifeDelta(0.05) == 0.05);
}

void TestStockValue() {
  std::printf("stock value\n");
  CHECK(ProtectedStockValue(3, 2) == 3);
  CHECK(ProtectedStockValue(3, 0) == 3);
  CHECK(ProtectedStockValue(3, 3) == 3);
  CHECK(ProtectedStockValue(3, 4) == 4);
  CHECK(ProtectedStockValue(0, 1) == 1);
  CHECK(ProtectedStockValue(0, 0) == 0);
  CHECK(ProtectedStockValue(99, 98) == 99);
  // The game clamps anything above 99 to 99, so these do not lower a full stock.
  CHECK(ProtectedStockValue(99, 100) == 100);
  CHECK(ProtectedStockValue(99, 0xFFFFFFFFu) == 0xFFFFFFFFu);
  // ...and they raise a smaller one.
  CHECK(ProtectedStockValue(5, 0xFFFFFFFFu) == 0xFFFFFFFFu);
}

uint32_t TotalCooldown(int32_t frames, int calls) {
  uint32_t carry = 0;
  uint32_t total = 0;
  for (int i = 0; i < calls; ++i) {
    total += static_cast<uint32_t>(RapidFireCooldown(frames, carry));
  }
  return total;
}

void TestRapidFireCooldown() {
  std::printf("rapid fire cooldown\n");
  uint32_t carry = 0;
  CHECK(RapidFireCooldown(0, carry) == 0);
  CHECK(RapidFireCooldown(1, carry) == 1);
  CHECK(RapidFireCooldown(-3, carry) == -3);
  CHECK(carry == 0);
  CHECK(RapidFireCooldown(2, carry) == 1);
  CHECK(RapidFireCooldown(40, carry) == 20);
  CHECK(RapidFireCooldown(16, carry) == 8);
  CHECK(carry == 0);

  // An odd cooldown alternates so the average is exactly half.
  carry = 0;
  CHECK(RapidFireCooldown(5, carry) == 2);
  CHECK(RapidFireCooldown(5, carry) == 3);
  CHECK(RapidFireCooldown(5, carry) == 2);

  // Weapons keep their differences: the cooldown values the game's weapons use, each halved.
  for (const int32_t frames : {5, 7, 8, 9, 10, 14, 15, 16, 30, 32, 35, 40}) {
    CHECK(TotalCooldown(frames, 1000) == static_cast<uint32_t>(frames) * 500);
  }
  CHECK(TotalCooldown(40, 1) > TotalCooldown(5, 1));
  CHECK(TotalCooldown(15, 1) > TotalCooldown(9, 1));

  // The result is never below one frame for a cooldown the game set.
  carry = 1;
  CHECK(RapidFireCooldown(2, carry) >= 1);
}

// Constant gravity, one step per frame: the speed drops by g, then the position moves by the speed.
double SimulatedApex(double launch_speed, double gravity) {
  double height = 0.0;
  double apex = 0.0;
  double speed = launch_speed;
  while (speed > 0.0) {
    speed -= gravity;
    height += speed;
    apex = height > apex ? height : apex;
  }
  return apex;
}

void TestJumpSpeed() {
  std::printf("jump speed\n");
  constexpr double kGravity = 0.1419;
  CHECK(ScaledJumpSpeed(0.0) == 0.0);
  CHECK(ScaledJumpSpeed(1.0) > 1.41421 && ScaledJumpSpeed(1.0) < 1.41422);
  // The game's own launch speeds, each scaled, give about twice the apex and keep their order.
  double previous_apex = 0.0;
  for (const double speed : {2.3, 2.7, 2.9, 3.0, 3.8}) {
    const double ratio = SimulatedApex(ScaledJumpSpeed(speed), kGravity) / SimulatedApex(speed, kGravity);
    CHECK(ratio > 1.9 && ratio < 2.1);
    const double apex = SimulatedApex(ScaledJumpSpeed(speed), kGravity);
    CHECK(apex > previous_apex);
    previous_apex = apex;
  }
  // Doubling the speed instead would give about four times the height.
  CHECK(SimulatedApex(2.0 * 2.7, kGravity) / SimulatedApex(2.7, kGravity) > 3.5);
}

void TestKeepsWeaponOnHit() {
  std::printf("keep weapon on hit\n");
  CHECK(KeepsWeaponOnHit(12500.0f));
  CHECK(KeepsWeaponOnHit(0.001f));
  CHECK(!KeepsWeaponOnHit(0.0f));
  CHECK(!KeepsWeaponOnHit(-5000.0f));
}

void TestMovementSpeed() {
  std::printf("movement speed\n");
  // The speeds the game's scripts set, as the single-precision values the game stores.
  for (const float stored : {0.608f, 0.656f, 0.704f, 0.816f}) {
    const double speed = stored;
    CHECK(IsLocomotionSpeed(speed));
    CHECK(IsLocomotionSpeed(-speed));
    CHECK(ScaledMovementSpeed(speed) == speed * 2.0);
    CHECK(ScaledMovementSpeed(-speed) == -speed * 2.0);
    CHECK(static_cast<float>(ScaledMovementSpeed(speed)) == stored * 2.0f);
  }
  CHECK(ScaledMovementSpeed(0.0) == 0.0);
  CHECK(LocomotionSpeedIndex(0.608) == 0 && LocomotionSpeedIndex(-0.656) == 1);
  CHECK(LocomotionSpeedIndex(0.704) == 2 && LocomotionSpeedIndex(0.816) == 3);
  CHECK(LocomotionSpeedIndex(1.6) == -1 && LocomotionSpeedIndex(0.0) == -1);

  // Dashes, slides, drifts and the other moves keep their own speed.
  for (const double other : {0.1, -0.1, 0.492, 0.96, 1.184, 1.2, 1.28, 1.48, 1.6, 1.76, 2.0, 2.2, 4.2}) {
    CHECK(!IsLocomotionSpeed(other));
    CHECK(ScaledMovementSpeed(other) == other);
  }

  // The characters keep their proportions.
  CHECK(ScaledMovementSpeed(0.816) / ScaledMovementSpeed(0.608) == 0.816 / 0.608);
  CHECK(ScaledMovementSpeed(0.816) > ScaledMovementSpeed(0.704));
  CHECK(ScaledMovementSpeed(0.704) > ScaledMovementSpeed(0.656));
  CHECK(ScaledMovementSpeed(0.656) > ScaledMovementSpeed(0.608));

  // Walking along a slope keeps its path: both parts double.
  const Velocity2 uphill = ScaledMovementVelocity({0.656, 0.75});
  CHECK(uphill.x == 0.656 * 2.0 && uphill.y == 0.75 * 2.0);
  const Velocity2 downhill = ScaledMovementVelocity({-0.656, -0.75});
  CHECK(downhill.x == -0.656 * 2.0 && downhill.y == -0.75 * 2.0);
  const Velocity2 backward = ScaledMovementVelocity({-0.656, 0.0});
  CHECK(backward.x == -0.656 * 2.0 && backward.y == 0.0);

  // A dash and a vertical-only velocity are not touched.
  const Velocity2 dash = ScaledMovementVelocity({1.6, 1.6});
  CHECK(dash.x == 1.6 && dash.y == 1.6);
  const Velocity2 vertical = ScaledMovementVelocity({0.0, 1.0});
  CHECK(vertical.x == 0.0 && vertical.y == 1.0);
}

void TestRunSpeed() {
  std::printf("run speed\n");
  constexpr uint32_t kRun = 99;
  constexpr uint32_t kRunShooting = 164;
  constexpr uint32_t kSlide = 110;
  // Each character's own run speed doubles, and the proportion between characters stays.
  for (const double run : {1.6, 1.76}) {
    for (const uint32_t state : {99u, 101u, 102u, 103u, 164u, 168u, 170u, 173u}) {
      CHECK(IsLocomotionSpeed(run, state));
      CHECK(ScaledMovementSpeed(run, state) == run * 2.0);
      CHECK(ScaledMovementSpeed(-run, state) == -run * 2.0);
    }
  }
  CHECK(ScaledMovementSpeed(1.76, kRun) / ScaledMovementSpeed(1.6, kRun) == 1.76 / 1.6);
  CHECK(ScaledMovementSpeed(1.6, kRunShooting) > ScaledMovementSpeed(0.656, kRun));
  // Walking still doubles from any state.
  CHECK(ScaledMovementSpeed(0.656, kRun) == 0.656 * 2.0);
  CHECK(ScaledMovementSpeed(0.656) == 0.656 * 2.0);
  // The same speed outside a run state (a slide, a roll, a dash special) stays the game's own.
  for (const uint32_t state : {0u, 61u, 98u, 100u, 110u, 111u, 112u, 169u, 174u, 240u, 241u, 244u}) {
    CHECK(!IsLocomotionSpeed(1.6, state));
    CHECK(ScaledMovementSpeed(1.6, state) == 1.6);
    CHECK(ScaledMovementSpeed(1.76, state) == 1.76);
  }
  CHECK(ScaledMovementSpeed(1.6, kSlide) == 1.6);
  // Specials keep their speed even inside a run state.
  for (const double special : {1.2, 1.48, 0.96, 0.492, 1.184, 1.28, 2.0, 4.2, 0.1}) {
    CHECK(ScaledMovementSpeed(special, kRun) == special);
  }
  // A run vector doubles its horizontal part only, so a run jump's launch speed is not raised.
  const Velocity2 run_jump = ScaledMovementVelocity({1.6, 2.1}, 168);
  CHECK(run_jump.x == 3.2 && run_jump.y == 2.1);
  const Velocity2 run_back = ScaledMovementVelocity({-1.76, 0.0}, kRun);
  CHECK(run_back.x == -3.52 && run_back.y == 0.0);
  const Velocity2 slope = ScaledMovementVelocity({0.656, 0.75}, kRun);
  CHECK(slope.x == 1.312 && slope.y == 1.5);
  // The same vector outside a run state is untouched.
  const Velocity2 slide = ScaledMovementVelocity({1.6, 1.6}, kSlide);
  CHECK(slide.x == 1.6 && slide.y == 1.6);
}

void TestSuperArmor() {
  std::printf("super armor\n");
  // Off: the hit is the game's own.
  CHECK(!SuperArmorActive(false, true, 12500.0f));
  CHECK(AppliesHitKnockback(false, true));
  CHECK(AccumulatedHitDamage(5000.0, false) == 5000.0);
  CHECK(AccumulatedHitDamage(0.0, false) == 0.0);

  // On, living player: no push and the hit reads as below the stagger threshold (0).
  for (const float life : {12500.0f, 100.0f, 0.001f}) {
    const bool armored = SuperArmorActive(true, true, life);
    CHECK(armored);
    CHECK(!AppliesHitKnockback(armored, true));
    CHECK(AccumulatedHitDamage(0.0, armored) < 0.0);
    CHECK(AccumulatedHitDamage(1.0e9, armored) < 0.0);
  }
  // A push the game would not give stays absent.
  CHECK(!AppliesHitKnockback(false, false));

  // Enemies and other objects are never armored.
  CHECK(!SuperArmorActive(true, false, 12500.0f));
  CHECK(AppliesHitKnockback(SuperArmorActive(true, false, 12500.0f), true));

  // A lethal hit keeps the game's reaction and push, so the death sequence is its own.
  for (const float life : {0.0f, -1.0f, -5000.0f}) {
    const bool armored = SuperArmorActive(true, true, life);
    CHECK(!armored);
    CHECK(AppliesHitKnockback(armored, true));
    CHECK(AccumulatedHitDamage(500.0, armored) == 500.0);
  }
}

void TestSuperArmorIsIndependent() {
  std::printf("super armor is independent\n");
  SessionCheats session;
  session.Set(Cheat::SuperArmor, false, true);
  CHECK(session.Effective(Cheat::SuperArmor, false));
  for (const Cheat other : {Cheat::Health, Cheat::Lives, Cheat::RapidFire, Cheat::JumpHeight, Cheat::KeepWeapons,
                            Cheat::MovementSpeed}) {
    CHECK(!session.Effective(other, false));
    CHECK(session.Effective(other, true));
  }
  session.Set(Cheat::Health, false, true);
  session.Set(Cheat::SuperArmor, false, false);
  CHECK(session.Effective(Cheat::Health, false));
  CHECK(!session.Effective(Cheat::SuperArmor, false));

  // The armor rule reads no health, weapon, movement or jump state and changes none of their helpers.
  CHECK(ProtectedLifeDelta(-12500.0) == 0.0);
  CHECK(KeepsWeaponOnHit(12500.0f) && !KeepsWeaponOnHit(0.0f));
  CHECK(ScaledMovementSpeed(0.656) == 0.656 * 2.0);
  CHECK(ScaledJumpSpeed(2.7) > 3.8 && ScaledJumpSpeed(2.7) < 3.82);
  CHECK(kCheatCount == 7);
}

void TestHitGrace() {
  std::printf("hit grace\n");
  HitGrace grace;
  CHECK(!grace.Active(0x1000, 0.0));
  grace.Start(0x1000, 10.0);
  CHECK(grace.Active(0x1000, 10.0));
  CHECK(grace.Active(0x1000, 10.0 + kHitGraceSeconds * 0.99));
  CHECK(!grace.Active(0x1000, 10.0 + kHitGraceSeconds));
  CHECK(!grace.Active(0x2000, 10.5));
  grace.Start(0x2000, 10.5);
  CHECK(grace.Active(0x1000, 10.6) && grace.Active(0x2000, 10.6));
  grace.Start(0x1000, 20.0);
  CHECK(grace.Active(0x1000, 20.5));
  CHECK(!grace.Active(0x2000, 20.5));
  for (uint32_t victim = 0x3000; victim < 0x3000 + 0x100 * 6; victim += 0x100) {
    grace.Start(victim, 30.0);
  }
  CHECK(grace.Active(0x3000 + 0x100 * 5, 30.5));
}

bool Overridden(const SessionCheats& session, Cheat cheat) {
  return session.Effective(cheat, false) == session.Effective(cheat, true);
}

void TestSessionCheats() {
  std::printf("session cheats\n");
  constexpr Cheat kAll[] = {Cheat::Health,     Cheat::Lives,         Cheat::RapidFire,  Cheat::JumpHeight,
                            Cheat::KeepWeapons, Cheat::MovementSpeed, Cheat::SuperArmor};
  static_assert(std::size(kAll) == kCheatCount);

  SessionCheats session;
  for (const Cheat cheat : kAll) {
    CHECK(!Overridden(session, cheat));
    CHECK(!session.Effective(cheat, false));
    CHECK(session.Effective(cheat, true));
  }

  // Saved Off and turned On in the game is On; saved On and turned Off is Off.
  session.Set(Cheat::Health, false, true);
  session.Set(Cheat::Lives, true, false);
  CHECK(session.Effective(Cheat::Health, false));
  CHECK(!session.Effective(Cheat::Lives, true));
  CHECK(Overridden(session, Cheat::Health) && Overridden(session, Cheat::Lives));
  // An override does not depend on the saved value any more.
  CHECK(session.Effective(Cheat::Health, true));
  CHECK(!session.Effective(Cheat::Lives, false));

  // The others still follow the saved value.
  CHECK(!session.Effective(Cheat::RapidFire, false));
  CHECK(session.Effective(Cheat::RapidFire, true));
  CHECK(!Overridden(session, Cheat::MovementSpeed));

  // Setting the saved value again returns to the saved value.
  session.Set(Cheat::Health, false, false);
  CHECK(!session.Effective(Cheat::Health, false));
  CHECK(session.Effective(Cheat::Health, true));
  CHECK(!Overridden(session, Cheat::Health));

  // Setting the saved value is not an override, and toggling back follows the saved value again.
  session.Set(Cheat::JumpHeight, true, true);
  CHECK(!Overridden(session, Cheat::JumpHeight));
  session.Toggle(Cheat::MovementSpeed, false);
  CHECK(session.Effective(Cheat::MovementSpeed, false) && Overridden(session, Cheat::MovementSpeed));
  session.Toggle(Cheat::MovementSpeed, false);
  CHECK(!session.Effective(Cheat::MovementSpeed, false) && !Overridden(session, Cheat::MovementSpeed));
  session.Toggle(Cheat::KeepWeapons, true);
  CHECK(!session.Effective(Cheat::KeepWeapons, true) && Overridden(session, Cheat::KeepWeapons));
}

// The layer takes the saved values by value, so nothing it does can change what the launcher saved.
void TestSessionNeverChangesSavedValues() {
  std::printf("session never changes saved values\n");
  const bool saved[kCheatCount] = {false, true, false, true, false, true, false};
  bool before[kCheatCount];
  std::copy(std::begin(saved), std::end(saved), std::begin(before));

  SessionCheats session;
  for (size_t round = 0; round < 3; ++round) {
    for (size_t i = 0; i < kCheatCount; ++i) {
      session.Toggle(static_cast<Cheat>(i), saved[i]);
      CHECK(session.Effective(static_cast<Cheat>(i), saved[i]) == (round % 2 == 0 ? !saved[i] : saved[i]));
    }
  }
  for (size_t i = 0; i < kCheatCount; ++i) {
    CHECK(saved[i] == before[i]);
  }
}

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// The in-game menu and the hooks it drives only keep the session's state in memory: they have no way to the
// launcher's saved settings (GameSettings, settings.toml) or to the cvar save that ReXGlue's F4 overlay uses.
void TestMenuHasNoPersistencePath() {
  std::printf("in-game menu has no persistence path\n");
  for (const char* file : {"cheat_menu.cpp", "cheat_menu.h", "cheats.cpp", "cheats.h"}) {
    const std::string text = ReadSource(file);
    CHECK(!text.empty());
    for (const char* persistence : {"settings.h", "GameSettings", "SaveConfig", "SetFlagByName", "ResetToDefault",
                                    "REXCVAR_SET", "TextFileSettings"}) {
      CHECK(text.find(persistence) == std::string::npos);
    }
  }
}

// The armor hooks act only inside the hit handler. They must not reach the script commands or velocity setters
// that movement and jumps use, and every layer lists the cheat under its final name.
void TestSuperArmorWiring() {
  std::printf("super armor wiring\n");
  const std::string cheats = ReadSource("cheats.cpp");
  const size_t movement = cheats.find("REX_HOOK_RAW(sub_82446A00)");
  const size_t handler = cheats.find("REX_HOOK_RAW(sub_824B2728)");
  CHECK(movement != std::string::npos && handler != std::string::npos && movement < handler);
  CHECK(cheats.substr(movement, handler - movement).find("SuperArmor") == std::string::npos);
  CHECK(cheats.find("SuperArmor", handler) != std::string::npos);
  CHECK(cheats.find("REX_HOOK_RAW(sub_8240DD00)") != std::string::npos);
  CHECK(cheats.find("REX_HOOK_RAW(sub_824B4398)") != std::string::npos);
  CHECK(cheats.find("hcu_cheat_super_armor") != std::string::npos);
  CHECK(ReadSource("settings.cpp").find("\"hcu_cheat_super_armor\"") != std::string::npos);
  CHECK(ReadSource("launcher.cpp").find("\"hcu_cheat_super_armor\"") != std::string::npos);
  CHECK(ReadSource("cheat_menu.cpp").find("\"Super Armor\"") != std::string::npos);
}

void TestNoStaleKnockbackCheat() {
  std::printf("no stale knockback cheat\n");
  const std::string old_name = std::string("No") + "Knockback";
  const std::string old_label = std::string("No ") + "Knockback";
  const std::string old_key = std::string("no_") + "knockback";
  for (const char* file : {"cheats.h", "cheats.cpp", "cheat_menu.cpp", "launcher.cpp", "settings.cpp",
                           "../README.md", "../docs/launcher.md", "../docs/input.md"}) {
    const std::string text = ReadSource(file);
    CHECK(!text.empty());
    CHECK(text.find(old_name) == std::string::npos);
    CHECK(text.find(old_label) == std::string::npos);
    CHECK(text.find(old_key) == std::string::npos);
  }
}

}  // namespace

int main() {
  TestPlayerClassification();
  TestLifeDelta();
  TestStockValue();
  TestRapidFireCooldown();
  TestJumpSpeed();
  TestKeepsWeaponOnHit();
  TestMovementSpeed();
  TestRunSpeed();
  TestSuperArmor();
  TestSuperArmorIsIndependent();
  TestHitGrace();
  TestSessionCheats();
  TestSessionNeverChangesSavedValues();
  TestMenuHasNoPersistencePath();
  TestSuperArmorWiring();
  TestNoStaleKnockbackCheat();
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
