#pragma once

#include <filesystem>
#include <functional>

#include "live_stats.h"

namespace rex::system {
class KernelState;
}  // namespace rex::system

namespace hcu::live {

// The app side of what the game asks the host for. Both run on a guest thread.
struct AppBridge {
  std::function<void()> open_achievements;
  std::function<void()> open_download_content;
};

struct InstallParams {
  std::filesystem::path stats_file;
  LocalUser user;
  AppBridge bridge;
  rex::system::KernelState* kernel = nullptr;
};

// Called once, before the game starts.
void Install(InstallParams params);

}  // namespace hcu::live
