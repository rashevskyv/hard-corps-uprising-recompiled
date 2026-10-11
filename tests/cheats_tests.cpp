// Tests for the cheat guards: player classification, the life delta and the stock counter.
// Run: hcu_cheats_tests

#include <cstdint>
#include <cstdio>
#include <map>

#include "cheats.h"

using namespace hcu::cheats;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
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

}  // namespace

int main() {
  TestPlayerClassification();
  TestLifeDelta();
  TestStockValue();
  std::printf(g_failures ? "FAILED: %d\n" : "OK\n", g_failures);
  return g_failures ? 1 : 0;
}
