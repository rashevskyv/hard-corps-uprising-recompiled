// The input replay lookup (src/input_replay.h): which record the game gets at each swap.
// Run: hcu_input_replay_tests

#include <cstdio>

#include "input_replay.h"

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

struct Rec {
  uint32_t swap;
};

}  // namespace

int main() {
  // Two polls in swap 5 (the later one wins), none in swaps 7-9 (swap 6's state holds), the last at swap 10.
  const Rec r[] = {{3}, {5}, {5}, {6}, {10}};
  size_t at = 0;
  CHECK(hcu::ReplayIndex(r, 5, at, 1) == -1);
  CHECK(hcu::ReplayIndex(r, 5, at, 3) == 0);
  CHECK(hcu::ReplayIndex(r, 5, at, 4) == 0);
  CHECK(hcu::ReplayIndex(r, 5, at, 5) == 2);
  CHECK(hcu::ReplayIndex(r, 5, at, 8) == 3);
  CHECK(hcu::ReplayIndex(r, 5, at, 10) == 4);
  CHECK(hcu::ReplayIndex(r, 5, at, 11) == 5);
  // A jump over several records (a long skipped stretch) lands on the last one at or below the swap.
  size_t at2 = 0;
  CHECK(hcu::ReplayIndex(r, 5, at2, 9) == 3);
  CHECK(hcu::ReplayIndex(r, 0, at2, 0) == 0);  // no records: over at once
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
