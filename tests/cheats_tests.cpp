// Tests for the cheat guards: player classification, life, stock, shot cooldown, jump speed and weapon drop.
// Run: hcu_cheats_tests

#include <cstdint>
#include <cstdio>
#include <map>

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

}  // namespace

int main() {
  TestPlayerClassification();
  TestLifeDelta();
  TestStockValue();
  TestRapidFireCooldown();
  TestJumpSpeed();
  TestKeepsWeaponOnHit();
  std::printf("%s: %d checks, %d failures\n", g_failures ? "FAILED" : "OK", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
