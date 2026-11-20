#pragma once
// The replay lookup of the pad driver's input replay (switch/host/input/nx_input_driver.cpp), tests/input_replay_tests.cpp.
#include <cstddef>
#include <cstdint>

namespace hcu {

// Records in poll order (so their swap numbers never fall): the one the game gets at the strip's swap `swap` is the
// last one whose swap is at or below it; it holds until the next record's swap. `at` only grows (start it at 0).
// -1: before the first record's swap. n: the swap is past the last record's (the replay is over).
template <class Record>
long ReplayIndex(const Record* records, size_t n, size_t& at, uint32_t swap) {
  if (n == 0 || swap > records[n - 1].swap) return long(n);
  if (swap < records[0].swap) return -1;
  while (at + 1 < n && records[at + 1].swap <= swap) at++;
  return long(at);
}

}  // namespace hcu
