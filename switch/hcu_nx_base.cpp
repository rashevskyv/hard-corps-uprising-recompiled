// The Switch start-up of the hcu port on the ultimate base's SDK (switch/switch-base.cmake). The base owns the process
// (main, threads, memory, the crash hooks, stderr, the exit); this file keeps what HCU's own host layer
// (switch/host/switch_nx.cpp, switch/host/ui/windowed_app_main_nx.cpp, old SDK only) gave the game: the log in
// sdmc:/switch/hcu/, the console defaults of the cvars, args.txt and the marker files, the version, the quick exit.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

#include <atomic>

#include "hcu_pgo.h"
#include "hcu_version.h"  // written by switch/build-base.sh: HCU_VERSION, HCU_BUILD_UTC, HCU_BUILD_ID
#include "settings_menu.h"  // hcu_clocks_key
#include "sysclk_owner.h"

extern "C" {
// libnx (init.c): the exit; with __nx_applet_exit_mode 1 it asks the system to close the application (the base's main,
// sdk/src/ui/windowed_app_main_switch.cpp, ExitProcess).
extern u32 __nx_applet_exit_mode;
[[noreturn]] void __libnx_exit(int rc);
int __real_main(int argc, char* argv[]);
}

namespace {

const char* const kDataDir = "sdmc:/switch/hcu";
Thread log_flush_thread;
volatile bool log_flush_running = false;

// settings.txt clocks=1 (default 0: the game touches no clock): CPU 1785, GPU 768, memory 1600 MHz through clkrst,
// as switch/host/switch_nx.cpp did in the old SDK's build. sys-clk has priority, per module: a module it sets (an
// override, or a title or global profile rate) is its own, and the game makes no pcv call for it; a module it leaves
// ("Do not override", or sys-clk disabled) the game sets. sys-clk is asked again every 2 s, so a change in its
// overlay takes effect. Horizon OC, sys-clk-OC or a sys-clk with an unknown IPC: the game touches no clock.
// No clkrst session stays open: each read or set opens one session and closes it (0.399 held three for the whole run,
// and pcv aborted with 2011-0102, out of session memory; see docs/switch.md).
struct Clock {
  const char* name;
  PcvModuleId module;
  u32 wanted;
  u32 old = 0;      // the rate before the game's set, put back on exit; 0 = nothing to put back
  u32 set = 0;      // the rate the game holds (read back after its set); 0 = the game does not hold the module
  u32 owner = ~0u;  // sys-clk's rate for it, Hz; 0 = left to the game; ~0u = not asked yet
  u32 skip = 0;     // checks to skip (back-off after a re-set or a failed set)
  u32 wait = 1;     // the next back-off: 1, 2, 4, 8, 16 checks (2 to 32 s)
};
constinit Clock clocks[] = {  // userAppInit runs before the C++ constructors
    {"CPU", PcvModuleId_CpuBus, 1785000000},
    {"GPU", PcvModuleId_GPU, 768000000},
    {"memory", PcvModuleId_EMC, 1600000000},
};
constexpr u32 kNoAnswer = 1;            // owner: sys-clk did not answer, so the module is left alone
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
  if (to && hz < to) {
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
              before >= clock.wanted ? "not applied (already at or above)" : R_FAILED(rc) ? "REFUSED" : "applied",
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
    if (now == 0 || now >= clock.set) continue;
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

// The log is buffered and flushed twice a second; fsync commits it to the card (after a hang and a hard reboot, a log
// that was only flushed ended in garbage). Every 4th pass (2 s) keeps the clocks.
void log_flush_loop(void*) {
  for (unsigned pass = 1; log_flush_running; pass++) {
    svcSleepThread(500'000'000LL);
    if (pass % 4 == 0) keep_clocks();
    std::fflush(stdout);
    fsync(fileno(stdout));
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
  restore_clocks();
  std::fflush(stdout);
  fsync(fileno(stdout));
}

// The way out the deko3d backend takes (HOME -> Close, the game's own quit, a GPU queue error): the log first, then the
// base's exit (libnx's, with the applet's Exit, as the base's main ends).
extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why) {
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
  return __real_main(int(args.size()), ptrs.data());
}
