#include "live_fix.h"

#include <atomic>
#include <utility>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "achievement_alias.h"
#include "guest_memory.h"
#include "marketplace_rules.h"

namespace hcu::live {
namespace {

struct Services {
  Services(std::filesystem::path file, LocalUser user) : stats(std::move(file)), board(stats, std::move(user)) {}

  LocalStats stats;
  LocalLeaderboard board;
  EnumeratorRegistry enumerators;
};

// Never freed: hooks may still run while the process exits.
std::atomic<Services*> g_services{nullptr};
std::atomic<rex::system::KernelState*> g_kernel{nullptr};
std::atomic<bool> g_alias_ready{false};
AppBridge g_bridge;

// The game asks for the Live state in many places, some of them online features. Only the functions that run the
// local scores and leaderboards open this scope, and inside it user 0 reads as signed in to Live.
thread_local int t_live_scope = 0;

struct LiveScope {
  LiveScope() { ++t_live_scope; }
  ~LiveScope() { --t_live_scope; }
};

Services* Active() { return g_services.load(std::memory_order_acquire); }

bool Succeeded(PPCContext& ctx, uint8_t* base, uint32_t overlapped) {
  uint32_t status = ctx.r3.u32;
  if (status == kErrorIoPending && overlapped) status = LoadBE32(base, overlapped);
  return status == kErrorSuccess;
}

bool CreateStatsEnumerator(PPCContext& ctx, uint8_t* base, StatsQuery::Kind kind) {
  Services* services = Active();
  if (!services || ctx.r3.u32 != 0) return false;

  const uint64_t pivot = kind == StatsQuery::Kind::kRanked ? ctx.r4.u32 : ctx.r4.u64;
  ctx.r3.u64 = CreateEnumerator(base, services->enumerators, kind, pivot, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32,
                                ctx.r8.u32, ctx.r9.u32);
  return true;
}

}  // namespace

void Install(InstallParams params) {
  g_bridge = std::move(params.bridge);
  g_kernel.store(params.kernel, std::memory_order_release);
  if (params.kernel) {
    const bool aliased = RegisterZeroAchievementAlias(*params.kernel);
    g_alias_ready.store(aliased, std::memory_order_release);
    if (!aliased) REXLOG_WARN("Achievement 0 could not be registered; it cannot unlock");
  }

  auto* services = new Services(std::move(params.stats_file), std::move(params.user));
  if (services->stats.Load() == LoadResult::kCorrupt) {
    REXLOG_WARN("Offline stats file could not be read; it was moved to stats.toml.corrupt");
  }
  g_services.store(services, std::memory_order_release);
}

}  // namespace hcu::live

using namespace hcu;
using namespace hcu::live;

// IsLive(players, index): the game's own check that a player is signed in to Xbox LIVE.
REX_EXTERN(__imp__sub_824F45E0);
REX_HOOK_RAW(sub_824F45E0) {
  if (t_live_scope > 0 && ctx.r4.u32 == 0 && Active()) {
    __imp__sub_824F45E0(ctx, base);
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_824F45E0(ctx, base);
}

// Title menu: enables or greys the items that need Live. The leaderboards run on local data, so they are enabled;
// the online items stay greyed because they also need the multiplayer privilege, which the runtime denies.
REX_EXTERN(__imp__sub_824F6870);
REX_HOOK_RAW(sub_824F6870) {
  if (Active() && ctx.r4.u32 == 0) {
    ctx.r4.u64 = 1;
  }
  __imp__sub_824F6870(ctx, base);
}

// Leaderboards screen update: it shows its sign-in popup while the player is not Live.
REX_EXTERN(__imp__sub_82534AC8);
REX_HOOK_RAW(sub_82534AC8) {
  LiveScope scope;
  __imp__sub_82534AC8(ctx, base);
}

// Pause menu: it greys its Leaderboards item while the player is not Live, at creation and whenever the state is
// polled. The Live scope enables just that item.
REX_EXTERN(__imp__sub_825164F0);
REX_HOOK_RAW(sub_825164F0) {
  LiveScope scope;
  __imp__sub_825164F0(ctx, base);
}

REX_EXTERN(__imp__sub_82516608);
REX_HOOK_RAW(sub_82516608) {
  LiveScope scope;
  __imp__sub_82516608(ctx, base);
}

// Score submission after a finished game: it only writes for a Live player.
REX_EXTERN(__imp__sub_82545728);
REX_HOOK_RAW(sub_82545728) {
  LiveScope scope;
  __imp__sub_82545728(ctx, base);
}

// XSessionWriteStats(session, xuid, view count, views, overlapped): the stats of a finished game.
REX_EXTERN(__imp__sub_827CC008);
REX_HOOK_RAW(sub_827CC008) {
  const uint64_t xuid = ctx.r4.u64;
  const uint32_t view_count = ctx.r5.u32;
  const uint32_t views = ctx.r6.u32;
  const uint32_t overlapped = ctx.r7.u32;
  __imp__sub_827CC008(ctx, base);
  Services* services = Active();
  if (!services || !Succeeded(ctx, base, overlapped)) return;
  if (xuid != services->board.user().xuid) return;

  for (const ViewWrite& write : ParseViewWrites(base, views, view_count)) {
    if (services->stats.Record(xuid, write.view, write.stats) == RecordResult::kNotSaved) {
      REXLOG_WARN("Offline stats could not be written to disk; the score is kept for this session only");
    }
  }
}

// XUserCreateStatsEnumeratorByRank / ByXuid(title, pivot, rows, spec count, specs, size, handle): ReXGlue only
// stubs the underlying call.
REX_EXTERN(__imp__sub_825705F0);
REX_HOOK_RAW(sub_825705F0) {
  if (!CreateStatsEnumerator(ctx, base, StatsQuery::Kind::kRanked)) __imp__sub_825705F0(ctx, base);
}

REX_EXTERN(__imp__sub_82570650);
REX_HOOK_RAW(sub_82570650) {
  if (!CreateStatsEnumerator(ctx, base, StatsQuery::Kind::kAround)) __imp__sub_82570650(ctx, base);
}

// XFriendsCreateEnumerator(user, flags, count, size, handle): ReXGlue fails it and leaves the handle unwritten.
// An offline player has no friends, so the game gets an enumerator with nothing in it.
REX_EXTERN(__imp__sub_827CB240);
REX_HOOK_RAW(sub_827CB240) {
  Services* services = Active();
  if (!services) {
    __imp__sub_827CB240(ctx, base);
    return;
  }
  ctx.r3.u64 = CreateFriendsEnumerator(base, services->enumerators, ctx.r6.u32, ctx.r7.u32);
}

// XamEnumerate(handle, buffer, size, items, overlapped). Other handles (content, achievements) are ReXGlue's.
REX_EXTERN(__imp__sub_82570E78);
REX_HOOK_RAW(sub_82570E78) {
  if (Services* services = Active()) {
    const uint32_t handle = ctx.r3.u32;
    if (const auto query = services->enumerators.Take(handle)) {
      ctx.r3.u64 = ServeEnumeration(base, services->board, *query, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
      return;
    }
  }
  __imp__sub_82570E78(ctx, base);
}

// XUserReadStats(title, xuid count, xuids, spec count, specs, size ptr, results, overlapped): the game's stats and
// its friends'. The game compares a new result with the stored one before it submits, so this has to be true.
REX_EXTERN(__imp__sub_82570518);
REX_HOOK_RAW(sub_82570518) {
  const uint32_t xuid_count = ctx.r4.u32;
  const uint32_t xuids = ctx.r5.u32;
  const uint32_t spec_count = ctx.r6.u32;
  const uint32_t specs = ctx.r7.u32;
  const uint32_t size_ptr = ctx.r8.u32;
  const uint32_t results = ctx.r9.u32;
  const uint32_t overlapped = ctx.r10.u32;
  __imp__sub_82570518(ctx, base);
  Services* services = Active();
  if (!services || !results || !size_ptr || !Succeeded(ctx, base, overlapped)) return;

  ServeReadStats(base, services->board, xuid_count, xuids, spec_count, specs, LoadBE32(base, size_ptr), results);
}

// XUserWriteAchievements(count, entries, overlapped).
REX_EXTERN(__imp__sub_825702A0);
REX_HOOK_RAW(sub_825702A0) {
  std::vector<uint32_t> rewritten;
  if (g_alias_ready.load(std::memory_order_acquire)) {
    rewritten = AliasZeroAchievementIds(base, ctx.r4.u32, ctx.r3.u32);
  }
  __imp__sub_825702A0(ctx, base);
  RestoreZeroAchievementIds(base, rewritten);
}

// XamShowAchievementsUI(user): opens the achievements overlay instead of ReXGlue's stub.
REX_EXTERN(__imp__sub_82570B68);
REX_HOOK_RAW(sub_82570B68) {
  if (!g_bridge.open_achievements) {
    __imp__sub_82570B68(ctx, base);
    return;
  }
  g_bridge.open_achievements();
  ctx.r3.u64 = kErrorSuccess;
}

// XamShowMarketplaceUI(user, type, offer, ...): there is no Marketplace. The game has already told its listeners
// that system UI opened, so it must also hear that it closed or the engine stays held.
REX_EXTERN(__imp__sub_82570B70);
REX_HOOK_RAW(sub_82570B70) {
  rex::system::KernelState* kernel = g_kernel.load(std::memory_order_acquire);
  if (!g_bridge.open_download_content || !kernel) {
    __imp__sub_82570B70(ctx, base);
    return;
  }
  const MarketplaceOutcome outcome = OfflineMarketplaceOutcome();
  g_bridge.open_download_content();
  ctx.r3.u64 = outcome.status;
  kernel->BroadcastNotification(outcome.notification, outcome.notification_data);
}
