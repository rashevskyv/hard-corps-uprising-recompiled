// main() for the Switch build: the SDK's windowed_app_main_sdl.cpp without SDL. The game data and
// user data live in sdmc:/switch/hcu/.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <switch.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window_nx.h>
#include <rex/ui/windowed_app.h>

namespace {
// Step marks in sdmc:/switch/hcu/hcu_log.txt (stdout), flushed at once: a crash names the step.
void step(const char* what) {
  std::printf("[nx] %s\n", what);
  std::fflush(stdout);
}
}  // namespace

int main(int argc, char* argv[]) {
  // Defaults for the console; a hcu.toml next to the NRO or arguments from hbmenu override them.
  std::vector<std::string> args(argv, argv + argc);
  if (args.empty()) {
    // Eden starts the NRO with no arguments at all; cvar::Init skips argv[0] as the program name.
    args.push_back("hcu");
  }
  // No-GPU mode (Eden, which cannot run NVK): the file sdmc:/switch/hcu/nogpu selects the plugin's
  // null backend: the command processor consumes the ring buffer and fires vblanks, nothing is drawn,
  // no presenter; the XEX, the kernel, memory, threads, files and input are real.
  const bool no_gpu = access("sdmc:/switch/hcu/nogpu", F_OK) == 0;
  // The native renderer (src/gpu/deko3d_backend.cpp): the file sdmc:/switch/hcu/deko3d selects it
  // (it wins over nogpu, so Eden's staging can keep both files).
  const bool deko3d = access("sdmc:/switch/hcu/deko3d", F_OK) == 0;
  std::vector<const char*> defaults = {
      "--game_data_root=sdmc:/switch/hcu/game",
      "--user_data_root=sdmc:/switch/hcu/user",
      "--fullscreen=true",
      "--log_file=sdmc:/switch/hcu/hcu_rex_log.txt",
      "--gpu_plugin=xenos",
  };
  if (deko3d) {
    defaults.push_back("--gpu_backend=deko3d");
  } else if (no_gpu) {
    defaults.push_back("--gpu_backend=null");
  }
  for (const char* d : defaults) {
    std::string key(d, std::string(d).find('='));
    bool given = false;
    for (const auto& a : args) {
      if (a.compare(0, key.size(), key) == 0) given = true;
    }
    if (!given) args.push_back(d);
  }
  std::vector<char*> argv_ptrs;
  for (auto& a : args) argv_ptrs.push_back(a.data());
  std::printf("[nx] %zu arguments:", args.size());
  for (const auto& a : args) std::printf(" %s", a.c_str());
  std::printf("\n");
  std::fflush(stdout);

  auto remaining = rex::cvar::Init(int(argv_ptrs.size()), argv_ptrs.data());
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();
  step("cvars and logging up");

  int result;
  {
    rex::ui::NXWindowedAppContext app_context;
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }
    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));
    step("app created, OnInitialize (window, Vulkan, runtime, XEX)");
    if (app->OnInitialize()) {
      step("OnInitialize done, entering the UI loop");
      result = app_context.RunMainMessageLoop();
      step("UI loop ended");
    } else {
      step("OnInitialize failed");
      result = EXIT_FAILURE;
    }
    app->InvokeOnDestroy();
    step("app destroyed");
  }
  return result;
}
