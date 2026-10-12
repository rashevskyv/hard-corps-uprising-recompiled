#pragma once

#include <cstdint>

namespace hcu {

constexpr uint32_t kSystemUiNotification = 9;

// How the offline port ends a request for the Xbox Marketplace. The game announces the system UI to its own
// listeners before it asks (XN_SYS_UI, opened) and holds the engine's UI event until the console reports it closed
// (XN_SYS_UI, closed). With no UI to wait for, the request succeeds at once and the closed notification follows.
struct MarketplaceOutcome {
  uint32_t status;
  uint32_t notification;
  uint32_t notification_data;
};

constexpr MarketplaceOutcome OfflineMarketplaceOutcome() {
  return {0, kSystemUiNotification, 0};
}

}  // namespace hcu
