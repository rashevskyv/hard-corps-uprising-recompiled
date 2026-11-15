// Switch button icons on the game's prompt images (BUTTON ICONS / button_glyphs in settings.txt), Switch only.
// The SDK's HostPathFile::ReadSync calls rex_hcu_glyph_read after each file read (switch/sdk-edits-hcu.py): for
// Gui/Img/guidebutton_<NAME>.bin it overwrites the bytes read with our icon in the game's own .bin format (same
// 5152-byte size). The art and the format: tools/make_glyphs.py -> button_glyphs.h.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <strings.h>

#include "button_glyphs.h"
#include "settings_menu.h"

namespace {

// XGAddress2DTiledOffset (tools/make_glyphs.py has the same).
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t log_bpp) {
  const uint32_t aw = (width + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aw >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 0xE) << 2)) << log_bpp;
  const uint32_t off = macro + ((micro & ~0xFu) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((((off & ~0x1FFu) << 3) + ((y & 16) << 7) + ((off & 0x1C0) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +
           (off & 0x3F))) >> log_bpp;
}

constexpr uint32_t kSize = 32, kHeader = 32, kPalette = 1024, kData = 4096, kFile = kHeader + kPalette + kData;

// The whole .bin file: header, palette (entry 0 = the transparent key), tiled indices with the rows bottom-up.
void Build(const HcuGlyph& g, uint8_t* f) {
  std::memset(f, 0, kFile);
  const uint32_t header[8] = {kSize, kSize, kPalette, kData, 7, 0, 0, 0};
  for (int i = 0; i < 8; i++)
    for (int b = 0; b < 4; b++) f[i * 4 + b] = uint8_t(header[i] >> (24 - 8 * b));
  uint8_t* pal = f + kHeader;
  for (uint32_t i = 0; i < 256; i++) pal[i * 4] = 0xFF;  // unused entries: opaque black
  pal[1] = pal[2] = 0xFF, pal[3] = 0;                     // entry 0: FF FF FF 00
  std::memcpy(pal + 4, k_glyph_palette, sizeof(k_glyph_palette));
  uint8_t* data = pal + kPalette;
  for (uint32_t y = 0; y < kSize; y++)
    for (uint32_t x = 0; x < kSize; x++) data[TiledOffset(x, y, kSize, 0)] = g.index[(kSize - 1 - y) * kSize + x];
}

}  // namespace

extern "C" int rex_hcu_button_glyphs(void) { return hcu_settings.button_glyphs; }

extern "C" void rex_hcu_glyph_read(const char* name, uint64_t offset, uint8_t* dst, size_t len) {
  if (!hcu_settings.button_glyphs || !name || strncasecmp(name, "guidebutton_", 12) || offset >= kFile) return;
  for (const HcuGlyph& g : k_glyphs) {
    if (strcasecmp(g.file, name)) continue;
    uint8_t f[kFile];
    Build(g, f);
    std::memcpy(dst, f + offset, len < kFile - offset ? len : kFile - offset);
    static int logged = 0;
    if (logged++ < 24) std::printf("[nx] glyphs: %s served as the Switch icon\n", name);
    return;
  }
}
