// The voice table: see xma_voice_table.h.
#include "xma_voice_table.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>

const char* const hcu_voice_default_prefixes =
    "NR_,BH_001,CL_001,HD_001,HT_001,LV_001,DWN_,LV_Enemy_,DM_,DMG_VC,TB_,BREATH,ZOMBIE,VOMIT";

namespace {

constexpr size_t kPacket = 2048;

struct Entry {
  uint64_t key;
  uint32_t name;  // index into g_names
  uint8_t cls;
  uint8_t voice_bank;
  uint8_t shared;
};

// Built once before the game's audio starts, read only after it (no lock).
std::vector<Entry> g_table;  // sorted by key, one entry per key
std::vector<std::string> g_names;

std::atomic<uint32_t> g_starts{0}, g_hits{0}, g_misses{0}, g_unprepared{0}, g_next_miss{0};
std::atomic<uint32_t> g_logged{0}, g_logged_second{0};
std::atomic<int64_t> g_log_second{-1};
constexpr uint32_t kLogPerSecond = 20, kLogTotal = 200;

uint32_t Le16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }

std::vector<std::string> Prefixes(const char* settings_path) {
  std::string list = hcu_voice_default_prefixes;
  if (FILE* f = settings_path ? std::fopen(settings_path, "r") : nullptr) {
    char line[1024];
    while (std::fgets(line, sizeof(line), f))
      if (!std::strncmp(line, "voice_prefixes=", 15)) list = line + 15;  // the last line wins
    std::fclose(f);
  }
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= list.size()) {
    size_t end = list.find(',', at);
    if (end == std::string::npos) end = list.size();
    std::string p = list.substr(at, end - at);
    p.erase(0, p.find_first_not_of(" \t\r\n"));
    p.erase(p.find_last_not_of(" \t\r\n") + 1);
    if (!p.empty()) out.push_back(p);
    at = end + 1;
  }
  return out;
}

// One FSB4 bank: its mono samples' (key, name) into raw. Little-endian header: "FSB4", samples, sample-header bytes,
// data bytes, version, mode, 24 more bytes; then the full sample headers (each starts with its size, the 30-byte name,
// the compressed length at +36, the channels at +62); the sample data follows, each sample right after the previous.
int ReadBank(const std::string& dir, const std::string& file, const std::vector<std::string>& prefixes,
             std::vector<Entry>& raw) {
  FILE* f = std::fopen((dir + file).c_str(), "rb");
  if (!f) return 0;
  uint8_t h[48];
  int added = 0;
  if (std::fread(h, 1, sizeof(h), f) == sizeof(h) && !std::memcmp(h, "FSB4", 4)) {
    const uint32_t count = Le32(h + 4), header_bytes = Le32(h + 8);
    std::vector<uint8_t> hdr(header_bytes);
    if (header_bytes < (1u << 24) && std::fread(hdr.data(), 1, hdr.size(), f) == hdr.size()) {
      const std::string bank = file.substr(0, file.rfind('.'));
      const bool voice_bank = !std::strncmp(file.c_str(), "Voice_", 6);
      uint8_t packet[kPacket];
      size_t off = 0;
      uint64_t data = sizeof(h) + uint64_t(header_bytes);
      for (uint32_t i = 0; i < count && off + 64 <= hdr.size(); i++) {
        const uint8_t* s = hdr.data() + off;
        const uint32_t size = Le16(s), compressed = Le32(s + 36), channels = Le16(s + 62);
        if (size < 64) break;  // (basic headers: not in this game's banks)
        const std::string name(reinterpret_cast<const char*>(s + 2), strnlen(reinterpret_cast<const char*>(s + 2), 30));
        if (channels == 1 && compressed >= kPacket && std::fseek(f, long(data), SEEK_SET) == 0 &&
            std::fread(packet, 1, kPacket, f) == kPacket) {
          bool voice = voice_bank;
          for (const auto& p : prefixes) voice |= name.compare(0, p.size(), p) == 0;
          g_names.push_back(bank + "/" + name);
          raw.push_back({hcu_xma_packet_key(packet), uint32_t(g_names.size() - 1),
                         uint8_t(voice ? HCU_XMA_VOICE : HCU_XMA_SFX), uint8_t(voice_bank), 0});
          added++;
        }
        off += size;
        data += compressed;
      }
    }
  }
  std::fclose(f);
  return added;
}

const char* ClassName(int cls) { return cls == HCU_XMA_VOICE ? "voice" : cls == HCU_XMA_MUSIC ? "music" : "sfx"; }

// At most kLogPerSecond start lines a second and kLogTotal a run.
bool MayLog() {
  const int64_t second = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
  if (g_log_second.exchange(second, std::memory_order_relaxed) != second) g_logged_second.store(0, std::memory_order_relaxed);
  if (g_logged_second.fetch_add(1, std::memory_order_relaxed) >= kLogPerSecond) return false;
  return g_logged.fetch_add(1, std::memory_order_relaxed) < kLogTotal;
}

}  // namespace

uint64_t hcu_xma_packet_key(const uint8_t* packet) {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (size_t i = 0; i < kPacket; i += 8) {
    uint64_t v;
    std::memcpy(&v, packet + i, 8);
    h = (h ^ v) * 0xFF51AFD7ED558CCDull;
    h ^= h >> 29;
  }
  return h ^ (h >> 32);
}

int hcu_voice_table_build(const char* sound_dir, const char* settings_path) {
  const auto t0 = std::chrono::steady_clock::now();
  g_table.clear();
  g_names.clear();
  const std::string dir = sound_dir ? sound_dir : "";
  const std::vector<std::string> prefixes = Prefixes(settings_path);
  std::vector<std::string> files;
  const std::string open_dir = dir.size() > 1 && dir.back() == '/' ? dir.substr(0, dir.size() - 1) : dir;
  if (DIR* d = opendir(open_dir.c_str())) {  // (without the trailing slash: libnx's devoptab may not take it)
    while (dirent* e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".bnk") == 0 || n.compare(n.size() - 4, 4, ".BNK") == 0))
        files.push_back(n);
    }
    closedir(d);
  }
  std::sort(files.begin(), files.end());
  std::vector<Entry> raw;
  int banks = 0;
  for (const auto& file : files) banks += ReadBank(dir, file, prefixes, raw) > 0;
  // One entry per key. Samples that share a first packet: a Voice_* bank's sample makes it a voice (SE_Common's LV_001
  // is Voice_LV's); otherwise different classes make it sfx, the safer side (SE_Common's 1UP and SE_Stage05's
  // DM_010_2 share their whole first packet and size).
  std::stable_sort(raw.begin(), raw.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
  int conflicts = 0, voices = 0;
  for (size_t i = 0; i < raw.size();) {
    size_t j = i;
    bool voice_bank = false, any_voice = false, any_sfx = false, shared = false;
    for (; j < raw.size() && raw[j].key == raw[i].key; j++) {
      voice_bank |= raw[j].voice_bank != 0;
      any_voice |= raw[j].cls == HCU_XMA_VOICE;
      any_sfx |= raw[j].cls != HCU_XMA_VOICE;
      shared |= g_names[raw[j].name].substr(g_names[raw[j].name].find('/')) !=
                g_names[raw[i].name].substr(g_names[raw[i].name].find('/'));
    }
    Entry e = raw[i];
    e.voice_bank = voice_bank;
    e.shared = shared;
    e.cls = voice_bank || !any_sfx ? HCU_XMA_VOICE : HCU_XMA_SFX;
    conflicts += !voice_bank && any_voice && any_sfx;
    voices += e.cls == HCU_XMA_VOICE;
    g_table.push_back(e);
    i = j;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("[audio] voice table: %zu mono samples of %d banks in %s, %zu keys (%d voice, %d voice/sfx conflicts as sfx), "
              "%zu voice prefixes, %.1f ms%s\n",
              raw.size(), banks, dir.c_str(), g_table.size(), voices, conflicts, prefixes.size(), ms,
              g_table.empty() ? ": no table, every mono sound is sfx" : "");
  std::fflush(stdout);
  return int(g_table.size());
}

int hcu_voice_lookup(uint64_t key, const char** name, int* shared) {
  auto it = std::lower_bound(g_table.begin(), g_table.end(), key, [](const Entry& e, uint64_t k) { return e.key < k; });
  if (it == g_table.end() || it->key != key) return -1;
  if (name) *name = g_names[it->name].c_str();
  if (shared) *shared = it->shared;
  return it->cls;
}

__attribute__((noinline)) int rex_hcu_xma_start(const uint8_t* packet, int fresh, int stereo, int cls, uint64_t* key,
                                                uint32_t packet_index, uint32_t bit_offset) {
  if (stereo) {
    if (fresh) {
      g_starts.fetch_add(1, std::memory_order_relaxed);
      if (MayLog()) std::printf("[audio] start class=music stereo pkt=%u bit=%u fresh\n", packet_index, bit_offset);
    }
    return HCU_XMA_MUSIC;
  }
  const uint64_t k = hcu_xma_packet_key(packet);
  const char* name = "?";
  int shared = 0;
  const int found = hcu_voice_lookup(k, &name, &shared);
  if (!fresh) {
    // The buffer's first frame with the codec kept: a stream's next buffer (a miss) or a loop to the start (the same
    // key) keep the class; a known first packet with another key is a new sound the game started without a clear.
    if (found < 0) {
      g_next_miss.fetch_add(1, std::memory_order_relaxed);
      return cls;
    }
    if (k == *key) return cls;
    g_unprepared.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_starts.fetch_add(1, std::memory_order_relaxed);
    (found < 0 ? g_misses : g_hits).fetch_add(1, std::memory_order_relaxed);
  }
  *key = k;
  const int out = found < 0 ? HCU_XMA_SFX : found;
  if (MayLog()) {
    char head[17];
    for (int i = 0; i < 8; i++) std::snprintf(head + 2 * i, 3, "%02x", packet[i]);
    std::printf("[audio] start class=%s %s name=%s%s key=%016llx head=%s pkt=%u bit=%u %s\n", ClassName(out),
                found < 0 ? "miss" : "hit", name, shared ? "+" : "", static_cast<unsigned long long>(k), head,
                packet_index, bit_offset, fresh ? "fresh" : "unprepared");
  }
  return out;
}

int hcu_voice_stats(char* buf, size_t size) {
  const uint32_t starts = g_starts.exchange(0, std::memory_order_relaxed);
  const uint32_t hits = g_hits.exchange(0, std::memory_order_relaxed);
  const uint32_t misses = g_misses.exchange(0, std::memory_order_relaxed);
  const uint32_t unprepared = g_unprepared.exchange(0, std::memory_order_relaxed);
  const uint32_t next_miss = g_next_miss.exchange(0, std::memory_order_relaxed);
  if (!starts && !unprepared && !next_miss) return 0;
  const int n = std::snprintf(buf, size, " starts %u hit %u miss %u unprepared %u next_miss %u", starts, hits, misses,
                              unprepared, next_miss);
  return n < 0 ? 0 : n;
}
