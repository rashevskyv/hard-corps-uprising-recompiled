// Profile collection of the PGO-instrumented Switch build (switch/switch-base.cmake, HCU_PGO=generate), see
// switch/hcu_pgo.cpp. From renut_nx_ultimate_base (app/src/banjo_pgo.h, GPL-3.0; itself from sa2-nx); only the names
// differ. In every other build these do nothing.

#pragma once

namespace hcu::pgo {

// Picks this session's file (pgo/banjo_pgo_<N>.stream next to the NRO) and starts the 2-minute dumps.
void Start();
// Writes every count so far to this session's file (the whole session each time, replacing the previous dump).
void DumpNow();
// The last dump, then the dump thread ends.
void Stop();

}  // namespace hcu::pgo
