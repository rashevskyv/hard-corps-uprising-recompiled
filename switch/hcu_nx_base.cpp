// The Switch start-up of the hcu port on the ultimate base's SDK (switch/switch-base.cmake). The base owns the process
// (main, threads, memory, the crash hooks, stderr, the exit); this file keeps what HCU's own host layer
// (switch/host/switch_nx.cpp, switch/host/ui/windowed_app_main_nx.cpp, old SDK only) gave the game: the log in
// sdmc:/switch/hcu/, the console defaults of the cvars, args.txt and the marker files, the version, the quick exit.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include <atomic>
#include <cmath>

#include <rex/hcu_audio_levels.h>  // switch/sdk-edits-base.py: the [audio] line

#include "hcu_pgo.h"
#include "clock_ladders.h"
#include "hcu_version.h"  // written by switch/build-base.sh: HCU_VERSION, HCU_BUILD_UTC, HCU_BUILD_ID
#include "settings_menu.h"  // hcu_clocks_key, hcu_settings.fps_mode
#include "sysclk_owner.h"
#include "xma_voice_table.h"  // the voice table: built before the base's main, its counters on the [audio] line

extern "C" {
// libnx (init.c): the exit; with __nx_applet_exit_mode 1 it asks the system to close the application (the base's main,
// sdk/src/ui/windowed_app_main_switch.cpp, ExitProcess).
extern u32 __nx_applet_exit_mode;
[[noreturn]] void __libnx_exit(int rc);
int __real_main(int argc, char* argv[]);
void hcu_input_flush(int stop);  // switch/host/input/nx_input_driver.cpp: input_record=1's file onto the card
float rex_hcu_xma_gain(int cls);  // src/settings_menu.cpp: 0 sfx_gain, 1 music_gain, 2 voice_gain, 0..2
}

extern "C" volatile uint32_t hcu_gpu_swap;  // the strip's swap number (src/gpu/deko3d_backend.cpp)
extern "C" volatile uint32_t hcu_speed_swaps;  // level swaps and their ticks, running totals (the governor's speed)
extern "C" volatile uint64_t hcu_speed_ticks;
extern "C" volatile uint32_t hcu_speed_drawn;  // of those swaps, the drawn ones

namespace {

const char* const kDataDir = "sdmc:/switch/hcu";
Thread log_flush_thread;
volatile bool log_flush_running = false;

// settings.txt clocks=1 (default 0: the game touches no clock): memory 1600 MHz, CPU / GPU the first rung of the frame-rate mode's
// ladder (clock_ladders.h; with clocks_auto=0: CPU 1581, GPU 384, 0.409 A/B: about 100 % speed at the replay in 30 fps) through clkrst,
// as switch/host/switch_nx.cpp did in the old SDK's build. sys-clk has priority, per module: a module it sets (an
// override, or a title or global profile rate) is its own, and the game makes no pcv call for it; a module it leaves
// ("Do not override", or sys-clk disabled) the game sets. sys-clk is asked again every 2 s, so a change in its
// overlay takes effect. Horizon OC, sys-clk-OC or a sys-clk with an unknown IPC: the game touches no clock.
// No module session stays open (the clkrst service session does): each read or set opens one and closes it (0.399
// held three for the whole run, and pcv aborted with 2011-0102, out of session memory; see docs/switch.md).
struct Clock {
  const char* name;
  PcvModuleId module;
  u32 wanted;
  u32 old = 0;      // the rate before the game's set, put back on exit; 0 = nothing to put back
  u32 set = 0;      // the rate the game holds (read back after its set); 0 = the game does not hold the module
  u32 owner = ~0u;  // sys-clk's rate for it, Hz; 0 = left to the game; ~0u = not asked yet
  u32 skip = 0;     // checks to skip (back-off after a re-set or a failed set)
  u32 wait = 1;     // the next back-off: 1, 2, 4, 8, 16 checks (2 to 32 s)
  bool exact = false;  // set to `wanted` also from a higher rate (the Erista GPU: held at 460.8, docked too)
};
constinit Clock clocks[] = {  // userAppInit runs before the C++ constructors
    {"CPU", PcvModuleId_CpuBus, 1581000000},
    {"GPU", PcvModuleId_GPU, 384000000},
    {"memory", PcvModuleId_EMC, 1600000000},
};
// clocks_ladder=<cpu>/<gpu>,... (MHz, may be fractional: 460.8; at most 16 pairs): the game steps through the pairs by the strip's swap number,
// the first pair from swap clocks_ladder_swap (default 2400, after the menus), a new pair every clocks_ladder_len swaps
// (default 1500, about 28 s), the last one stays. The pairs may be lower than the rates the game holds (A/B of the
// steps above stock: find where the speed starts to fall).
constexpr unsigned kLadderMax = 16;
using LadderStep = hcu_clock::Rung;  // kHz
LadderStep ladder[kLadderMax];
unsigned ladder_n = 0, ladder_start = 2400, ladder_len = 1500;
int ladder_at = -1;
// The governor (on with clocks=1; clocks_auto=0 turns it off; a clocks_ladder or a clocks_cpu / clocks_gpu key wins). Its
// ladder (clock_ladders.h) is the frame-rate mode's in handheld (30, 45, 60) and one for every mode docked: from
// settings.txt fps_mode and the console mode at start, then from the menu's FRAME RATE and the console mode (both read
// every 2 s in level play); it starts on the first rung, which is also the floor. Every 0.1 s it takes the speed
// of the last second from the level swaps (the [speed] meter's counts; in 45 and 60 also the drawn ones: gov_speed). 3 or more
// frames lost in that second: one rung up at once (two for 6 or more), then a second of the new rate is judged. Every 2 s: under 1 frame lost for
// gov_wait samples in a row: one rung down; a rung that fails within 3 such samples of the step down doubles that wait
// (5 to 64). Between (1 and 3 frames lost) and in a stretch of fewer than 30 level swaps in the second (a load, a menu) the rung stays. (0.410 A/B at the replay, 30 fps: CPU 1581 / GPU 384 held 100 %, 1326 / 384 did not.)
bool gov_on = true;  // on with clocks=1; off with clocks_auto=0, with clocks_cpu / clocks_gpu (unless clocks_auto=1), or a ladder
int gov_mode = 0;    // the fps_mode and the console mode the ladder is for
bool gov_docked = false;
constinit hcu_clock::Ladder gov_ladder = hcu_clock::ladder_for(0, false);  // constinit: raise_clocks runs before the constructors
int gov_rung = 0;    // the ladder's first rung: the start and the floor
u32 gov_wait = 5, gov_good = 0, gov_since_down = 100;
struct GovSnap { u32 sw, dr; u64 tk; };
GovSnap gov_ring[10];  // the running totals at the last 10 passes (100 ms each): the window is the last second
int gov_n = 0;         // snapshots of this rung and mode in the ring; below 0 = passes to wait (the backend takes a new fps_mode within 60 swaps)
unsigned gov_pass = 0;
bool erista = false;  // splGetConfig HardwareType 0 or 1 (raise_clocks): the GPU is held at 460.8 (clock_ladders.h)
constexpr u32 kNoAnswer = 1;           // owner: sys-clk did not answer, so the module is left alone
bool clkrst_up = false;
Service sysclk_srv;                     // sys-clk's IPC (trusted API): asked before every pcv call
bool sysclk = false;
bool sysclk_silent = false;             // the last question got no answer (logged once)
std::atomic<bool> clocks_on{false};     // raise_clocks is done: keep_clocks may run
int keep_checks = 0;

// The CPU clock as the core runs it, whatever clkrst says: 8 dependent adds per iteration (1 cycle each on the A57),
// timed with the 19.2 MHz tick; the fastest of three runs. 2M cycles, 1.1 ms at 1785 MHz.
u32 busy_loop_mhz() {
  u64 best = ~0ull;
  for (int run = 0; run < 3; run++) {
    u64 n = 250000, x = 0;
    const u64 t0 = armGetSystemTick();
    asm volatile(
        "1:\n"
        "add %0, %0, #1\n add %0, %0, #1\n add %0, %0, #1\n add %0, %0, #1\n"
        "add %0, %0, #1\n add %0, %0, #1\n add %0, %0, #1\n add %0, %0, #1\n"
        "subs %1, %1, #1\n b.ne 1b\n"
        : "+r"(x), "+r"(n)::"cc");
    const u64 ticks = armGetSystemTick() - t0;
    if (ticks < best) best = ticks;
  }
  return best ? (u32)(250000ull * 8 * 19200000 / best / 1000000) : 0;
}

// Is another process serving this name? sys-clk's own check (sysclkIpcRunning): registering the name fails with
// 0x815 (already registered) while the owner runs, and changes nothing for it. 0 = nobody serves it (our registration
// is taken back at once); any other rc = cannot tell. smGetService is not usable for this: sm holds the request until
// the name is registered, so it would hang without sys-clk.
constexpr Result kAlreadyRegistered = MAKERESULT(21, 4);
Result service_probe(const char* name) {
  Handle handle;
  const Result rc = smRegisterService(&handle, smEncodeName(name), false, 1);
  if (R_SUCCEEDED(rc)) {
    smUnregisterService(smEncodeName(name));
    svcCloseHandle(handle);
  }
  return rc;
}

// Horizon OC and sys-clk-OC: their rates are not asked, so the game touches no clock while one runs.
bool other_clock_tool(const char* name, const char* who) {
  const Result rc = service_probe(name);
  if (R_SUCCEEDED(rc)) return false;
  if (rc == kAlreadyRegistered) {
    std::printf("[clock] %s is running: the game does not touch clocks\n", who);
  } else {
    std::printf("[clock] cannot tell whether %s runs (sm rc 0x%X): the game does not touch clocks\n", who, rc);
  }
  return true;
}

// sys-clk's rate for each module now (Hz, 0 = left to the game) and the rates it last read. Three IPC calls to sys-clk,
// none to pcv. No answer: every module counts as sys-clk's.
void ask_sysclk(u32 owner[3], u32 seen[3], const char* why[3]) {
  using namespace hcu_sysclk;
  Context c{};
  Profiles title{}, own{}, global{};
  u64 pid = 0;
  svcGetInfo(&pid, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
  const u64 global_tid = kGlobalTid;
  Result rc = serviceDispatchOut(&sysclk_srv, kGetCurrentContext, c);
  if (R_SUCCEEDED(rc)) rc = serviceDispatchInOut(&sysclk_srv, kGetProfiles, c.applicationId, title);
  // sys-clk learns the title on its next poll: until then its context can name the title before ours.
  if (R_SUCCEEDED(rc) && pid != c.applicationId) rc = serviceDispatchInOut(&sysclk_srv, kGetProfiles, pid, own);
  if (R_SUCCEEDED(rc)) rc = serviceDispatchInOut(&sysclk_srv, kGetProfiles, global_tid, global);
  if (R_FAILED(rc)) {
    if (!sysclk_silent) std::printf("[clock] sys-clk does not answer (rc 0x%X): the game leaves every clock\n", rc);
    sysclk_silent = true;
    for (int i = 0; i < 3; i++) owner[i] = kNoAnswer, seen[i] = 0, why[i] = "no answer";
    return;
  }
  sysclk_silent = false;
  for (int i = 0; i < 3; i++) {
    const u32 a = target_hz(c, title, global, i), b = target_hz(c, own, global, i);
    owner[i] = a > b ? a : b;
    seen[i] = c.freqs[i];
    why[i] = !c.enabled ? "disabled" : c.overrideFreqs[i] ? "override" : owner[i] ? "profile" : "Do not override";
  }
}

// One clkrst session for one module, closed again. `to` != 0: set it when the rate is below `to`.
// Returns the rate read back (0 = no session); *before = the rate before the set.
u32 pcv_rate(const Clock& clock, u32 to, u32* before = nullptr, Result* set_rc = nullptr) {
  if (!clkrst_up) {
    if (const Result rc = clkrstInitialize(); R_FAILED(rc)) {
      std::printf("[clock] clkrstInitialize failed (rc 0x%X): the clocks are unchanged\n", rc);
      return 0;
    }
    clkrst_up = true;
  }
  ClkrstSession s;
  if (const Result rc = clkrstOpenSession(&s, clock.module, 3); R_FAILED(rc)) {
    std::printf("[clock] %s: no clkrst session (rc 0x%X)\n", clock.name, rc);
    return 0;
  }
  u32 hz = 0;
  clkrstGetClockRate(&s, &hz);
  if (before) *before = hz;
  if (to && (hz < to || (clock.exact && hz != to))) {
    const Result rc = clkrstSetClockRate(&s, to);
    if (set_rc) *set_rc = rc;
    clkrstGetClockRate(&s, &hz);
  }
  clkrstCloseSession(&s);
  return hz;
}

void back_off(Clock& clock) {
  clock.skip = clock.wait;
  if (clock.wait < 16) clock.wait *= 2;
}

// The game takes a module: never lower a rate (docked memory, or someone else's overclock).
void take(Clock& clock) {
  u32 before = 0;
  Result rc = 0;
  const u32 now = pcv_rate(clock, clock.wanted, &before, &rc);
  if (now == 0) {
    back_off(clock);
    return;
  }
  clock.old = before;
  clock.set = now;
  std::printf("[clock] %s: was %u, requested %u, %s, read back %u MHz\n", clock.name, before / 1000000,
              clock.wanted / 1000000,
              before == clock.wanted || (!clock.exact && before > clock.wanted) ? "not applied (already at or above)"
              : R_FAILED(rc)                                                   ? "REFUSED"
                                                                               : "applied",
              now / 1000000);
  if (R_FAILED(rc)) std::printf("[clock] %s: clkrstSetClockRate rc 0x%X\n", clock.name, rc);
}

// One pass: sys-clk's choice per module (asked every time), then the game's set, or its check of a module it holds.
// HOME, docking and sleep put the system's rates back: a module below the game's rate is set again, then the next
// check waits twice as long (up to 32 s). A rate at or above the game's is never set again.
void clock_pass(u32 now_out[3]) {
  u32 owner[3] = {}, seen[3] = {};
  const char* why[3] = {};
  if (sysclk) ask_sysclk(owner, seen, why);
  for (int i = 0; i < 3; i++) {
    Clock& clock = clocks[i];
    now_out[i] = seen[i];
    if (sysclk && owner[i] != clock.owner) {
      if (owner[i] == kNoAnswer) {
        std::printf("[clock] %s: no answer from sys-clk -> game leaves it\n", clock.name);
      } else if (owner[i]) {
        std::printf("[clock] %s: sys-clk %s %u -> game leaves it\n", clock.name, why[i], owner[i] / 1000000);
      } else {
        std::printf("[clock] %s: sys-clk %s -> game sets %u\n", clock.name, why[i], clock.wanted / 1000000);
      }
      clock.owner = owner[i];
      clock.skip = 0;
      clock.wait = 1;
    }
    if (owner[i]) {  // sys-clk's: handed back without a set, and not put back on exit
      clock.set = clock.old = 0;
      continue;
    }
    if (clock.skip) {
      clock.skip--;
      continue;
    }
    if (clock.set == 0) {
      take(clock);
      now_out[i] = clock.set;
      continue;
    }
    const u32 now = seen[i] ? seen[i] : pcv_rate(clock, 0);
    now_out[i] = now;
    if (now == 0 || now == clock.set || (!clock.exact && now > clock.set)) continue;
    const u32 again = pcv_rate(clock, clock.set);
    std::printf("[clock] %s was put back to %u MHz: set to %u MHz again, read back %u MHz; next check in %u s\n",
                clock.name, now / 1000000, clock.set / 1000000, again / 1000000, clock.wait * 2);
    back_off(clock);
  }
}

void raise_clocks() {
  if (!hcu_clocks_key("sdmc:/switch/hcu/settings.txt")) {
    std::printf("[clock] settings.txt clocks=0 (or no key): the game does not touch clocks; busy-loop equals %u MHz\n",
                busy_loop_mhz());
    return;
  }
  // clocks_cpu=<MHz> / clocks_gpu=<MHz> in settings.txt (clocks=1 only): a fixed pair instead of the governor's first rung (at most 1785 / 768; 0 = not set)
  // (A/B of the steps above stock, CPU 1020 / GPU 307; the last line wins; a 0 is ignored, the default stays).
  // fps_mode picks the governor's ladder: hcu_settings is not loaded yet (the backend loads it), so it is read here; the
  // fps file of the debug dir is not, the governor's first sample follows hcu_settings.fps_mode.
  int mode = 0;
  bool fixed_key = false;
  if (FILE* f = std::fopen("sdmc:/switch/hcu/settings.txt", "r")) {
    char line[256];
    unsigned v = 0;
    bool auto_key = false;
    while (std::fgets(line, sizeof(line), f)) {
      if (std::sscanf(line, "fps_mode=%u", &v) == 1) mode = v <= 4 && v != 3 ? int(v) : 0;  // as hcu_settings_load
      if (std::sscanf(line, "clocks_auto=%u", &v) == 1) gov_on = v != 0, auto_key = true;
      if (std::sscanf(line, "clocks_cpu=%u", &v) == 1 && v) fixed_key = true;
      if (std::sscanf(line, "clocks_gpu=%u", &v) == 1 && v) fixed_key = true;
      if (std::sscanf(line, "clocks_ladder_swap=%u", &v) == 1) ladder_start = v;
      if (std::sscanf(line, "clocks_ladder_len=%u", &v) == 1 && v) ladder_len = v;
      if (std::strncmp(line, "clocks_ladder=", 14) == 0) {  // cpu/gpu MHz pairs, comma-separated; the last line wins
        ladder_n = 0;
        for (const char* p = line + 14; ladder_n < kLadderMax; p = std::strchr(p, ',') ? std::strchr(p, ',') + 1 : "") {
          float c = 0, g = 0;  // MHz, may be fractional (460.8)
          if (std::sscanf(p, "%f/%f", &c, &g) != 2 || c < 100 || g < 50) break;
          ladder[ladder_n++] = {u32(std::min(c, 1785.f) * 1000.f + .5f), u32(std::min(g, 768.f) * 1000.f + .5f)};  // kHz
        }
      }
      if (std::sscanf(line, "clocks_cpu=%u", &v) == 1 && v) clocks[0].wanted = std::min(v, 1785u) * 1000000u;
      if (std::sscanf(line, "clocks_gpu=%u", &v) == 1 && v) clocks[1].wanted = std::min(v, 768u) * 1000000u;
    }
    std::fclose(f);
    if (fixed_key && !auto_key) gov_on = false;  // a fixed pair from the file is meant: no governor unless clocks_auto=1
  }
  // Erista or not: spl HardwareType 0 (Icosa) or 1 (Copper) is Erista, 2..5 Mariko (sys-clk's board.cpp). A failed
  // call counts as not Erista (the ladders unchanged).
  u64 hw = ~0ull;
  Result spl_rc = splInitialize();
  if (R_SUCCEEDED(spl_rc)) {
    spl_rc = splGetConfig(SplConfigItem_HardwareType, &hw);
    splExit();
  }
  erista = R_SUCCEEDED(spl_rc) && hw <= 1;
  if (erista) {
    clocks[1].wanted = hcu_clock::kEristaGpu * 1000u;  // also over a clocks_gpu key
    clocks[1].exact = true;
    std::printf("[clock] erista, GPU held at 460.8 MHz (hardware type %u; every rung, handheld and docked)\n", unsigned(hw));
  } else {
    std::printf("[clock] hardware type %d (rc 0x%X): not erista, the GPU follows the ladder\n", R_SUCCEEDED(spl_rc) ? int(hw) : -1, spl_rc);
  }
  if (gov_on && !ladder_n) {
    gov_mode = mode;
    gov_docked = appletGetOperationMode() == AppletOperationMode_Console;  // libnx's value, kept by applet messages
    gov_ladder = hcu_clock::ladder_for(mode, gov_docked);
    if (!fixed_key) {
      clocks[0].wanted = gov_ladder.rungs[0].cpu * 1000u;
      clocks[1].wanted = hcu_clock::for_soc(gov_ladder.rungs[0], erista).gpu * 1000u;
    }
  }
  std::printf("[clock] settings.txt clocks=1 requested: CPU %u, GPU %u, memory %u MHz\n", clocks[0].wanted / 1000000,
              clocks[1].wanted / 1000000, clocks[2].wanted / 1000000);
  // Before any pcv call: who else sets clocks. Horizon OC puts stock rates back every tick (fighting it froze consoles
  // in NFSU2's port); sys-clk-OC's IPC is another one.
  if (other_clock_tool("hoc:clk", "Horizon OC") || other_clock_tool("sysclkOC", "sys-clk-OC")) return;
  if (const Result rc = service_probe("sys:clk"); rc == kAlreadyRegistered) {
    u32 api = 0;
    if (R_FAILED(smGetService(&sysclk_srv, "sys:clk")) ||
        R_FAILED(serviceDispatchOut(&sysclk_srv, hcu_sysclk::kGetApiVersion, api)) || !hcu_sysclk::api_trusted(api)) {
      std::printf("[clock] sys-clk is running (IPC API %u, not known here): the game does not touch clocks\n", api);
      if (serviceIsActive(&sysclk_srv)) serviceClose(&sysclk_srv);
      return;
    }
    sysclk = true;
    std::printf("[clock] sys-clk is running (IPC API %u): it keeps the modules it sets, the game sets the others\n", api);
  } else if (R_FAILED(rc)) {
    std::printf("[clock] cannot tell whether sys-clk runs (sm rc 0x%X): the game does not touch clocks\n", rc);
    return;
  }
  if (!hosversionAtLeast(8, 0, 0)) {
    std::printf("[clock] firmware below 8.0.0: no clkrst, the clocks are unchanged\n");
    if (sysclk) {
      serviceClose(&sysclk_srv);
      sysclk = false;
    }
    return;
  }
  if (gov_on && !ladder_n) {  // after every early return: the clocks are really the game's
    std::printf("[clock] auto: mode %s (fps_mode %d), %s, ladder %s (%d rungs), start rung 0 (CPU %g, GPU %g MHz; the floor); the memory stays on every rung\n",
                hcu_clock::mode_name(gov_mode), gov_mode, gov_docked ? "docked" : "handheld", gov_ladder.name, gov_ladder.count, gov_ladder.rungs[0].cpu / 1000.0,
                hcu_clock::for_soc(gov_ladder.rungs[0], erista).gpu / 1000.0);
  }
  const u32 loop_before = busy_loop_mhz();
  u32 now[3];
  clock_pass(now);
  std::printf("[clock] busy-loop equals %u MHz before the raise, %u MHz after it\n", loop_before, busy_loop_mhz());
  clocks_on.store(true, std::memory_order_release);
}

// Every 2 s (log_flush_loop). The first check logs the rates 2 s after the raise (an immediate read-back can show a
// rate the system takes back later).
void keep_clocks() {
  if (!clocks_on.load(std::memory_order_acquire)) return;
  u32 now[3];
  clock_pass(now);
  if (keep_checks++ == 0) {
    std::printf("[clock] 2 s after the raise: CPU %u, GPU %u, memory %u MHz (0 = not read); busy-loop equals %u MHz\n",
                now[0] / 1000000, now[1] / 1000000, now[2] / 1000000, busy_loop_mhz());
  }
}

// Every 0.5 s (log_flush_loop): the ladder step of the current swap. A module the game holds is set to the step's rate
// (up or down); clock.set follows it, so the check above does not put the old rate back. sys-clk's modules are left.
// `wanted` follows the step too: a module sys-clk hands back later is taken at the current step, not the start one.
void set_pair(const LadderStep& step, u32 got[2], Result rcs[2]) {
  const u32 want[2] = {step.cpu * 1000u, hcu_clock::for_soc(step, erista).gpu * 1000u};
  for (int i = 0; i < 2; i++) {
    Clock& clock = clocks[i];
    clock.wanted = want[i];
    if (clock.set == 0) continue;  // not held by the game (sys-clk's module, or no set yet)
    ClkrstSession s;
    if (R_FAILED(clkrstOpenSession(&s, clock.module, 3))) continue;
    clock.set = want[i];  // before the set: the check must not put the old rate back
    rcs[i] = clkrstSetClockRate(&s, want[i]);
    clkrstGetClockRate(&s, &got[i]);
    clkrstCloseSession(&s);
    if (got[i]) clock.set = got[i];  // the rate the system took (the check compares with it)
  }
}

void step_ladder() {
  if (!clocks_on.load(std::memory_order_acquire) || !ladder_n) return;
  const unsigned swap = hcu_gpu_swap;
  if (swap < ladder_start) return;
  const int at = int(std::min<unsigned>((swap - ladder_start) / ladder_len, ladder_n - 1));
  if (at == ladder_at) return;
  ladder_at = at;
  u32 got[2] = {};
  Result rcs[2] = {};
  set_pair(ladder[at], got, rcs);
  std::printf("[clock] ladder step %d at swap #%u: CPU %u, GPU %u kHz asked, read back CPU %u, GPU %u kHz (rc 0x%X 0x%X; 0 = not held)\n",
              at, swap, ladder[at].cpu, hcu_clock::for_soc(ladder[at], erista).gpu, got[0] / 1000, got[1] / 1000, rcs[0], rcs[1]);
}

// Every 0.1 s (log_flush_loop): the governor (see gov_on). The speed is of the last second (10 passes), taken from the
// swaps' own tick sums, so it has no phase noise; below 98 % it goes up at once. The rest (the mode check, a step down)
// is every 2 s as before.
void govern() {
  if (!gov_on || ladder_n || !clocks_on.load(std::memory_order_acquire)) return;
  const GovSnap cur = {hcu_speed_swaps, hcu_speed_drawn, hcu_speed_ticks};
  const bool slow = ++gov_pass % 20 == 0;
  const bool full = gov_n >= 10;
  const GovSnap base = full ? gov_ring[gov_n % 10] : cur;
  if (gov_n >= 0) gov_ring[gov_n % 10] = cur;
  gov_n++;
  const u32 dsw = cur.sw - base.sw, ddr = cur.dr - base.dr;
  const u64 dtk = cur.tk - base.tk;
  const bool level = full && dsw >= 30 && dtk;
  if (!slow && !level) return;
  // The menu's FRAME RATE and the console mode are live. The console mode is read every 2 s (an undock in a menu must
  // not keep the docked rates in handheld); fps_mode only in level play (hcu_settings is loaded by then). A change drops
  // this sample (part of it is the old mode's); a new ladder starts on its first rung.
  const int mode = level ? hcu_settings.fps_mode : gov_mode;
  const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
  if (slow && (mode != gov_mode || docked != gov_docked)) {
    const hcu_clock::Ladder was = gov_ladder;
    gov_mode = mode;
    gov_docked = docked;
    gov_ladder = hcu_clock::ladder_for(mode, docked);
    gov_good = 0;
    gov_wait = 5;
    gov_since_down = 100;
    gov_n = -10;  // 1 s more: the backend may still pace the old mode for up to 60 swaps of it
    if (gov_ladder.rungs == was.rungs) {
      std::printf("[clock] auto: mode %s (fps_mode %d), %s, ladder %s stays, rung %d\n", hcu_clock::mode_name(mode), mode,
                  docked ? "docked" : "handheld", gov_ladder.name, gov_rung);
      return;
    }
    gov_rung = 0;
    u32 got[2] = {};
    Result rcs[2] = {};
    set_pair(gov_ladder.rungs[0], got, rcs);
    std::printf("[clock] auto: mode %s (fps_mode %d), %s, ladder %s (%d rungs), start rung 0 (CPU %g, GPU %g MHz; the floor) at swap #%u, read back CPU %u, GPU %u kHz (rc 0x%X 0x%X)\n",
                hcu_clock::mode_name(mode), mode, docked ? "docked" : "handheld", gov_ladder.name, gov_ladder.count, gov_ladder.rungs[0].cpu / 1000.0,
                hcu_clock::for_soc(gov_ladder.rungs[0], erista).gpu / 1000.0, (unsigned)hcu_gpu_swap, got[0] / 1000, got[1] / 1000, rcs[0], rcs[1]);
    return;
  }
  if (!level) return;
  const double pct = hcu_clock::gov_speed(gov_mode, dsw, ddr, double(dtk) / double(armGetSystemTickFreq()));
  if (slow) gov_since_down++;
  int to = gov_rung;
  const double lost = hcu_clock::gov_lost(gov_mode, pct);
  if (lost >= 3.0) {
    gov_good = 0;
    // The lower rung failed at once: wait longer before trying it again; it held for more than 3 samples: back to 5.
    gov_wait = gov_since_down <= 3 ? std::min(gov_wait * 2, 64u) : 5;
    to = std::min(gov_rung + (lost >= 6.0 ? 2 : 1), gov_ladder.count - 1);
  } else if (!slow) {
  } else if (lost < 1.0 && gov_rung > 0) {
    if (++gov_good >= gov_wait) {
      gov_good = 0;
      to = gov_rung - 1;
    }
  } else {
    gov_good = 0;
  }
  if (to == gov_rung) return;
  if (to < gov_rung) gov_since_down = 0;
  u32 got[2] = {};
  Result rcs[2] = {};
  set_pair(gov_ladder.rungs[to], got, rcs);
  std::printf("[clock] auto: speed %.1f %% over %u level swaps (%u drawn), ladder %s, rung %d -> %d at swap #%u: CPU %u, GPU %u kHz asked, read back CPU %u, GPU %u kHz (rc 0x%X 0x%X; wait %u samples)\n",
              pct, dsw, ddr, gov_ladder.name, gov_rung, to, (unsigned)hcu_gpu_swap, gov_ladder.rungs[to].cpu,
              hcu_clock::for_soc(gov_ladder.rungs[to], erista).gpu, got[0] / 1000, got[1] / 1000, rcs[0], rcs[1], gov_wait);
  gov_rung = to;
  gov_n = 0;  // the next judgement needs a second of the new rate
}

// The old rates come back for the modules the game holds; sys-clk is asked first, its modules are left to it.
void restore_clocks() {
  if (clocks_on.exchange(false, std::memory_order_acq_rel)) {
    u32 owner[3] = {}, seen[3] = {};
    const char* why[3] = {};
    if (sysclk) ask_sysclk(owner, seen, why);
    for (int i = 0; i < 3; i++) {
      Clock& clock = clocks[i];
      if (owner[i] || clock.old == 0 || clock.set == clock.old) continue;
      ClkrstSession s;
      if (R_SUCCEEDED(clkrstOpenSession(&s, clock.module, 3))) {
        clkrstSetClockRate(&s, clock.old);
        clkrstCloseSession(&s);
      }
    }
  }
  if (clkrst_up) {
    clkrstExit();
    clkrst_up = false;
  }
  if (sysclk) {
    serviceClose(&sysclk_srv);
    sysclk = false;
  }
}

// The [audio] line, once a second (switch/sdk-edits-base.py, include/rex/hcu_audio_levels.h): peak/RMS (1 = full
// scale) of the mono (sfx: every mono stream that is not a voice), voice and stereo XMA streams after sfx_gain /
// voice_gain / music_gain, and of each render client c0..c3 after the 5.1 fold, before the sum and the clamp. A slot
// with no samples this second is left out; no samples at all, no line. Then the gains and the voice table's sound
// starts of this second (src/xma_voice_table.cpp).
void print_audio_levels() {
  static const char* const kName[7] = {"mono", "stereo", "c0", "c1", "c2", "c3", "voice"};
  static const int kOrder[7] = {0, 6, 1, 2, 3, 4, 5};  // mono, voice, stereo, the clients
  char text[260] = "";
  int len = 0;
  for (int i : kOrder) {
    HcuAudioLevel& l = rex_hcu_audio_levels[i];
    const uint64_t n = l.n.exchange(0, std::memory_order_relaxed);
    const uint32_t peak = l.peak.exchange(0, std::memory_order_relaxed);
    const uint64_t sumsq = l.sumsq.exchange(0, std::memory_order_relaxed);
    if (!n) continue;
    len += std::snprintf(text + len, sizeof(text) - len, " %s %.3f/%.3f", kName[i], peak / 32767.0,
                         std::sqrt(double(sumsq) / double(n)) / 32767.0);
  }
  char starts[120];
  const int slen = hcu_voice_stats(starts, sizeof(starts));
  if (len || slen)
    std::printf("[audio] peak/rms:%s; sfx_gain %.2f voice_gain %.2f music_gain %.2f;%s\n", text, rex_hcu_xma_gain(0),
                rex_hcu_xma_gain(2), rex_hcu_xma_gain(1), slen ? starts : " no starts");
}

// The log is buffered and flushed twice a second; fsync commits it to the card (after a hang and a hard reboot, a log
// that was only flushed ended in garbage). Passes are 0.1 s: the governor every pass, the log every 5th (twice a second),
// the clocks every 20th (2 s), the input record and the [audio] line every 10th (1 s).
void log_flush_loop(void*) {
  for (unsigned pass = 1; log_flush_running; pass++) {
    svcSleepThread(100'000'000LL);
    if (pass % 20 == 0) keep_clocks();
    govern();
    if (pass % 5 == 0) step_ladder();
    if (pass % 10 == 0) hcu_input_flush(0), print_audio_levels();
    if (pass % 5 == 0) {
      std::fflush(stdout);
      fsync(fileno(stdout));
    }
  }
}

void open_log() {
  mkdir("sdmc:/switch", 0777);
  mkdir(kDataDir, 0777);
  char path[256];
  // One log per build (HCU_BUILD_ID): two builds of one commit do not write over each other's log.
  std::snprintf(path, sizeof(path), "%s/hcu_log_" HCU_BUILD_ID ".txt", kDataDir);
  if (std::freopen(path, "w", stdout) == nullptr) return;
  // Buffered on the card; line-buffered in no-GPU runs, so an abort keeps every line.
  const bool line_buffered = access("sdmc:/switch/hcu/nogpu", F_OK) == 0;
  std::setvbuf(stdout, nullptr, line_buffered ? _IOLBF : _IOFBF, 64 * 1024);
  log_flush_running = true;
  if (R_SUCCEEDED(threadCreate(&log_flush_thread, log_flush_loop, nullptr, nullptr, 0x4000, 0x20, 0))) {
    threadStart(&log_flush_thread);
  }
}

}  // namespace

// The version for the on-screen strip of the deko3d backend.
extern "C" const char* rex_nx_version(void) { return HCU_VERSION; }

extern "C" void userAppInit(void) {
  open_log();
  std::printf("[nx] Hard Corps: Uprising, version " HCU_VERSION ", built " HCU_BUILD_UTC " (ultimate base SDK)\n");
  appletSetAutoSleepDisabled(true);
  raise_clocks();
}

extern "C" void userAppExit(void) {
  static bool done = false;  // rex_nx_quick_exit runs it before libnx's exit does
  if (done) return;
  done = true;
  if (log_flush_running) {
    log_flush_running = false;
    threadWaitForExit(&log_flush_thread);
    threadClose(&log_flush_thread);
  }
  hcu_input_flush(1);  // the record's last polls and its stop line
  restore_clocks();
  std::fflush(stdout);
  fsync(fileno(stdout));
}

// The way out the deko3d backend takes (HOME -> Close, the game's own quit, a GPU queue error): the log first, then the
// base's exit (libnx's, with the applet's Exit, as the base's main ends). Also XThread::StopTitleThreads when a guest
// thread outlives the game's quit (switch/sdk-edits-base.py). One caller only: a second one waits for the end.
extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why) {
  static std::atomic_flag entered = ATOMIC_FLAG_INIT;
  if (entered.test_and_set()) {
    for (;;) svcSleepThread(1'000'000'000LL);
  }
  std::printf("[nx] exit: %s\n", why);
  hcu::pgo::DumpNow();  // this exit skips OnShutdown: the PGO build's last counts (nothing otherwise)
  userAppExit();
  std::fflush(nullptr);
  __nx_applet_exit_mode = 1;
  __libnx_exit(0);
}

// The base's main (linked with --wrap=main) gets the console defaults first: each only when no argument (hbmenu's,
// sdmc:/switch/hcu/args.txt) sets that cvar already. On the command line, not in a config: the base reads
// game_data_root and user_data_root before any config is loaded.
extern "C" int __wrap_main(int argc, char* argv[]) {
  std::vector<std::string> args(argv, argv + argc);
  if (args.empty()) args.push_back("hcu");  // cvar::Init skips argv[0] as the program name
  // Extra arguments, one per line (e.g. --log_level=trace); lines starting with # are skipped.
  if (FILE* fp = std::fopen("sdmc:/switch/hcu/args.txt", "r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), fp)) {
      std::string a(line);
      while (!a.empty() && (a.back() == '\n' || a.back() == '\r' || a.back() == ' ')) a.pop_back();
      if (!a.empty() && a[0] != '#') args.push_back(a);
    }
    std::fclose(fp);
  }
  // The deko3d renderer (src/gpu/deko3d_backend.cpp) is the only one: this build links no Vulkan driver. The file
  // sdmc:/switch/hcu/nogpu selects the plugin's null backend instead (nothing drawn). The file deko3d wins over it, as
  // in the old host layer (switch/host/ui/windowed_app_main_nx.cpp), so Eden's staging can keep both files.
  const bool no_gpu = access("sdmc:/switch/hcu/nogpu", F_OK) == 0 && access("sdmc:/switch/hcu/deko3d", F_OK) != 0;
  const char* const defaults[] = {
      // The root form, not "sdmc:/...": libstdc++ takes "sdmc:/x" for a relative path, std::filesystem::absolute puts
      // the current folder in front, and status() throws (0.348 aborted in Runtime::SetupVfs). newlib opens "/x" on
      // the SD (the base's GetExecutablePath, sdk/src/core/filesystem_posix.cpp, does the same).
      "--game_data_root=/switch/hcu/game",
      "--user_data_root=/switch/hcu/user",
      "--fullscreen=true",
      "--log_file=sdmc:/switch/hcu/hcu_rex_log.txt",
      "--gpu_plugin=xenos",
      no_gpu ? "--gpu_backend=null" : "--gpu_backend=deko3d",
      // The full XBLA licence (the SDK's default 0 made the game run as a trial: "Not available in the trial").
      "--license_mask=4294967295",
      // config/hard_corps_uprising.toml: the 16:9 Original guest video mode.
      "--video_mode_width=853",
      "--video_mode_height=480",
      // No ImGui under deko3d: the game's message boxes (save data) are answered with their defaults.
      "--headless=true",
      // The guest's critical sections spin before they sleep, as HCU's old SDK patch did (the base's default 0).
      "--kernel_critsec_spin=256",
      // The base writes an NPDM into atmosphere/contents when it runs as Banjo's forwarder: never for this game.
      "--banjo_forwarder_program_id=0",  // 0 = never (an empty value is not an argument CLI11 takes)
  };
  for (const char* d : defaults) {
    const std::string key(d, std::strchr(d, '=') - d + 1);
    bool given = false;
    for (const auto& a : args) given |= a.compare(0, key.size(), key) == 0;
    if (!given) args.push_back(d);
  }
  std::vector<char*> ptrs;
  for (auto& a : args) ptrs.push_back(a.data());
  ptrs.push_back(nullptr);
  std::printf("[nx] %zu arguments:", args.size());
  for (const auto& a : args) std::printf(" %s", a.c_str());
  std::printf("\n");
  std::fflush(stdout);
  // The voice table (src/xma_voice_table.cpp), before the game's audio starts: the banks of the game folder the base
  // gets (the first --game_data_root, as above; the root form "/switch/hcu/game" opens on the SD).
  for (const auto& a : args)
    if (a.compare(0, 17, "--game_data_root=") == 0) {
      hcu_voice_table_build((a.substr(17) + "/DataXENON/Sound/").c_str(), "sdmc:/switch/hcu/settings.txt");
      break;
    }
  return __real_main(int(args.size()), ptrs.data());
}
