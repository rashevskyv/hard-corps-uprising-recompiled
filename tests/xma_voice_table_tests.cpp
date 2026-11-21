// The voice table (src/xma_voice_table.cpp) against the game's real banks. Host only:
//   g++ -std=c++20 -Isrc -o /tmp/vtt tests/xma_voice_table_tests.cpp src/xma_voice_table.cpp && /tmp/vtt [Sound dir/]
// (default /root/renut-hcu/src/game/extracted/DataXENON/Sound/).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "xma_voice_table.h"

namespace {

int g_failures = 0;
std::string g_dir = "/root/renut-hcu/src/game/extracted/DataXENON/Sound/";

void Check(bool ok, const char* what) {
  if (!ok) ++g_failures, std::printf("FAIL: %s\n", what);
}

uint32_t Le(const uint8_t* p, int n) {
  uint32_t v = 0;
  for (int i = n - 1; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

// The first 2048-byte packet of a sample, found by name (the test's own FSB4 walk).
std::vector<uint8_t> FirstPacket(const char* bank, const char* sample) {
  std::vector<uint8_t> b;
  if (FILE* f = std::fopen((g_dir + bank).c_str(), "rb")) {
    std::fseek(f, 0, SEEK_END);
    b.resize(size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (std::fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    std::fclose(f);
  }
  if (b.size() < 48) return {};
  size_t off = 48, data = 48 + Le(&b[8], 4);
  for (uint32_t i = 0, n = Le(&b[4], 4); i < n; i++) {
    if (!std::strcmp(reinterpret_cast<const char*>(&b[off + 2]), sample))
      return std::vector<uint8_t>(b.begin() + long(data), b.begin() + long(data) + 2048);
    data += Le(&b[off + 36], 4);
    off += Le(&b[off], 2);
  }
  return {};
}

int ClassOf(const char* bank, const char* sample) {
  const auto p = FirstPacket(bank, sample);
  return p.empty() ? -2 : hcu_voice_lookup(hcu_xma_packet_key(p.data()), nullptr, nullptr);
}

void WriteFile(const std::string& path, const char* text) {
  FILE* f = std::fopen(path.c_str(), "w");
  std::fputs(text, f);
  std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) g_dir = argv[1];
  const std::string settings = "/tmp/hcu_vtt_settings.txt";
  std::remove(settings.c_str());

  Check(hcu_voice_table_build("/nonexistent/", settings.c_str()) == 0, "no banks: no table");
  Check(hcu_voice_lookup(1, nullptr, nullptr) == -1, "no table: a miss");

  // The default prefixes (no settings file).
  const int keys = hcu_voice_table_build(g_dir.c_str(), settings.c_str());
  if (keys == 0) {
    std::printf("SKIP: no banks in %s\n", g_dir.c_str());
    return 0;
  }
  Check(keys == 962, "962 distinct first packets of the 1209 mono samples");
  Check(ClassOf("SE_Common_xenon.bnk", "NR_001.wav") == HCU_XMA_VOICE, "SE_Common NR_001: voice");
  Check(ClassOf("SE_Common_xenon.bnk", "DWN_HB.wav") == HCU_XMA_VOICE, "SE_Common DWN_HB: voice");
  Check(ClassOf("SE_Common_xenon.bnk", "BH_001.wav") == HCU_XMA_VOICE, "SE_Common BH_001: voice");
  Check(ClassOf("SE_Common_xenon.bnk", "SHT_MG.wav") == HCU_XMA_SFX, "SE_Common SHT_MG (gunfire): sfx");
  Check(ClassOf("SE_Common_xenon.bnk", "BOM_A.wav") == HCU_XMA_SFX, "SE_Common BOM_A: sfx");
  Check(ClassOf("SE_Common_xenon.bnk", "LV_001.wav") == HCU_XMA_VOICE, "SE_Common LV_001 (= Voice_LV's): voice");
  Check(ClassOf("SE_Common_xenon.bnk", "1UP.wav") == HCU_XMA_SFX, "1UP (shares DM_010_2's first packet): sfx");
  Check(ClassOf("SE_Stage05_xenon.bnk", "DM_010_2.wav") == HCU_XMA_SFX, "DM_010_2 (shares 1UP's first packet): sfx");
  Check(ClassOf("SE_Stage05_xenon.bnk", "DM_001.wav") == HCU_XMA_VOICE, "SE_Stage05 DM_001: voice");
  Check(ClassOf("SE_Stage05_xenon.bnk", "DMG_VC_BA5.wav") == HCU_XMA_VOICE, "SE_Stage05 DMG_VC_BA5: voice");
  Check(ClassOf("SE_Stage02_xenon.bnk", "LV_Enemy_1.wav") == HCU_XMA_VOICE, "SE_Stage02 LV_Enemy_1: voice");
  Check(ClassOf("SE_Stage02_xenon.bnk", "DG_BARK1.wav") == HCU_XMA_SFX, "DG_BARK1: sfx");
  Check(ClassOf("SE_Stage07_xenon.bnk", "BREATH.wav") == HCU_XMA_VOICE, "SE_Stage07 BREATH: voice");
  Check(ClassOf("SE_Stage08_xenon.bnk", "TB_002.wav") == HCU_XMA_VOICE, "SE_Stage08 TB_002: voice");
  Check(ClassOf("SE_Stage08_xenon.bnk", "ZOMBIE.wav") == HCU_XMA_VOICE, "SE_Stage08 ZOMBIE: voice");
  Check(ClassOf("SE_Stage08_xenon.bnk", "VOMIT.wav") == HCU_XMA_VOICE, "SE_Stage08 VOMIT: voice");
  Check(ClassOf("SE_Stage08_xenon.bnk", "PUNCH_BA8.wav") == HCU_XMA_SFX, "SE_Stage08 PUNCH_BA8: sfx");
  Check(ClassOf("Voice_LV_XENON.bnk", "LV_001.wav") == HCU_XMA_VOICE, "Voice_LV LV_001: voice");
  {
    const char* name = nullptr;
    int shared = 0;
    const auto p = FirstPacket("SE_Common_xenon.bnk", "1UP.wav");
    hcu_voice_lookup(hcu_xma_packet_key(p.data()), &name, &shared);
    Check(name && !std::strcmp(name, "SE_Common_xenon/1UP.wav") && shared == 1, "1UP's name, marked shared");
    const auto q = FirstPacket("SE_Common_xenon.bnk", "NR_001.wav");
    hcu_voice_lookup(hcu_xma_packet_key(q.data()), &name, &shared);
    Check(name && !std::strcmp(name, "SE_Common_xenon/NR_001.wav") && shared == 0, "NR_001's name, not shared");
  }

  // The decoder's start calls.
  {
    const auto voice = FirstPacket("SE_Common_xenon.bnk", "NR_001.wav");
    const auto sfx = FirstPacket("SE_Common_xenon.bnk", "SHT_MG.wav");
    std::vector<uint8_t> other(2048, 0x5a);  // a stream's next buffer: no sample starts with it
    char stats[200];
    hcu_voice_stats(stats, sizeof(stats));
    uint64_t key = 0;
    int cls = HCU_XMA_SFX;
    cls = rex_hcu_xma_start(voice.data(), 1, 0, cls, &key, 0, 32);
    Check(cls == HCU_XMA_VOICE && key == hcu_xma_packet_key(voice.data()), "a fresh start of NR_001: voice, key latched");
    cls = rex_hcu_xma_start(other.data(), 0, 0, cls, &key, 0, 32);
    Check(cls == HCU_XMA_VOICE, "a stream's next buffer (a miss without a fresh codec) keeps voice");
    cls = rex_hcu_xma_start(voice.data(), 0, 0, cls, &key, 0, 32);
    Check(cls == HCU_XMA_VOICE, "a loop to the start (the same key) keeps voice");
    cls = rex_hcu_xma_start(sfx.data(), 0, 0, cls, &key, 0, 32);
    Check(cls == HCU_XMA_SFX, "a known first packet without a fresh codec (a new sound): sfx");
    cls = rex_hcu_xma_start(voice.data(), 1, 0, cls, &key, 0, 32);
    cls = rex_hcu_xma_start(other.data(), 1, 0, cls, &key, 0, 32);
    Check(cls == HCU_XMA_SFX, "a fresh miss: sfx");
    Check(rex_hcu_xma_start(other.data(), 1, 1, HCU_XMA_SFX, &key, 0, 32) == HCU_XMA_MUSIC, "stereo: music");
    hcu_voice_stats(stats, sizeof(stats));
    Check(std::string(stats) == " starts 4 hit 2 miss 1 unprepared 1 next_miss 1", "the [audio] counters");
    Check(hcu_voice_stats(stats, sizeof(stats)) == 0, "the counters start again at 0");
  }

  // voice_prefixes in settings.txt replaces the SE list; a Voice_* bank stays voice.
  WriteFile(settings, "sfx_gain=1\nvoice_prefixes= SHT_ , BOM_\r\n");
  hcu_voice_table_build(g_dir.c_str(), settings.c_str());
  Check(ClassOf("SE_Common_xenon.bnk", "SHT_MG.wav") == HCU_XMA_VOICE, "voice_prefixes=SHT_,BOM_: SHT_MG voice");
  Check(ClassOf("SE_Common_xenon.bnk", "BOM_A.wav") == HCU_XMA_VOICE, "voice_prefixes=SHT_,BOM_: BOM_A voice");
  Check(ClassOf("SE_Common_xenon.bnk", "NR_001.wav") == HCU_XMA_SFX, "voice_prefixes=SHT_,BOM_: NR_001 sfx");
  Check(ClassOf("SE_Common_xenon.bnk", "LV_001.wav") == HCU_XMA_VOICE, "LV_001 stays voice (Voice_LV has it)");
  WriteFile(settings, "voice_prefixes=\n");
  hcu_voice_table_build(g_dir.c_str(), settings.c_str());
  Check(ClassOf("SE_Common_xenon.bnk", "NR_001.wav") == HCU_XMA_SFX, "an empty voice_prefixes: NR_001 sfx");
  Check(ClassOf("Voice_Common_xenon.bnk", "BM_003.wav") == HCU_XMA_VOICE, "an empty voice_prefixes: Voice_Common voice");
  std::remove(settings.c_str());

  std::printf("%d failures\n", g_failures);
  return g_failures != 0;
}
