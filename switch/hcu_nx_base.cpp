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

// settings.txt clocks=1 (default 0 = stock, or what sys-clk sets): CPU 1785, GPU 768, memory 1600 MHz through clkrst,
// as switch/host/switch_nx.cpp did in the old SDK's build (0.310 ran with them). The base never sets clocks: up to 0.398
// this build had no clock code at all, so clocks=1 did nothing. The old rates come back on exit.
struct Clock {
  const char* name;
  PcvModuleId module;
  u32 wanted;
  ClkrstSession session;
  bool open;
  u32 old;
  u32 set;
  bool lowered;  // keep_clocks found it below `set` at its last check
};
Clock clocks[] = {
    {"CPU", PcvModuleId_CpuBus, 1785000000},
    {"GPU", PcvModuleId_GPU, 768000000},
    {"memory", PcvModuleId_EMC, 1600000000},
};
bool clkrst_up = false;
bool sysclk = false;                    // sys-clk runs: a module its profile names goes to the profile
std::atomic<bool> clocks_kept{false};   // raise_clocks is done: keep_clocks may read the sessions
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

// Is another process serving this name? (sys-clk's own check.)
bool service_running(const char* name) {
  Handle handle;
  const Result rc = smRegisterService(&handle, smEncodeName(name), false, 1);
  if (R_SUCCEEDED(rc)) {
    smUnregisterService(smEncodeName(name));
    svcCloseHandle(handle);
    return false;
  }
  return rc == MAKERESULT(21, 4);
}

// clocks=0: the rates as they are, read through sessions that are closed again (keep and restore do nothing).
void log_stock_clocks() {
  u32 mhz[3] = {};
  if (hosversionAtLeast(8, 0, 0) && R_SUCCEEDED(clkrstInitialize())) {
    for (int i = 0; i < 3; i++) {
      ClkrstSession s;
      u32 hz = 0;
      if (R_SUCCEEDED(clkrstOpenSession(&s, clocks[i].module, 3))) {
        clkrstGetClockRate(&s, &hz);
        clkrstCloseSession(&s);
      }
      mhz[i] = hz / 1000000;
    }
    clkrstExit();
  }
  std::printf("[clock] settings.txt clocks=0 (or no key): not changed; CPU %u, GPU %u, memory %u MHz; busy-loop "
              "equals %u MHz\n", mhz[0], mhz[1], mhz[2], busy_loop_mhz());
}

void raise_clocks() {
  if (!hcu_clocks_key("sdmc:/switch/hcu/settings.txt")) {
    log_stock_clocks();
    return;
  }
  std::printf("[clock] settings.txt clocks=1 requested: CPU %u, GPU %u, memory %u MHz\n", clocks[0].wanted / 1000000,
              clocks[1].wanted / 1000000, clocks[2].wanted / 1000000);
  // Horizon OC puts stock rates back every tick; fighting it froze consoles (NFSU2's port).
  if (service_running("hoc:clk")) {
    std::printf("[clock] Horizon OC is running: the clocks are left to it (nothing set)\n");
    return;
  }
  // Kefir's sys-clk sets only the modules its profile names: the game sets the others as without it.
  sysclk = service_running("sys:clk");
  if (sysclk) std::printf("[clock] sys-clk is running: the game sets its clocks, a sys-clk profile wins over them\n");
  if (!hosversionAtLeast(8, 0, 0)) {
    std::printf("[clock] firmware below 8.0.0: no clkrst, the clocks are unchanged\n");
    return;
  }
  if (const Result rc = clkrstInitialize(); R_FAILED(rc)) {
    std::printf("[clock] clkrstInitialize failed (rc 0x%X): the clocks are unchanged\n", rc);
    return;
  }
  clkrst_up = true;
  const u32 loop_before = busy_loop_mhz();
  for (Clock& clock : clocks) {
    if (const Result rc = clkrstOpenSession(&clock.session, clock.module, 3); R_FAILED(rc)) {
      std::printf("[clock] %s: no clkrst session (rc 0x%X): unchanged\n", clock.name, rc);
      continue;
    }
    clock.open = true;
    clkrstGetClockRate(&clock.session, &clock.old);
    // Never lower a rate (docked memory, or someone else's overclock).
    const bool apply = clock.old < clock.wanted;
    const Result rc = apply ? clkrstSetClockRate(&clock.session, clock.wanted) : 0;
    u32 now = 0;
    clkrstGetClockRate(&clock.session, &now);
    clock.set = now;
    std::printf("[clock] %s: was %u, requested %u, %s, read back %u MHz\n", clock.name, clock.old / 1000000,
                clock.wanted / 1000000, !apply ? "not applied (already at or above)" : R_FAILED(rc) ? "REFUSED" : "applied",
                now / 1000000);
    if (R_FAILED(rc)) std::printf("[clock] %s: clkrstSetClockRate rc 0x%X\n", clock.name, rc);
  }
  std::printf("[clock] busy-loop equals %u MHz before the raise, %u MHz after it\n", loop_before, busy_loop_mhz());
  clocks_kept.store(true, std::memory_order_release);
}

// Every 2 s (log_flush_loop): HOME, docking and sleep put the system's clocks back: set ours again. The first check
// logs the rates 2 s after the raise (an immediate read-back can show a rate the system takes back later).
void keep_clocks() {
  if (!clocks_kept.load(std::memory_order_acquire)) return;
  u32 hz[3] = {};
  for (int i = 0; i < 3; i++) {
    Clock& clock = clocks[i];
    u32 now = 0;
    if (clock.open) clkrstGetClockRate(&clock.session, &now);
    hz[i] = now;
    if (clock.set == 0 || now == 0 || now >= clock.set) {
      clock.lowered = false;
      continue;
    }
    // sys-clk: lower again 2 s after our re-set = its profile (it sets it every tick). The profile wins; the old rate
    // does not come back on exit either.
    if (sysclk && clock.lowered) {
      std::printf("[clock] %s held at %u MHz by a sys-clk profile: left to it\n", clock.name, now / 1000000);
      clock.set = clock.old = 0;
      continue;
    }
    clock.lowered = true;
    clkrstSetClockRate(&clock.session, clock.set);
    std::printf("[clock] %s was put back to %u MHz: set to %u MHz again\n", clock.name, now / 1000000,
                clock.set / 1000000);
  }
  if (keep_checks++ == 0) {
    std::printf("[clock] 2 s after the raise: CPU %u, GPU %u, memory %u MHz; busy-loop equals %u MHz\n",
                hz[0] / 1000000, hz[1] / 1000000, hz[2] / 1000000, busy_loop_mhz());
  }
}

void restore_clocks() {
  if (!clkrst_up) return;
  clocks_kept.store(false, std::memory_order_release);
  for (Clock& clock : clocks) {
    if (!clock.open) continue;
    if (clock.old != 0 && clock.set != clock.old) clkrstSetClockRate(&clock.session, clock.old);
    clkrstCloseSession(&clock.session);
  }
  clkrstExit();
  clkrst_up = false;
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
