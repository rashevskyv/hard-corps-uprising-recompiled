// Settings menu RECORD 5 SEC (Switch): every drawn frame's front buffer for 5 s, lossless, in one file, plus one log line
// per frame with 16x9 tile hashes (a flicker shows as a tile hash that changes while the game is paused).
//
// File <dir>/rec_<first swap>.hrec: per frame a 32-byte header {"HREC", index, swap, microseconds since the start,
// width, height, compressed bytes, 0} and the frame XOR the previous one (the first one XOR zero), LZ4 block compressed:
// a still picture costs almost nothing. tools/dkrec.py turns it into PNGs, a video, a contact sheet and change maps.
// The backend copies the frame (GPU) and hands it over once the frame's fence has passed; the writer thread does the
// hashes, the XOR, the compression and the SD writes. A full ring drops the frame (counted in the log).
#include "frame_recorder.h"

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <lz4.h>
#include <sys/stat.h>

namespace {

constexpr int kRing = 8;  // frames waiting for the writer (8 x 3.6 MB)

struct Pending {
  std::vector<uint8_t> px;
  uint32_t swap = 0;
  uint64_t us = 0;
};

std::mutex g_lock;
std::condition_variable g_cv;
std::deque<Pending*> g_queue, g_free;
std::thread g_thread;
bool g_active = false, g_stop = false;
uint32_t g_w = 0, g_h = 0, g_pushed = 0, g_dropped = 0;

uint16_t TileHash(const uint8_t* px, uint32_t w, uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th) {
  uint32_t h = 2166136261u;
  for (uint32_t y = y0; y < y0 + th; y++) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(px) + size_t(y) * w + x0;
    for (uint32_t x = 0; x < tw; x++) h = (h ^ row[x]) * 16777619u;
  }
  return uint16_t(h ^ (h >> 16));
}

void Writer(std::string path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  const size_t bytes = size_t(g_w) * g_h * 4;
  std::vector<uint8_t> prev(bytes, 0), delta(bytes), out(size_t(LZ4_compressBound(int(bytes))));
  uint32_t index = 0;
  uint64_t written = 0;
  for (;;) {
    Pending* p;
    {
      std::unique_lock<std::mutex> l(g_lock);
      g_cv.wait(l, [] { return !g_queue.empty() || g_stop; });
      if (g_queue.empty()) break;
      p = g_queue.front();
      g_queue.pop_front();
    }
    // 16x9 tiles of 80x80 pixels (1280x720): one 16-bit hash each, and one for the frame.
    char line[16 * 9 * 5 + 96];
    int n = std::snprintf(line, sizeof(line), "[rec] frame %u swap %u t %llu ms:", index, p->swap, (unsigned long long)(p->us / 1000));
    const uint32_t tw = g_w / 16, th = g_h / 9;
    for (uint32_t ty = 0; ty < 9; ty++)
      for (uint32_t tx = 0; tx < 16; tx++) n += std::snprintf(line + n, sizeof(line) - n, " %04X", TileHash(p->px.data(), g_w, tx * tw, ty * th, tw, th));
    std::printf("%s\n", line);
    for (size_t i = 0; i < bytes; i++) delta[i] = p->px[i] ^ prev[i];
    prev.swap(p->px);
    const int c = LZ4_compress_default(reinterpret_cast<const char*>(delta.data()), reinterpret_cast<char*>(out.data()), int(bytes), int(out.size()));
    if (f && c > 0) {
      const uint32_t hdr[8] = {0x43455248u /* "HREC" */, index, p->swap, uint32_t(p->us), g_w, g_h, uint32_t(c), 0};
      std::fwrite(hdr, 1, sizeof(hdr), f);
      std::fwrite(out.data(), 1, size_t(c), f);
      written += sizeof(hdr) + size_t(c);
    }
    index++;
    std::lock_guard<std::mutex> l(g_lock);
    g_free.push_back(p);
  }
  if (f) std::fclose(f);
  std::printf("[rec] stop: %u frames written (%llu KB) to %s, %u dropped (the writer was behind)\n", index, (unsigned long long)(written >> 10),
              path.c_str(), g_dropped);
  std::fflush(stdout);
}

}  // namespace

bool hcu_rec_start(const char* dir, uint32_t first_swap, uint32_t w, uint32_t h) {
  if (g_active) return true;
  mkdir(dir, 0777);
  g_w = w;
  g_h = h;
  g_pushed = g_dropped = 0;
  g_stop = false;
  try {
    for (int i = 0; i < kRing; i++) {
      auto* p = new Pending;
      p->px.resize(size_t(w) * h * 4);
      g_free.push_back(p);
    }
  } catch (...) {
    std::printf("[rec] not started: no memory for %d frames\n", kRing);
    for (Pending* p : g_free) delete p;
    g_free.clear();
    return false;
  }
  char path[256];
  std::snprintf(path, sizeof(path), "%srec_%u.hrec", dir, first_swap);
  std::printf("[rec] start at swap %u: %ux%u, 5 s, to %s\n", first_swap, w, h, path);
  g_thread = std::thread(Writer, std::string(path));
  g_active = true;
  return true;
}

bool hcu_rec_active() { return g_active; }

void hcu_rec_push(const uint8_t* rgba, uint32_t swap, uint64_t us) {
  Pending* p = nullptr;
  {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_active || g_free.empty()) {
      if (g_active) g_dropped++;
      return;
    }
    p = g_free.front();
    g_free.pop_front();
  }
  std::memcpy(p->px.data(), rgba, p->px.size());
  p->swap = swap;
  p->us = us;
  std::lock_guard<std::mutex> l(g_lock);
  g_queue.push_back(p);
  g_pushed++;
  g_cv.notify_one();
}

void hcu_rec_stop() {
  if (!g_active) return;
  {
    std::lock_guard<std::mutex> l(g_lock);
    g_stop = true;
    g_cv.notify_one();
  }
  g_thread.join();
  for (Pending* p : g_free) delete p;
  g_free.clear();
  g_active = false;
}
