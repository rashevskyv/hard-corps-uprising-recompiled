// CPU work per frame, per thread: the measure that carries over between the console, Eden and the Linux
// test host (Eden is GPU-bound for HCU, so its fps hides CPU changes). Wall time between frame marks minus
// the time the thread waited:
//   main thread  VdSwap to VdSwap, minus waiting kernel exports (REX_HOOK wrapper, nx_hook_counter.h) and
//                the guest's ring wait sub_82556728 (it spins until the command processor reads past a point);
//   GPU commands swap to swap, minus its idle waits and WAIT_REG_MEM polls (rex_nx_cp_wait_ticks, set by the
//                SDK edit in switch/sdk-edits-hcu.py) and the backend's fence wait and acquire;
//   audio        the audio client callbacks per frame (switch/audio_client_threads.inc, Switch only).
// One line every 30 s ([nx] cpu per frame ... p50/p95/p99 ms) and rex_nx_meter_text for the on-screen strip.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define REX_NX_METER_IMPL  // the real meter entry points, not the header's weak defaults
#include <rex/chrono/clock.h>
#include <rex/hook.h>

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
};

uint64_t Now() { return rex::chrono::Clock::QueryHostTickCount(); }
double ToMs(uint64_t ticks) { return double(ticks) * 1000.0 / double(rex::chrono::Clock::QueryHostTickFrequency()); }

thread_local int t_depth = 0;
thread_local uint64_t t_wait_ticks = 0;

Hist g_main_work, g_main_wait, g_cp_work;
uint64_t g_main_last = 0, g_main_last_wait = 0, g_report_tick = 0;
uint64_t g_cp_last = 0, g_cp_last_wait = 0, g_cp_last_block_us = 0;
uint64_t g_audio_last_us = 0;
uint32_t g_frames = 0;

void Report(uint64_t now) {
  const uint64_t audio_us = __atomic_load_n(&rex_nx_audio_cb_us, __ATOMIC_RELAXED);
  const double audio_ms = g_frames ? double(audio_us - g_audio_last_us) / 1000.0 / g_frames : 0.0;
  g_audio_last_us = audio_us;
  char cp[96] = "gpu commands work - (no swaps from this backend)";
  if (g_cp_work.n)
    std::snprintf(cp, sizeof(cp), "gpu commands work p50 %.1f p95 %.1f p99 %.1f ms", g_cp_work.Pct(0.5), g_cp_work.Pct(0.95),
                  g_cp_work.Pct(0.99));
  std::printf("[nx] cpu per frame (30 s, %u frames): main work p50 %.1f p95 %.1f p99 %.1f ms (waits p50 %.1f); %s; audio %.2f ms\n",
              g_main_work.n, g_main_work.Pct(0.5), g_main_work.Pct(0.95), g_main_work.Pct(0.99), g_main_wait.Pct(0.5), cp,
              audio_ms);
  std::fflush(stdout);
  std::snprintf(rex_nx_meter_text, sizeof(rex_nx_meter_text), "main %.1f:%.1f gpu %.1f:%.1f", g_main_work.Pct(0.5),
                g_main_work.Pct(0.95), g_cp_work.Pct(0.5), g_cp_work.Pct(0.95));
  g_main_work = Hist{};
  g_main_wait = Hist{};
  g_cp_work = Hist{};
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

// kind: 1 = a waiting export, 2 = VdSwap (the main thread's frame mark).
void rex_nx_meter_leave(uint64_t t0, int kind) {
  const uint64_t now = Now();
  if (--t_depth == 0 && kind == 1) t_wait_ticks += now - t0;
  if (kind != 2) return;
  if (g_main_last) {
    const uint64_t wall = now - g_main_last, wait = t_wait_ticks - g_main_last_wait;
    g_main_work.Add(ToMs(wall > wait ? wall - wait : 0));
    g_main_wait.Add(ToMs(wait));
    g_frames++;
  }
  g_main_last = now;
  g_main_last_wait = t_wait_ticks;
  if (!g_report_tick) g_report_tick = now;
  if (ToMs(now - g_report_tick) >= 30000.0) Report(now);
}

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
}

// The guest's ring wait (the XDK spins here until the command processor reads past a ring position): wait time.
REX_EXTERN(__imp__sub_82556728);
REX_HOOK_RAW(sub_82556728) {
  const uint64_t t0 = rex_nx_meter_enter();
  __imp__sub_82556728(ctx, base);
  rex_nx_meter_leave(t0, 1);
}
