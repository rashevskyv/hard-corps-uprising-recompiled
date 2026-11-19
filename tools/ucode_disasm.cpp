// The SDK's disassembly of one Xenos shader, offline (tools/shader_scan.py builds and runs it):
//   ucode_disasm <in.bin> <vert|frag> <big|little> <out dir>
// writes <out dir>/shader_<hash>.ucode.<vert|frag> (the text tools/xenos2glsl.py reads) and .ucode.bin.<vert|frag>,
// the same two files and the same hash as the deko3d backend's missing/ dumps (src/gpu/deko3d_backend.cpp DumpMissing),
// and prints the hash. <in.bin> holds the dwords big-endian (as in the game's files) or little-endian (a dump's .bin).
// Checked on 2026-11-19: the 103 console and trace dumps give the same hash and the same text, byte for byte.
#include <cstdio>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/string/buffer.h>
#include "ucode_hash.h"

// The SDK's translator.cpp reads this flag; graphics/flags.cpp, which defines it, pulls in RenderDoc.
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "unused by this tool");

using namespace rex::graphics;

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr, "usage: ucode_disasm <in.bin> <vert|frag> <big|little> <out dir>\n");
    return 2;
  }
  FILE* f = std::fopen(argv[1], "rb");
  if (!f) return 1;
  std::vector<uint32_t> host;
  uint32_t x;
  while (std::fread(&x, 4, 1, f) == 1) host.push_back(x);
  std::fclose(f);
  bool big = std::string(argv[3]) == "big";
  if (big) for (auto& v : host) v = __builtin_bswap32(v);
  // The backends hash the guest's bytes (big-endian); the Shader keeps the host's dwords.
  std::vector<uint32_t> guest = host;
  for (auto& v : guest) v = __builtin_bswap32(v);
  uint64_t hash = hcu::UcodeHash(guest.data(), uint32_t(guest.size()));
  bool vs = std::string(argv[2]) == "vert";
  Shader s(vs ? xenos::ShaderType::kVertex : xenos::ShaderType::kPixel, hash, host.data(), host.size(),
           std::endian::little);
  rex::string::StringBuffer buf;
  s.AnalyzeUcode(buf);
  char name[1024];
  std::snprintf(name, sizeof(name), "%s/shader_%016llX.ucode.%s", argv[4], (unsigned long long)hash, argv[2]);
  FILE* o = std::fopen(name, "w");
  if (!o) return 1;
  std::fwrite(s.ucode_disassembly().data(), 1, s.ucode_disassembly().size(), o);
  std::fclose(o);
  std::snprintf(name, sizeof(name), "%s/shader_%016llX.ucode.bin.%s", argv[4], (unsigned long long)hash, argv[2]);
  o = std::fopen(name, "wb");
  if (!o) return 1;
  std::fwrite(host.data(), 4, host.size(), o);
  std::fclose(o);
  std::printf("%016llX\n", (unsigned long long)hash);
  return 0;
}
