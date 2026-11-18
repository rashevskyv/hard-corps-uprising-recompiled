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

#include "hcu_pgo.h"
#include "hcu_version.h"  // written by switch/build-base.sh: HCU_VERSION, HCU_BUILD_UTC, HCU_BUILD_ID

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

// The log is buffered and flushed twice a second; fsync commits it to the card (after a hang and a hard reboot, a log
// that was only flushed ended in garbage).
void log_flush_loop(void*) {
  while (log_flush_running) {
    svcSleepThread(500'000'000LL);
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
  // sdmc:/switch/hcu/nogpu selects the plugin's null backend instead (nothing drawn).
  const bool no_gpu = access("sdmc:/switch/hcu/nogpu", F_OK) == 0;
  const char* const defaults[] = {
      "--game_data_root=sdmc:/switch/hcu/game",
      "--user_data_root=sdmc:/switch/hcu/user",
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
