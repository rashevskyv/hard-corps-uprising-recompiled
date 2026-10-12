// Tests for the offline Live services: the local stats store, the leaderboard answers and the guest buffers they
// write, the achievement 0 alias, the Marketplace outcome and the overlay rules.
// Run: hcu_live_tests

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "achievement_alias.h"
#include "achievement_list.h"
#include "guest_memory.h"
#include "live_stats.h"
#include "local_stats.h"
#include "marketplace_rules.h"
#include "overlay_rules.h"

using namespace hcu;
using namespace hcu::live;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr uint64_t kXuid = 0xB13EBABEBABEBABE;
constexpr uint64_t kOtherXuid = 0xB13E000000000001;

struct TempDir {
  TempDir() {
    path = std::filesystem::temp_directory_path() / ("hcu_live_tests_" + std::to_string(std::rand()));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

std::string ReadAll(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteAll(const std::filesystem::path& file, const std::string& text) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  out << text;
}

ViewStats Stats(int64_t rating, std::map<uint16_t, StatColumn> columns = {}) {
  return ViewStats{rating, std::move(columns)};
}

// ---- guest memory ----

struct Guest {
  std::vector<uint8_t> memory = std::vector<uint8_t>(1 << 16, 0xCD);
  uint8_t* base() { return memory.data(); }

  void Spec(uint32_t at, uint32_t view, const std::vector<uint16_t>& columns) {
    StoreBE32(base(), at, view);
    StoreBE32(base(), at + 4, static_cast<uint32_t>(columns.size()));
    for (size_t i = 0; i < columns.size(); ++i) StoreBE16(base(), at + 8 + 2 * static_cast<uint32_t>(i), columns[i]);
  }
  void Property(uint32_t at, uint32_t id, uint8_t type, uint64_t value) {
    std::memset(base() + at, 0, 24);
    StoreBE32(base(), at, id);
    base()[at + 8] = type;
    if (type == kTypeInt32 || type == kTypeFloat) {
      StoreBE32(base(), at + 16, static_cast<uint32_t>(value));
    } else {
      StoreBE64(base(), at + 16, value);
    }
  }
  void View(uint32_t at, uint32_t view, uint32_t count, uint32_t properties) {
    StoreBE32(base(), at, view);
    StoreBE32(base(), at + 4, count);
    StoreBE32(base(), at + 8, properties);
  }
};

struct Board {
  explicit Board(const TempDir& dir) : stats(dir.path / "stats.toml"), board(stats, LocalUser{kXuid, "Player"}) {}
  LocalStats stats;
  LocalLeaderboard board;
};

// ---- stats store ----

void TestCleanFileAndFirstValue() {
  std::printf("stats: clean file and first value\n");
  TempDir dir;
  LocalStats stats(dir.path / "live" / "stats.toml");
  CHECK(stats.Load() == LoadResult::kMissing);
  CHECK(!stats.Get(kXuid, 3));
  CHECK(stats.Record(kXuid, 3, Stats(1000, {{1, {kTypeInt32, 2}}})) == RecordResult::kSaved);
  const auto got = stats.Get(kXuid, 3);
  CHECK(got && got->rating == 1000 && got->columns.at(1).bits == 2);
  CHECK(std::filesystem::exists(dir.path / "live" / "stats.toml"));
}

void TestRestartKeepsValues() {
  std::printf("stats: restart\n");
  TempDir dir;
  const auto file = dir.path / "stats.toml";
  {
    LocalStats stats(file);
    stats.Load();
    stats.Record(kXuid, 3, Stats(1000, {{1, {kTypeInt32, 2}}, {2, {kTypeDouble, 4609434218613702656}}}));
    stats.Record(kXuid, 7, Stats(55));
    stats.Record(kOtherXuid, 3, Stats(9));
  }
  LocalStats reloaded(file);
  CHECK(reloaded.Load() == LoadResult::kLoaded);
  const auto first = reloaded.Get(kXuid, 3);
  CHECK(first && first->rating == 1000);
  CHECK(first && first->columns.size() == 2 && first->columns.at(2) == (StatColumn{kTypeDouble, 4609434218613702656}));
  CHECK(reloaded.Get(kXuid, 7) && reloaded.Get(kXuid, 7)->rating == 55);
  CHECK(reloaded.Get(kOtherXuid, 3) && reloaded.Get(kOtherXuid, 3)->rating == 9);
  CHECK(!reloaded.Get(kXuid, 4));
}

void TestSubmissionReplacesAndViewsAreIndependent() {
  std::printf("stats: latest submission and independent views\n");
  TempDir dir;
  LocalStats stats(dir.path / "stats.toml");
  stats.Load();
  stats.Record(kXuid, 1, Stats(100));
  stats.Record(kXuid, 2, Stats(200));
  CHECK(stats.Record(kXuid, 1, Stats(100)) == RecordResult::kUnchanged);
  CHECK(stats.Record(kXuid, 1, Stats(40)) == RecordResult::kSaved);  // the game decides what is worth submitting
  CHECK(stats.Get(kXuid, 1)->rating == 40);
  CHECK(stats.Get(kXuid, 2)->rating == 200);
  CHECK(stats.Record(kXuid, 1, Stats(40, {{5, {kTypeFloat, 1}}})) == RecordResult::kSaved);
  CHECK(stats.Get(kXuid, 1)->columns.size() == 1);
}

void TestRejectedSubmissions() {
  std::printf("stats: rejected submissions\n");
  TempDir dir;
  LocalStats stats(dir.path / "stats.toml");
  stats.Load();
  CHECK(stats.Record(kXuid, 1, Stats(0)) == RecordResult::kUnchanged);
  CHECK(stats.Record(kXuid, 1, Stats(-5)) == RecordResult::kUnchanged);
  CHECK(stats.Record(kXuid, 1, Stats(5, {{1, {4, 1}}})) == RecordResult::kUnchanged);
  std::map<uint16_t, StatColumn> many;
  for (uint16_t i = 0; i <= LocalStats::kMaxColumns; ++i) many[i] = {kTypeInt32, 1};
  CHECK(stats.Record(kXuid, 1, Stats(5, many)) == RecordResult::kUnchanged);
  for (uint32_t view = 0; view < LocalStats::kMaxViewsPerUser; ++view) CHECK(stats.Record(kXuid, view, Stats(1)) == RecordResult::kSaved);
  CHECK(stats.Record(kXuid, 1000, Stats(1)) == RecordResult::kUnchanged);
  CHECK(stats.Record(kXuid, 3, Stats(2)) == RecordResult::kSaved);  // an existing view still updates
  for (uint32_t user = 1; user < LocalStats::kMaxUsers; ++user) CHECK(stats.Record(user, 1, Stats(1)) == RecordResult::kSaved);
  CHECK(stats.Record(99, 1, Stats(1)) == RecordResult::kUnchanged);
  CHECK(!std::filesystem::exists(dir.path / "never"));
}

void TestMalformedFiles() {
  std::printf("stats: malformed, oversized and hand-edited files\n");
  const char* bad[] = {
      "garbage",
      "version = 2\n",
      "[users.B13EBABEBABEBABE.view_1]\nrating = 5\n",                                    // no version
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\nrating = 0\n",                       // not a score
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\n",                                   // no rating
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\nrating = 5\ncol_1 = [4, 1]\n",       // unknown type
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\nrating = 5\ncol_70000 = [1, 1]\n",   // column id
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\nrating = 5\nextra = 1\n",
      "version = 1\n[users.B13EBABEBABEBABE.view_1]\nrating = 5\n[users.B13EBABEBABEBABE.view_1]\nrating = 6\n",
      "version = 1\n[users.XYZ.view_1]\nrating = 5\n",
      "version = 1\n[users.B13EBABEBABEBABE.view_x]\nrating = 5\n",
      "version = 1\n[other]\n",
  };
  for (const char* text : bad) {
    TempDir dir;
    const auto file = dir.path / "stats.toml";
    WriteAll(file, text);
    LocalStats stats(file);
    CHECK(stats.Load() == LoadResult::kCorrupt);
    CHECK(!stats.Get(kXuid, 1));
    CHECK(!std::filesystem::exists(file));
    CHECK(std::filesystem::exists(dir.path / "stats.toml.corrupt"));
    CHECK(stats.Record(kXuid, 1, Stats(7)) == RecordResult::kSaved);  // starts over
  }
  {
    TempDir dir;
    const auto file = dir.path / "stats.toml";
    WriteAll(file, "version = 1\n" + std::string((1u << 20) + 1, '#'));
    LocalStats stats(file);
    CHECK(stats.Load() == LoadResult::kCorrupt);
  }
  {
    TempDir dir;
    const auto file = dir.path / "stats.toml";
    WriteAll(file, "# note\nversion = 1\n\n[users.B13EBABEBABEBABE.view_12]\nrating = 77  \ncol_3 = [ 2 , 9 ]\n");
    LocalStats stats(file);
    CHECK(stats.Load() == LoadResult::kLoaded);
    CHECK(stats.Get(kXuid, 12) && stats.Get(kXuid, 12)->rating == 77 && stats.Get(kXuid, 12)->columns.at(3).bits == 9);
  }
}

void TestAtomicReplacement() {
  std::printf("stats: atomic replacement\n");
  TempDir dir;
  const auto file = dir.path / "stats.toml";
  LocalStats stats(file);
  stats.Load();
  stats.Record(kXuid, 1, Stats(10));
  const std::string before = ReadAll(file);
  CHECK(!before.empty());
  CHECK(!std::filesystem::exists(dir.path / "stats.toml.tmp"));

  // The temporary name is taken by a directory: the write fails, the file on disk stays the last good one.
  std::filesystem::create_directories(dir.path / "stats.toml.tmp");
  CHECK(stats.Record(kXuid, 1, Stats(20)) == RecordResult::kNotSaved);
  CHECK(ReadAll(file) == before);
  CHECK(stats.Get(kXuid, 1)->rating == 20);  // still served from memory
  std::filesystem::remove_all(dir.path / "stats.toml.tmp");
  CHECK(stats.Record(kXuid, 1, Stats(30)) == RecordResult::kSaved);
  LocalStats reloaded(file);
  CHECK(reloaded.Load() == LoadResult::kLoaded && reloaded.Get(kXuid, 1)->rating == 30);
}

void TestPathSafety() {
  std::printf("stats: path stays under the user data root\n");
  const std::filesystem::path root = std::filesystem::path("data") / "user";
  const auto path = StatsFilePath(root, 0x58410A18);
  CHECK(path == root / "58410A18" / "live" / "stats.toml");
  CHECK(StatsFilePath(root, 0x0000001F).parent_path().parent_path().filename() == "0000001F");
  // Only numbers from the game reach the file's text, never a path.
  TempDir dir;
  LocalStats stats(StatsFilePath(dir.path, 0x58410A18));
  stats.Load();
  stats.Record(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFF, Stats(1));
  std::error_code ec;
  size_t files = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.path, ec)) {
    if (entry.is_regular_file()) {
      ++files;
      CHECK(std::filesystem::relative(entry.path(), dir.path).string().rfind("58410A18", 0) == 0);
    }
  }
  CHECK(files == 1);
}

// ---- guest buffers and leaderboard answers ----

void TestParseViewWrites() {
  std::printf("writes: rating and columns from the properties\n");
  Guest g;
  g.Property(0x1000, 0x10000001, kTypeInt32, 3);
  g.Property(0x1018, 0x10000002, kTypeFloat, std::bit_cast<uint32_t>(1.5f));
  g.Property(0x1030, 0x10000003, kTypeDouble, std::bit_cast<uint64_t>(2.5));
  g.Property(0x1048, 0x20000004, kTypeInt64, 123456);
  g.View(0x2000, 9, 4, 0x1000);
  const auto writes = ParseViewWrites(g.base(), 0x2000, 1);
  CHECK(writes.size() == 1);
  CHECK(writes[0].view == 9 && writes[0].stats.rating == 123456);
  CHECK(writes[0].stats.columns.size() == 4);  // the rating is a column too: 0x20000004 is column 4
  CHECK(writes[0].stats.columns.at(4) == (StatColumn{kTypeInt64, 123456}));
  CHECK(writes[0].stats.columns.at(1) == (StatColumn{kTypeInt32, 3}));
  CHECK(writes[0].stats.columns.at(2).type == kTypeFloat && writes[0].stats.columns.at(2).bits == std::bit_cast<uint32_t>(1.5f));
  CHECK(writes[0].stats.columns.at(3).bits == static_cast<int64_t>(std::bit_cast<uint64_t>(2.5)));

  g.Property(0x1000, 0x10000001, kTypeInt32, 0xFFFFFFFE);  // negative int32 keeps its sign
  CHECK(ParseViewWrites(g.base(), 0x2000, 1)[0].stats.columns.at(1).bits == -2);

  g.Property(0x1048, 0x20000004, 9, 1);  // no 64-bit rating: nothing to record
  CHECK(ParseViewWrites(g.base(), 0x2000, 1).empty());
  CHECK(ParseViewWrites(g.base(), 0, 1).empty());
  CHECK(ParseViewWrites(g.base(), 0x2000, 17).empty());
  g.View(0x2000, 9, 33, 0x1000);
  CHECK(ParseViewWrites(g.base(), 0x2000, 1).empty());
  g.View(0x2000, 9, 4, 0);
  CHECK(ParseViewWrites(g.base(), 0x2000, 1).empty());
}

void TestEnumeratorCreation() {
  std::printf("enumerator: creation\n");
  TempDir dir;
  Guest g;
  EnumeratorRegistry registry;
  g.Spec(0x1000, 3, {1, 2});
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 1, 0x1000, 0x3000, 0x3004) ==
        kErrorSuccess);
  const uint32_t handle = LoadBE32(g.base(), 0x3004);
  CHECK(handle >= EnumeratorRegistry::kHandleBase && handle < 0xF8000000);
  CHECK(LoadBE32(g.base(), 0x3000) == kResultsHeaderSize + kViewSize + kRowSize + 2 * kColumnSize);
  CHECK(registry.pending() == 1);

  const uint32_t before = registry.pending();
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 0, 0x1000, 0x3000, 0x3004) ==
        kErrorInvalidParameter);
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 65, 0x1000, 0x3000, 0x3004) ==
        kErrorInvalidParameter);
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 1, 0, 0x3000, 0x3004) ==
        kErrorInvalidParameter);
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 1, 0x1000, 0, 0x3004) ==
        kErrorInvalidParameter);
  CHECK(CreateEnumerator(g.base(), registry, StatsQuery::Kind::kRanked, 1, 100, 1, 0x1000, 0x3000, 0) ==
        kErrorInvalidParameter);
  CHECK(registry.pending() == before);
}

void TestRegistryRetiresAndBounds() {
  std::printf("enumerator: handles retire and stay bounded\n");
  EnumeratorRegistry registry;
  const uint32_t first = registry.Add({});
  CHECK(registry.Take(first));
  CHECK(!registry.Take(first));        // used once
  CHECK(!registry.Take(0xF8000001));   // the runtime's own handles are not ours
  CHECK(!registry.Take(0));
  CHECK(registry.pending() == 0);
  std::vector<uint32_t> handles;
  for (size_t i = 0; i < EnumeratorRegistry::kMaxPending + 10; ++i) handles.push_back(registry.Add({}));
  CHECK(registry.pending() == EnumeratorRegistry::kMaxPending);
  CHECK(!registry.Take(handles.front()));  // the oldest unserved handle made room
  CHECK(registry.Take(handles.back()));
  for (const uint32_t handle : handles) CHECK((handle & 0xFF000000) == EnumeratorRegistry::kHandleBase);
}

void TestEnumerationAnswers() {
  std::printf("enumerator: empty and local answers\n");
  TempDir dir;
  Board b(dir);
  Guest g;
  g.Spec(0x1000, 3, {1, 2, 9});
  StatsQuery ranked;
  ranked.kind = StatsQuery::Kind::kRanked;
  ranked.pivot = 1;
  ranked.max_rows = 100;
  ranked.views = ReadSpecs(g.base(), 0x1000, 1);
  const uint32_t size = kResultsHeaderSize + kViewSize + kRowSize + 3 * kColumnSize;

  // Nobody has played: one view, no rows.
  CHECK(ServeEnumeration(g.base(), b.board, ranked, 0x4000, size, 0x3000, 0) == kErrorSuccess);
  CHECK(LoadBE32(g.base(), 0x3000) == 1);
  CHECK(LoadBE32(g.base(), 0x4000) == 1);
  const uint32_t views = LoadBE32(g.base(), 0x4004);
  CHECK(LoadBE32(g.base(), views) == 3 && LoadBE32(g.base(), views + 4) == 0 && LoadBE32(g.base(), views + 8) == 0);
  CHECK(LoadBE32(g.base(), views + 12) == 0);

  b.stats.Record(kXuid, 3, Stats(5000, {{1, {kTypeInt32, 4}}, {2, {kTypeInt64, 77}}, {8, {kTypeInt32, 1}}}));
  CHECK(ServeEnumeration(g.base(), b.board, ranked, 0x4000, size, 0x3000, 0) == kErrorSuccess);
  const uint32_t view = LoadBE32(g.base(), 0x4004);
  CHECK(LoadBE32(g.base(), view + 4) == 1 && LoadBE32(g.base(), view + 8) == 1);
  const uint32_t row = LoadBE32(g.base(), view + 12);
  CHECK(row != 0);
  CHECK(LoadBE64(g.base(), row) == kXuid && LoadBE32(g.base(), row + 8) == 1);
  CHECK(LoadBE64(g.base(), row + 16) == 5000);
  CHECK(std::strcmp(reinterpret_cast<const char*>(g.base() + row + 24), "Player") == 0);
  CHECK(LoadBE32(g.base(), row + 40) == 2);  // columns 1 and 2 were asked for and stored; 9 was never stored
  const uint32_t columns = LoadBE32(g.base(), row + 44);
  CHECK(LoadBE16(g.base(), columns) == 1 && g.base()[columns + 8] == kTypeInt32 && LoadBE32(g.base(), columns + 16) == 4);
  CHECK(LoadBE16(g.base(), columns + 24) == 2 && g.base()[columns + 24 + 8] == kTypeInt64 &&
        LoadBE64(g.base(), columns + 24 + 16) == 77);

  // A page past the first rank, and another player's neighbourhood, hold nobody: the local player is the whole board.
  ranked.pivot = 2;
  CHECK(ServeEnumeration(g.base(), b.board, ranked, 0x4000, size, 0x3000, 0) == kErrorSuccess);
  CHECK(LoadBE32(g.base(), LoadBE32(g.base(), 0x4004) + 8) == 0);
  CHECK(LoadBE32(g.base(), LoadBE32(g.base(), 0x4004) + 4) == 1);
  StatsQuery around = ranked;
  around.kind = StatsQuery::Kind::kAround;
  around.pivot = kOtherXuid;
  CHECK(ServeEnumeration(g.base(), b.board, around, 0x4000, size, 0x3000, 0) == kErrorSuccess);
  CHECK(LoadBE32(g.base(), LoadBE32(g.base(), 0x4004) + 8) == 0);
  around.pivot = kXuid;
  CHECK(ServeEnumeration(g.base(), b.board, around, 0x4000, size, 0x3000, 0) == kErrorSuccess);
  CHECK(LoadBE32(g.base(), LoadBE32(g.base(), 0x4004) + 8) == 1);
}

void TestEnumerationBufferAndOverlapped() {
  std::printf("enumerator: buffer size and overlapped completion\n");
  TempDir dir;
  Board b(dir);
  b.stats.Record(kXuid, 3, Stats(5000));
  Guest g;
  g.Spec(0x1000, 3, {});
  StatsQuery query;
  query.kind = StatsQuery::Kind::kRanked;
  query.pivot = 1;
  query.max_rows = 10;
  query.views = ReadSpecs(g.base(), 0x1000, 1);
  const uint32_t needed = kResultsHeaderSize + kViewSize + kRowSize;

  StoreBE32(g.base(), 0x3000, 77);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0x4000, needed - 1, 0x3000, 0) == kErrorInsufficientBuffer);
  CHECK(LoadBE32(g.base(), 0x3000) == 0);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0, needed, 0x3000, 0) == kErrorInsufficientBuffer);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0x4000, needed, 0, 0) == kErrorSuccess);

  // With an overlapped the answer is in the structure and the call reports IO_PENDING.
  std::memset(g.base() + 0x5000, 0xAA, 0x20);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0x4000, needed, 0x3000, 0x5000) == kErrorIoPending);
  CHECK(LoadBE32(g.base(), 0x5000) == kErrorSuccess && LoadBE32(g.base(), 0x5004) == 1 &&
        LoadBE32(g.base(), 0x5018) == kErrorSuccess);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0x4000, 4, 0x3000, 0x5000) == kErrorIoPending);
  CHECK(LoadBE32(g.base(), 0x5000) == kErrorInsufficientBuffer && LoadBE32(g.base(), 0x5004) == 0);
}

void TestFriendsHaveNobody() {
  std::printf("enumerator: friends list is empty\n");
  TempDir dir;
  Board b(dir);
  b.stats.Record(kXuid, 3, Stats(5000));
  Guest g;
  EnumeratorRegistry registry;
  StoreBE32(g.base(), 0x3000, 99);
  CHECK(CreateFriendsEnumerator(g.base(), registry, 0, 0x3004) == kErrorInvalidParameter);
  CHECK(CreateFriendsEnumerator(g.base(), registry, 0x3000, 0x3004) == kErrorSuccess);
  const uint32_t handle = LoadBE32(g.base(), 0x3004);
  CHECK(LoadBE32(g.base(), 0x3000) == 0);
  const auto query = registry.Take(handle);
  CHECK(query);
  StoreBE32(g.base(), 0x3008, 5);
  CHECK(ServeEnumeration(g.base(), b.board, *query, 0, 0, 0x3008, 0) == kErrorNoMoreFiles);
  CHECK(LoadBE32(g.base(), 0x3008) == 0);
}

void TestReadStats() {
  std::printf("read stats: only a stored player has a row\n");
  TempDir dir;
  Board b(dir);
  Guest g;
  g.Spec(0x1000, 3, {1});
  StoreBE64(g.base(), 0x1800, kXuid);
  StoreBE64(g.base(), 0x1808, kOtherXuid);

  // Never played: the view has no rows, so the game finds nothing to beat.
  CHECK(ServeReadStats(g.base(), b.board, 2, 0x1800, 1, 0x1000, 4096, 0x4000));
  uint32_t view = LoadBE32(g.base(), 0x4004);
  CHECK(LoadBE32(g.base(), 0x4000) == 1 && LoadBE32(g.base(), view) == 3 && LoadBE32(g.base(), view + 8) == 0);

  b.stats.Record(kXuid, 3, Stats(321, {{1, {kTypeInt32, 6}}}));
  CHECK(ServeReadStats(g.base(), b.board, 2, 0x1800, 1, 0x1000, 4096, 0x4000));
  view = LoadBE32(g.base(), 0x4004);
  CHECK(LoadBE32(g.base(), view + 8) == 1 && LoadBE32(g.base(), view + 4) == 1);
  const uint32_t row = LoadBE32(g.base(), view + 12);
  CHECK(LoadBE64(g.base(), row) == kXuid && LoadBE64(g.base(), row + 16) == 321);

  CHECK(ServeReadStats(g.base(), b.board, 1, 0x1808, 1, 0x1000, 4096, 0x4000));  // someone else asked for
  CHECK(LoadBE32(g.base(), LoadBE32(g.base(), 0x4004) + 8) == 0);
  CHECK(!ServeReadStats(g.base(), b.board, 2, 0x1800, 1, 0x1000, 8, 0x4000));    // buffer too small
  CHECK(!ServeReadStats(g.base(), b.board, 2, 0x1800, 1, 0x1000, 4096, 0));
  CHECK(!ServeReadStats(g.base(), b.board, 2, 0x1800, 65, 0x1000, 4096, 0x4000));
  CHECK(!ServeReadStats(g.base(), b.board, 2, 0x1800, 1, 0, 4096, 0x4000));
}

void TestResultsNeverRunPastTheBuffer() {
  std::printf("results: exactly the computed size is written\n");
  TempDir dir;
  Board b(dir);
  b.stats.Record(kXuid, 3, Stats(5, {{1, {kTypeInt64, 1}}, {2, {kTypeInt32, 2}}}));
  Guest g;
  g.Spec(0x1000, 3, {1, 2});
  const std::vector<StatsView> views = {b.board.Ranked(ReadSpecs(g.base(), 0x1000, 1)[0], 1, 100)};
  const uint32_t size = StatsResultsSize(views);
  CHECK(size == kResultsHeaderSize + kViewSize + kRowSize + 2 * kColumnSize);
  CHECK(WriteStatsResults(g.base(), 0x6000, size, views));
  CHECK(g.base()[0x6000 + size] == 0xCD && g.base()[0x6000 - 1] == 0xCD);
  CHECK(!WriteStatsResults(g.base(), 0x6000, size - 1, views));
  CHECK(!WriteStatsResults(g.base(), 0, size, views));
}

// ---- achievement alias, Marketplace, overlay rules ----

void TestZeroAchievementAlias() {
  std::printf("achievement 0: alias while the write is delivered\n");
  Guest g;
  const uint32_t entries = 0x1000;
  const uint32_t ids[] = {5, 0, 7, 0};
  for (uint32_t i = 0; i < 4; ++i) {
    StoreBE32(g.base(), entries + i * kAchievementEntrySize, 0);  // user index
    StoreBE32(g.base(), entries + i * kAchievementEntrySize + 4, ids[i]);
  }
  const auto rewritten = AliasZeroAchievementIds(g.base(), entries, 4);
  CHECK(rewritten.size() == 2);
  CHECK(LoadBE32(g.base(), entries + 4) == 5 && LoadBE32(g.base(), entries + 12) == kZeroAchievementAlias);
  CHECK(LoadBE32(g.base(), entries + 20) == 7 && LoadBE32(g.base(), entries + 28) == kZeroAchievementAlias);
  CHECK(LoadBE32(g.base(), entries) == 0);  // the user index is not an achievement
  RestoreZeroAchievementIds(g.base(), rewritten);
  for (uint32_t i = 0; i < 4; ++i) CHECK(LoadBE32(g.base(), entries + i * kAchievementEntrySize + 4) == ids[i]);

  CHECK(AliasZeroAchievementIds(g.base(), 0, 1).empty());
  CHECK(AliasZeroAchievementIds(g.base(), entries, kMaxAchievementWrites + 1).empty());
  CHECK(AliasZeroAchievementIds(g.base(), entries, 0).empty());
  CHECK(kZeroAchievementAlias != 0 && kZeroAchievementAlias > 11);
  CHECK(GameAchievementNumber(kZeroAchievementAlias) == 0 && GameAchievementNumber(11) == 11);
}

void TestMarketplaceCompletes() {
  std::printf("marketplace: completes at once and reports the UI closed\n");
  constexpr MarketplaceOutcome outcome = OfflineMarketplaceOutcome();
  CHECK(outcome.status == 0);                                // success, never a pending or retry status
  CHECK(outcome.notification == kSystemUiNotification);      // XN_SYS_UI
  CHECK(outcome.notification_data == 0);                     // closed, not opened
}

void TestOverlayRules() {
  std::printf("overlay rules\n");
  CHECK(FitWindowExtent(1000.0f, 420.0f, 560.0f) == 560.0f);  // 70% is 700, capped
  CHECK(FitWindowExtent(700.0f, 420.0f, 560.0f) == 490.0f);
  CHECK(FitWindowExtent(500.0f, 420.0f, 560.0f) == 420.0f);   // 70% is below the minimum
  CHECK(FitWindowExtent(300.0f, 420.0f, 560.0f) == 268.0f);   // never wider than the viewport minus the margins
  CHECK(FitWindowExtent(20.0f, 420.0f, 560.0f) == 0.0f);

  const auto none = Summarize({}, {});
  CHECK(none.total == 0 && none.unlocked == 0 && none.total_gamerscore == 0 && none.earned_gamerscore == 0);
  const auto mixed = Summarize({5, 10, 30}, {true, false, true});
  CHECK(mixed.total == 3 && mixed.unlocked == 2 && mixed.earned_gamerscore == 35 && mixed.total_gamerscore == 45);
  const auto short_flags = Summarize({5, 10}, {true});
  CHECK(short_flags.unlocked == 1 && short_flags.total == 2);

  CHECK(ScrollDirection(true, false, 0.0f) == -1.0f);
  CHECK(ScrollDirection(false, true, 0.0f) == 1.0f);
  CHECK(ScrollDirection(true, true, 0.0f) == 0.0f);
  CHECK(ScrollDirection(false, false, 0.1f) == 0.0f);        // inside the dead zone
  CHECK(ScrollDirection(false, false, 1.0f) == -1.0f);       // stick up scrolls up
  CHECK(ScrollDirection(false, false, -1.0f) == 1.0f);
  CHECK(std::fabs(ScrollDirection(false, false, -0.625f) - 0.5f) < 1e-6f);
  CHECK(ScrollDirection(true, false, -1.0f) == -1.0f);       // the D-pad wins

  CHECK(!InputReleased(true, 0.2, 1.0));                     // B or Escape still down
  CHECK(InputReleased(false, 0.0, 1.0));
  CHECK(InputReleased(true, 1.0, 1.0));                      // a button that never reports release cannot block forever
}

void TestCloseGate() {
  std::printf("close gate: B, Escape and OK never reach the game\n");
  CloseGate gate;
  CHECK(!gate.requested());
  CHECK(!gate.Step(false, 10.0));                            // open: nothing to close
  gate.Request();                                            // B, Escape, F7 or OK
  CHECK(gate.requested());
  CHECK(!gate.Step(true, 10.0));                             // B still down: the game stays blocked
  CHECK(!gate.Step(true, 10.5));
  CHECK(gate.Step(false, 10.6));                             // released: the game may read again
  CloseGate stuck;
  stuck.Request();
  CHECK(!stuck.Step(true, 5.0));
  CHECK(!stuck.Step(true, 5.9));
  CHECK(stuck.Step(true, 6.0));                              // never released: the wait ends
  CloseGate immediate;
  immediate.Request();
  CHECK(immediate.Step(false, 0.0));                         // nothing was pressed: no wait at all
  gate.Request();
  CHECK(gate.requested());                                   // asking again changes nothing
}


// ---- Hard Corps schema, as the game writes and reads it ----

// The game's submission: columns 0x10000002 and 0x10000003 (int32) and the rating 0x20000001 (int64).
void WriteRun(Guest& g, uint32_t view, int32_t first, int32_t second, int64_t rating) {
  g.Property(0x1000, 0x10000002, kTypeInt32, static_cast<uint32_t>(first));
  g.Property(0x1018, 0x10000003, kTypeInt32, static_cast<uint32_t>(second));
  g.Property(0x1030, 0x20000001, kTypeInt64, static_cast<uint64_t>(rating));
  g.View(0x2000, view, 3, 0x1000);
}

std::vector<StatsView> ReadBoard(Board& b, Guest& g, StatsQuery::Kind kind, uint64_t pivot, uint32_t view) {
  g.Spec(0x1800, view, {1, 2});
  StatsQuery query;
  query.kind = kind;
  query.pivot = pivot;
  query.max_rows = 10;
  query.views = ReadSpecs(g.base(), 0x1800, 1);
  CHECK(ServeEnumeration(g.base(), b.board, query, 0x6000, 4096, 0, 0) == kErrorSuccess);
  return AnswerQuery(b.board, query);
}

void TestSubmittedRunIsTheLocalRow() {
  std::printf("leaderboards: only what the game submitted becomes a row\n");
  TempDir dir;
  Board b(dir);
  Guest g;
  constexpr uint32_t kSingle = 3, kMulti = 4;

  // Before any submission nothing is registered, whatever the HUD shows.
  for (const auto kind : {StatsQuery::Kind::kRanked, StatsQuery::Kind::kAround}) {
    const auto views = ReadBoard(b, g, kind, kind == StatsQuery::Kind::kAround ? kXuid : 1, kSingle);
    CHECK(views.size() == 1 && views[0].rows.empty() && views[0].total_rows == 0);
  }

  WriteRun(g, kSingle, 2, 5, 16650);
  const auto writes = ParseViewWrites(g.base(), 0x2000, 1);
  CHECK(writes.size() == 1 && writes[0].view == kSingle && writes[0].stats.rating == 16650);
  CHECK(writes[0].stats.columns.size() == 3);
  CHECK(b.stats.Record(kXuid, writes[0].view, writes[0].stats) == RecordResult::kSaved);

  // ALL and MY SCORE return the local player alone, at rank 1, with the columns the screen asks for.
  for (const auto kind : {StatsQuery::Kind::kRanked, StatsQuery::Kind::kAround}) {
    const auto views = ReadBoard(b, g, kind, kind == StatsQuery::Kind::kAround ? kXuid : 1, kSingle);
    CHECK(views.size() == 1 && views[0].rows.size() == 1 && views[0].total_rows == 1);
    const StatsRow& row = views[0].rows[0];
    CHECK(row.xuid == kXuid && row.rank == 1 && row.rating == 16650 && row.gamertag == "Player");
    CHECK(row.columns.size() == 2);
    CHECK(row.columns[0].first == 1 && row.columns[0].second == (StatColumn{kTypeInt64, 16650}));  // the rating column
    CHECK(row.columns[1].first == 2 && row.columns[1].second == (StatColumn{kTypeInt32, 2}));
  }

  // Nobody else exists: another XUID has no row, a page past rank 1 is empty, and no other view has a row.
  CHECK(ReadBoard(b, g, StatsQuery::Kind::kAround, kOtherXuid, kSingle)[0].rows.empty());
  CHECK(ReadBoard(b, g, StatsQuery::Kind::kRanked, 2, kSingle)[0].rows.empty());
  CHECK(ReadBoard(b, g, StatsQuery::Kind::kRanked, 1, kMulti)[0].rows.empty());

  // A local two-player run lands in its own view and leaves the single-player board alone.
  WriteRun(g, kMulti, 1, 1, 9000);
  const auto multi = ParseViewWrites(g.base(), 0x2000, 1);
  b.stats.Record(kXuid, multi[0].view, multi[0].stats);
  CHECK(ReadBoard(b, g, StatsQuery::Kind::kRanked, 1, kMulti)[0].rows[0].rating == 9000);
  CHECK(ReadBoard(b, g, StatsQuery::Kind::kRanked, 1, kSingle)[0].rows[0].rating == 16650);

  // The friends board asks for the friends' stats plus the player's own: only the player has a row.
  g.Spec(0x1800, kSingle, {1, 2});
  StoreBE64(g.base(), 0x1900, kOtherXuid);
  StoreBE64(g.base(), 0x1908, kXuid);
  CHECK(ServeReadStats(g.base(), b.board, 2, 0x1900, 1, 0x1800, 4096, 0x6000));
  const uint32_t view = LoadBE32(g.base(), 0x6004);
  CHECK(LoadBE32(g.base(), view + 8) == 1);
  CHECK(LoadBE64(g.base(), LoadBE32(g.base(), view + 12)) == kXuid);

  // It survives a restart.
  LocalStats reloaded(dir.path / "stats.toml");
  CHECK(reloaded.Load() == LoadResult::kLoaded);
  CHECK(reloaded.Get(kXuid, kSingle)->rating == 16650 && reloaded.Get(kXuid, kMulti)->rating == 9000);
}

// The game submits only a result that beats the stored one, judged on what XUserReadStats returned: higher is
// better (the write object's compare mode is 1). A view without a row must therefore always allow the write.
void TestGameComparesWithTheStoredResult() {
  std::printf("submit: the game's own comparison decides\n");
  TempDir dir;
  Board b(dir);
  Guest g;
  const auto allowed = [&](int64_t new_rating) {
    g.Spec(0x1800, 3, {1});
    StoreBE64(g.base(), 0x1900, kXuid);
    CHECK(ServeReadStats(g.base(), b.board, 1, 0x1900, 1, 0x1800, 4096, 0x6000));
    const uint32_t view = LoadBE32(g.base(), 0x6004);
    if (LoadBE32(g.base(), view + 8) == 0) return true;
    return static_cast<int64_t>(LoadBE64(g.base(), LoadBE32(g.base(), view + 12) + 16)) < new_rating;
  };
  const auto submit = [&](int64_t rating) {
    if (!allowed(rating)) return false;
    WriteRun(g, 3, 1, 1, rating);
    const auto w = ParseViewWrites(g.base(), 0x2000, 1);
    return b.stats.Record(kXuid, w[0].view, w[0].stats) == RecordResult::kSaved;
  };
  CHECK(submit(500));
  CHECK(!submit(400));
  CHECK(!submit(500));
  CHECK(submit(900));
  CHECK(b.stats.Get(kXuid, 3)->rating == 900);
}

// ---- achievements as the launcher lists them ----

struct FakeManager {
  std::vector<rex::system::AchievementInfo> infos;
  std::vector<uint32_t> unlocked;
  std::vector<rex::system::AchievementInfo> ListAchievements() const { return infos; }
  bool IsUnlocked(uint32_t id) const { return std::find(unlocked.begin(), unlocked.end(), id) != unlocked.end(); }
};

FakeManager HardCorpsAchievements() {
  // The store keeps achievements 1 to 11 as the XDBF numbers them and achievement 0 last, under its alias.
  const uint32_t scores[12] = {5, 5, 10, 10, 10, 10, 20, 25, 25, 25, 25, 30};
  FakeManager manager;
  for (uint32_t number = 1; number <= 11; ++number) {
    manager.infos.push_back({number, "Name " + std::to_string(number), "Done " + std::to_string(number),
                             "Do " + std::to_string(number), "", number + 1, scores[number], 0});
  }
  manager.infos.push_back({kZeroAchievementAlias, "Name 0", "Done 0", "Do 0", "", 8, scores[0], 0});
  return manager;
}

void TestLauncherAchievementList() {
  std::printf("launcher achievements: the store's list in the game's order\n");
  FakeManager manager = HardCorpsAchievements();
  auto entries = ListAchievements(manager);
  CHECK(entries.size() == 12);
  CHECK(entries[0].info.id == kZeroAchievementAlias && entries[0].info.label == "Name 0");  // achievement 0 first
  for (uint32_t i = 1; i < 12; ++i) CHECK(entries[i].info.id == i);
  auto summary = Summarize(entries);
  CHECK(summary.total == 12 && summary.unlocked == 0 && summary.total_gamerscore == 200 && summary.earned_gamerscore == 0);

  manager.unlocked = {kZeroAchievementAlias};  // earned in game: stored under the alias
  entries = ListAchievements(manager);
  CHECK(entries[0].unlocked);
  for (uint32_t i = 1; i < 12; ++i) CHECK(!entries[i].unlocked);
  summary = Summarize(entries);
  CHECK(summary.unlocked == 1 && summary.earned_gamerscore == 5 && summary.total_gamerscore == 200);

  // The listing only reads: the store is the same afterwards and a second listing is identical.
  CHECK(manager.unlocked.size() == 1 && manager.infos.size() == 12 && manager.infos.back().id == kZeroAchievementAlias);
  const auto again = ListAchievements(manager);
  CHECK(again.size() == entries.size() && again[0].unlocked == entries[0].unlocked);

  CHECK(EntryDescription(entries[0]) == "Done 0");
  CHECK(EntryDescription(entries[1]) == "Do 1");
  AchievementEntry no_locked_text = entries[1];
  no_locked_text.info.unachieved_description.clear();
  CHECK(EntryDescription(no_locked_text) == "Done 1");

  const auto none = Summarize(ListAchievements(FakeManager{}));
  CHECK(none.total == 0 && none.unlocked == 0);
}

// ---- wiring that has to stay as it is ----

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(HCU_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// Between two markers of a source file.
std::string Between(const std::string& text, const char* from, const char* to) {
  const size_t start = text.find(from);
  const size_t end = text.find(to, start == std::string::npos ? 0 : start);
  return start == std::string::npos || end == std::string::npos ? std::string() : text.substr(start, end - start);
}

void TestLiveGateStaysNarrow() {
  std::printf("live gate: main menu and pause menu enabled, nothing online touched\n");
  const std::string live = ReadSource("live_fix.cpp");
  CHECK(!live.empty());
  // Leaderboards path: the main menu item, the ranking screen and the score write.
  CHECK(Contains(live, "REX_HOOK_RAW(sub_824F6870)"));
  for (const char* scoped : {"sub_82534AC8", "sub_82545728", "sub_825164F0", "sub_82516608"}) {
    const std::string hook = std::string("REX_HOOK_RAW(") + scoped + ")";
    const size_t at = live.find(hook);
    CHECK(at != std::string::npos);
    CHECK(at != std::string::npos && live.substr(at, 120).find("LiveScope scope;") != std::string::npos);
  }
  // The IsLive override only applies inside a scope and only to user 0.
  CHECK(Contains(live, "t_live_scope > 0 && ctx.r4.u32 == 0"));
  // Everything else that reads the sign-in state or privilege stays real: the UDP-socket class (82541A38), the
  // multiplayer privilege (824EEF58, 82570488), the sign-in thunk (82570478), presence (8253EDE0), the title
  // update (824F5218) and the friends check (82570480).
  for (const char* real : {"sub_82541A38", "sub_824EEF58", "sub_82570488", "sub_82570478", "sub_8253EDE0",
                           "sub_824F5218", "sub_82570480", "sub_8253EF20"}) {
    CHECK(!Contains(live, (std::string("REX_HOOK_RAW(") + real + ")").c_str()));
  }
  // No network, no privilege grant, no global sign-in state, and the game alone decides when a score is submitted.
  for (const char* forbidden : {"NetDll", "socket", "XamUserCheckPrivilege", "XamUserGetSigninState", "signin_state",
                                "BroadcastNotification(2", "sub_82570480", "sub_824B7AA0", "sub_824DF5E8", "HCU_DEBUG"}) {
    CHECK(!Contains(live, forbidden));
  }
}

void TestModalOwnership() {
  std::printf("modal ownership: one overlay owns the game's input\n");
  const std::string menu = ReadSource("cheat_menu.cpp");
  const std::string app = ReadSource("hard_corps_uprising_app.h");
  const std::string modal = ReadSource("modal_dialog.cpp");
  CHECK(!menu.empty() && !app.empty() && !modal.empty());
  // F10 does not open over another overlay, and F7 and the game's requests do not either.
  CHECK(Contains(Between(menu, "void CheatMenu::OnKeyDown", "void CheatMenu::DialogClosed"), "ModalDialog::AnyOpen()"));
  CHECK(Contains(Between(app, "void OpenAchievements()", "void OpenDownloadContent()"), "ModalDialog::AnyOpen()"));
  CHECK(Contains(Between(app, "void OpenDownloadContent()", "hcu::GameSettings game_settings_"), "ModalDialog::AnyOpen()"));
  // The game's input is off while any overlay is open, and the F10 menu keeps its seven rows.
  CHECK(Contains(Between(menu, "bool CheatMenu::GameInputActive", "void CheatMenu::OnKeyDown"), "ModalDialog::AnyOpen()"));
  size_t rows = 0;
  for (size_t at = menu.find("{cheats::Cheat::"); at != std::string::npos; at = menu.find("{cheats::Cheat::", at + 1)) ++rows;
  CHECK(rows == 7);
  CHECK(Contains(menu, "class CheatMenuDialog final : public ModalDialog"));
  CHECK(Contains(menu, "cheats::ToggleForSession"));
  // B and Escape go through the same close gate, and Escape is consumed before the game sees it.
  CHECK(Contains(modal, "ImGuiKey_GamepadFaceRight"));
  CHECK(Contains(modal, "close_.Step(pressed_.any()"));
  CHECK(Contains(modal, "e.set_handled(true)"));
}

void TestLauncherPageWiring() {
  std::printf("launcher: Achievements page is read-only and sits between Cheats and Controls\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(!launcher.empty());
  size_t last = 0;
  for (const char* page : {"{\"Play\"", "{\"DLC\"", "{\"Display\"", "{\"Graphics\"", "{\"Gameplay\"", "{\"Cheats\"",
                           "{\"Achievements\"", "{\"Controls\"", "{\"About\""}) {
    const size_t at = launcher.find(page);
    CHECK(at != std::string::npos && at > last);
    last = at;
  }
  const std::string page = Between(launcher, "void PageAchievements", "static void PageControls");
  CHECK(!page.empty());
  for (const char* control : {"Button", "Toggle", "Unlock", "Reset", "Import", "Export", "Save("}) {
    CHECK(!Contains(page, control));
  }
  // The headless runtime that read the achievements is gone before the game builds its own.
  const std::string start = Between(launcher, "void StartGame()", "// Drawn outside the disabled frame");
  const size_t reset = start.find("achievements_.reset()");
  CHECK(reset != std::string::npos && reset < start.find("Finish(cb_.play)"));
}

}  // namespace

int main() {
  TestCleanFileAndFirstValue();
  TestRestartKeepsValues();
  TestSubmissionReplacesAndViewsAreIndependent();
  TestRejectedSubmissions();
  TestMalformedFiles();
  TestAtomicReplacement();
  TestPathSafety();
  TestParseViewWrites();
  TestEnumeratorCreation();
  TestRegistryRetiresAndBounds();
  TestEnumerationAnswers();
  TestEnumerationBufferAndOverlapped();
  TestFriendsHaveNobody();
  TestReadStats();
  TestResultsNeverRunPastTheBuffer();
  TestZeroAchievementAlias();
  TestMarketplaceCompletes();
  TestOverlayRules();
  TestCloseGate();
  TestSubmittedRunIsTheLocalRow();
  TestGameComparesWithTheStoredResult();
  TestLauncherAchievementList();
  TestLiveGateStaysNarrow();
  TestModalOwnership();
  TestLauncherPageWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
