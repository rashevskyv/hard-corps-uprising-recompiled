// CPU work per frame, per thread: the measure that carries over between the console, Eden and the Linux
// test host (Eden is GPU-bound for HCU, so its fps hides CPU changes). Wall time between frame marks minus
// the time the thread waited:
//   main thread  VdSwap to VdSwap, minus waiting kernel exports (REX_HOOK wrapper, nx_hook_counter.h) and
//                the guest's ring wait sub_82556728 (it spins until the command processor reads past a point);
//   GPU commands swap to swap, minus its idle waits and WAIT_REG_MEM polls (rex_nx_cp_wait_ticks, set by the
//                SDK edit in switch/sdk-edits-hcu.py) and the backend's fence wait and acquire;
//   audio        the audio client callbacks per frame (switch/audio_client_threads.inc, Switch only).
// One line every 30 s ([nx] cpu per frame ... p50/p95/p99 ms), the main thread's three longest waits by export
// ([nx] main waits top: <export> <ms>, ... per frame) and rex_nx_meter_text for the on-screen strip.
// The same split by drawn and skipped frames ([nx] gpu commands per swap ..., [nx] main per frame by its swap ...):
// which thread makes the 33 ms frames.
// Switch: the FPCR switches per second, all guest contexts and the four busiest ([nx] fpcr ..., the SDK counter of
// switch/sdk-edits-hcu.py in rex/ppc/context.h).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <utility>

#define REX_NX_METER_IMPL  // the real meter entry points, not the header's weak defaults
#include <rex/chrono/clock.h>
#include <rex/hook.h>
#include <rex/nx_hook_counter.h>  // rex/hook.h includes it only with HCU_NX_METER

#include "settings_menu.h"

extern "C" volatile uint64_t rex_nx_audio_cb_us;
extern "C" char rex_nx_meter_text[48];

namespace {

constexpr int kBins = 1001;  // 0.1 ms bins, the last one holds everything from 100 ms up

struct Hist {
  uint32_t bin[kBins] = {};
  uint32_t n = 0;
  void Add(double ms) {
    bin[std::clamp(int(ms * 10.0), 0, kBins - 1)]++;
    n++;
  }
  double Pct(double p) const {
    const uint64_t want = uint64_t(p * double(n));
    uint64_t acc = 0;
    for (int i = 0; i < kBins; i++) {
      acc += bin[i];
      if (acc > want) return (i + 1) * 0.1;
    }
    return 100.0;
  }
  double Over(double ms) const {  // percent of the samples at or over ms
    uint64_t c = 0;
    for (int i = int(ms * 10.0); i < kBins; i++) c += bin[i];
    return n ? c * 100.0 / n : 0.0;
  }
};

uint64_t Now() { return rex::chrono::Clock::QueryHostTickCount(); }
double ToMs(uint64_t ticks) { return double(ticks) * 1000.0 / double(rex::chrono::Clock::QueryHostTickFrequency()); }

thread_local int t_depth = 0;
thread_local uint64_t t_wait_ticks = 0;
thread_local uint64_t t_ring_ticks = 0;  // the part of t_wait_ticks in the guest ring wait
thread_local bool t_main = false;        // this thread calls VdSwap (the main thread)
uint64_t g_ring_main_wait = 0;           // the main thread's ticks in the ring wait (a row of the waits top)

Hist g_main_work, g_main_wait, g_main_ring, g_cp_work, g_gpu_frame, g_shown;
uint64_t g_main_last = 0, g_main_last_wait = 0, g_main_last_ring = 0, g_report_tick = 0;
uint64_t g_cp_last = 0, g_cp_last_wait = 0, g_cp_last_block_us = 0, g_shown_last = 0;
uint64_t g_audio_last_us = 0;
uint32_t g_frames = 0;

// Drawn and skipped frames apart (30skip draws every second guest frame and skips one the GPU is still busy for):
// the command processor's swap-to-swap time and work per swap, taken where the SDK's [nx] frame times line takes
// its time (the swap packet), and the main thread's frames joined to the swaps by number (the Nth VdSwap is the
// command processor's Nth swap; the join runs in Report, so a swap not processed yet waits for the next one).
constexpr uint32_t kRing = 4096;
struct MainRec { uint32_t n; float wall, work; };
MainRec g_main_rec[kRing];   // main thread: its frame ending at VdSwap n (wall < 0: none, the first one)
uint32_t g_main_n = 0;       // main thread: VdSwaps so far
uint64_t g_cp_rec[kRing];    // command processor: n << 1 | drawn
uint32_t g_cp_n = 0;         // command processor: swaps so far (release store after its record)
uint32_t g_join_n = 0;       // main thread: swaps joined so far
// [0] skipped, [1] drawn: command processor frame and work; the main thread's frame and work by its own swap; its
// frame by the swap before (the one the main thread's ring wait waits for).
Hist g_cpf[2], g_cpw[2], g_mf[2], g_mw[2], g_mf_prev[2];
uint64_t g_any_last = 0, g_any_last_wait = 0, g_any_last_block_us = 0;

uint64_t ThreadKey() { return std::hash<std::thread::id>{}(std::this_thread::get_id()); }
uint64_t g_main_key = 0;  // ThreadKey of the thread that calls VdSwap

#if defined(__SWITCH__) && !defined(HCU_BASE_SDK)  // the old SDK's FPCR counter (the base has its census)
constexpr int kFpcrSlots = 64;
rex::ppc::RexNxFpcrCount g_fpcr[kFpcrSlots];
int g_fpcr_n = 0;

// Calls and writes per second since the last report, all contexts and the four with the most calls.
void ReportFpcr(double secs) {
  static uint64_t last[kFpcrSlots][3];
  const int n = std::min(__atomic_load_n(&g_fpcr_n, __ATOMIC_RELAXED), kFpcrSlots);
  struct Row { int i; uint64_t e, d, w; } top[4] = {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}};
  uint64_t te = 0, td = 0, tw = 0;
  for (int i = 0; i < n; i++) {
    const uint64_t v[3] = {__atomic_load_n(&g_fpcr[i].enable, __ATOMIC_RELAXED), __atomic_load_n(&g_fpcr[i].disable, __ATOMIC_RELAXED),
                           __atomic_load_n(&g_fpcr[i].writes, __ATOMIC_RELAXED)};
    Row r{i, v[0] - last[i][0], v[1] - last[i][1], v[2] - last[i][2]};
    std::memcpy(last[i], v, sizeof(v));
    te += r.e;
    td += r.d;
    tw += r.w;
    for (Row& t : top) if (r.e + r.d > t.e + t.d) std::swap(r, t);
  }
  if (secs <= 0) return;
  char busy[256] = "none";
  int len = 0;
  for (const Row& t : top) {
    if (t.i < 0 || !(t.e + t.d)) break;
    len += std::snprintf(busy + len, sizeof(busy) - len, "%s#%d%s e %.0f d %.0f w %.0f", len ? ", " : "", t.i,
                         g_fpcr[t.i].owner == g_main_key ? " main" : "", t.e / secs, t.d / secs, t.w / secs);
    if (len >= int(sizeof(busy))) break;
  }
#if defined(REX_FPCR_ONE_MODE)
  const char* mode = "on";
#else
  const char* mode = "off";
#endif
  std::printf("[nx] fpcr (30 s, one-mode %s, %d contexts): per s enable %.0f, disable %.0f, writes %.0f; busiest: %s\n", mode, n,
              te / secs, td / secs, tw / secs, busy);
}
#endif

void Report(uint64_t now) {
  const uint64_t audio_us = __atomic_load_n(&rex_nx_audio_cb_us, __ATOMIC_RELAXED);
  const double audio_ms = g_frames ? double(audio_us - g_audio_last_us) / 1000.0 / g_frames : 0.0;
  g_audio_last_us = audio_us;
  char cp[96] = "gpu commands work - (no swaps from this backend)";
  if (g_cp_work.n)
    std::snprintf(cp, sizeof(cp), "gpu commands work p50 %.1f p95 %.1f p99 %.1f ms", g_cp_work.Pct(0.5), g_cp_work.Pct(0.95),
                  g_cp_work.Pct(0.99));
  char gpu[80] = "";
  if (g_gpu_frame.n)
    std::snprintf(gpu, sizeof(gpu), "; gpu frame p50 %.1f p95 %.1f p99 %.1f ms", g_gpu_frame.Pct(0.5), g_gpu_frame.Pct(0.95),
                  g_gpu_frame.Pct(0.99));
  std::printf("[nx] cpu per frame (30 s, %u frames): main work p50 %.1f p95 %.1f p99 %.1f ms (waits p50 %.1f, of it ring wait p50 %.1f); %s; audio %.2f ms%s\n",
              g_main_work.n, g_main_work.Pct(0.5), g_main_work.Pct(0.95), g_main_work.Pct(0.99), g_main_wait.Pct(0.5), g_main_ring.Pct(0.5), cp,
              audio_ms, gpu);
  // The main thread's waits by export (HookCounter::main_wait, outermost calls only, as in "waits" above): the top three.
  struct Row { const char* name; uint64_t ticks; } top[3] = {};
  auto offer = [&](const char* name, uint64_t& ticks) {
    Row r{name, ticks};
    ticks = 0;
    for (Row& t : top) if (r.ticks > t.ticks) std::swap(r, t);
  };
  for (rex_nx::HookCounter* h = rex_nx::HookCounter::head(); h; h = h->next) if (h->kind == 1) offer(h->name, h->main_wait);
  offer("sub_82556728 (ring wait)", g_ring_main_wait);
  char waits[160] = "none";
  int len = 0;
  for (const Row& t : top) {
    if (!t.ticks || !g_frames) break;
    len += std::snprintf(waits + len, sizeof(waits) - len, "%s%s %.2f", len ? ", " : "", t.name, ToMs(t.ticks) / g_frames);
    if (len >= int(sizeof(waits))) break;
  }
  std::printf("[nx] main waits top: %s ms per frame\n", waits);
  // Game speed: the guest's frames (VdSwap) per second against 60 (the game is frame-locked); shown frames: the interval
  // between presents (30skip draws every second frame: 33.3 ms when even).
  const double secs = ToMs(now - g_report_tick) / 1000.0;
  uint32_t le34 = 0;
  for (int i = 0; i <= 340; i++) le34 += g_shown.bin[i];
  // fps_mode and the main thread frames with over 16.7 ms of work (they cannot keep 60 guest frames/s).
  std::printf("[nx] game speed %.1f %% (%.1f guest frames/s); shown %.1f/s, interval p50 %.1f p95 %.1f p99 %.1f ms, %.1f %% <= 34 ms; fps_mode %d, main work >= 16.7 ms %.1f %% of frames\n",
              secs > 0 ? g_main_work.n / secs / 60.0 * 100.0 : 0.0, secs > 0 ? g_main_work.n / secs : 0.0, secs > 0 ? g_shown.n / secs : 0.0,
              g_shown.Pct(0.5), g_shown.Pct(0.95), g_shown.Pct(0.99), g_shown.n ? le34 * 100.0 / g_shown.n : 0.0, hcu_settings.fps_mode,
              g_main_work.Over(16.7));
  {
    const uint32_t cp_n = __atomic_load_n(&g_cp_n, __ATOMIC_ACQUIRE);
    const uint32_t end = std::min(cp_n, g_main_n);
    if (end > kRing && g_join_n < end - kRing) g_join_n = end - kRing;  // older records are overwritten
    for (; g_join_n < end; g_join_n++) {
      const uint64_t c = __atomic_load_n(&g_cp_rec[g_join_n % kRing], __ATOMIC_RELAXED);
      const MainRec& m = g_main_rec[g_join_n % kRing];
      if (uint32_t(c >> 1) != g_join_n || m.n != g_join_n || m.wall < 0) continue;
      g_mf[c & 1].Add(m.wall);
      g_mw[c & 1].Add(m.work);
      const uint64_t p = g_join_n ? __atomic_load_n(&g_cp_rec[(g_join_n - 1) % kRing], __ATOMIC_RELAXED) : 0;
      if (g_join_n && uint32_t(p >> 1) == g_join_n - 1) g_mf_prev[p & 1].Add(m.wall);
    }
    auto row = [](char* out, size_t size, const char* what, const Hist& f, const Hist& w) {
      std::snprintf(out, size, "%s %u: frame p50 %.1f p95 %.1f p99 %.1f, %.1f %% >= 25 ms, work p50 %.1f p95 %.1f p99 %.1f", what, f.n,
                    f.Pct(0.5), f.Pct(0.95), f.Pct(0.99), f.Over(25.0), w.Pct(0.5), w.Pct(0.95), w.Pct(0.99));
    };
    char d[160], s[160];
    row(d, sizeof(d), "drawn", g_cpf[1], g_cpw[1]);
    row(s, sizeof(s), "skipped", g_cpf[0], g_cpw[0]);
    std::printf("[nx] gpu commands per swap (30 s): %s; %s\n", d, s);
    row(d, sizeof(d), "drawn", g_mf[1], g_mw[1]);
    row(s, sizeof(s), "skipped", g_mf[0], g_mw[0]);
    std::printf("[nx] main per frame by its swap (30 s, joined to #%u, cp #%u, main #%u): %s; %s; by the swap before: after drawn %u, %.1f %% >= 25 ms, after skipped %u, %.1f %% >= 25 ms\n",
                g_join_n, cp_n, g_main_n, d, s, g_mf_prev[1].n, g_mf_prev[1].Over(25.0), g_mf_prev[0].n, g_mf_prev[0].Over(25.0));
    for (Hist* h : {g_cpf, g_cpw, g_mf, g_mw, g_mf_prev}) h[0] = h[1] = Hist{};
  }
#if defined(__SWITCH__) && !defined(HCU_BASE_SDK)
  ReportFpcr(secs);
#endif
  std::fflush(stdout);
  // The strip: m = main thread work, c = GPU command thread work, g = GPU frame (p50:p95, ms).
  std::snprintf(rex_nx_meter_text, sizeof(rex_nx_meter_text), "m %.1f:%.1f c %.1f:%.1f g %.1f:%.1f", g_main_work.Pct(0.5),
                g_main_work.Pct(0.95), g_cp_work.Pct(0.5), g_cp_work.Pct(0.95), g_gpu_frame.n ? g_gpu_frame.Pct(0.5) : 0.0,
                g_gpu_frame.n ? g_gpu_frame.Pct(0.95) : 0.0);
  g_main_work = Hist{};
  g_main_wait = Hist{};
  g_main_ring = Hist{};
  g_cp_work = Hist{};
  g_gpu_frame = Hist{};
  g_shown = Hist{};
  g_frames = 0;
  g_report_tick = now;
}

}  // namespace

extern "C" {
volatile uint64_t rex_nx_cp_wait_ticks = 0;  // the command processor's waits (host ticks), SDK edit
volatile uint64_t rex_nx_audio_cb_us = 0;     // audio client callback time (us), audio_client_threads.inc
char rex_nx_meter_text[48] = "";              // for the on-screen strip (deko3d backend)

uint64_t rex_nx_meter_enter() {
  t_depth++;
  return Now();
}

// kind: 1 = a waiting export, 2 = VdSwap (the main thread's frame mark), 3 = the guest ring wait.
// main_wait: the export's main-thread wait sum (HookCounter::main_wait), null for none.
void rex_nx_meter_leave(uint64_t t0, int kind, uint64_t* main_wait) {
  const uint64_t now = Now();
  if (--t_depth == 0 && (kind == 1 || kind == 3)) {
    t_wait_ticks += now - t0;
    if (kind == 3) t_ring_ticks += now - t0;
    if (t_main && main_wait) *main_wait += now - t0;
  }
  if (kind != 2) return;
  if (!t_main) g_main_key = ThreadKey();
  t_main = true;
  MainRec& rec = g_main_rec[g_main_n % kRing];
  rec = {g_main_n++, -1.f, -1.f};
  if (g_main_last) {
    const uint64_t wall = now - g_main_last, wait = t_wait_ticks - g_main_last_wait;
    g_main_work.Add(ToMs(wall > wait ? wall - wait : 0));
    g_main_wait.Add(ToMs(wait));
    g_main_ring.Add(ToMs(t_ring_ticks - g_main_last_ring));
    g_frames++;
    rec.wall = float(ToMs(wall));
    rec.work = float(ToMs(wall > wait ? wall - wait : 0));
  }
  g_main_last = now;
  g_main_last_wait = t_wait_ticks;
  g_main_last_ring = t_ring_ticks;
  if (!g_report_tick) g_report_tick = now;
  if (ToMs(now - g_report_tick) >= 30000.0) Report(now);
}

// A present (deko3d backend, the command processor thread): the shown-frame interval.
void rex_nx_meter_present() {
  const uint64_t now = Now();
  if (g_shown_last) g_shown.Add(ToMs(now - g_shown_last));
  g_shown_last = now;
}

#if defined(__SWITCH__) && !defined(HCU_BASE_SDK)
// A guest context's first FPCR switch (rex/ppc/context.h): its counter slot.
__attribute__((noinline, cold)) rex::ppc::RexNxFpcrCount* rex_nx_fpcr_count_new() {
  int i = __atomic_fetch_add(&g_fpcr_n, 1, __ATOMIC_RELAXED);
  // ponytail: past 64 contexts the last slot is shared (its counts lose increments); more slots if the log shows 64.
  if (i >= kFpcrSlots) i = kFpcrSlots - 1;
  g_fpcr[i].owner = ThreadKey();
  return &g_fpcr[i];
}
#endif

// The GPU's own time for one frame (deko3d backend, two GPU timestamps; Switch and Eden).
void rex_nx_meter_gpu_frame(double ms) { g_gpu_frame.Add(ms); }

// On the command processor thread at each swap; block_us_total: the backend's fence wait + acquire so far.
void rex_nx_meter_cp_swap(uint64_t block_us_total) {
  const uint64_t now = Now();
  const uint64_t wait_total = rex_nx_cp_wait_ticks;
  if (g_cp_last) {
    const double wall = ToMs(now - g_cp_last);
    const double wait = ToMs(wait_total - g_cp_last_wait) + double(block_us_total - g_cp_last_block_us) / 1000.0;
    g_cp_work.Add(std::max(0.0, wall - wait));
  }
  g_cp_last = now;
  g_cp_last_wait = wait_total;
  g_cp_last_block_us = block_us_total;
}

// On the command processor thread at the start of every swap, drawn or skipped (drawn: this frame's draws were
// recorded): the drawn/skipped split of Report.
void rex_nx_meter_cp_frame(uint64_t block_us_total, int drawn) {
  const uint64_t now = Now();
  const uint64_t wait_total = rex_nx_cp_wait_ticks;
  const int d = drawn ? 1 : 0;
  if (g_any_last) {
    const double wall = ToMs(now - g_any_last);
    const double wait = ToMs(wait_total - g_any_last_wait) + double(block_us_total - g_any_last_block_us) / 1000.0;
    g_cpf[d].Add(wall);
    g_cpw[d].Add(std::max(0.0, wall - wait));
  }
  g_any_last = now;
  g_any_last_wait = wait_total;
  g_any_last_block_us = block_us_total;
  const uint32_t n = __atomic_load_n(&g_cp_n, __ATOMIC_RELAXED);
  __atomic_store_n(&g_cp_rec[n % kRing], uint64_t(n) << 1 | uint64_t(d), __ATOMIC_RELAXED);
  __atomic_store_n(&g_cp_n, n + 1, __ATOMIC_RELEASE);
}
}

// The guest's ring wait (the XDK spins here until the command processor reads past a ring position): wait time.
REX_EXTERN(__imp__sub_82556728);
REX_HOOK_RAW(sub_82556728) {
  const uint64_t t0 = rex_nx_meter_enter();
  __imp__sub_82556728(ctx, base);
  rex_nx_meter_leave(t0, 3, &g_ring_main_wait);
}

#if defined(HCU_NX_METER)
// The collision pass (sub_824A9DC0, -meter flavour only): it sorts the objects into two groups on its own stack and
// keeps only the counts in registers. Its frame is 2224 bytes: group 1 from r1+96 (up to 255 entries, to r1+1116),
// group 2 from r1+1120 (240 entries before its register save area). The hook zeroes both arrays first (the stack
// below r1 is free at the entry), then counts the entries the pass filled (object pointers, never 0). An early-out
// pass (flag bit 0 of +152) fills none. Read by the deko3d backend's [nx] load line, once per second.
namespace {
std::atomic<uint32_t> g_collide_a{0}, g_collide_b{0}, g_collide_calls{0};

uint32_t Filled(const uint8_t* base, uint32_t at, uint32_t max) {
  uint32_t n = 0, v;
  while (n < max && (std::memcpy(&v, base + at + 4 * n, 4), v != 0)) n++;
  return n;
}
}  // namespace

extern "C" void hcu_meter_collision(uint32_t* a, uint32_t* b, uint32_t* calls) {
  *a = g_collide_a.exchange(0, std::memory_order_relaxed);
  *b = g_collide_b.exchange(0, std::memory_order_relaxed);
  *calls = g_collide_calls.exchange(0, std::memory_order_relaxed);
}

REX_EXTERN(__imp__sub_824A9DC0);
REX_HOOK_RAW(sub_824A9DC0) {
  const uint32_t frame = ctx.r1.u32 - 2224;
  std::memset(base + frame + 96, 0, 255 * 4);
  std::memset(base + frame + 1120, 0, 240 * 4);
  __imp__sub_824A9DC0(ctx, base);
  const uint32_t a = Filled(base, frame + 96, 255), b = Filled(base, frame + 1120, 240);
  if (a > g_collide_a.load(std::memory_order_relaxed)) g_collide_a.store(a, std::memory_order_relaxed);
  if (b > g_collide_b.load(std::memory_order_relaxed)) g_collide_b.store(b, std::memory_order_relaxed);
  g_collide_calls.fetch_add(1, std::memory_order_relaxed);
}
#endif
