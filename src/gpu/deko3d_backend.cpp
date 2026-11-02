// deko3d GPU backend (--gpu_backend=deko3d, Nintendo Switch): the SDK's Xenos command processor
// parses the ring (fences, read-pointer write-back, vblank, bin masks), this backend draws with
// deko3d. No EDRAM model: a host image per render target (keyed by EDRAM base and pitch), resolves
// are image copies into images keyed by guest address, textures bound at such an address alias
// them (no readback), the swap presents the image at the front buffer's address through one
// textured triangle. Shaders are translated offline (tools/xenos2glsl.py, uam) and loaded by the
// ucode hash from sdmc:/switch/hcu/shaders/. docs/native-renderer-plan.md, M3.
// cvhd-recomp, BSD-3 like the SDK.
// Linux (HCU_DK_GL): the same code over an OpenGL stand-in for libdeko3d (dk_gl/dk_gl_shim.cpp), a test
// host for the backend logic, with the shaders as translated GLSL instead of DKSH.
#if defined(__SWITCH__) || (defined(__linux__) && __has_include(<EGL/egl.h>))
#ifdef __SWITCH__
#include <switch.h>
#include <deko3d.h>
#else
#define HCU_DK_GL 1
#include "dk_gl/switch.h"
#include "dk_gl/dk_gl_shim.h"
#endif
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/string/buffer.h>
#include <rex/system/gpu_plugin.h>

#include "../settings_menu.h"
#ifdef __SWITCH__
#include "../frame_recorder.h"
#endif
#include "ucode_hash.h"
#include "untile_fast.h"

// Where the DKSH shaders are (tools/xenos2glsl.py + uam, staged by switch/build.sh): a game sets it
// in its hcu.toml / arguments. Missing shaders are dumped to its missing/ subdirectory. The debug
// markers are files next to the directory: dk_dump (image readback at swaps 120 and 600),
// dk_present_rt (present the widest colour render target instead of the front buffer).
REXCVAR_DEFINE_STRING(deko3d_shader_dir, "sdmc:/switch/hcu/shaders/", "GPU",
                      "deko3d backend: directory of shader_<ucode hash>.vert|geom|frag.dksh and present.vert|frag.dksh")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// Xenos rectangle lists (3 vertices, the 4th = v1 + v2 - v0): by default through the geometry shader
// the translator writes next to each vertex shader; the hardware fill-rectangle mode (NV_fill_rectangle,
// the same semantics) is an option for the console: Eden ignores that mode (one triangle is drawn).
REXCVAR_DEFINE_BOOL(deko3d_fill_rectangle, false, "GPU",
                    "deko3d backend: draw rectangle lists with the hardware fill-rectangle mode instead of a geometry shader")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The Switch host layer's dirty-page bytes (switch/host/memory_nx.cpp): one byte per 4 KB page of the
// physical arena, set by every guest store (the codegen pch) and by host writes into guest memory
// (file reads). This backend is their only consumer (there is no SharedMemory here).
extern "C" volatile uint8_t rex_nx_dirty[0x20000];
extern "C" volatile uint8_t rex_nx_dirty_coarse[0x200];
#ifdef HCU_DK_GL
// No store macros mark pages on Linux: the arrays stay empty and changes are found by hashing the guest
// memory of each texture at its bind (MemoryChanged).
extern "C" volatile uint8_t rex_nx_dirty[0x20000] = {};
extern "C" volatile uint8_t rex_nx_dirty_coarse[0x200] = {};
#endif

namespace rex::graphics::deko {

namespace {

constexpr uint32_t kFrames = 2;
constexpr uint32_t kFrameDataBytes = 24u << 20;  // vertex, index and uniform data of one frame
constexpr uint32_t kFrameCmdBytes = 4u << 20;
// One frame's texture uploads. The first stage's backdrop layers are 4320x2432 RGBA8 textures (41 MB each): a
// texture larger than this is not uploaded at all ("staging ring full") and its draws sample nothing.
constexpr uint32_t kStagingBytes = 72u << 20;
// ponytail: a bump pool, nothing is freed (the title 132 MB, the menu 185 MB): a free list with
// eviction of textures not bound for a while is the upgrade when a stage fills it.
constexpr uint32_t kImagePoolBytes = 448u << 20;
constexpr uint32_t kCodePoolBytes = 4u << 20;
constexpr uint32_t kMaxImageDescriptors = 512;
constexpr uint32_t kSamplerCount = 512;  // see SamplerIndex: min, mag, border, clamp x, clamp y
constexpr uint32_t kPresentWidth = 1280;
constexpr uint32_t kPresentHeight = 720;
constexpr uint32_t kEdramTiles = 2048;  // 10 MB of 5120-byte tiles (80 x 16 x 32 bit)
constexpr uint32_t kMaxTargetHeight = 1088;

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::printf("[dk] ");
  std::vprintf(fmt, ap);
  std::printf("\n");
  va_end(ap);
#ifndef __SWITCH__
  std::fflush(stdout);
#endif
  // On the Switch stdout is the SD log, buffered by the host layer and flushed twice a second: a flush
  // per line is a filesystem call (a kernel wait of a millisecond or more on FAT) in the command
  // thread. HCU console 0.160: tens of image lines per stage frame made 63-66 ms of "draws" per frame.
}

// Settings menu DEPTH 32 BIT (at start): the format of every depth target and depth texture. Z24S8 = what the 360's
// D24S8 / D24FS8 EDRAM targets map to by default; ZF32_X24S8 = 32-bit float depth (an A/B of the console's rings and
// grid, which look like two nearly coplanar surfaces fighting; Eden gives D24 targets 32-bit float depth on the PC).
DkImageFormat g_depth_fmt = DkImageFormat_Z24S8;
bool g_dxt_cpu = false;  // DXT ON CPU, read once at start (a live change flipped a texture's format between draws)

// DXT ON CPU: one 4x4 block of DXT1 (bc 1), DXT3 (2) or DXT5 (3) to RGBA8 at dst (row pitch in bytes), as the D3D9
// decoders do (DXT1 with c0 <= c1: three colours and transparent black).
void Rgb565(uint16_t c, int out[3]) {
  out[0] = ((c >> 11) & 31) * 255 / 31;
  out[1] = ((c >> 5) & 63) * 255 / 63;
  out[2] = (c & 31) * 255 / 31;
}
void DecodeDxtBlock(const uint8_t* b, int bc, uint8_t* dst, uint32_t pitch, uint32_t cols, uint32_t rows) {
  uint8_t alpha[16];
  const uint8_t* cb = b;
  if (bc == 2) {
    for (int i = 0; i < 16; i++) alpha[i] = uint8_t(((b[i / 2] >> ((i & 1) * 4)) & 15) * 17);
    cb = b + 8;
  } else if (bc == 3) {
    int a[8] = {b[0], b[1]};
    if (a[0] > a[1]) for (int i = 2; i < 8; i++) a[i] = ((8 - i) * a[0] + (i - 1) * a[1]) / 7;
    else { for (int i = 2; i < 6; i++) a[i] = ((6 - i) * a[0] + (i - 1) * a[1]) / 5; a[6] = 0; a[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) alpha[i] = uint8_t(a[(bits >> (3 * i)) & 7]);
    cb = b + 8;
  }
  const uint16_t c0 = uint16_t(cb[0] | cb[1] << 8), c1 = uint16_t(cb[2] | cb[3] << 8);
  int pal[4][4];
  Rgb565(c0, pal[0]);
  Rgb565(c1, pal[1]);
  const bool four = bc != 1 || c0 > c1;
  for (int k = 0; k < 3; k++) {
    pal[2][k] = four ? (2 * pal[0][k] + pal[1][k]) / 3 : (pal[0][k] + pal[1][k]) / 2;
    pal[3][k] = four ? (pal[0][k] + 2 * pal[1][k]) / 3 : 0;
  }
  pal[0][3] = pal[1][3] = pal[2][3] = 255;
  pal[3][3] = four ? 255 : 0;
  const uint32_t idx = uint32_t(cb[4] | cb[5] << 8 | cb[6] << 16 | uint32_t(cb[7]) << 24);
  for (uint32_t y = 0; y < rows; y++)
    for (uint32_t x = 0; x < cols; x++) {
      const int i = int(y * 4 + x), s = (idx >> (2 * i)) & 3;
      uint8_t* p = dst + y * pitch + x * 4;
      p[0] = uint8_t(pal[s][0]);
      p[1] = uint8_t(pal[s][1]);
      p[2] = uint8_t(pal[s][2]);
      p[3] = bc == 1 ? uint8_t(pal[s][3]) : alpha[i];
    }
}

// Bytes per pixel of an image read back for a dump (the block formats: their blocks, rounded up).
uint32_t DumpBytesPerPixel(DkImageFormat f) {
  switch (f) {
    case DkImageFormat_RGBA16_Float: case DkImageFormat_ZF32_X24S8: return 8;
    case DkImageFormat_R8_Unorm: return 1;
    case DkImageFormat_RG8_Unorm: return 2;
    default: return 4;
  }
}

void DebugCallback(void*, const char* context, DkResult result, const char* message) {
  Log("deko3d %s: result %d: %s", context ? context : "?", int(result), message ? message : "");
  std::fflush(stdout);
}

#ifdef __SWITCH__
extern "C" [[noreturn]] void rex_nx_quick_exit(const char* why);  // switch/host/switch_nx.cpp

// The GPU channel's error, when deko3d put the queue in its error state (a fault in a list submitted before; the release
// libdeko3d drops its message and aborts at the next queue call). The channel's fd is not in deko3d's API: every nvdrv fd
// is asked, the others refuse the ioctl. Types: 8 idle timeout, 24 semaphore timeout, 25 illegal method, 31 MMU fault,
// 32 PBDMA error.
void LogGpuChannelErrors() {
  for (u32 fd = 0; fd < 128; fd++) {
    NvNotification n{};
    if (R_FAILED(nvioctlChannel_GetErrorNotification(fd, &n))) continue;
    NvError e{};
    const Result ri = nvioctlChannel_GetErrorInfo(fd, &e);
    Log("gpu channel fd %u: notification type %u info16 0x%X status 0x%X time %llu; error info rc 0x%X type %u info %08X %08X %08X %08X", fd,
        n.info32, n.info16, n.status, (unsigned long long)n.timestamp, ri, e.type, e.info[0], e.info[1], e.info[2], e.info[3]);
  }
}
#endif

struct MemBlock {
  DkMemBlock block = nullptr;
  uint8_t* cpu = nullptr;
  DkGpuAddr gpu = 0;
  uint32_t size = 0;
  uint32_t used = 0;
  uint32_t peak = 0;
  bool Create(DkDevice device, uint32_t bytes, uint32_t flags) {
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, bytes);
    maker.flags = flags;
    block = dkMemBlockCreate(&maker);
    if (!block) return false;
    size = bytes;
    cpu = (flags & DkMemBlockFlags_CpuUncached) || (flags & DkMemBlockFlags_CpuCached)
              ? static_cast<uint8_t*>(dkMemBlockGetCpuAddr(block))
              : nullptr;
    gpu = dkMemBlockGetGpuAddr(block);
    return true;
  }
  // Bump allocation; UINT32_MAX when full.
  uint32_t Alloc(uint32_t bytes, uint32_t align) {
    uint32_t off = (used + align - 1) & ~(align - 1);
    if (off + bytes > size) return UINT32_MAX;
    used = off + bytes;
    if (used > peak) peak = used;
    return off;
  }
  void Reset() { used = 0; }
};

struct HostImage {
  DkImage image{};
  DkImageLayout layout{};
  DkImageFormat format = DkImageFormat_RGBA8_Unorm;
  uint32_t width = 0, height = 0;
  bool is_depth = false;
  uint32_t flags = 0;         // gh: the layout flags (a recycled image must match)
  bool from_resolve = false;  // host-native channels: the fetch swizzle is not applied
  bool reversed_channels = false;  // the format stores the Xenos channels in reverse order (4-4-4-4)
  uint32_t content_serial = 0;  // the draw serial when the contents were set (upload or resolve); 0 = never
  uint64_t mem_hash = 0;        // Linux (no dirty bytes): hash of the guest memory when the contents were set
  uint32_t mem_bytes = 0;       // ... over this many bytes (the same range at every later check)
  uint32_t checked_serial = 0;  // the draw serial of the last dirty-page check
  uint32_t guest_base = 0;    // textures and resolve destinations
  uint32_t edram_base = 0, edram_pitch = 0;  // render targets
  std::unordered_map<HostImage*, uint32_t> merged;  // depth textures: piece image -> its content serial when copied in
  uint32_t resolve_swap = 0;  // the swap count when a resolve last filled this image (0 = never)
  uint32_t merge_epoch = 0;  // depth textures: the depth resolve count when the pieces were last looked at
};

// Microseconds spent in a scope, added to a counter (the 60-swap log prints the per-frame averages: CPU recording
// time against the waits for the GPU tells a CPU-bound frame from a GPU-bound one).
struct ScopedUs {
  uint64_t& total;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  explicit ScopedUs(uint64_t& t) : total(t) {}
  ~ScopedUs() { total += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()); }
};

class DkShaderObj : public Shader {
 public:
  DkShaderObj(xenos::ShaderType type, uint64_t hash, const uint32_t* dwords, uint32_t count)
      : Shader(type, hash, dwords, count) {}
  DkShader shader{};
  bool loaded = false;
  DkShader geom{};           // vertex shaders: the rectangle-list geometry shader
  bool geom_loaded = false;
};

struct Frame {
  DkCmdBuf cmdbuf = nullptr;
  uint32_t cmd_offset = 0;
  MemBlock data;     // vertex/index/uniform ring
  MemBlock staging;  // texture uploads
  uint32_t desc_offset = 0;  // image descriptors of this frame, inside `data`? no: a separate static block
  DkFence fence{};
  bool fenced = false;
  uint32_t next_descriptor = 0;
  std::unordered_map<uint64_t, uint32_t> descriptors;  // image (and swizzle) -> descriptor slot this frame
  uint32_t ts_off = UINT32_MAX;  // two GPU timestamps (16 bytes each) in `data`: frame start, after the last barrier
  bool rec_pending = false;  // RECORD 5 SEC: this frame's front buffer copy waits in rec_ for the fence
  uint32_t rec_swap = 0;
  uint64_t rec_us = 0;
};

struct SysConstants {
  float ndc_scale[4];
  float ndc_offset[4];
  float alpha[4];
  uint32_t bools[8];
  uint32_t loops[32];  // gh: integer loop constants i0..i31 (count | start << 8 | step << 16)
};

// Xenos counts from kNever = 0, deko3d from DkCompareOp_Never = 1 (HCU 0.120: LEQUAL became EQUAL, the
// depth-tested 3D level never showed).
DkCompareOp CompareOp(xenos::CompareFunction f) { return DkCompareOp(uint32_t(f) + 1); }

// Xenos StencilOp (kKeep = 0 ...) to deko3d (Keep = 1 ...).
DkStencilOp StencilOpOf(xenos::StencilOp op) {
  switch (op) {
    case xenos::StencilOp::kZero: return DkStencilOp_Zero;
    case xenos::StencilOp::kReplace: return DkStencilOp_Replace;
    case xenos::StencilOp::kIncrementClamp: return DkStencilOp_Incr;
    case xenos::StencilOp::kDecrementClamp: return DkStencilOp_Decr;
    case xenos::StencilOp::kInvert: return DkStencilOp_Invert;
    case xenos::StencilOp::kIncrementWrap: return DkStencilOp_IncrWrap;
    case xenos::StencilOp::kDecrementWrap: return DkStencilOp_DecrWrap;
    default: return DkStencilOp_Keep;
  }
}

DkBlendFactor BlendFactor(xenos::BlendFactor f) {
  switch (f) {
    case xenos::BlendFactor::kZero: return DkBlendFactor_Zero;
    case xenos::BlendFactor::kOne: return DkBlendFactor_One;
    case xenos::BlendFactor::kSrcColor: return DkBlendFactor_SrcColor;
    case xenos::BlendFactor::kOneMinusSrcColor: return DkBlendFactor_InvSrcColor;
    case xenos::BlendFactor::kSrcAlpha: return DkBlendFactor_SrcAlpha;
    case xenos::BlendFactor::kOneMinusSrcAlpha: return DkBlendFactor_InvSrcAlpha;
    case xenos::BlendFactor::kDstColor: return DkBlendFactor_DstColor;
    case xenos::BlendFactor::kOneMinusDstColor: return DkBlendFactor_InvDstColor;
    case xenos::BlendFactor::kDstAlpha: return DkBlendFactor_DstAlpha;
    case xenos::BlendFactor::kOneMinusDstAlpha: return DkBlendFactor_InvDstAlpha;
    case xenos::BlendFactor::kConstantColor: return DkBlendFactor_ConstColor;
    case xenos::BlendFactor::kOneMinusConstantColor: return DkBlendFactor_InvConstColor;
    case xenos::BlendFactor::kConstantAlpha: return DkBlendFactor_ConstAlpha;
    case xenos::BlendFactor::kOneMinusConstantAlpha: return DkBlendFactor_InvConstAlpha;
    case xenos::BlendFactor::kSrcAlphaSaturate: return DkBlendFactor_SrcAlphaSaturate;
    default: return DkBlendFactor_One;
  }
}

DkBlendOp BlendOp(xenos::BlendOp op) {
  switch (op) {
    case xenos::BlendOp::kAdd: return DkBlendOp_Add;
    case xenos::BlendOp::kSubtract: return DkBlendOp_Sub;
    case xenos::BlendOp::kMin: return DkBlendOp_Min;
    case xenos::BlendOp::kMax: return DkBlendOp_Max;
    case xenos::BlendOp::kRevSubtract: return DkBlendOp_RevSub;
    default: return DkBlendOp_Add;
  }
}

bool PrimitiveOf(xenos::PrimitiveType t, DkPrimitive& out, bool& rects) {
  rects = false;
  switch (t) {
    case xenos::PrimitiveType::kPointList: out = DkPrimitive_Points; return true;
    case xenos::PrimitiveType::kLineList: out = DkPrimitive_Lines; return true;
    case xenos::PrimitiveType::kLineStrip: out = DkPrimitive_LineStrip; return true;
    case xenos::PrimitiveType::kLineLoop: out = DkPrimitive_LineLoop; return true;
    case xenos::PrimitiveType::kTriangleList: out = DkPrimitive_Triangles; return true;
    case xenos::PrimitiveType::kTriangleFan: out = DkPrimitive_TriangleFan; return true;
    case xenos::PrimitiveType::kTriangleStrip: out = DkPrimitive_TriangleStrip; return true;
    case xenos::PrimitiveType::kRectangleList: out = DkPrimitive_Triangles; rects = true; return true;
    case xenos::PrimitiveType::kQuadList: out = DkPrimitive_Quads; return true;
    case xenos::PrimitiveType::kQuadStrip: out = DkPrimitive_QuadStrip; return true;
    case xenos::PrimitiveType::kPolygon: out = DkPrimitive_Polygon; return true;
    default: return false;
  }
}

bool VertexAttribOf(const ParsedVertexFetchInstruction::Attributes& a, DkVtxAttribSize& size, DkVtxAttribType& type) {
  using VF = xenos::VertexFormat;
  DkVtxAttribType norm = a.is_integer ? (a.is_signed ? DkVtxAttribType_Sscaled : DkVtxAttribType_Uscaled)
                                      : (a.is_signed ? DkVtxAttribType_Snorm : DkVtxAttribType_Unorm);
  type = norm;
  switch (a.data_format) {
    case VF::k_8_8_8_8: size = DkVtxAttribSize_4x8; return true;
    case VF::k_2_10_10_10: size = DkVtxAttribSize_10_10_10_2; return true;
    case VF::k_16_16: size = DkVtxAttribSize_2x16; return true;
    case VF::k_16_16_16_16: size = DkVtxAttribSize_4x16; return true;
    case VF::k_16_16_FLOAT: size = DkVtxAttribSize_2x16; type = DkVtxAttribType_Float; return true;
    case VF::k_16_16_16_16_FLOAT: size = DkVtxAttribSize_4x16; type = DkVtxAttribType_Float; return true;
    case VF::k_32: size = DkVtxAttribSize_1x32; type = a.is_signed ? DkVtxAttribType_Sscaled : DkVtxAttribType_Uscaled; return true;
    case VF::k_32_32: size = DkVtxAttribSize_2x32; type = a.is_signed ? DkVtxAttribType_Sscaled : DkVtxAttribType_Uscaled; return true;
    case VF::k_32_32_32_32: size = DkVtxAttribSize_4x32; type = a.is_signed ? DkVtxAttribType_Sscaled : DkVtxAttribType_Uscaled; return true;
    case VF::k_32_FLOAT: size = DkVtxAttribSize_1x32; type = DkVtxAttribType_Float; return true;
    case VF::k_32_32_FLOAT: size = DkVtxAttribSize_2x32; type = DkVtxAttribType_Float; return true;
    case VF::k_32_32_32_FLOAT: size = DkVtxAttribSize_3x32; type = DkVtxAttribType_Float; return true;
    case VF::k_32_32_32_32_FLOAT: size = DkVtxAttribSize_4x32; type = DkVtxAttribType_Float; return true;
    case VF::k_10_11_11: case VF::k_11_11_10: size = DkVtxAttribSize_11_11_10; type = DkVtxAttribType_Float; return true;
    default: return false;
  }
}

// Guest texture format -> deko3d format. The packed 16-bit formats need care: deko3d names its formats
// by memory bit order, most significant field first (format_traits.inc in libdeko3d): RGBA4, RGB565 and
// RGB5A1 keep R in the low bits; the Xenos packs R in the high bits (R5G6B5, A1R5G5B5 with R at 14:10) and
// that is the BGR565 / BGR5A1 pair. Xenos 4-4-4-4 is R:G:B:A from the top nibble; deko3d's only 4-bit
// format has the reverse order, so its channels are reversed in the view swizzle (`reversed`).
bool TextureFormatOf(xenos::TextureFormat f, DkImageFormat& out, bool& reversed) {
  using TF = xenos::TextureFormat;
  reversed = false;
  switch (f) {
    case TF::k_8_8_8_8: case TF::k_8_8_8_8_A: out = DkImageFormat_RGBA8_Unorm; return true;
    case TF::k_8: case TF::k_8_A: case TF::k_8_B: out = DkImageFormat_R8_Unorm; return true;
    case TF::k_8_8: out = DkImageFormat_RG8_Unorm; return true;
    // DXT ON CPU (settings menu, at start): decoded to RGBA8 in UploadTexture (A/B of the console's brick-like hatching,
    // which has the size of the sky's and sand's DXT1 blocks magnified).
    case TF::k_DXT1: out = g_dxt_cpu ? DkImageFormat_RGBA8_Unorm : DkImageFormat_RGBA_BC1; return true;
    case TF::k_DXT2_3: out = g_dxt_cpu ? DkImageFormat_RGBA8_Unorm : DkImageFormat_RGBA_BC2; return true;
    case TF::k_DXT4_5: out = g_dxt_cpu ? DkImageFormat_RGBA8_Unorm : DkImageFormat_RGBA_BC3; return true;
    case TF::k_DXN: out = DkImageFormat_RG_BC5_Unorm; return true;
    case TF::k_DXT5A: out = DkImageFormat_R_BC4_Unorm; return true;
    case TF::k_4_4_4_4: out = DkImageFormat_RGBA4_Unorm; reversed = true; return true;
    case TF::k_5_6_5: out = DkImageFormat_BGR565_Unorm; return true;
    case TF::k_1_5_5_5: out = DkImageFormat_BGR5A1_Unorm; return true;
    case TF::k_16_16_16_16_FLOAT: out = DkImageFormat_RGBA16_Float; return true;
    case TF::k_16_16_16_16: out = DkImageFormat_RGBA16_Unorm; return true;
    case TF::k_32_FLOAT: out = DkImageFormat_R32_Float; return true;
    case TF::k_32_32_FLOAT: out = DkImageFormat_RG32_Float; return true;
    case TF::k_32_32_32_32_FLOAT: out = DkImageFormat_RGBA32_Float; return true;
    case TF::k_16_FLOAT: out = DkImageFormat_R16_Float; return true;
    case TF::k_16_16_FLOAT: out = DkImageFormat_RG16_Float; return true;
    case TF::k_16: out = DkImageFormat_R16_Unorm; return true;
    case TF::k_16_16: out = DkImageFormat_RG16_Unorm; return true;
    // Resolves of 2_10_10_10 and 8_8_8_8 targets (HCU's blur chain and post effects read them back as textures,
    // fetch formats "AS_16_16_16_16" = the resolve expanded to 64 bits): the host image keeps the target's format.
    case TF::k_2_10_10_10: case TF::k_2_10_10_10_AS_16_16_16_16: out = DkImageFormat_RGB10A2_Unorm; return true;
    case TF::k_8_8_8_8_AS_16_16_16_16: out = DkImageFormat_RGBA8_Unorm; return true;
    // A depth resolve target (HCU's stage shadow map, 8-tap compare in the pixel shader): the host depth image, sampled as depth.
    case TF::k_24_8: case TF::k_24_8_FLOAT: out = g_depth_fmt; return true;
    default: return false;
  }
}

DkImageFormat ColorTargetFormatOf(xenos::ColorRenderTargetFormat f) {
  switch (f) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA: return DkImageFormat_RGBA8_Unorm_sRGB;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10: return DkImageFormat_RGB10A2_Unorm;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT: return DkImageFormat_RGBA16_Float;
    default: return DkImageFormat_RGBA8_Unorm;
  }
}

// Xenos clamp modes as the Vulkan backend maps them (texture_cache.cpp kAddressModeMap): the half-way
// modes have no host equivalent and use the edge modes, the mirror-clamp ones need MirrorClampToEdge.
DkWrapMode WrapModeOf(xenos::ClampMode m) {
  switch (m) {
    case xenos::ClampMode::kRepeat: return DkWrapMode_Repeat;
    case xenos::ClampMode::kMirroredRepeat: return DkWrapMode_MirroredRepeat;
    case xenos::ClampMode::kClampToEdge: return DkWrapMode_ClampToEdge;
    case xenos::ClampMode::kMirrorClampToEdge: return DkWrapMode_MirrorClampToEdge;
    case xenos::ClampMode::kClampToHalfway: return DkWrapMode_ClampToEdge;
    case xenos::ClampMode::kMirrorClampToHalfway: return DkWrapMode_MirrorClampToEdge;
    case xenos::ClampMode::kClampToBorder: return DkWrapMode_ClampToBorder;
    default: return DkWrapMode_MirrorClampToEdge;
  }
}

// Sampler slot of a fetch: filters as the Vulkan backend resolves them (GetSamplerParameters: the fetch
// instruction's filter unless it says "use the fetch constant"), the clamp modes for the texture's
// dimension, the border colour only where a clamp mode uses it.
uint32_t SamplerIndex(const xenos::xe_gpu_texture_fetch_t& fetch, const ParsedTextureFetchInstruction& instr) {
  xenos::TextureFilter mag = instr.attributes.mag_filter == xenos::TextureFilter::kUseFetchConst ? fetch.mag_filter : instr.attributes.mag_filter;
  xenos::TextureFilter min = instr.attributes.min_filter == xenos::TextureFilter::kUseFetchConst ? fetch.min_filter : instr.attributes.min_filter;
  xenos::ClampMode cx, cy, cz;
  texture_util::GetClampModesForDimension(fetch, cx, cy, cz);
  bool border_white = (xenos::ClampModeUsesBorder(cx) || xenos::ClampModeUsesBorder(cy)) && fetch.border_color == xenos::BorderColor::k_ABGR_White;
  return (min == xenos::TextureFilter::kLinear ? 1u : 0u) | (mag == xenos::TextureFilter::kLinear ? 2u : 0u) | (border_white ? 4u : 0u) |
         (uint32_t(cx) << 3) | (uint32_t(cy) << 6);
}

DkImageSwizzle SwizzleOf(uint32_t s) {
  switch (s & 7) {
    case 0: return DkImageSwizzle_Red;
    case 1: return DkImageSwizzle_Green;
    case 2: return DkImageSwizzle_Blue;
    case 3: return DkImageSwizzle_Alpha;
    case 4: return DkImageSwizzle_Zero;
    default: return DkImageSwizzle_One;
  }
}

// ----- on-screen version / fps strip (from gh-recomp) ---------------------------------------------
// Every build shows its version on screen (and the fps). There is no ImGui under deko3d: a 5x7 bitmap font
// rendered on the CPU into a small RGBA image per frame in flight, drawn with the present shaders into a
// viewport at the bottom-left corner. The file overlay_off next to the shader dir hides it.
// The CPU-work meter (hcu src/frame_meter.cpp, Switch and Linux): this thread's blocked time per swap, and the strip text.
extern "C" void rex_nx_meter_cp_swap(uint64_t block_us_total);
extern "C" void rex_nx_meter_present();
extern "C" void rex_nx_meter_gpu_frame(double ms);
extern "C" char rex_nx_meter_text[48];
#ifdef __SWITCH__
extern "C" const char* rex_nx_version(void);
#else
// The Linux test host has no libnx: nanosecond ticks and no version string.
#include <chrono>
static inline uint64_t armGetSystemTick() { return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()); }
static inline uint64_t armGetSystemTickFreq() { return 1000000000ull; }
static inline const char* rex_nx_version() { return "linux"; }
#endif

constexpr uint32_t kOverlayW = 768, kOverlayH = 20, kOverlayScale = 2;

struct Glyph {
  char ch;
  uint8_t rows[7];  // 5 bits per row, bit 4 = leftmost pixel
};
constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 2, 12}},
    {'a', {0, 0, 14, 1, 15, 17, 15}},
    {'b', {16, 16, 22, 25, 17, 17, 30}},
    {'c', {0, 0, 14, 16, 16, 17, 14}},
    {'d', {1, 1, 13, 19, 17, 17, 15}},
    {'e', {0, 0, 14, 17, 31, 16, 14}},
    {'f', {6, 9, 8, 28, 8, 8, 8}},
    {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'v', {0, 0, 17, 17, 17, 10, 4}},
    {'p', {0, 0, 30, 17, 30, 16, 16}},
    {'s', {0, 0, 15, 16, 14, 1, 30}},
    {'g', {0, 15, 17, 17, 15, 1, 14}},
    {'i', {4, 0, 12, 4, 4, 4, 14}},
    {'k', {16, 16, 18, 20, 24, 20, 18}},
    {'l', {12, 4, 4, 4, 4, 4, 14}},
    {'m', {0, 0, 26, 21, 21, 21, 21}},
    {'n', {0, 0, 22, 25, 17, 17, 17}},
    {'o', {0, 0, 14, 17, 17, 17, 14}},
    {'r', {0, 0, 22, 25, 16, 16, 16}},
    {'t', {8, 8, 30, 8, 8, 9, 6}},
    {'u', {0, 0, 17, 17, 17, 19, 13}},
    {'x', {0, 0, 17, 10, 4, 10, 17}},
    {'y', {0, 0, 17, 17, 15, 1, 14}},
    {'>', {8, 4, 2, 1, 2, 4, 8}},
    {':', {0, 12, 12, 0, 12, 12, 0}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 17, 25, 21, 19, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 17, 10, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'/', {0, 1, 2, 4, 8, 16, 0}},
    {'(', {2, 4, 8, 8, 8, 4, 2}},
    {')', {8, 4, 2, 2, 2, 4, 8}},
    {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'%', {24, 25, 2, 4, 8, 19, 3}},
};

// Draws `text` in white into px (w pixels wide, RGBA8 little-endian = 0xAABBGGRR) from (x, y).
void DrawText(uint32_t* px, uint32_t w, uint32_t x, uint32_t y, const char* text) {
  uint32_t pen = x;
  for (const char* p = text; *p && pen + 6 * kOverlayScale <= w; p++) {
    const Glyph* g = nullptr;
    for (const Glyph& cand : kGlyphs) if (cand.ch == *p) { g = &cand; break; }
    if (g) {
      for (uint32_t row = 0; row < 7; row++)
        for (uint32_t col = 0; col < 5; col++)
          if (g->rows[row] & (16u >> col))
            for (uint32_t sy = 0; sy < kOverlayScale; sy++)
              for (uint32_t sx = 0; sx < kOverlayScale; sx++)
                px[(y + row * kOverlayScale + sy) * w + pen + col * kOverlayScale + sx] = 0xFFFFFFFFu;
    }
    pen += 6 * kOverlayScale;
  }
}

// Fills px (kOverlayW x kOverlayH) with `text` in white on black.
void RenderOverlayText(uint32_t* px, const char* text) {
  for (uint32_t i = 0; i < kOverlayW * kOverlayH; i++) px[i] = 0xFF000000u;
  DrawText(px, kOverlayW, 4, 3, text);
}

// The settings menu (src/settings_menu.cpp): kMenuW x kMenuH, one line per 18 rows.
constexpr uint32_t kMenuLines = 16, kMenuW = 768, kMenuH = kMenuLines * 18 + 12;

class Deko3dCommandProcessor : public CommandProcessor {
 public:
  using CommandProcessor::CommandProcessor;

 protected:
  bool SetupContext() override {
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.cbDebug = DebugCallback;
    device_ = dkDeviceCreate(&dm);
    if (!device_) { Log("dkDeviceCreate failed"); return false; }
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, device_);
    qm.flags = DkQueueFlags_Graphics;
    queue_ = dkQueueCreate(&qm);
    if (!queue_) { Log("dkQueueCreate failed"); return false; }
    if (!code_.Create(device_, kCodePoolBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code) ||
        !images_.Create(device_, kImagePoolBytes, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image) ||
        !descriptors_.Create(device_, DK_MEMBLOCK_ALIGNMENT * 4 * kFrames + DK_MEMBLOCK_ALIGNMENT * 2 + kSamplerCount * sizeof(DkSamplerDescriptor), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached) ||
        !cmdmem_.Create(device_, kFrameCmdBytes * kFrames, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) {
      Log("memory block creation failed");
      return false;
    }
    for (uint32_t i = 0; i < kFrames; i++) {
      Frame& f = frames_[i];
      if (!f.data.Create(device_, kFrameDataBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached) ||
          !f.staging.Create(device_, kStagingBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)) {
        Log("frame %u memory failed", i);
        return false;
      }
      DkCmdBufMaker cm;
      dkCmdBufMakerDefaults(&cm, device_);
      f.cmdbuf = dkCmdBufCreate(&cm);
      f.cmd_offset = i * kFrameCmdBytes;
      f.desc_offset = i * kMaxImageDescriptors * sizeof(DkImageDescriptor);
    }
    sampler_desc_offset_ = kFrames * kMaxImageDescriptors * sizeof(DkImageDescriptor);
    // Samplers, indexed by SamplerIndex: the min and mag filters (the fetch instruction may override the
    // fetch constant's), the border colour (black / white) and every Xenos clamp mode per axis.
    // The settings (settings.txt and the A/B marker files in the debug dir, src/settings_menu.cpp). The sampler ones apply
    // here, at start: the fine horizontal hatching over the whole picture (console only) has the period of the game's 480x272
    // post-effect buffers stretched to 1280x720 (TEXTURE FILTER, file sfix).
    const std::string sdir = REXCVAR_GET(deko3d_shader_dir);  // shader_dir_ is set below; the debug dir is its parent
    const size_t scut = sdir.size() >= 2 ? sdir.rfind('/', sdir.size() - 2) : std::string::npos;
    hcu_settings_load(scut == std::string::npos ? "" : sdir.substr(0, scut + 1).c_str());
    g_dxt_cpu = hcu_settings.dxt_cpu != 0;
    g_depth_fmt = hcu_settings.depth32 ? DkImageFormat_ZF32_X24S8 : DkImageFormat_Z24S8;
    Log("depth images: %s (DEPTH 32 BIT %s); DXT ON CPU %s", hcu_settings.depth32 ? "ZF32_X24S8" : "Z24S8", hcu_settings.depth32 ? "on" : "off",
        g_dxt_cpu ? "on" : "off");
    const int force_filter = hcu_settings.filter;  // 1 linear, 2 nearest
    const float lod_bias = hcu_settings.lod_bias, aniso = hcu_settings.aniso;
    Log("sfix: filters %s, lod bias %g, anisotropy %g", force_filter == 1 ? "all linear" : force_filter == 2 ? "all nearest" : "as the game sets them",
        lod_bias, aniso);
    for (uint32_t i = 0; i < kSamplerCount; i++) {
      DkSampler s;
      dkSamplerDefaults(&s);
      s.minFilter = (force_filter ? force_filter == 1 : (i & 1)) ? DkFilter_Linear : DkFilter_Nearest;
      s.magFilter = (force_filter ? force_filter == 1 : (i & 2) != 0) ? DkFilter_Linear : DkFilter_Nearest;
      s.mipFilter = DkMipFilter_None;
      s.lodBias = lod_bias;
      s.maxAnisotropy = aniso;
      s.wrapMode[0] = WrapModeOf(xenos::ClampMode((i >> 3) & 7));
      s.wrapMode[1] = WrapModeOf(xenos::ClampMode((i >> 6) & 7));
      s.wrapMode[2] = DkWrapMode_ClampToEdge;
      float border = (i & 4) ? 1.0f : 0.0f;
      for (int c = 0; c < 4; c++) s.borderColor[c].value_f = border;
      DkSamplerDescriptor sd;
      dkSamplerDescriptorInitialize(&sd, &s);
      std::memcpy(descriptors_.cpu + sampler_desc_offset_ + i * sizeof(DkSamplerDescriptor), &sd, sizeof(sd));
    }
    // The swapchain on the default window.
    for (uint32_t i = 0; i < kFrames; i++) {
      HostImage* img = CreateImage(kPresentWidth, kPresentHeight, DkImageFormat_RGBA8_Unorm,
                                   DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression, false);
      if (!img) { Log("swapchain image %u failed", i); return false; }
      swap_images_[i] = img;
      swap_image_ptrs_[i] = &img->image;
    }
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, device_, nwindowGetDefault(), swap_image_ptrs_, kFrames);
    swapchain_ = dkSwapchainCreate(&sm);
    if (!swapchain_) { Log("dkSwapchainCreate failed"); return false; }
    shader_dir_ = REXCVAR_GET(deko3d_shader_dir);
    if (shader_dir_.empty() || shader_dir_.back() != '/') shader_dir_ += '/';
    size_t cut = shader_dir_.size() >= 2 ? shader_dir_.rfind('/', shader_dir_.size() - 2) : std::string::npos;
    std::string debug_dir = cut == std::string::npos ? "" : shader_dir_.substr(0, cut + 1);
    debug_dir_ = debug_dir;
    ReadSwapInterval();
    present_target_ = access((debug_dir + "dk_present_rt").c_str(), F_OK) == 0;
    dump_images_ = access((debug_dir + "dk_dump").c_str(), F_OK) == 0;  // readback dumps stall the GPU: debug only
    always_upload_ = access((debug_dir + "dk_reupload").c_str(), F_OK) == 0;  // debug A/B: textures upload at every bind (missed dirty pages = stale sprites)
    // The depth fixes (PRECISE SHADERS at start; the biases live, ReadSwapInterval). File zfix: "noprecise" = the vertex
    // shaders without precise outputs (shader_*.vert.np.dksh), "sb=<units>,<slope>" = the shadow-pass depth bias ("sb=0,0" =
    // off), "cb=<units>" = the colour-pass depth bias ("cb=0" = off).
    no_precise_ = !hcu_settings.precise;
    Log("zfix: vertex shaders %s; shadow-pass depth bias %g units, slope %g; colour-pass depth bias %g units",
        no_precise_ ? "without precise (.np)" : "precise", hcu_settings.shadow_bias, hcu_settings.shadow_slope, hcu_settings.colour_bias);
    no_depth_copy_ = access((debug_dir + "dk_nodepthcopy").c_str(), F_OK) == 0;  // debug A/B: depth resolves are ignored (shadow maps stay empty)
    present_vs_ = LoadDksh(shader_dir_ + "present.vert.dksh", present_vs_shader_);
    present_fs_ = LoadDksh(shader_dir_ + "present.frag.dksh", present_fs_shader_);
    Log("deko3d backend up: shaders from %s (%s present shaders), %u MB images, %u MB data per frame",
        shader_dir_.c_str(), present_vs_ && present_fs_ ? "with" : "WITHOUT", kImagePoolBytes >> 20, kFrameDataBytes >> 20);
    BeginFrame();
    return true;
  }

  void ShutdownContext() override {
    if (queue_) dkQueueWaitIdle(queue_);
    Log("deko3d backend down: %u swaps, %u draws, %u skipped, %u without shaders, %u clears, %u resolves, %u images (%u MB), %u uploads (%u KB, %u re-uploads), %llu dirty pages",
        swaps_, draws_, skipped_, missing_draws_, clears_, resolves_, image_count_, images_.peak >> 20, uploads_,
        uint32_t(upload_bytes_ >> 10), reuploads_, (unsigned long long)dirty_pages_);
  }

  Shader* LoadShader(xenos::ShaderType shader_type, uint32_t guest_address, const uint32_t* host_address,
                     uint32_t dword_count) override {
    uint64_t hash = hcu::UcodeHash(host_address, dword_count);
    auto it = shaders_.find(hash);
    if (it != shaders_.end()) return it->second;
    DkShaderObj* shader = new DkShaderObj(shader_type, hash, host_address, dword_count);
    string::StringBuffer disasm;
    shader->AnalyzeUcode(disasm);
    shaders_.emplace(hash, shader);
    char name[512];
    std::snprintf(name, sizeof(name), "%sshader_%016llX.%s.dksh", shader_dir_.c_str(), (unsigned long long)hash,
                  shader_type == xenos::ShaderType::kVertex ? "vert" : "frag");
    if (no_precise_ && shader_type == xenos::ShaderType::kVertex) {
      char np[512];
      std::snprintf(np, sizeof(np), "%sshader_%016llX.vert.np.dksh", shader_dir_.c_str(), (unsigned long long)hash);
      shader->loaded = LoadDksh(np, shader->shader);
    }
    if (!shader->loaded) shader->loaded = LoadDksh(name, shader->shader);
    if (shader->loaded && shader_type == xenos::ShaderType::kVertex) {
      std::snprintf(name, sizeof(name), "%sshader_%016llX.geom.dksh", shader_dir_.c_str(), (unsigned long long)hash);
      shader->geom_loaded = LoadDksh(name, shader->geom);
      if (!shader->geom_loaded) LogOnce("geom", "no rectangle-list geometry shader for VS %016llX (%s)", (unsigned long long)hash, name);
    }
    if (!shader->loaded) {
      missing_shaders_++;
      Log("no DKSH for %s %016llX (%u dwords at %08X): dumping the ucode to %smissing/",
          shader_type == xenos::ShaderType::kVertex ? "VS" : "PS", (unsigned long long)hash, dword_count, guest_address,
          shader_dir_.c_str());
      DumpMissing(*shader);
    } else {
      REXLOG_INFO("[dk] shader {} {:016X} loaded ({} dwords)", shader_type == xenos::ShaderType::kVertex ? "VS" : "PS",
                  hash, dword_count);
    }
    return shader;
  }

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count, IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override {
    ScopedUs timer(us_draw_);
    return IssueDrawImpl(prim_type, index_count, index_buffer_info, major_mode_explicit);
  }

  bool IssueDrawImpl(xenos::PrimitiveType prim_type, uint32_t index_count, IndexBufferInfo* index_buffer_info,
                     bool major_mode_explicit) {
    if (Skipping()) return true;
    SyncDirtyPages();
    const RegisterFile& regs = *register_file_;
    auto mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
    if (mode == xenos::EdramMode::kCopy) return Resolve();
    auto* vs = static_cast<DkShaderObj*>(active_vertex_shader());
    auto* ps = static_cast<DkShaderObj*>(active_pixel_shader());
    if (!vs || !ps) { skipped_++; frame_skipped_++; return true; }
    auto mask = regs.Get<reg::RB_COLOR_MASK>();
    auto dc = draw_util::GetNormalizedDepthControl(regs);
    bool writes_color = mask.write_red0 || mask.write_green0 || mask.write_blue0 || mask.write_alpha0;
    bool writes_depth = dc.z_enable && dc.z_write_enable;
    if (!writes_color && !writes_depth && !dc.stencil_enable) { skipped_++; frame_skipped_++; return true; }
    if (IsClearDraw(vs, ps, prim_type)) return ClearDraw(index_count, mode);
    if (!vs->loaded || !ps->loaded) {
#ifdef HCU_DK_GL
      if (TraceFrame()) Log("T NO SHADER draw vs %016llX (%d) ps %016llX (%d)", (unsigned long long)vs->ucode_data_hash(), vs->loaded, (unsigned long long)ps->ucode_data_hash(), ps->loaded);
#endif
      missing_draws_++;
      return true;
    }
    DkPrimitive prim;
    bool rects;
    if (!PrimitiveOf(prim_type, prim, rects)) { LogOnce("prim", "unsupported primitive type %u", uint32_t(prim_type)); skipped_++; return true; }
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;
    {
      // Each shader pair's first draw, once: the state a new kind of draw comes with.
      auto blend = regs.Get<reg::RB_BLENDCONTROL>();
      if (First(vs->ucode_data_hash() * 0x9E3779B97F4A7C15ull ^ ps->ucode_data_hash())) {
        char key[48];
        std::snprintf(key, sizeof(key), "pair %016llX %016llX", (unsigned long long)vs->ucode_data_hash(), (unsigned long long)ps->ucode_data_hash());
        LogOnce(key, "draw VS %016llX PS %016llX: prim %u n=%u%s, blend %u/%u op %u, mask %x, z %u/%u func %u, %zu vfetch %zu tfetch",
                (unsigned long long)vs->ucode_data_hash(), (unsigned long long)ps->ucode_data_hash(), uint32_t(prim_type), index_count,
                index_buffer_info ? " indexed" : "", uint32_t(blend.color_srcblend), uint32_t(blend.color_destblend),
                uint32_t(blend.color_comb_fcn), regs.values[XE_GPU_REG_RB_COLOR_MASK] & 0xF, dc.z_enable, dc.z_write_enable,
                uint32_t(dc.zfunc), vs->vertex_bindings().size(), ps->texture_bindings().size());
        // Its vertex layout and the first vertices (byte-swapped words as hex and as floats).
        for (const auto& vb : vs->vertex_bindings()) {
          auto fetch = regs.GetVertexFetch(vb.fetch_constant);
          std::string attrs;
          for (const auto& a : vb.attributes) {
            char s[64];
            std::snprintf(s, sizeof(s), " [fmt %u off %u%s%s]", uint32_t(a.fetch_instr.attributes.data_format), a.fetch_instr.attributes.offset,
                          a.fetch_instr.attributes.is_signed ? " signed" : "", a.fetch_instr.attributes.is_integer ? " integer" : "");
            attrs += s;
          }
          const uint32_t* w = reinterpret_cast<const uint32_t*>(memory_->TranslatePhysical(fetch.address << 2));
          uint32_t n = std::min<uint32_t>(vb.stride_words * 3, 24);
          std::string words;
          for (uint32_t i = 0; i < n && i < fetch.size; i++) {
            uint32_t v = fetch.endian == xenos::Endian::k8in32 ? __builtin_bswap32(w[i]) : w[i];
            float fl;
            std::memcpy(&fl, &v, 4);
            char s[48];
            std::snprintf(s, sizeof(s), " %08X(%g)", v, fl);
            words += s;
          }
          Log("  vf%u stride %u words, %u words at %08X endian %u:%s; data%s", vb.fetch_constant, vb.stride_words, fetch.size, fetch.address << 2,
              uint32_t(fetch.endian), attrs.c_str(), words.c_str());
        }
      }
    }

    // Textures of the pixel shader (before the render targets: a new image is cleared on creation, which
    // rebinds the targets).
    std::string bound_textures;  // for the draw-type log below: built only when it is printed
    bool textures_ok = true;
    // A fetch constant can be read by several tfetch instructions (HCU's shadow filter reads one nine times): one
    // binding per constant slot, the last instruction's sampler (what the loop did before: the last bind won).
    const auto& tbs = ps->texture_bindings();
    uint32_t last_of[32];
    uint64_t set_key = vs->ucode_data_hash() * 0x9E3779B97F4A7C15ull ^ ps->ucode_data_hash() * 0xC2B2AE3D27D4EB4Full;
    for (size_t i = 0; i < tbs.size(); i++) {
      if (tbs[i].fetch_constant < 32) last_of[tbs[i].fetch_constant] = uint32_t(i);
      set_key = (set_key ^ (uint64_t(regs.GetTextureFetch(tbs[i].fetch_constant).base_address) << 5 | tbs[i].fetch_constant)) * 0x100000001B3ull;
    }
    const bool first_set = First(set_key);
    bool want_text = first_set;
#ifdef HCU_DK_GL
    want_text = want_text || TraceFrame();
#endif
    for (size_t ti = 0; ti < tbs.size(); ti++) {
      const auto& tb = tbs[ti];
      if (tb.fetch_constant < 32 && last_of[tb.fetch_constant] != ti) continue;
      auto fetch = regs.GetTextureFetch(tb.fetch_constant);
      TextureInfo info;
      // 1D textures are 2D textures of height 1 on the Xenos (the shaders sample them so).
      if (!TextureInfo::Prepare(fetch, &info) ||
          (info.dimension != xenos::DataDimension::k2DOrStacked && info.dimension != xenos::DataDimension::k1D)) {
        LogOnce("tex", "texture fetch %u not preparable or not 1D/2D (dim %u)", tb.fetch_constant, uint32_t(fetch.dimension));
        textures_ok = false;
        continue;
      }
      HostImage* tex = TextureImage(f, info, fetch);
      if (!tex) { textures_ok = false; continue; }  // e.g. the staging ring is full this frame: draw next frame
      uint32_t swizzle = tex->from_resolve ? 0 : fetch.swizzle;
      if (swizzle && tex->reversed_channels) {
        uint32_t r = 0;
        for (uint32_t i = 0; i < 4; i++) {
          uint32_t c = (swizzle >> (3 * i)) & 7;
          r |= (c < 4 ? 3 - c : c) << (3 * i);
        }
        swizzle = r;
      }
      uint32_t slot = DescriptorFor(f, tex, swizzle);
      if (slot == UINT32_MAX) { textures_ok = false; continue; }
      uint32_t sampler = SamplerIndex(fetch, tb.fetch_instr);
      if (First(0xA5A5000000000000ull ^ info.memory.base_address)) {
        // Each texture's first bind, once: what the game draws with (correlates with the uploads).
        char key[32];
        std::snprintf(key, sizeof(key), "tex %08X", info.memory.base_address);
        LogOnce(key, "texture at %08X: %s %ux%u %s endian %u swizzle %03X exp_adjust %d sign %u%u%u%u filters min %u mag %u (instr %u/%u) clamp %u/%u (%u KB)%s",
                info.memory.base_address, info.format_info()->name, info.width + 1, info.height + 1, info.is_tiled ? "tiled" : "linear",
                uint32_t(fetch.endianness), fetch.swizzle, int(fetch.exp_adjust), uint32_t(fetch.sign_x), uint32_t(fetch.sign_y), uint32_t(fetch.sign_z), uint32_t(fetch.sign_w), uint32_t(fetch.min_filter), uint32_t(fetch.mag_filter),
                uint32_t(tb.fetch_instr.attributes.min_filter), uint32_t(tb.fetch_instr.attributes.mag_filter), uint32_t(fetch.clamp_x),
                uint32_t(fetch.clamp_y), info.memory.base_size >> 10, tex->from_resolve ? " (a resolve image)" : "");
      }
      DkResHandle handle = dkMakeTextureHandle(slot, sampler);
      dkCmdBufBindTextures(cmd, DkStage_Fragment, tb.fetch_constant, &handle, 1);
      char t[96];
#ifdef HCU_DK_GL
      if (TraceFrame() && std::getenv("HCU_DK_TRACE_DUMP")) {
        DumpDecoded(info, fetch.endianness, std::getenv("HCU_DK_TRACE_DUMP"), trace_draws_);
        // What the host image holds at this point of the command stream (R8 shows as red).
        char hn[300];
        std::snprintf(hn, sizeof(hn), "%s/h%u-%03u-%08X.ppm", std::getenv("HCU_DK_TRACE_DUMP"), swaps_ + 1, trace_draws_, info.memory.base_address);
        dkglCmdDumpImage(cmd, &tex->image, hn);
      }
      if (TraceFrame() && info.format == xenos::TextureFormat::k_8)
        Log("T   k_8 tex %08X: %ux%u pitch %u blockpitch %ux%u tiled %d mips %u..%u packed %d mip_addr %08X sizes %u/%u dim %u", info.memory.base_address, info.width + 1,
            info.height + 1, info.pitch, info.extent.block_pitch_h, info.extent.block_pitch_v, info.is_tiled, info.mip_min_level, info.mip_max_level,
            info.has_packed_mips, info.memory.mip_address, info.memory.base_size, info.memory.mip_size, uint32_t(info.dimension));
      if (TraceFrame() && info.width + 1 <= 256 && info.height == 0) {
        // A palette-sized texture: its memory hash and the number of non-zero words (palette changes between draws).
        const uint32_t* pw = reinterpret_cast<const uint32_t*>(memory_->TranslatePhysical(info.memory.base_address));
        uint32_t nz = 0, first = 0;
        for (uint32_t i = 0; i <= info.width; i++) if (pw[i]) { if (!nz) first = pw[i]; nz++; }
        std::snprintf(t, sizeof(t), " tf%u=%08X(%ux%u h%04llX nz%u f%08X)", tb.fetch_constant, info.memory.base_address, tex->width, tex->height,
                      (unsigned long long)(tex->mem_hash & 0xFFFF), nz, first);
        char pal[200];
        int n = std::snprintf(pal, sizeof(pal), "T   palette %08X endian %u swz %03X:", info.memory.base_address, uint32_t(fetch.endianness), fetch.swizzle);
        for (uint32_t i = 0; i < 12 && n < 180; i++) n += std::snprintf(pal + n, sizeof(pal) - n, " %08X", __builtin_bswap32(pw[i]));
        Log("%s", pal);
        bound_textures += t;
      } else
#endif
      if (want_text) {
        std::snprintf(t, sizeof(t), " tf%u=%08X(%ux%u)", tb.fetch_constant, info.memory.base_address, tex->width, tex->height);
        bound_textures += t;
      }
    }
    if (first_set) {
      // Each shader pair's first draw with each texture set, once (the texture bases name the layer).
      char key[160];
      std::snprintf(key, sizeof(key), "pairtex %016llX %016llX%s", (unsigned long long)vs->ucode_data_hash(),
                    (unsigned long long)ps->ucode_data_hash(), bound_textures.c_str());
      LogOnce(key, "draw VS %016llX PS %016llX prim %u n=%u:%s", (unsigned long long)vs->ucode_data_hash(),
              (unsigned long long)ps->ucode_data_hash(), uint32_t(prim_type), index_count, bound_textures.empty() ? " no textures" : bound_textures.c_str());
    }
    // A draw with a missing texture would sample whatever the slot held before (noise): skip it.
    if (!textures_ok) {
#ifdef HCU_DK_GL
      if (TraceFrame()) Log("T SKIPPED draw vs %016llX ps %016llX n=%u: a texture could not be bound |%s", (unsigned long long)vs->ucode_data_hash(),
                            (unsigned long long)ps->ucode_data_hash(), index_count, bound_textures.c_str());
#endif
      skipped_++;
      return true;
    }

    // Render targets.
    auto surf = regs.Get<reg::RB_SURFACE_INFO>();
    auto color_info = regs.Get<reg::RB_COLOR_INFO>();
    auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    uint32_t pitch = EffectivePitch(surf);
    HostImage* color = (mode != xenos::EdramMode::kDepthOnly && writes_color)
                           ? TargetImage(color_info.color_base, pitch, ColorTargetFormatOf(color_info.color_format), false)
                           : nullptr;
    HostImage* depth = (dc.z_enable || dc.stencil_enable) ? TargetImage(depth_info.depth_base, pitch, g_depth_fmt, true) : nullptr;
    if (depth) {
      const bool d24f = depth_info.depth_format == xenos::DepthRenderTargetFormat::kD24FS8;
      char key[24];
      std::snprintf(key, sizeof(key), "depthfmt %u", d24f ? 1u : 0u);
      LogOnce(key, "guest depth target format %s (RB_DEPTH_INFO) on base %u: host %s", d24f ? "D24FS8 (20e4 float)" : "D24S8 (unorm)",
              uint32_t(depth_info.depth_base), g_depth_fmt == DkImageFormat_Z24S8 ? "Z24S8" : "ZF32_X24S8");
    }
    if (!color && !depth) { skipped_++; frame_skipped_++; return true; }
    // Settings menu SHADOW MAP OFF (A/B of the console rings): the shadow pass (no colour target, depth written) is not
    // drawn, so the map keeps its clear and every receiver is lit. Rings that stay then do not come from the shadow test.
    if (!color && dc.z_write_enable && !hcu_settings.shadow_map) { skipped_++; frame_skipped_++; return true; }
    BindTargets(cmd, color, depth);
    uint32_t target_w = color ? color->width : depth->width;
    uint32_t target_h = color ? color->height : depth->height;

    // Viewport and scissor (the window offset included, as the SDK computes them).
    // deko3d (upper-left origin) maps NDC y = +1 to the bottom row like Vulkan; the Xenos clip space is
    // y-up like Direct3D, so the SDK's "origin_bottom_left" negation of the NDC y scale/offset applies
    // (the first Eden picture was upside down without it).
    draw_util::ViewportInfo vi;
    draw_util::GetHostViewportInfo(regs, 1, 1, true, target_w, target_h, false, dc, false, false, false, vi);
    DkViewport vp{float(vi.xy_offset[0]), float(vi.xy_offset[1]), float(vi.xy_extent[0]), float(vi.xy_extent[1]), vi.z_min, vi.z_max};
    dkCmdBufSetViewports(cmd, 0, &vp, 1);
    draw_util::Scissor sc;
    draw_util::GetScissor(regs, sc, true);
    DkScissor scissor{sc.offset[0], sc.offset[1], sc.extent[0], sc.extent[1]};
    if (scissor.x + scissor.width > target_w) scissor.width = scissor.x < target_w ? target_w - scissor.x : 0;
    if (scissor.y + scissor.height > target_h) scissor.height = scissor.y < target_h ? target_h - scissor.y : 0;
    dkCmdBufSetScissors(cmd, 0, &scissor, 1);

    // Fixed-function state.
    auto su = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
    // Rectangle lists: the geometry shader makes the 4th vertex, or the hardware mode (cvar).
    const bool use_gs = rects && vs->geom_loaded && !REXCVAR_GET(deko3d_fill_rectangle);
    DkRasterizerState rs;
    bool z_biased = false, shadow_biased = false;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;  // TODO(cull): cull_front/cull_back once the winding is verified on a picture
    rs.fillRectangleEnable = rects && !use_gs;
    if (su.poly_offset_front_enable || su.poly_offset_back_enable || su.poly_offset_para_enable) {
      // Not applied yet: logged to see whether HCU uses it (the shadow pass would).
      char key[48];
      std::snprintf(key, sizeof(key), "polyoff %u%u%u", uint32_t(su.poly_offset_front_enable), uint32_t(su.poly_offset_back_enable),
                    uint32_t(su.poly_offset_para_enable));
      LogOnce(key, "guest polygon offset: front %u back %u para %u, scale %f offset %f (VS %016llX)", uint32_t(su.poly_offset_front_enable),
              uint32_t(su.poly_offset_back_enable), uint32_t(su.poly_offset_para_enable), regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_SCALE),
              regs.Get<float>(XE_GPU_REG_PA_SU_POLY_OFFSET_FRONT_OFFSET), (unsigned long long)vs->ucode_data_hash());
    }
    {
      // With the clipper disabled the guest keeps geometry that lies in front of the near plane or behind the far one.
      auto clip = regs.Get<reg::PA_CL_CLIP_CNTL>();
      rs.depthClampEnable = clip.clip_disable;
      // The colour pass of a mesh repeats its depth pre-pass with another vertex shader (depth func LESS or LEQUAL): the two
      // positions differ in the last bits on a real GPU, and the whole mesh fails the test. The colour pass is pulled closer.
      z_biased = z_bias_ != 0.f && writes_color && dc.z_enable && (uint32_t(dc.zfunc) == 1 || uint32_t(dc.zfunc) == 3);
      // The shadow pass (no colour target, depth written; HCU's stage shadow map): its depth is pushed away from the light.
      // The receivers (PS EFC5D978279C500E) compare it with their own light-space depth; on the console the two differ in the
      // last bits and the ground, rocks and sky show rings and stripes of self-shadowing (Eden and the Linux GL host do not).
      shadow_biased = !color && depth && dc.z_write_enable && (shadow_bias_ != 0.f || shadow_slope_ != 0.f);
      rs.depthBiasEnableMask = (z_biased || shadow_biased) ? DkPolygonFlag_Fill : 0;  // 0.188-0.232 set 1 = DkPolygonFlag_Point: triangles got no bias
      char key[48];
      std::snprintf(key, sizeof(key), "clip %u %u", uint32_t(clip.clip_disable), uint32_t(clip.dx_clip_space_def));
      LogOnce(key, "clip control: clip_disable %u dx_clip_space_def %u (first draw VS %016llX)", uint32_t(clip.clip_disable),
              uint32_t(clip.dx_clip_space_def), (unsigned long long)vs->ucode_data_hash());
    }
    dkCmdBufBindRasterizerState(cmd, &rs);
    if (z_biased) {
      dkCmdBufSetDepthBias(cmd, z_bias_, 0.f, 0.f);
      char key[32];
      std::snprintf(key, sizeof(key), "colourbias %g", z_bias_);
      LogOnce(key, "colour-pass depth bias %g units: first draw (VS %016llX, swap #%u)", z_bias_, (unsigned long long)vs->ucode_data_hash(), swaps_);
    }
    if (shadow_biased) {
      dkCmdBufSetDepthBias(cmd, shadow_bias_, 0.f, shadow_slope_);
      frame_shadow_biased_++;
      char key[48];
      std::snprintf(key, sizeof(key), "shadowbias %u %g %g", depth_info.depth_base, shadow_bias_, shadow_slope_);
      LogOnce(key, "shadow-pass depth bias %g units, slope %g on depth base %u (VS %016llX)", shadow_bias_, shadow_slope_, depth_info.depth_base,
              (unsigned long long)vs->ucode_data_hash());
    }
    auto blend = regs.Get<reg::RB_BLENDCONTROL>();
    bool blend_enabled = !(blend.color_srcblend == xenos::BlendFactor::kOne && blend.color_destblend == xenos::BlendFactor::kZero &&
                           blend.color_comb_fcn == xenos::BlendOp::kAdd && blend.alpha_srcblend == xenos::BlendFactor::kOne &&
                           blend.alpha_destblend == xenos::BlendFactor::kZero && blend.alpha_comb_fcn == xenos::BlendOp::kAdd);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    cs.blendEnableMask = blend_enabled ? 1 : 0;
    dkCmdBufBindColorState(cmd, &cs);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    cw.masks = (mask.write_red0 ? DkColorMask_R : 0) | (mask.write_green0 ? DkColorMask_G : 0) |
               (mask.write_blue0 ? DkColorMask_B : 0) | (mask.write_alpha0 ? DkColorMask_A : 0);
    dkCmdBufBindColorWriteState(cmd, &cw);
    DkBlendState bs{BlendOp(blend.color_comb_fcn), BlendFactor(blend.color_srcblend), BlendFactor(blend.color_destblend),
                    BlendOp(blend.alpha_comb_fcn), BlendFactor(blend.alpha_srcblend), BlendFactor(blend.alpha_destblend)};
    dkCmdBufBindBlendStates(cmd, 0, &bs, 1);
    {
      // The constant factors (Xenos kConstantColor/Alpha and their inverses) read RB_BLEND_RED..ALPHA.
      auto is_const = [](xenos::BlendFactor f) { return uint32_t(f) >= 12 && uint32_t(f) <= 15; };
      if (blend_enabled && (is_const(blend.color_srcblend) || is_const(blend.color_destblend) || is_const(blend.alpha_srcblend) || is_const(blend.alpha_destblend))) {
        float bc[4];
        for (int i = 0; i < 4; i++) std::memcpy(&bc[i], &regs.values[XE_GPU_REG_RB_BLEND_RED + i], 4);
        dkCmdBufSetBlendConst(cmd, bc[0], bc[1], bc[2], bc[3]);
        char key[96];
        std::snprintf(key, sizeof(key), "blendconst %u %u %g %g", uint32_t(blend.color_srcblend), uint32_t(blend.color_destblend), bc[0], bc[3]);
        LogOnce(key, "constant blend factors %u/%u (alpha %u/%u): constant %g %g %g %g", uint32_t(blend.color_srcblend), uint32_t(blend.color_destblend),
                uint32_t(blend.alpha_srcblend), uint32_t(blend.alpha_destblend), bc[0], bc[1], bc[2], bc[3]);
      }
    }
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = depth && dc.z_enable;
    ds.depthWriteEnable = depth && dc.z_write_enable;
    ds.depthCompareOp = CompareOp(dc.zfunc);
    // Stencil (HCU's UI clips its panels with it): needs the depth image, which holds the stencil bits.
    ds.stencilTestEnable = depth && dc.stencil_enable;
    if (ds.stencilTestEnable) {
      ds.stencilFrontFailOp = StencilOpOf(dc.stencilfail);
      ds.stencilFrontPassOp = StencilOpOf(dc.stencilzpass);
      ds.stencilFrontDepthFailOp = StencilOpOf(dc.stencilzfail);
      ds.stencilFrontCompareOp = CompareOp(dc.stencilfunc);
      ds.stencilBackFailOp = StencilOpOf(dc.stencilfail_bf);
      ds.stencilBackPassOp = StencilOpOf(dc.stencilzpass_bf);
      ds.stencilBackDepthFailOp = StencilOpOf(dc.stencilzfail_bf);
      ds.stencilBackCompareOp = CompareOp(dc.stencilfunc_bf);
      auto fr = regs.Get<reg::RB_STENCILREFMASK>();
      reg::RB_STENCILREFMASK bk;
      bk.value = regs.values[XE_GPU_REG_RB_STENCILREFMASK_BF];
      dkCmdBufSetStencil(cmd, DkFace_Front, uint8_t(fr.stencilwritemask), uint8_t(fr.stencilref), uint8_t(fr.stencilmask));
      dkCmdBufSetStencil(cmd, DkFace_Back, uint8_t(bk.stencilwritemask), uint8_t(bk.stencilref), uint8_t(bk.stencilmask));
      char key[96];
      std::snprintf(key, sizeof(key), "stencil %u %u %u %u %u ref %u", uint32_t(dc.stencilfunc), uint32_t(dc.stencilfail), uint32_t(dc.stencilzpass),
                    uint32_t(dc.stencilzfail), uint32_t(dc.backface_enable), uint32_t(fr.stencilref));
      LogOnce(key, "stencil on: func %u fail %u zpass %u zfail %u (back %u: func %u fail %u zpass %u zfail %u), ref %u mask %02X write %02X", uint32_t(dc.stencilfunc),
              uint32_t(dc.stencilfail), uint32_t(dc.stencilzpass), uint32_t(dc.stencilzfail), uint32_t(dc.backface_enable), uint32_t(dc.stencilfunc_bf),
              uint32_t(dc.stencilfail_bf), uint32_t(dc.stencilzpass_bf), uint32_t(dc.stencilzfail_bf), uint32_t(fr.stencilref), uint32_t(fr.stencilmask),
              uint32_t(fr.stencilwritemask));
    }
    dkCmdBufBindDepthStencilState(cmd, &ds);

    // Shaders (the whole graphics mask: a stage without a shader here is unbound).
    DkShader const* shaders[3] = {&vs->shader, &ps->shader, &vs->geom};
    dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, use_gs ? 3 : 2);

    // Vertex layout and data: one buffer per fetch constant the vertex shader uses.
    DkVtxAttribState attribs[DK_MAX_VERTEX_ATTRIBS];
    DkVtxBufferState buffers[DK_MAX_VERTEX_BUFFERS];
    uint32_t attrib_count = 0, buffer_count = 0;
    uint32_t vertex_count = index_count;
    uint32_t index_offset = UINT32_MAX;
    if (index_buffer_info) {
      if (!CopyIndices(f, *index_buffer_info, index_offset, vertex_count)) { skipped_++; frame_skipped_++; return true; }
    }
    const uint32_t base_vertex = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
    for (const auto& vb : vs->vertex_bindings()) {
      if (buffer_count >= DK_MAX_VERTEX_BUFFERS) break;
      auto fetch = regs.GetVertexFetch(vb.fetch_constant);
      uint32_t stride = vb.stride_words * 4;
      uint32_t bytes = vertex_count * stride;
      // The first vertex: VGT_INDX_OFFSET is added to every vertex index (auto-indexed and indexed draws alike).
      // HCU draws all of a frame's sprite quads from one vertex buffer, each with its own offset.
      uint32_t src_off = base_vertex * stride;
      uint32_t avail = fetch.size * 4;
      avail = avail > src_off ? avail - src_off : 0;
      if (bytes > avail) {
        LogOnce("vbclamp", "vertex data clamped: %u vertices x %u bytes = %u, fetch constant %u holds %u bytes after offset %u (VS %016llX)", vertex_count,
                stride, bytes, vb.fetch_constant, avail, base_vertex, (unsigned long long)vs->ucode_data_hash());
        bytes = avail;
      }
      uint32_t off = f.data.Alloc(bytes ? bytes : 16, 64);
      if (off == UINT32_MAX) { LogOnce("ring", "frame data ring full"); skipped_++; return true; }
      const uint8_t* src = memory_->TranslatePhysical(fetch.address << 2) + src_off;
      texture_conversion::CopySwapBlock(fetch.endian, f.data.cpu + off, src, bytes);
      frame_vtx_bytes_ += bytes;
#ifdef HCU_DK_GL
      if (TraceFrame() && vertex_count <= 6) {
        // The vertices of small draws (sprites) as floats, after the byte swap.
        std::string v;
        const float* fl = reinterpret_cast<const float*>(f.data.cpu + off);
        for (uint32_t i = 0; i < std::min(bytes / 4, 30u); i++) { char b[24]; std::snprintf(b, sizeof(b), " %g", fl[i]); v += b; }
        Log("T   vf%u n=%u stride %u addr %08X+%u:%s", vb.fetch_constant, vertex_count, stride, fetch.address << 2, src_off, v.c_str());
        trace_vf_.emplace_back((fetch.address << 2) + src_off, std::min(bytes, 24u));
      }
#endif
      dkCmdBufBindVtxBuffer(cmd, buffer_count, f.data.gpu + off, bytes);
      buffers[buffer_count] = DkVtxBufferState{stride, 0};
      for (const auto& a : vb.attributes) {
        if (attrib_count >= DK_MAX_VERTEX_ATTRIBS) break;
        DkVtxAttribSize size;
        DkVtxAttribType type;
        if (!VertexAttribOf(a.fetch_instr.attributes, size, type)) {
          LogOnce("vfmt", "unsupported vertex format %u", uint32_t(a.fetch_instr.attributes.data_format));
          size = DkVtxAttribSize_4x32;
          type = DkVtxAttribType_Float;
        }
        DkVtxAttribState st{};
        st.bufferId = buffer_count;
        st.isFixed = 0;
        st.offset = uint32_t(a.fetch_instr.attributes.offset) * 4;
        st.size = size;
        st.type = type;
        st.isBgra = 0;
        attribs[attrib_count++] = st;
      }
      buffer_count++;
    }
    dkCmdBufBindVtxAttribState(cmd, attribs, attrib_count);
    dkCmdBufBindVtxBufferState(cmd, buffers, buffer_count);

    // Uniforms: the ALU constants and the system constants, per stage.
    if (!BindUniforms(f, vs, ps, vi, regs)) { skipped_++; frame_skipped_++; return true; }

#ifdef HCU_DK_GL
    if (TraceFrame()) {
      // Linux test host: HCU_DK_TRACE_AT=N logs every draw, clear and resolve of swap N.
      auto bl = regs.Get<reg::RB_BLENDCONTROL>();
      Log("T draw %u vs %016llX ps %016llX prim %u n=%u%s | color %s base %u fmt %u | depth %s base %u | pitch %u msaa %u | mask %x | blend %u/%u/%u a %u/%u/%u | z %u/%u f %u stencil %u | vp %g,%g %gx%g | sc %u,%u %ux%u |%s",
          trace_draws_++, (unsigned long long)vs->ucode_data_hash(), (unsigned long long)ps->ucode_data_hash(), uint32_t(prim_type), index_count,
          index_buffer_info ? "i" : "", color ? "yes" : "no", color_info.color_base, uint32_t(color_info.color_format), depth ? "yes" : "no",
          depth_info.depth_base, pitch, uint32_t(surf.msaa_samples), uint32_t(mask.value & 0xF), uint32_t(bl.color_srcblend), uint32_t(bl.color_destblend), uint32_t(bl.color_comb_fcn),
          uint32_t(bl.alpha_srcblend), uint32_t(bl.alpha_destblend), uint32_t(bl.alpha_comb_fcn), dc.z_enable, dc.z_write_enable, uint32_t(dc.zfunc),
          dc.stencil_enable, vp.x, vp.y, vp.width, vp.height, scissor.x, scissor.y, scissor.width, scissor.height, bound_textures.c_str());
    }
#endif
    if (index_buffer_info) {
      dkCmdBufBindIdxBuffer(cmd, index_buffer_info->format == xenos::IndexFormat::kInt32 ? DkIdxFormat_Uint32 : DkIdxFormat_Uint16,
                            f.data.gpu + index_offset);
      dkCmdBufDrawIndexed(cmd, prim, index_count, 1, 0, 0, 0);
    } else {
      dkCmdBufDraw(cmd, prim, index_count, 1, 0, 0);
    }
#ifdef HCU_DK_GL
    if (TraceFrame() && std::getenv("HCU_DK_TRACE_DUMP") && (color ? color : depth)->width) {
      // One picture per draw (HCU_DK_TRACE_DUMP=<dir>): the colour target after the draw, named by swap and draw number.
      char name[256];
      std::snprintf(name, sizeof(name), "%s/d%u-%03u.ppm", std::getenv("HCU_DK_TRACE_DUMP"), swaps_ + 1, trace_draws_ - 1);
      if (color) dkglCmdDumpImage(cmd, &color->image, name);
    }
#endif
    draws_++;
    frame_draws_++;
    step_draws_++;
    return true;
  }

  bool IssueCopy() override {
    ScopedUs timer(us_copy_);
    if (Skipping()) return true;
    SyncDirtyPages();
    return Resolve();
  }

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width, uint32_t frontbuffer_height) override {
    if (Skipping()) {
      swaps_++;  // nothing was recorded: the frame is just counted
      view_image_ = nullptr;
      step_n_ = step_draws_ = 0;
      if (swaps_ % 60 == 0) {
        if (!skip_odd_frames_) std::printf("[nx] gpu: swap #%u (skipped)\n", swaps_);
        ReadSwapInterval();
      }
      LogSkips();
      NextFrame(false);
      return;
    }
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;
    HostImage* front = nullptr;
    if (guest_images_.count(frontbuffer_ptr) && frontbuffer_width && frontbuffer_height) {
      // The resolve created it with the aligned height: the first swap trims it to the front buffer's size.
      front = GuestImage(f, frontbuffer_ptr, frontbuffer_width, frontbuffer_height, guest_images_[frontbuffer_ptr]->format, false);
    }
    if (view_image_) front = view_image_;  // settings menu VIEW (A/B of the console hatching)
    view_image_ = nullptr;
    if (step_log_) {
      Log("frame passes end: %u resolves, %u draws after the last one (VIEW STEP shows resolve %d)", step_n_, step_draws_,
          hcu_settings.view_step + 1);
      step_log_ = false;
    }
    if (hcu_settings.view == 3 && (hcu_settings.view_step != step_logged_ || !step_logged_once_)) {
      step_log_ = true;  // the next drawn frame lists its passes
      step_logged_ = hcu_settings.view_step;
      step_logged_once_ = true;
    }
    step_n_ = step_draws_ = 0;
    if (present_target_) {
      // Debug (the file sdmc:/switch/hcu/dk_present_rt): show the widest colour render target itself,
      // bypassing the resolve copies and the front buffer image.
      HostImage* widest = nullptr;
      for (auto& it : targets_) if (!it.second->is_depth && (!widest || it.second->width > widest->width)) widest = it.second;
      if (widest) front = widest;
    }
    // Debug readback at two swaps: what the images hold (uniform colour = a broken stage).
#ifdef __SWITCH__
    RecordFrame(f, cmd, front);
#endif
    std::vector<std::pair<std::string, uint32_t>> dumps;
    const bool menu_dump = hcu_settings.dump != 0;  // settings menu DUMP NEXT FRAME: every image as a raw file
    if (menu_dump || (dump_images_ && (swaps_ + 1 == 120 || swaps_ + 1 == 600 || swaps_ + 1 == 2400))) {
      std::vector<HostImage*> dump_imgs;
      auto queue_dump = [&](const char* what, HostImage* img) {
        if (!img) return;
        if (menu_dump) {
          for (HostImage* d : dump_imgs) if (d == img) return;
          dump_imgs.push_back(img);
        }
        uint32_t bytes = img->width * img->height * DumpBytesPerPixel(img->format);
        uint32_t off = f.staging.Alloc(bytes, 0x1000);
        if (off == UINT32_MAX) return;
        DkImageView v;
        dkImageViewDefaults(&v, &img->image);
        DkImageRect r{0, 0, 0, img->width, img->height, 1};
        DkCopyBuf cb{f.staging.gpu + off, 0, 0};
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
        dkCmdBufCopyImageToBuffer(cmd, &v, &r, &cb, 0);
        char name[96];
        std::snprintf(name, sizeof(name), "%s fmt%d %ux%u", what, int(img->format), img->width, img->height);
        dumps.emplace_back(name, off);
      };
      queue_dump("front", front);
      for (auto& it : targets_) if (menu_dump || !it.second->is_depth) { char n[48]; std::snprintf(n, sizeof(n), "target%s base %u pitch %u", it.second->is_depth ? "-depth" : "", it.second->edram_base, it.second->edram_pitch); queue_dump(n, it.second); }
      for (auto& it : guest_images_) if ((menu_dump ? it.second->width >= 64 : it.second->width >= 600) && it.second != front) { char n[48]; std::snprintf(n, sizeof(n), "guest %08X%s", it.first, it.second->from_resolve ? " (resolve)" : " (upload)"); queue_dump(n, it.second); }
      dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
    }
#ifdef HCU_DK_GL
    if (TraceFrame()) {
      for (auto& v : trace_vf_) {
        const uint32_t* w = reinterpret_cast<const uint32_t*>(memory_->TranslatePhysical(v.first));
        float fl[6];
        for (int i = 0; i < 6; i++) { uint32_t b = __builtin_bswap32(w[i]); std::memcpy(&fl[i], &b, 4); }
        Log("T   at the swap, %08X now holds: %g %g %g %g %g %g", v.first, fl[0], fl[1], fl[2], fl[3], fl[4], fl[5]);
      }
      trace_vf_.clear();
    }
#endif
    // The on-screen strip (gh-recomp): the version and the fps, uploaded into this frame's image before the present pass.
    HostImage* overlay = nullptr;
    if (overlay_on_) {
      const uint64_t now = armGetSystemTick();
      if (!fps_tick_) fps_tick_ = now;
      fps_frames_++;
      if (now - fps_tick_ >= armGetSystemTickFreq()) {
        fps_x10_ = uint32_t(uint64_t(fps_frames_) * 10 * armGetSystemTickFreq() / (now - fps_tick_));
        fps_tick_ = now;
        fps_frames_ = 0;
      }
      if (!overlay_[frame_]) overlay_[frame_] = CreateImage(kOverlayW, kOverlayH, DkImageFormat_RGBA8_Unorm, DkImageFlags_UsageRender, false);
      overlay = overlay_[frame_];
      if (overlay) {
        char text[96];
        std::snprintf(text, sizeof(text), "HCU %s  %u.%u fps  %s", rex_nx_version(), fps_x10_ / 10, fps_x10_ % 10, rex_nx_meter_text);
        if (std::strcmp(text, overlay_text_) != 0) {
          RenderOverlayText(overlay_px_.data(), text);
          std::memcpy(overlay_text_, text, sizeof(overlay_text_));
        }
        const uint32_t bytes = kOverlayW * kOverlayH * 4;
        const uint32_t off = f.staging.Alloc(bytes, 0x100);
        if (off != UINT32_MAX) {
          std::memcpy(f.staging.cpu + off, overlay_px_.data(), bytes);
          DkCopyBuf cb{f.staging.gpu + off, 0, 0};
          DkImageView ov;
          dkImageViewDefaults(&ov, &overlay->image);
          DkImageRect rect{0, 0, 0, kOverlayW, kOverlayH, 1};
          dkCmdBufCopyBufferToImage(cmd, &cb, &ov, &rect, 0);
          dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
        } else {
          overlay = nullptr;
        }
      }
    }
    // The settings menu, while it is open: drawn over the picture in the middle of the screen.
    HostImage* menu = nullptr;
    {
      char lines[kMenuLines][48];
      const int n = hcu_menu_lines(lines, kMenuLines);
      if (n && !menu_[frame_]) menu_[frame_] = CreateImage(kMenuW, kMenuH, DkImageFormat_RGBA8_Unorm, DkImageFlags_UsageRender, false);
      if (n && menu_[frame_]) {
        for (uint32_t& p : menu_px_) p = 0xFF202020u;
        for (int i = 0; i < n; i++) DrawText(menu_px_.data(), kMenuW, 12, 8 + uint32_t(i) * 18, lines[i]);
        const uint32_t bytes = kMenuW * kMenuH * 4;
        const uint32_t off = f.staging.Alloc(bytes, 0x100);
        if (off != UINT32_MAX) {
          std::memcpy(f.staging.cpu + off, menu_px_.data(), bytes);
          DkCopyBuf cb{f.staging.gpu + off, 0, 0};
          DkImageView mv;
          dkImageViewDefaults(&mv, &menu_[frame_]->image);
          DkImageRect rect{0, 0, 0, kMenuW, kMenuH, 1};
          dkCmdBufCopyBufferToImage(cmd, &cb, &mv, &rect, 0);
          dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
          menu = menu_[frame_];
        }
      }
    }
    if (swaps_ < 5) Log("swap #%u: acquiring a swapchain image", swaps_ + 1);
#ifdef __SWITCH__
    if (dkQueueIsInErrorState(queue_)) {
      Log("GPU queue in its error state at swap #%u (a list before it faulted); colour bias %g, shadow bias %g/%g, shadow map %d, "
          "menu %s, %u draws this frame",
          swaps_ + 1, z_bias_, shadow_bias_, shadow_slope_, hcu_settings.shadow_map, menu ? "open" : "closed", frame_draws_);
      LogGpuChannelErrors();
      rex_nx_quick_exit("the GPU queue is in its error state (see the lines before)");
    }
#endif
    int slot;
    {
      ScopedUs timer(us_acquire_);
      ScopedUs meter(meter_block_us_);
      slot = dkQueueAcquireImage(queue_, swapchain_);
    }
    if (swaps_ < 5) Log("swap #%u: slot %d", swaps_ + 1, slot);
    HostImage* target = swap_images_[slot];
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    DkImageView tv;
    dkImageViewDefaults(&tv, &target->image);
    DkImageView const* targets[1] = {&tv};
    dkCmdBufBindRenderTargets(cmd, targets, 1, nullptr);
    DkViewport vp{0, 0, float(kPresentWidth), float(kPresentHeight), 0.0f, 1.0f};
    dkCmdBufSetViewports(cmd, 0, &vp, 1);
    DkScissor sc{0, 0, kPresentWidth, kPresentHeight};
    dkCmdBufSetScissors(cmd, 0, &sc, 1);
    const float navy[4] = {0.0f, 0.0f, 0.25f, 1.0f};  // dark blue: visible when the present draw does not run
    dkCmdBufClearColor(cmd, 0, DkColorMask_RGBA, navy);
    if (front && present_vs_ && present_fs_) {
      DkRasterizerState rs;
      dkRasterizerStateDefaults(&rs);
      rs.cullMode = DkFace_None;
      dkCmdBufBindRasterizerState(cmd, &rs);
      DkColorState cs;
      dkColorStateDefaults(&cs);
      dkCmdBufBindColorState(cmd, &cs);
      DkColorWriteState cw;
      dkColorWriteStateDefaults(&cw);
      dkCmdBufBindColorWriteState(cmd, &cw);
      DkDepthStencilState ds;
      dkDepthStencilStateDefaults(&ds);
      ds.depthTestEnable = 0;
      ds.depthWriteEnable = 0;
      dkCmdBufBindDepthStencilState(cmd, &ds);
      DkShader const* shaders[2] = {&present_vs_shader_, &present_fs_shader_};
      dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, shaders, 2);
      dkCmdBufBindVtxAttribState(cmd, nullptr, 0);
      dkCmdBufBindVtxBufferState(cmd, nullptr, 0);
      uint32_t dslot = DescriptorFor(f, front, 0);
      if (dslot != UINT32_MAX) {
        DkResHandle handle = dkMakeTextureHandle(dslot, 4);  // linear, clamp
        dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &handle, 1);
        // The display gamma ramp the game set (D3D SetGammaRamp): HCU's is not linear (128 -> 462 of 1023), the picture is darker
        // with it. 8888 front buffers use the 256-entry table; a 2_10_10_10 front buffer would use the piecewise linear one
        // (not done: identity).
        uint32_t ramp_off = f.data.Alloc(4096, DK_UNIFORM_BUF_ALIGNMENT);
        if (ramp_off != UINT32_MAX) {
          float* r = reinterpret_cast<float*>(f.data.cpu + ramp_off);
          const auto* ramp = gamma_ramp_256_entry_table();
          const bool identity = front->format != DkImageFormat_RGBA8_Unorm;
          for (uint32_t i = 0; i < 256; i++) {
            r[i * 4 + 0] = identity ? i / 255.0f : ramp[i].color_10_red / 1023.0f;
            r[i * 4 + 1] = identity ? i / 255.0f : ramp[i].color_10_green / 1023.0f;
            r[i * 4 + 2] = identity ? i / 255.0f : ramp[i].color_10_blue / 1023.0f;
            r[i * 4 + 3] = 0.0f;
          }
          DkBufExtents ramp_buf{f.data.gpu + ramp_off, 4096};
          dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, &ramp_buf, 1);
        }
        dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
      }
      if (overlay) {
        const uint32_t oslot = DescriptorFor(f, overlay, 0);
        if (oslot != UINT32_MAX) {
          const uint32_t ox = 8, oy = kPresentHeight - kOverlayH - 8;
          DkViewport ovp{float(ox), float(oy), float(kOverlayW), float(kOverlayH), 0.0f, 1.0f};
          dkCmdBufSetViewports(cmd, 0, &ovp, 1);
          DkScissor osc{ox, oy, kOverlayW, kOverlayH};
          dkCmdBufSetScissors(cmd, 0, &osc, 1);
          DkResHandle oh = dkMakeTextureHandle(oslot, 0);  // nearest, clamp
          dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &oh, 1);
          dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
        }
      }
      if (menu) {
        const uint32_t mslot = DescriptorFor(f, menu, 0);
        if (mslot != UINT32_MAX) {
          const uint32_t mx = (kPresentWidth - kMenuW) / 2, my = (kPresentHeight - kMenuH) / 2;
          DkViewport mvp{float(mx), float(my), float(kMenuW), float(kMenuH), 0.0f, 1.0f};
          dkCmdBufSetViewports(cmd, 0, &mvp, 1);
          DkScissor msc{mx, my, kMenuW, kMenuH};
          dkCmdBufSetScissors(cmd, 0, &msc, 1);
          DkResHandle mh = dkMakeTextureHandle(mslot, 0);
          dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &mh, 1);
          dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
        }
      }
    }
    dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
#ifdef __SWITCH__
    if (f.ts_off != UINT32_MAX) dkCmdBufReportCounter(cmd, DkCounter_Timestamp, f.data.gpu + f.ts_off + 16);
#endif
    DkCmdList list = dkCmdBufFinishList(cmd);
    {
      ScopedUs timer(us_submit_);
      dkQueueSubmitCommands(queue_, list);
    }
    dkQueueSignalFence(queue_, &f.fence, false);
    f.fenced = true;
    dkQueuePresentImage(queue_, swapchain_, slot);
    rex_nx_meter_present();
#ifdef HCU_DK_GL
    FlushTextureDumps();
    trace_draws_ = 0;
#endif
    if (!dumps.empty()) {
      dkQueueWaitIdle(queue_);
      if (menu_dump) {
        // Raw bytes as the GPU holds them (RGBA8 = R,G,B,A; Z24S8 = depth in the low 24 bits, stencil on top; BC = blocks),
        // one file each: dump/f<swap>_<what>_fmt<host format>_<w>x<h>.raw.
        const std::string dir = debug_dir_ + "dump/";
        mkdir(dir.c_str(), 0777);
        uint64_t total = 0;
        for (auto& d : dumps) {
          uint32_t w = 0, h = 0;
          std::sscanf(d.first.c_str() + d.first.rfind(' ') + 1, "%ux%u", &w, &h);
          int fmt = 0;
          const size_t fp = d.first.find(" fmt");
          if (fp != std::string::npos) fmt = std::atoi(d.first.c_str() + fp + 4);
          std::string n = d.first;
          for (char& c : n) if (c == ' ' || c == '(' || c == ')') c = '_';
          char path[300];
          std::snprintf(path, sizeof(path), "%sf%u_%s.raw", dir.c_str(), swaps_ + 1, n.c_str());
          const uint32_t bytes = w * h * DumpBytesPerPixel(DkImageFormat(fmt));
          if (FILE* fp2 = std::fopen(path, "wb")) {
            std::fwrite(f.staging.cpu + d.second, 1, bytes, fp2);
            std::fclose(fp2);
            total += bytes;
          }
        }
        Log("dump at swap #%u: %zu images, %llu KB written to %s (DUMP NEXT FRAME)", swaps_ + 1, dumps.size(), (unsigned long long)(total >> 10),
            dir.c_str());
        hcu_settings.dump = 0;
      }
      for (auto& d : dumps) {
        // The image's dims are in the name; recover them from the staging layout: sample a 16x16 grid.
        uint32_t w = 0, h = 0;
        std::sscanf(d.first.c_str() + d.first.rfind(' ') + 1, "%ux%u", &w, &h);
        const uint32_t* px = reinterpret_cast<const uint32_t*>(f.staging.cpu + d.second);
        std::unordered_set<uint32_t> distinct;
        for (uint32_t y = 0; y < 16 && h; y++)
          for (uint32_t x = 0; x < 16 && w; x++) distinct.insert(px[(y * h / 16) * w + x * w / 16]);
        Log("dump at swap #%u: %s: %zu distinct colours in a 16x16 grid; pixels %08X %08X %08X %08X %08X", swaps_ + 1,
            d.first.c_str(), distinct.size(), w && h ? px[0] : 0, w && h ? px[(h / 4) * w + w / 4] : 0,
            w && h ? px[(h / 2) * w + w / 2] : 0, w && h ? px[(3 * h / 4) * w + 3 * w / 4] : 0, w && h ? px[(h - 1) * w + w - 1] : 0);
      }
    }
    rex_nx_meter_cp_swap(meter_block_us_);
    swaps_++;
    if (swaps_ % 60 == 0) ReadSwapInterval();
    LogSkips();
    if (swaps_ <= 3 || swaps_ % 60 == 0) {
      std::printf("[nx] gpu: swap #%u\n", swaps_);  // the Eden gate's frame counter
      Log("swap #%u: front %08X %ux%u %s; frame: %u draws, %u clears, %u resolves, %u skipped, %u no-shader, %u re-uploads, %u shadow-biased, %llu dirty pages; data %u KB (vertices %u KB) staging %u KB; images %u (%u MB), %u image flips so far",
          swaps_, frontbuffer_ptr, frontbuffer_width, frontbuffer_height, front ? "shown" : "MISSING", frame_draws_,
          frame_clears_, frame_resolves_, frame_skipped_, frame_missing_, frame_reuploads_, frame_shadow_biased_, (unsigned long long)(dirty_pages_ - dirty_pages_at_swap_),
          f.data.used >> 10, frame_vtx_bytes_ >> 10, f.staging.used >> 10, image_count_, images_.used >> 20, image_flips_);
      // CPU time per frame (average over the swaps since the last line): recording the draws, the resolves, the wait for the
      // previous frame's GPU work (a long one = GPU-bound), the wait for a swapchain image (vsync) and the submit.
      uint32_t n = swaps_ <= 3 ? 1 : 60;
      Log("swap #%u: per frame %.2f ms draws, %.2f ms resolves, %.2f ms fence wait, %.2f ms acquire, %.2f ms submit", swaps_, us_draw_ / 1000.0 / n,
          us_copy_ / 1000.0 / n, us_fence_ / 1000.0 / n, us_acquire_ / 1000.0 / n, us_submit_ / 1000.0 / n);
      us_draw_ = us_copy_ = us_fence_ = us_acquire_ = us_submit_ = 0;
      if (swaps_ % 60 == 0 || swaps_ <= 3) {
        const auto* ramp = gamma_ramp_256_entry_table();
        Log("swap #%u: display gamma ramp (10 bit red, of 255 in/out): in 32 -> %u, 64 -> %u, 128 -> %u, 192 -> %u, 255 -> %u", swaps_, uint32_t(ramp[32].color_10_red),
            uint32_t(ramp[64].color_10_red), uint32_t(ramp[128].color_10_red), uint32_t(ramp[192].color_10_red), uint32_t(ramp[255].color_10_red));
      }
    }
    dirty_pages_at_swap_ = dirty_pages_;
    frame_draws_ = frame_clears_ = frame_resolves_ = frame_skipped_ = frame_missing_ = frame_reuploads_ = frame_vtx_bytes_ = frame_shadow_biased_ = 0;
    frame_ = (frame_ + 1) % kFrames;
    begun_ = false;
    drawn_frames_++;
    NextFrame(true);
  }

  // Every 1800 swaps in the skip modes: frames drawn, and frames skipped for a busy GPU.
  void LogSkips() {
    if (swaps_ % 1800 || !(skip_odd_frames_ || skip_busy_)) return;
    std::printf("[nx] gpu: %s: %u of the last 1800 frames drawn, %u skipped because the GPU was still busy\n",
                skip_odd_frames_ ? "30skip" : "60skip", drawn_frames_, busy_skips_);
    drawn_frames_ = busy_skips_ = 0;
  }

  // Whether the next frame is drawn. 60 and 30: every frame. 30skip: not the frame after a drawn one, and not one the GPU
  // has no free frame slot for yet (its fence is still pending): that frame is skipped too instead of waiting, so the
  // guest never waits for the GPU (the game keeps its speed, fewer frames are shown). 60skip: only the busy skip.
  void NextFrame(bool drawn) {
    skip_frame_ = false;
#ifdef __SWITCH__
    if (dkQueueIsInErrorState(queue_)) {
      Log("GPU queue in its error state at swap #%u (a list before it faulted); shadow map %d, view %d", swaps_, hcu_settings.shadow_map,
          hcu_settings.view);
      LogGpuChannelErrors();
      rex_nx_quick_exit("the GPU queue is in its error state (see the lines before)");
    }
    if (busy_in_a_row_ == 300) {  // 5 s of skipped frames: the GPU has not finished the oldest one
      Log("the GPU has not finished frame slot %u for 300 swaps (a GPU hang?); shadow map %d, view %d", frame_, hcu_settings.shadow_map,
          hcu_settings.view);
      LogGpuChannelErrors();
      std::fflush(stdout);
    }
#endif
    if (skip_odd_frames_ && drawn) {
      skip_frame_ = true;
    } else if ((skip_odd_frames_ || skip_busy_) && frames_[frame_].fenced &&
               dkFenceWait(&frames_[frame_].fence, 0) != DkResult_Success) {
      skip_frame_ = true;
      busy_skips_++;
      busy_in_a_row_++;
    }
    if (!skip_frame_) busy_in_a_row_ = 0;
    if (!skip_frame_ && !begun_) {
      BeginFrame();
      begun_ = true;
    }
  }

 private:
  // The frame-rate mode: file fps in the debug dir (sdmc:/switch/hcu/fps). No file or "30skip" (the default) = the guest
  // keeps 60 frames a second but only every second one is drawn (the command processor still runs every packet: fences,
  // write-backs, the guest's waits; see NextFrame); "60" = every frame drawn; "60skip" = every frame the GPU has a free
  // slot for; "30" = every frame drawn, presented every second vblank (the game is frame-locked: it runs at half speed).
  // Read at start and every 60 swaps.
  void ReadSwapInterval() {
    // The files record_now / dump_now in the debug dir do what the menu's RECORD 5 SEC / DUMP NEXT FRAME do (scripted
    // runs: Eden); each is removed when seen.
    if (!debug_dir_.empty()) {
      if (unlink((debug_dir_ + "record_now").c_str()) == 0) hcu_settings.record = 1;
      if (unlink((debug_dir_ + "dump_now").c_str()) == 0) hcu_settings.dump = 1;
    }
    // The settings menu's live values (src/settings_menu.cpp; the fps file sets the start value).
    overlay_on_ = hcu_settings.strip != 0;
    shadow_bias_ = hcu_settings.shadow_bias;
    shadow_slope_ = hcu_settings.shadow_slope;
    z_bias_ = hcu_settings.colour_bias;
    const int mode = hcu_settings.fps_mode;
    const uint32_t interval = mode == 3 ? 2 : 1;
    const bool skip = mode == 0, busy = mode == 1;
    if (interval == swap_interval_ && skip == skip_odd_frames_ && busy == skip_busy_ && swaps_) return;
    swap_interval_ = interval;
    skip_odd_frames_ = skip;
    skip_busy_ = busy;
#ifdef __SWITCH__
    dkSwapchainSetSwapInterval(swapchain_, interval);
#endif
    std::printf("[nx] gpu: swap interval %u, %s (%s) from swap #%u\n", interval, skip ? "every second frame drawn" : "every frame drawn",
                skip ? "30skip" : interval == 2 ? "30 fps cap" : busy ? "60skip" : "60", swaps_);
  }

  // Linux test host: HCU_DK_START_SWAP=N turns the backend into a null backend until swap N (the guest runs
  // at full speed to a late scene; textures and targets are created lazily when drawing starts).
  // The file <HCU_DK_DUMP_DIR>/skip_on does the same while it exists (checked every 8 swaps): fast forward
  // through loading screens, then delete it.
  bool Skipping() const {
    if (skip_frame_) return true;  // the 30skip mode (NextFrame)
#ifdef HCU_DK_GL
    static const uint32_t start = std::getenv("HCU_DK_START_SWAP") ? uint32_t(std::atoi(std::getenv("HCU_DK_START_SWAP"))) : 0;
    static const char* dir = std::getenv("HCU_DK_DUMP_DIR");
    static bool file_skip = false;
    if (dir && (swaps_ & 7) == 0 && !skip_checked_[0]) {
      file_skip = access((std::string(dir) + "/skip_on").c_str(), F_OK) == 0;
      skip_checked_[0] = true;
    } else if ((swaps_ & 7) != 0) {
      skip_checked_[0] = false;
    }
    return file_skip || swaps_ + 1 < start;
#else
    return false;
#endif
  }
  mutable bool skip_checked_[1] = {false};
#ifdef HCU_DK_GL
  bool TraceFrame() const {
    static const char* at = std::getenv("HCU_DK_TRACE_AT");
    if (!at) return false;
    // "7600,7700" or ranges "7600-7640" (comma separated).
    uint32_t cur = swaps_ + 1;
    std::string list = at;
    size_t pos = 0;
    while (pos < list.size()) {
      size_t end = list.find(',', pos);
      if (end == std::string::npos) end = list.size();
      std::string item = list.substr(pos, end - pos);
      size_t dash = item.find('-');
      uint32_t lo = uint32_t(std::atoi(item.c_str())), hi = dash == std::string::npos ? lo : uint32_t(std::atoi(item.c_str() + dash + 1));
      if (cur >= lo && cur <= hi) return true;
      pos = end + 1;
    }
    return false;
  }
  uint32_t trace_draws_ = 0;
  std::vector<std::pair<uint32_t, uint32_t>> trace_vf_;
#endif

  // ----- frames ---------------------------------------------------------------------------------
  void BeginFrame() {
    Frame& f = frames_[frame_];
    if (f.fenced) {
      if (swaps_ < 6) Log("frame %u: waiting for its fence", frame_);
      DkResult r;
      {
        ScopedUs timer(us_fence_);
        ScopedUs meter(meter_block_us_);
        r = dkFenceWait(&f.fence, 2000000000ll);  // 2 s: a hang is logged, not silent
      }
      if (r != DkResult_Success) Log("frame %u: fence wait result %d (GPU hang?)", frame_, int(r));
#ifdef __SWITCH__
      if (f.rec_pending) {
        const uint32_t bytes = kPresentWidth * kPresentHeight * 4;
        dkMemBlockFlushCpuCache(rec_.block, frame_ * bytes, bytes);
        hcu_rec_push(rec_.cpu + frame_ * bytes, f.rec_swap, f.rec_us);
        f.rec_pending = false;
      }
#endif
      if (swaps_ < 6) Log("frame %u: fence done", frame_);
      f.fenced = false;
#ifdef __SWITCH__
      // The GPU's own time for that frame (timestamps in GPU ticks, 625/384 ns each): the CPU-work meter's gpu line.
      if (f.ts_off != UINT32_MAX) {
        const uint64_t* t = reinterpret_cast<const uint64_t*>(f.data.cpu + f.ts_off);
        if (t[1] && t[3] > t[1]) rex_nx_meter_gpu_frame(double(t[3] - t[1]) * 625.0 / 384.0 / 1e6);
      }
#endif
    }
    f.ts_off = UINT32_MAX;
    f.data.Reset();
    f.staging.Reset();
    f.next_descriptor = 0;
    f.descriptors.clear();
    dkCmdBufClear(f.cmdbuf);
    dkCmdBufAddMemory(f.cmdbuf, cmdmem_.block, f.cmd_offset, kFrameCmdBytes);
    dkCmdBufBindImageDescriptorSet(f.cmdbuf, descriptors_.gpu + f.desc_offset, kMaxImageDescriptors);
    dkCmdBufBindSamplerDescriptorSet(f.cmdbuf, descriptors_.gpu + sampler_desc_offset_, kSamplerCount);
#ifdef __SWITCH__
    f.ts_off = f.data.Alloc(32, 16);
    if (f.ts_off != UINT32_MAX) {
      std::memset(f.data.cpu + f.ts_off, 0, 32);
      dkCmdBufReportCounter(f.cmdbuf, DkCounter_Timestamp, f.data.gpu + f.ts_off);
    }
#endif
    bound_color_ = nullptr;
    bound_depth_ = nullptr;
  }

  // ----- dirty pages ----------------------------------------------------------------------------
  // Consumes the dirty-page bytes before every draw and stamps each written page with the draw
  // serial; a texture whose pages were written after the upload or resolve that filled its image
  // is uploaded again at its next bind (TextureImage). The coarse map makes a clean frame cost 512
  // byte reads. ponytail: the guest marks a page after the store without a barrier, so a texture
  // written in the same microsecond as its bind can be uploaded stale once (the next write fixes it).
  void SyncDirtyPages() {
    serial_++;
    const uint64_t* words = reinterpret_cast<const uint64_t*>(const_cast<const uint8_t*>(rex_nx_dirty));
    for (uint32_t c = 0; c < 0x200; c++) {
      if (!rex_nx_dirty_coarse[c]) continue;
      rex_nx_dirty_coarse[c] = 0;
      for (uint32_t w = c * 32; w < c * 32 + 32; w++) {
        if (!words[w]) continue;
        for (uint32_t i = w * 8; i < w * 8 + 8; i++) {
          if (!rex_nx_dirty[i]) continue;
          rex_nx_dirty[i] = 0;
          page_serial_[i] = serial_;
          dirty_pages_++;
        }
      }
    }
  }

  bool WrittenSince(uint32_t physical, uint32_t bytes, uint32_t serial) const {
    uint32_t first = (physical & 0x1FFFFFFFu) >> 12;
    uint32_t last = ((physical & 0x1FFFFFFFu) + (bytes ? bytes - 1 : 0)) >> 12;
    for (uint32_t p = first; p <= last && p < page_serial_.size(); p++) {
      if (page_serial_[p] > serial) return true;
    }
    return false;
  }

  // Whether the guest memory behind an image changed since its contents were set. Switch: the dirty-page
  // serials. Linux: a hash of the range against the hash taken when the contents were set.
  uint64_t HashMemory(uint32_t physical, uint32_t bytes) const {
#ifdef HCU_DK_GL
    static const bool nohash = std::getenv("HCU_DK_NOHASH") != nullptr;  // timing experiment: textures never re-upload
    if (nohash) return 0;
    const uint8_t* p = memory_->TranslatePhysical(physical);
    uint64_t h = 1469598103934665603ull;
    uint32_t words = bytes / 8;
    const uint64_t* w = reinterpret_cast<const uint64_t*>(p);
    for (uint32_t i = 0; i < words; i++) {
      h ^= w[i];
      h *= 1099511628211ull;
      h ^= h >> 29;
    }
    return h;
#else
    (void)physical; (void)bytes;
    return 0;
#endif
  }
  bool MemoryChanged(const HostImage& img, uint32_t physical, uint32_t bytes) const {
#ifdef HCU_DK_GL
    (void)bytes;
    return HashMemory(physical, img.mem_bytes) != img.mem_hash;
#else
    return WrittenSince(physical, bytes, img.content_serial);
#endif
  }

  // ----- images ---------------------------------------------------------------------------------
  // gh: images replaced by a resize (or a format change) wait here for a request of the same shape: the
  // pool is a bump allocator and a per-frame resize ping-pong filled 448 MB in minutes (finding 0.22).
  std::vector<HostImage*> free_images_;
  void RecycleImage(HostImage* img) {
    if (img && !img->is_depth) free_images_.push_back(img);
  }

  HostImage* CreateImage(uint32_t width, uint32_t height, DkImageFormat format, uint32_t flags, bool is_depth) {
    for (size_t i = 0; i < free_images_.size(); i++) {
      HostImage* r = free_images_[i];
      if (r->width == width && r->height == height && r->format == format && r->flags == flags && r->is_depth == is_depth) {
        free_images_.erase(free_images_.begin() + i);
        r->from_resolve = false;
        r->content_serial = 0;
        r->checked_serial = 0;
        r->guest_base = 0;
        r->edram_base = r->edram_pitch = 0;
        return r;
      }
    }
    auto* img = new HostImage();
    img->flags = flags;
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, device_);
    lm.flags = flags;
    lm.format = format;
    lm.dimensions[0] = width;
    lm.dimensions[1] = height;
    lm.dimensions[2] = 1;
    dkImageLayoutInitialize(&img->layout, &lm);
    uint64_t size = dkImageLayoutGetSize(&img->layout);
    uint32_t align = dkImageLayoutGetAlignment(&img->layout);
    uint32_t off = images_.Alloc(uint32_t(size), align);
    if (off == UINT32_MAX) {
      Log("image pool full (%u MB used): %ux%u not created", images_.used >> 20, width, height);
      delete img;
      return nullptr;
    }
    dkImageInitialize(&img->image, &img->layout, images_.block, off);
    img->format = format;
    img->width = width;
    img->height = height;
    img->is_depth = is_depth;
    image_count_++;
    return img;
  }

  // Fresh pool memory holds whatever an earlier owner left (noise on screen where the game's clears do
  // not reach). Render targets and resolve destinations start cleared to zero instead; textures are
  // fully uploaded, and block-compressed images cannot be cleared.
  void ClearNewImage(HostImage* img) {
    if (!img || !frames_[frame_].cmdbuf) return;
    switch (img->format) {
      case DkImageFormat_RGBA_BC1: case DkImageFormat_RGBA_BC2: case DkImageFormat_RGBA_BC3: case DkImageFormat_RG_BC5_Unorm:
      case DkImageFormat_R_BC4_Unorm: return;
      default: break;
    }
    DkCmdBuf cmd = frames_[frame_].cmdbuf;
    DkScissor sc{0, 0, img->width, img->height};
    if (img->is_depth) {
      BindTargets(cmd, nullptr, img);
      dkCmdBufSetScissors(cmd, 0, &sc, 1);
      dkCmdBufClearDepthStencil(cmd, true, 1.0f, 0xFF, 0);
    } else {
      const float zero[4] = {0, 0, 0, 0};
      BindTargets(cmd, img, nullptr);
      dkCmdBufSetScissors(cmd, 0, &sc, 1);
      dkCmdBufClearColor(cmd, 0, DkColorMask_RGBA, zero);
    }
  }

  static uint32_t EffectivePitch(reg::RB_SURFACE_INFO surf) {
    // The XDK clears through a 4x MSAA alias at half the pitch (2x2 samples per pixel: the same
    // EDRAM bytes as the 1x surface, both axes halved).
    return surf.msaa_samples == xenos::MsaaSamples::k4X ? surf.surface_pitch * 2 : surf.surface_pitch;
  }

  HostImage* TargetImage(uint32_t edram_base, uint32_t pitch, DkImageFormat format, bool is_depth) {
    if (!pitch) return nullptr;
    uint64_t key = (uint64_t(edram_base) << 32) | (uint64_t(pitch) << 1) | (is_depth ? 1 : 0);
    auto it = targets_.find(key);
    if (it != targets_.end()) return it->second;
    uint32_t tiles = edram_base < kEdramTiles ? kEdramTiles - edram_base : 0;
    uint32_t rows = tiles ? uint32_t((uint64_t(tiles) * 80 * 16) / pitch) : 0;
    if (rows > kMaxTargetHeight) rows = kMaxTargetHeight;
    if (rows < 16) rows = 16;
    HostImage* img = CreateImage(pitch, rows, format, DkImageFlags_UsageRender, is_depth);
    if (!img) return nullptr;
    img->edram_base = edram_base;
    img->edram_pitch = pitch;
    targets_.emplace(key, img);
    ClearNewImage(img);
    Log("%s target: EDRAM base %u pitch %u -> %ux%u image (%u MB in images)", is_depth ? "depth" : "color", edram_base, pitch,
        img->width, img->height, images_.used >> 20);
    return img;
  }

  void BindTargets(DkCmdBuf cmd, HostImage* color, HostImage* depth) {
    if (color == bound_color_ && depth == bound_depth_) return;
    dkCmdBufBarrier(cmd, DkBarrier_Fragments, 0);
    DkImageView cv, dv;
    DkImageView const* colors[1] = {&cv};
    if (color) dkImageViewDefaults(&cv, &color->image);
    if (depth) dkImageViewDefaults(&dv, &depth->image);
    dkCmdBufBindRenderTargets(cmd, color ? colors : nullptr, color ? 1 : 0, depth ? &dv : nullptr);
    bound_color_ = color;
    bound_depth_ = depth;
  }

  // The image at a guest address (resolve destinations, textures). A fetch or a swap asks for the
  // exact size (a resolve created it with the aligned height: resized once, contents copied); a
  // resolve asks for a minimum size (`at_least`) and creates with the aligned size (`create_w/h`).
  // 32-bit colour formats that read the same through a sampler (an 8-bit and a 10-bit colour image).
  static bool IsColor32(DkImageFormat f) { return f == DkImageFormat_RGBA8_Unorm || f == DkImageFormat_RGB10A2_Unorm; }

  // `texture`: a texture fetch asking for the image. The image of a resolve is then used as it is when its colour
  // format reads the same (HCU's blur chain: a 2_10_10_10 target resolved, then fetched as 8_8_8_8) and when it is at
  // least as large (the resolve's aligned size, the fetch's exact one). Console 0.160 recreated or resized about
  // thirty such images every frame.
  HostImage* GuestImage(Frame& f, uint32_t base, uint32_t width, uint32_t height, DkImageFormat format, bool at_least,
                        uint32_t create_w = 0, uint32_t create_h = 0, bool texture = false) {
    auto it = guest_images_.find(base);
    if (it != guest_images_.end()) {
      HostImage* img = it->second;
      const bool compat = texture && img->from_resolve && IsColor32(img->format) && IsColor32(format);
      if (img->format != format && !compat) {
        // The memory was reused with another format (a texture over an old resolve destination or the
        // reverse): a new image. ponytail: the old image's pool memory is not reclaimed (bump pool);
        // a free list when the pool fills in a stage.
        HostImage* fresh = CreateImage(std::max(width, create_w), std::max(height, create_h), format, DkImageFlags_UsageRender, format == g_depth_fmt);
        if (!fresh) return nullptr;
        fresh->guest_base = base;
        if (create_w || create_h) ClearNewImage(fresh);
        if (image_flips_++ < 40) Log("image at %08X recreated: format %u -> %u, %ux%u -> %ux%u", base, uint32_t(img->format), uint32_t(format), img->width,
            img->height, fresh->width, fresh->height);
        it->second = fresh;
        RecycleImage(img);
        return fresh;
      }
      if (img->width == width && img->height == height) return img;
      if (at_least && width <= img->width && height <= img->height) return img;
      if (texture && img->from_resolve && width <= img->width && height <= img->height) return img;
      if (at_least) {
        width = std::max(width, img->width);
        height = std::max(height, img->height);
      }
      HostImage* resized = CreateImage(width, height, img->format, DkImageFlags_UsageRender, img->is_depth);
      if (!resized) return img;
      ClearNewImage(resized);
      uint32_t cw = std::min(width, img->width), ch = std::min(height, img->height);
      DkImageView sv, dv;
      dkImageViewDefaults(&sv, &img->image);
      dkImageViewDefaults(&dv, &resized->image);
      DkImageRect sr{0, 0, 0, cw, ch, 1}, dr{0, 0, 0, cw, ch, 1};
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      dkCmdBufCopyImage(f.cmdbuf, &sv, &sr, &dv, &dr, 0);
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      resized->from_resolve = img->from_resolve;
      resized->resolve_swap = img->resolve_swap;
      resized->content_serial = img->content_serial;
      resized->checked_serial = img->checked_serial;
      resized->guest_base = base;
      if (image_flips_++ < 40) Log("image at %08X resized %ux%u -> %ux%u", base, img->width, img->height, width, height);
      it->second = resized;
      RecycleImage(img);  // gh: its pool memory is reused by the next request of its size
      return resized;
    }
    HostImage* img = CreateImage(std::max(width, create_w), std::max(height, create_h), format,
                                 DkImageFlags_UsageRender, format == g_depth_fmt);
    if (!img) return nullptr;
    img->guest_base = base;
    guest_images_.emplace(base, img);
    if (create_w || create_h) ClearNewImage(img);
    return img;
  }

  // HCU's shadow maps: three cascades are resolved to three addresses (a tile row of 32 apart in the
  // memory of one 2048x2048 texture) and sampled as that one texture. Each resolve made its own depth
  // image at its own address; the texture takes the ones resolved after its own contents at their rows.
  void MergeDepthPieces(Frame& f, HostImage* img) {
    if (img->merge_epoch == depth_resolves_) return;  // no depth resolve since the last look (a draw binds this texture nine times)
    img->merge_epoch = depth_resolves_;
    const uint32_t stride = ((img->width + 31) & ~31u) * 32 * 4;  // bytes of one row of 32x32 tiles
    for (auto& it : guest_images_) {
      HostImage* p = it.second;
      if (p == img || p->format != g_depth_fmt || !p->from_resolve || it.first <= img->guest_base) continue;
      uint32_t delta = it.first - img->guest_base;
      if (delta % stride) continue;
      uint32_t y = delta / stride * 32;
      if (y >= img->height) continue;
      auto m = img->merged.find(p);
      if (p->content_serial <= img->content_serial) continue;                      // older than the texture's own contents
      if (m != img->merged.end() && m->second >= p->content_serial) continue;      // already copied in
      uint32_t w = std::min(p->width, img->width), h = std::min(p->height, img->height - y);
      DkImageView sv, dv;
      dkImageViewDefaults(&sv, &p->image);
      dkImageViewDefaults(&dv, &img->image);
      DkImageRect sr{0, 0, 0, w, h, 1}, dr{0, y, 0, w, h, 1};
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      dkCmdBufCopyImage(f.cmdbuf, &sv, &sr, &dv, &dr, 0);
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      img->merged[p] = p->content_serial;
      char key[64];
      std::snprintf(key, sizeof(key), "merge %08X %08X", img->guest_base, it.first);
      LogOnce(key, "depth image at %08X (%ux%u) copied into the texture at %08X at row %u", it.first, p->width, p->height, img->guest_base, y);
    }
  }

  HostImage* TextureImage(Frame& f, const TextureInfo& info, const xenos::xe_gpu_texture_fetch_t& fetch) {
    DkImageFormat format;
    bool reversed;
    if (!TextureFormatOf(info.format, format, reversed)) {
      LogOnce((std::string("tfmt ") + info.format_info()->name).c_str(), "unsupported texture format %s", info.format_info()->name);
      return nullptr;
    }
    uint32_t base = info.memory.base_address;
    // TextureInfo keeps the fetch constant's fields: the size minus one.
    HostImage* img = GuestImage(f, base, info.width + 1, info.height + 1, format, format == g_depth_fmt, 0, 0, true);
    if (!img) return nullptr;
    if (format == g_depth_fmt) {
      // Only a depth resolve fills a depth image. One never resolved here (a sample before the first
      // shadow pass) reads "nothing in the way": depth 1.
      if (!img->content_serial) {
        ClearNewImage(img);
        img->from_resolve = true;
        img->content_serial = serial_;
        LogOnce("deptex", "depth texture at %08X (%ux%u) sampled before any depth resolve: cleared to 1", base, img->width, img->height);
      }
      MergeDepthPieces(f, img);
      return img;
    }
    if (format == DkImageFormat_RGB10A2_Unorm && !img->from_resolve) {
      // Only a resolve of a 2_10_10_10 target fills such an image; memory uploads would need the Xenos bit order.
      LogOnce("rgb10a2", "texture at %08X: %s uploads are not supported (only resolves)", base, info.format_info()->name);
      return nullptr;
    }
    if (img->checked_serial == serial_) return img;
    img->reversed_channels = reversed;  // already checked for this draw
    img->checked_serial = serial_;
    // A resolve destination bound in the swap that resolved it holds what the GPU just drew. A guest write to its memory since
    // is not for this frame: the guest runs ahead of the command processor and prepares the next frame's memory (HCU's blur
    // chain read back as black or noise).
    if (img->from_resolve && img->resolve_swap == swaps_ + 1) return img;
    // Never filled, or written by the guest (or a file read) after the upload / resolve that filled it.
    if (img->content_serial && !MemoryChanged(*img, base, info.memory.base_size) && (!always_upload_ || img->from_resolve)) return img;
    if (img->format != format) {
      // An upload needs an image of the texture's own format: the resolve's (a compatible colour format, taken
      // as it was above) was only a stand-in for a fetch of its contents.
      img = GuestImage(f, base, info.width + 1, info.height + 1, format, false);
      if (!img) return nullptr;
      img->checked_serial = serial_;
    }
    // A failed re-upload (staging ring full) keeps the previous contents rather than unbinding the slot.
    if (!UploadTexture(f, *img, info, fetch.endianness)) return img->content_serial ? img : nullptr;
    if (img->content_serial) {
      reuploads_++;
      frame_reuploads_++;
      if (reuploads_ <= 8) Log("texture at %08X re-uploaded (%s, %ux%u): its memory changed", base,
                               img->from_resolve ? "was a resolve destination" : "was uploaded before", img->width, img->height);
    }
    img->from_resolve = false;
    img->content_serial = serial_;
    img->mem_bytes = info.memory.base_size;
    img->mem_hash = HashMemory(base, img->mem_bytes);
#ifdef HCU_DK_GL
    QueueTextureDump(img, info);
#endif
    return img;
  }

#ifdef HCU_DK_GL
  // Linux test host: every uploaded texture (first 400, and re-uploads of the first 60 images) is written
  // as a PPM after the frame's commands ran, named by guest address, size, format and upload count.
  void QueueTextureDump(HostImage* img, const TextureInfo& info) {
    static const char* dir = std::getenv("HCU_DK_DUMP_DIR");
    if (!dir || dump_count_ >= 400) return;
    uint32_t n = ++upload_counts_[img->guest_base];
    if (n > 3) return;
    char name[256];
    std::snprintf(name, sizeof(name), "%s/tex-%08X-%ux%u-%s-%s%u.ppm", dir, img->guest_base, img->width, img->height, info.format_info()->name,
                  info.is_tiled ? "t" : "l", n);
    pending_dumps_.emplace_back(img, name);
    dump_count_++;
  }
  void FlushTextureDumps() {
    for (auto& d : pending_dumps_) dkglDumpImage(&d.first->image, d.second.c_str());
    pending_dumps_.clear();
    // HCU_DK_DUMP_AT=2900,3000: at those swaps every render target and resolve/texture image is written
    // (after the swap's commands ran).
    // Or on demand: creating the file <dump dir>/dump_now dumps the next swap (the file is removed).
    static const char* at = std::getenv("HCU_DK_DUMP_AT");
    static const char* dir = std::getenv("HCU_DK_DUMP_DIR");
    if (!dir) return;
    std::string trigger = std::string(dir) + "/dump_now";
    bool now = (swaps_ & 7) == 0 && access(trigger.c_str(), F_OK) == 0;
    if (now) unlink(trigger.c_str());
    if (!now) {
      if (!at) return;
      std::string list = std::string(",") + at + ",";
      if (list.find("," + std::to_string(swaps_ + 1) + ",") == std::string::npos) return;
    }
    char name[256];
    for (auto& it : targets_) {
      if (it.second->is_depth) continue;
      std::snprintf(name, sizeof(name), "%s/at%u-target-base%u-pitch%u.ppm", dir, swaps_ + 1, it.second->edram_base, it.second->edram_pitch);
      dkglDumpImage(&it.second->image, name);
    }
    for (auto& it : guest_images_) {
      if (it.second->width < 64) continue;
      std::snprintf(name, sizeof(name), "%s/at%u-guest-%08X-%ux%u%s.ppm", dir, swaps_ + 1, it.first, it.second->width, it.second->height,
                    it.second->from_resolve ? "-resolve" : "");
      dkglDumpImage(&it.second->image, name);
    }
  }
  std::vector<std::pair<HostImage*, std::string>> pending_dumps_;
  std::unordered_map<uint32_t, uint32_t> upload_counts_;
  uint32_t dump_count_ = 0;
#endif

#ifdef HCU_DK_GL
  // Trace frames: the decoded texels of an uncompressed texture as <dir>/t<swap>-<draw>-<base>-<w>x<h>-<format>.raw (a CPU reference for the picture).
  void DumpDecoded(const TextureInfo& info, xenos::Endian endian, const char* dir, uint32_t draw) {
    const FormatInfo* fi = info.format_info();
    if (fi->block_width != 1 || fi->block_height != 1) return;
    uint32_t w = info.width + 1, h = info.height + 1, bpb = fi->bytes_per_block();
    std::vector<uint8_t> buf(size_t(w) * h * bpb);
    const uint8_t* src = memory_->TranslatePhysical(info.memory.base_address);
    if (info.is_tiled) {
      if (!::hcu::UntileFast(buf.data(), src, w, h, info.extent.block_pitch_h, bpb, static_cast<::hcu::SwapMode>(uint32_t(endian)))) return;
    } else {
      for (uint32_t y = 0; y < h; y++) texture_conversion::CopySwapBlock(endian, buf.data() + y * w * bpb, src + y * info.extent.block_pitch_h * bpb, w * bpb);
    }
    char name[300];
    std::snprintf(name, sizeof(name), "%s/t%u-%03u-%08X-%ux%u-%s.raw", dir, swaps_ + 1, draw, info.memory.base_address, w, h, fi->name);
    if (FILE* fp = std::fopen(name, "wb")) { std::fwrite(buf.data(), 1, buf.size(), fp); std::fclose(fp); }
  }
#endif

  bool UploadTexture(Frame& f, HostImage& img, const TextureInfo& info, xenos::Endian endian) {
    const FormatInfo* fi = info.format_info();
    const uint32_t width = info.width + 1, height = info.height + 1;  // TextureInfo keeps size minus one
    uint32_t bw = (width + fi->block_width - 1) / fi->block_width;
    uint32_t bh = (height + fi->block_height - 1) / fi->block_height;
    uint32_t bpb = fi->bytes_per_block();
    uint32_t bytes = bw * bh * bpb;
    const int dxt = img.format == DkImageFormat_RGBA8_Unorm && fi->block_width == 4
                        ? (info.format == xenos::TextureFormat::k_DXT1 ? 1 : info.format == xenos::TextureFormat::k_DXT2_3 ? 2
                           : info.format == xenos::TextureFormat::k_DXT4_5 ? 3 : 0)
                        : 0;
    if (dxt) {
      static std::vector<uint8_t> blocks;
      blocks.resize(bytes);
      const uint32_t rgba = width * height * 4;
      uint32_t off = f.staging.Alloc(rgba, 0x100);
      if (off == UINT32_MAX) { LogOnce("staging", "staging ring full: %u KB texture not uploaded", rgba >> 10); return false; }
      const uint8_t* src = memory_->TranslatePhysical(info.memory.base_address);
      if (info.is_tiled) {
        if (!::hcu::UntileFast(blocks.data(), src, bw, bh, info.extent.block_pitch_h, bpb, static_cast<::hcu::SwapMode>(uint32_t(endian)))) return false;
      } else {
        for (uint32_t y = 0; y < bh; y++)
          texture_conversion::CopySwapBlock(endian, blocks.data() + y * bw * bpb, src + y * info.extent.block_pitch_h * bpb, bw * bpb);
      }
      uint8_t* out = f.staging.cpu + off;
      for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++)
          DecodeDxtBlock(blocks.data() + (size_t(by) * bw + bx) * bpb, dxt, out + (size_t(by) * 4 * width + bx * 4) * 4, width * 4,
                         std::min(4u, width - bx * 4), std::min(4u, height - by * 4));
      DkCopyBuf dcb{f.staging.gpu + off, 0, 0};
      DkImageView dview;
      dkImageViewDefaults(&dview, &img.image);
      DkImageRect drect{0, 0, 0, width, height, 1};
      if (img.content_serial) dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, 0);
      dkCmdBufCopyBufferToImage(f.cmdbuf, &dcb, &dview, &drect, 0);
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      uploads_++;
      upload_bytes_ += rgba;
      LogOnce("dxtcpu", "DXT ON CPU: DXT textures are decoded to RGBA8 on the CPU (first: %s %ux%u at %08X)", fi->name, width, height,
              info.memory.base_address);
      return true;
    }
    uint32_t off = f.staging.Alloc(bytes, 0x100);
    if (off == UINT32_MAX) { LogOnce("staging", "staging ring full: %u KB texture not uploaded", bytes >> 10); return false; }
    uint8_t* dst = f.staging.cpu + off;
    const uint8_t* src = memory_->TranslatePhysical(info.memory.base_address);
    if (info.is_tiled) {
      // Row pitch in blocks, tile-aligned (TextureInfo::pitch is in texels: 4x too wide for DXT).
      if (!::hcu::UntileFast(dst, src, bw, bh, info.extent.block_pitch_h, bpb, static_cast<::hcu::SwapMode>(uint32_t(endian)))) {
        LogOnce("untile", "no untile for %u-byte blocks (%s)", bpb, fi->name);
        return false;
      }
    } else {
      uint32_t row = bw * bpb;
      uint32_t src_row = info.extent.block_pitch_h * bpb;
      for (uint32_t y = 0; y < bh; y++) texture_conversion::CopySwapBlock(endian, dst + y * row, src + y * src_row, row);
    }
    DkCopyBuf cb{f.staging.gpu + off, 0, 0};
    DkImageView view;
    dkImageViewDefaults(&view, &img.image);
    DkImageRect rect{0, 0, 0, width, height, 1};
    // A re-upload (the game reuses sprite memory between draws): earlier draws of this frame may still be
    // sampling the image.
    if (img.content_serial) dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, 0);
    if (hcu_settings.upload_strips && bh > 64) {
      // Settings menu UPLOAD IN STRIPS (A/B): the console's hatching has the size of one block-linear GOB of the stage
      // backdrop (16x8 texels at its 3.4x minification); one copy of a 41 MB image is suspect.
      const uint32_t row = bw * bpb;
      for (uint32_t by = 0; by < bh; by += 64) {
        const uint32_t nb = std::min(64u, bh - by);
        const uint32_t ty = by * fi->block_height, th = std::min(nb * fi->block_height, height - ty);
        DkCopyBuf sb{f.staging.gpu + off + uint64_t(by) * row, 0, 0};
        DkImageRect sr{0, ty, 0, width, th, 1};
        dkCmdBufCopyBufferToImage(f.cmdbuf, &sb, &view, &sr, 0);
      }
      LogOnce("strips", "textures are uploaded in strips of 64 block rows (UPLOAD IN STRIPS)");
    } else {
      dkCmdBufCopyBufferToImage(f.cmdbuf, &cb, &view, &rect, 0);
    }
    dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
    uploads_++;
    upload_bytes_ += bytes;
    if (uploads_ <= 8) Log("texture %s %ux%u %s endian %u at %08X: %u KB uploaded", fi->name, width, height,
                           info.is_tiled ? "tiled" : "linear", uint32_t(endian), info.memory.base_address, bytes >> 10);
    return true;
  }

  uint32_t DescriptorFor(Frame& f, HostImage* img, uint32_t swizzle) {
    uint64_t key = uint64_t(reinterpret_cast<uintptr_t>(img)) ^ (uint64_t(swizzle) << 48);
    auto it = f.descriptors.find(key);
    if (it != f.descriptors.end()) return it->second;
    if (f.next_descriptor >= kMaxImageDescriptors) { LogOnce("desc", "image descriptors exhausted"); return UINT32_MAX; }
    DkImageView view;
    dkImageViewDefaults(&view, &img->image);
    if (swizzle) {
      view.swizzle[0] = SwizzleOf(swizzle);
      view.swizzle[1] = SwizzleOf(swizzle >> 3);
      view.swizzle[2] = SwizzleOf(swizzle >> 6);
      view.swizzle[3] = SwizzleOf(swizzle >> 9);
    }
    DkImageDescriptor desc;
    dkImageDescriptorInitialize(&desc, &view, false, false);
    uint32_t slot = f.next_descriptor++;
    std::memcpy(descriptors_.cpu + f.desc_offset + slot * sizeof(DkImageDescriptor), &desc, sizeof(desc));
    // The GPU caches texture headers: a slot rewritten by the CPU must be invalidated before the draw.
    dkCmdBufBarrier(f.cmdbuf, DkBarrier_None, DkInvalidateFlags_Descriptors);
    f.descriptors.emplace(key, slot);
    return slot;
  }

  // ----- draws ----------------------------------------------------------------------------------
  bool CopyIndices(Frame& f, const IndexBufferInfo& ib, uint32_t& offset_out, uint32_t& vertex_count_out) {
    uint32_t bytes = uint32_t(ib.length);
    uint32_t off = f.data.Alloc(bytes, 64);
    if (off == UINT32_MAX) { LogOnce("ring", "frame data ring full (indices)"); return false; }
    const uint8_t* src = memory_->TranslatePhysical(ib.guest_base);
    uint32_t max_index = 0;
    if (ib.format == xenos::IndexFormat::kInt32) {
      const uint32_t* s = reinterpret_cast<const uint32_t*>(src);
      uint32_t* d = reinterpret_cast<uint32_t*>(f.data.cpu + off);
      for (uint32_t i = 0; i < ib.count; i++) {
        uint32_t v = xenos::GpuSwap(s[i], ib.endianness);
        d[i] = v;
        if (v > max_index) max_index = v;
      }
    } else {
      const uint16_t* s = reinterpret_cast<const uint16_t*>(src);
      uint16_t* d = reinterpret_cast<uint16_t*>(f.data.cpu + off);
      for (uint32_t i = 0; i < ib.count; i++) {
        uint16_t v = xenos::GpuSwap(s[i], ib.endianness);
        d[i] = v;
        if (v > max_index) max_index = v;
      }
    }
    offset_out = off;
    vertex_count_out = max_index + 1;
    return true;
  }

  bool BindUniforms(Frame& f, DkShaderObj* vs, DkShaderObj* ps, const draw_util::ViewportInfo& vi, const RegisterFile& regs) {
    DkCmdBuf cmd = f.cmdbuf;
    uint32_t vs_off = f.data.Alloc(4096, DK_UNIFORM_BUF_ALIGNMENT);
    uint32_t ps_off = f.data.Alloc(4096, DK_UNIFORM_BUF_ALIGNMENT);
    uint32_t sys_off = f.data.Alloc(sizeof(SysConstants), DK_UNIFORM_BUF_ALIGNMENT);
    if (vs_off == UINT32_MAX || ps_off == UINT32_MAX || sys_off == UINT32_MAX) { LogOnce("ring", "frame data ring full (uniforms)"); return false; }
    // Only the float constants a shader reads (its register map), in runs: most sprite shaders read six of
    // 256 and the buffer is write-combined memory. Dynamic addressing (c[N + a0]) copies all of them.
    auto copy_constants = [](uint8_t* dst, const uint32_t* src, const Shader::ConstantRegisterMap& m) {
#ifdef HCU_DK_GL
      static const bool full = std::getenv("HCU_DK_FULLCONST") != nullptr;  // A/B: every constant, as before the partial copy
      if (full) { std::memcpy(dst, src, 4096); return; }
#endif
      if (m.float_dynamic_addressing) { std::memcpy(dst, src, 4096); return; }
      for (uint32_t w = 0; w < 4; w++) {
        uint64_t bits = m.float_bitmap[w];
        while (bits) {
          uint32_t lo = uint32_t(__builtin_ctzll(bits));
          uint64_t run = bits >> lo;
          uint32_t len = ~run == 0 ? 64 - lo : uint32_t(__builtin_ctzll(~run));
          std::memcpy(dst + (w * 64 + lo) * 16, src + (w * 64 + lo) * 4, len * 16);
          bits = len + lo >= 64 ? 0 : bits & ~(((uint64_t(1) << len) - 1) << lo);
        }
      }
    };
    copy_constants(f.data.cpu + vs_off, &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X], vs->constant_register_map());
    copy_constants(f.data.cpu + ps_off, &regs.values[XE_GPU_REG_SHADER_CONSTANT_256_X], ps->constant_register_map());
    SysConstants sys{};
    for (int i = 0; i < 3; i++) {
      sys.ndc_scale[i] = vi.ndc_scale[i];
      sys.ndc_offset[i] = vi.ndc_offset[i];
    }
    auto cc = regs.Get<reg::RB_COLORCONTROL>();
    sys.alpha[0] = cc.alpha_test_enable ? 1.0f : 0.0f;
    sys.alpha[1] = float(uint32_t(cc.alpha_func));
    sys.alpha[2] = regs.Get<float>(XE_GPU_REG_RB_ALPHA_REF);
    std::memcpy(sys.bools, &regs.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], sizeof(sys.bools));
    std::memcpy(sys.loops, &regs.values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00], sizeof(sys.loops));
    std::memcpy(f.data.cpu + sys_off, &sys, sizeof(sys));
    DkBufExtents vs_bufs[2] = {{f.data.gpu + vs_off, 4096}, {f.data.gpu + sys_off, sizeof(SysConstants)}};
    DkBufExtents ps_bufs[2] = {{f.data.gpu + ps_off, 4096}, {f.data.gpu + sys_off, sizeof(SysConstants)}};
    dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, vs_bufs, 2);
    dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, ps_bufs, 2);
    return true;
  }

  // The XDK's Clear: rectangles (position xyz + color rgba, stride 7 words) with the depth
  // function "always"; half of them through the 2x MSAA half-pitch alias of the surface.
  bool IsClearDraw(DkShaderObj* vs, DkShaderObj* ps, xenos::PrimitiveType prim) {
    if (prim != xenos::PrimitiveType::kRectangleList || !ps->texture_bindings().empty()) return false;
    const auto& vbs = vs->vertex_bindings();
    if (vbs.size() != 1 || vbs[0].stride_words != 7 || vbs[0].attributes.size() != 2) return false;
    const auto& a0 = vbs[0].attributes[0].fetch_instr.attributes;
    const auto& a1 = vbs[0].attributes[1].fetch_instr.attributes;
    return a0.data_format == xenos::VertexFormat::k_32_32_32_FLOAT && a0.offset == 0 &&
           a1.data_format == xenos::VertexFormat::k_32_32_32_32_FLOAT && a1.offset == 3;
  }

  bool ClearDraw(uint32_t vertex_count, xenos::EdramMode mode) {
    const RegisterFile& regs = *register_file_;
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;
    auto fetch = regs.GetVertexFetch(0);
    const uint32_t* words = reinterpret_cast<const uint32_t*>(memory_->TranslatePhysical(fetch.address << 2));
    auto surf = regs.Get<reg::RB_SURFACE_INFO>();
    auto color_info = regs.Get<reg::RB_COLOR_INFO>();
    auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    uint32_t scale = surf.msaa_samples == xenos::MsaaSamples::k4X ? 2 : 1;
    uint32_t pitch = EffectivePitch(surf);
    auto word = [&](uint32_t i) {
      uint32_t w = words[i];
      if (fetch.endian == xenos::Endian::k8in32) w = __builtin_bswap32(w);
      float v;
      std::memcpy(&v, &w, 4);
      return v;
    };
    for (uint32_t r = 0; r + 3 <= vertex_count && r * 7 + 21 <= fetch.size; r += 3) {
      float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
      for (uint32_t v = 0; v < 3; v++) {
        float x = word((r + v) * 7), y = word((r + v) * 7 + 1);
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
      float z = word(r * 7 + 2);
      float color[4] = {word(r * 7 + 3), word(r * 7 + 4), word(r * 7 + 5), word(r * 7 + 6)};
      DkScissor sc;
      sc.x = uint32_t(std::max(0.0f, std::floor(x0 + 0.5f))) * scale;
      sc.y = uint32_t(std::max(0.0f, std::floor(y0 + 0.5f))) * scale;
      sc.width = uint32_t(std::max(0.0f, std::floor(x1 + 0.5f) - std::floor(x0 + 0.5f))) * scale;
      sc.height = uint32_t(std::max(0.0f, std::floor(y1 + 0.5f) - std::floor(y0 + 0.5f))) * scale;
      HostImage* color_img = nullptr;
      HostImage* depth_img = nullptr;
      if (mode == xenos::EdramMode::kColorDepth) {
        color_img = TargetImage(color_info.color_base, pitch, ColorTargetFormatOf(color_info.color_format), false);
        depth_img = TargetImage(depth_info.depth_base, pitch, g_depth_fmt, true);
      } else if (mode == xenos::EdramMode::kDepthOnly) {
        // Depth-only through the alias: the XDK fills a color surface's EDRAM this way too.
        auto it = targets_.find((uint64_t(depth_info.depth_base) << 32) | (uint64_t(pitch) << 1));
        if (it != targets_.end()) color_img = it->second;
        else depth_img = TargetImage(depth_info.depth_base, pitch, g_depth_fmt, true);
      }
      if (!color_img && !depth_img) continue;
      BindTargets(cmd, color_img, depth_img);
      uint32_t w = color_img ? color_img->width : depth_img->width, h = color_img ? color_img->height : depth_img->height;
      if (sc.x >= w || sc.y >= h) continue;
      if (sc.x + sc.width > w) sc.width = w - sc.x;
      if (sc.y + sc.height > h) sc.height = h - sc.y;
      dkCmdBufSetScissors(cmd, 0, &sc, 1);
      if (color_img) dkCmdBufClearColor(cmd, 0, DkColorMask_RGBA, color);
      if (depth_img) dkCmdBufClearDepthStencil(cmd, true, z, 0xFF, 0);
      {
        char key[96];
        std::snprintf(key, sizeof(key), "clear %u %u %u %u %u %u %u %u", uint32_t(mode), color_info.color_base, depth_info.depth_base, pitch,
                      sc.x, sc.y, sc.width, sc.height);
        LogOnce(key, "clear %s%s at EDRAM %u/%u pitch %u: rect %u,%u %ux%u color %g %g %g %g z %g (alias x%u)", color_img ? "color" : "",
                depth_img ? " depth" : "", color_info.color_base, depth_info.depth_base, pitch, sc.x, sc.y, sc.width, sc.height, color[0],
                color[1], color[2], color[3], z, scale);
      }
#ifdef HCU_DK_GL
      if (TraceFrame()) Log("T clear %s%s EDRAM %u/%u pitch %u rect %u,%u %ux%u color %g %g %g %g z %g", color_img ? "color" : "", depth_img ? " depth" : "",
                            color_info.color_base, depth_info.depth_base, pitch, sc.x, sc.y, sc.width, sc.height, color[0], color[1], color[2], color[3], z);
#endif
      clears_++;
      frame_clears_++;
    }
    return true;
  }

  bool Resolve() {
    const RegisterFile& regs = *register_file_;
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;
    draw_util::ResolveInfo info;
    if (!draw_util::GetResolveInfo(regs, *memory_, 1, 1, false, false, info)) { LogOnce("resolve", "GetResolveInfo failed"); return true; }
    if (info.IsClearingDepth()) {
      // The XDK clears depth with the resolve (D3DRESOLVE_CLEARDEPTHSTENCIL). Without it a depth-tested
      // scene keeps the last frame's (or uninitialized) depth and every LEQUAL fragment fails (HCU's
      // stage: the 3D level never showed, the depth-less HUD did). D24S8: depth in the top 24 bits.
      uint32_t dpitch = info.depth_edram_info.pitch_tiles * 80;
      HostImage* d = TargetImage(info.depth_original_base, dpitch, g_depth_fmt, true);
      uint32_t cx = info.coordinate_info.edram_offset_x_div_8 * 8, cy = info.coordinate_info.edram_offset_y_div_8 * 8;
      uint32_t cw = info.coordinate_info.width_div_8 * 8, ch = info.height_div_8 * 8;
      if (d && cx < d->width && cy < d->height) {
        if (cx + cw > d->width) cw = d->width - cx;
        if (cy + ch > d->height) ch = d->height - cy;
        BindTargets(cmd, nullptr, d);
        DkScissor sc{cx, cy, cw, ch};
        dkCmdBufSetScissors(cmd, 0, &sc, 1);
        float z = float(info.rb_depth_clear >> 8) / float(0xFFFFFF);
        dkCmdBufClearDepthStencil(cmd, true, z, 0xFF, info.rb_depth_clear & 0xFF);
        char key[64];
        std::snprintf(key, sizeof(key), "dclear %u %u %u %u %u %u", info.depth_original_base, dpitch, cx, cy, cw, ch);
        LogOnce(key, "resolve clears depth at EDRAM %u pitch %u: rect %u,%u %ux%u to %g, stencil %u", info.depth_original_base, dpitch, cx,
                cy, cw, ch, z, info.rb_depth_clear & 0xFF);
      }
    }
    if (info.IsCopyingDepth() && no_depth_copy_) return true;
    if (info.IsCopyingDepth()) {
      // A depth resolve into a texture (the stage's shadow map): the depth image is copied into a depth
      // image aliased at the destination address, which the pixel shaders then sample as a k_24_8 texture.
      uint32_t dpitch = info.depth_edram_info.pitch_tiles * 80;
      HostImage* dsrc = TargetImage(info.depth_original_base, dpitch, g_depth_fmt, true);
      uint32_t x = info.coordinate_info.edram_offset_x_div_8 * 8, y = info.coordinate_info.edram_offset_y_div_8 * 8;
      uint32_t w = info.coordinate_info.width_div_8 * 8, h = info.height_div_8 * 8;
      uint32_t dx = info.copy_dest_coordinate_info.offset_x_div_8 * 8, dy = info.copy_dest_coordinate_info.offset_y_div_8 * 8;
      if (!dsrc || !w || !h) return true;
      HostImage* dd = GuestImage(f, info.copy_dest_base, dx + w, dy + h, g_depth_fmt, true);
      if (!dd) return true;
      dd->from_resolve = true;
      dd->content_serial = serial_;
      dd->resolve_swap = swaps_ + 1;
      depth_resolves_++;
      // Settings menu VIEW SHADOW MAP DEPTH: the depth texture itself is shown (red = depth as the receivers' 9 point taps
      // read it). Smooth = the map is right and the receivers' compare is the suspect; blocks or noise = the depth
      // copy or the Z24S8 texture fetch on the console.
      if (!view_image_ && hcu_settings.view == 2) view_image_ = dd;
      StepPass(dd, "depth", info.depth_original_base, dpitch, x, y, w, h, info.copy_dest_base);
      if (x + w > dsrc->width) w = x < dsrc->width ? dsrc->width - x : 0;
      if (y + h > dsrc->height) h = y < dsrc->height ? dsrc->height - y : 0;
      if (dx + w > dd->width) w = dx < dd->width ? dd->width - dx : 0;
      if (dy + h > dd->height) h = dy < dd->height ? dd->height - dy : 0;
      if (w && h) {
        DkImageView sv, dv;
        dkImageViewDefaults(&sv, &dsrc->image);
        dkImageViewDefaults(&dv, &dd->image);
        DkImageRect sr{x, y, 0, w, h, 1}, dr{dx, dy, 0, w, h, 1};
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
        dkCmdBufCopyImage(cmd, &sv, &sr, &dv, &dr, 0);
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
      }
      char key[96];
      std::snprintf(key, sizeof(key), "depthcopy %u %u %u %u %u %u %08X", info.depth_original_base, dpitch, x, y, w, h, info.copy_dest_base);
      LogOnce(key, "depth resolve EDRAM %u pitch %u rect %u,%u %ux%u -> %08X (%ux%u depth image) at %u,%u", info.depth_original_base, dpitch, x, y, w, h,
              info.copy_dest_base, dd->width, dd->height, dx, dy);
      return true;
    }
    uint32_t src_pitch = info.color_edram_info.pitch_tiles * 80;
    HostImage* src = TargetImage(info.color_edram_info.base_tiles, src_pitch, DkImageFormat_RGBA8_Unorm, false);
    if (!src) return true;
    uint32_t x = info.coordinate_info.edram_offset_x_div_8 * 8, y = info.coordinate_info.edram_offset_y_div_8 * 8;
    uint32_t w = info.coordinate_info.width_div_8 * 8, h = info.height_div_8 * 8;
    uint32_t dest_pitch = info.copy_dest_coordinate_info.pitch_aligned_div_32 * 32;
    uint32_t dest_height = info.copy_dest_coordinate_info.height_aligned_div_32 * 32;
    (void)dest_pitch; (void)dest_height;
    uint32_t dx = info.copy_dest_coordinate_info.offset_x_div_8 * 8, dy = info.copy_dest_coordinate_info.offset_y_div_8 * 8;
    // The destination keeps the target's host format (RGB10A2, RGBA16F), so the raw image copy is exact and the
    // texture fetch of the resolve finds an image of its own format. sRGB targets resolve to plain RGBA8: the
    // guest reads the stored (gamma-encoded) bytes as a normal texture.
    DkImageFormat dst_format = src->format == DkImageFormat_RGBA8_Unorm_sRGB ? DkImageFormat_RGBA8_Unorm : src->format;
    HostImage* dst = GuestImage(f, info.copy_dest_base, dx + w, dy + h, dst_format, true);  // gh: exact size (the aligned one made it ping-pong with the texture bind)
    if (!dst) return true;
    dst->from_resolve = true;
    // Settings menu VIEW: the frame's first resolve of the scene (1280x720, before the post effects) is shown at the swap.
    if (!view_image_ && hcu_settings.view == 1 && w == kPresentWidth && h == kPresentHeight) view_image_ = dst;
    dst->content_serial = serial_;
    dst->resolve_swap = swaps_ + 1;
    dst->mem_bytes = dest_pitch * dest_height * 4;
    dst->mem_hash = HashMemory(info.copy_dest_base, dst->mem_bytes);
    if (x + w > src->width) w = x < src->width ? src->width - x : 0;
    if (y + h > src->height) h = y < src->height ? src->height - y : 0;
    if (dx + w > dst->width) w = dx < dst->width ? dst->width - dx : 0;
    if (dy + h > dst->height) h = dy < dst->height ? dst->height - dy : 0;
    if (w && h) {
      DkImageView sv, dv;
      dkImageViewDefaults(&sv, &src->image);
      dkImageViewDefaults(&dv, &dst->image);
      DkImageRect sr{x, y, 0, w, h, 1}, dr{dx, dy, 0, w, h, 1};
      dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
      dkCmdBufCopyImage(cmd, &sv, &sr, &dv, &dr, 0);
      dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    }
    if (info.IsClearingColor()) {
      BindTargets(cmd, src, nullptr);
      DkScissor sc{x, y, w, h};
      dkCmdBufSetScissors(cmd, 0, &sc, 1);
      uint32_t c = info.rb_color_clear;
      float color[4] = {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f};
      dkCmdBufClearColor(cmd, 0, DkColorMask_RGBA, color);
    }
    {
      // Every distinct resolve (source rect, destination and offset) once: the frame structure.
      char key[96];
      std::snprintf(key, sizeof(key), "resolve %u %u %u %u %u %u %08X %u %u", info.color_edram_info.base_tiles, src_pitch, x, y, w, h,
                    info.copy_dest_base, dx, dy);
      LogOnce(key, "resolve EDRAM %u pitch %u rect %u,%u %ux%u -> %08X (%ux%u image) at %u,%u%s", info.color_edram_info.base_tiles, src_pitch,
              x, y, w, h, info.copy_dest_base, dst->width, dst->height, dx, dy, info.IsClearingColor() ? ", then cleared" : "");
    }
#ifdef HCU_DK_GL
    if (TraceFrame()) Log("T resolve EDRAM %u pitch %u rect %u,%u %ux%u -> %08X (%ux%u) at %u,%u%s%s", info.color_edram_info.base_tiles, src_pitch, x, y, w, h,
                          info.copy_dest_base, dst->width, dst->height, dx, dy, info.IsClearingColor() ? " clearC" : "", info.IsClearingDepth() ? " clearD" : "");
#endif
    StepPass(dst, "colour", info.color_edram_info.base_tiles, src_pitch, x, y, w, h, info.copy_dest_base);
    resolves_++;
    frame_resolves_++;
    return true;
  }

  // Settings menu VIEW STEP: every resolve of a drawn frame gets a number; resolve view_step + 1 is shown at the swap.
  // When the step changes, the next drawn frame lists its passes: number, draws before it, source and destination.
  void StepPass(HostImage* img, const char* kind, uint32_t base, uint32_t pitch, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t dest) {
    step_n_++;
    if (hcu_settings.view == 3 && int(step_n_) == hcu_settings.view_step + 1) view_image_ = img;
    if (step_log_)
      Log("pass %u: %u draws, then a %s resolve of EDRAM %u pitch %u rect %u,%u %ux%u -> %08X (%ux%u, host format %d)%s", step_n_, step_draws_,
          kind, base, pitch, x, y, w, h, dest, img->width, img->height, int(img->format),
          int(step_n_) == hcu_settings.view_step + 1 ? "  <- VIEW STEP" : "");
    step_draws_ = 0;
  }

  // ----- shaders --------------------------------------------------------------------------------
  // The same two files as Shader::DumpUcode (shader_<hash>.ucode.bin.<ext> and the disassembly
  // shader_<hash>.ucode.<ext>, the translator's input), written with stdio: the SDK's version goes
  // through std::filesystem::absolute, which turns sdmc:/x into /sdmc:/x on newlib (finding 0.41).
  void DumpMissing(const DkShaderObj& shader) {
    std::string dir = shader_dir_ + "missing/";
    mkdir(dir.c_str(), 0777);
    const char* ext = shader.type() == xenos::ShaderType::kVertex ? "vert" : "frag";
    char name[512];
    std::snprintf(name, sizeof(name), "%sshader_%016llX.ucode.bin.%s", dir.c_str(), (unsigned long long)shader.ucode_data_hash(), ext);
    if (FILE* fp = std::fopen(name, "wb")) {
      std::fwrite(shader.ucode_data().data(), 4, shader.ucode_data().size(), fp);
      std::fclose(fp);
    } else {
      Log("cannot write %s", name);
    }
    std::snprintf(name, sizeof(name), "%sshader_%016llX.ucode.%s", dir.c_str(), (unsigned long long)shader.ucode_data_hash(), ext);
    if (FILE* fp = std::fopen(name, "w")) {
      std::fwrite(shader.ucode_disassembly().data(), 1, shader.ucode_disassembly().size(), fp);
      std::fclose(fp);
    }
  }

  bool LoadDksh(const std::string& path, DkShader& out) {
#ifdef HCU_DK_GL
    // Linux: the translated GLSL next to where the DKSH would be (name.dksh -> name.glsl).
    std::string glsl = path.size() > 5 && path.compare(path.size() - 5, 5, ".dksh") == 0 ? path.substr(0, path.size() - 5) + ".glsl" : path;
    FILE* gp = std::fopen(glsl.c_str(), "rb");
    if (!gp) return false;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), gp)) > 0) text.append(buf, n);
    std::fclose(gp);
    DkStage stage = path.find(".vert") != std::string::npos ? DkStage_Vertex : path.find(".geom") != std::string::npos ? DkStage_Geometry : DkStage_Fragment;
    dkShaderInitializeGlsl(&out, stage, text.c_str());
    return true;
#else
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;
    struct { uint32_t magic, header_sz, control_sz, code_sz, programs_off, num_programs; } hdr;
    if (std::fread(&hdr, sizeof(hdr), 1, fp) != 1 || hdr.control_sz < sizeof(hdr) || hdr.control_sz > (1u << 20) || hdr.code_sz > (4u << 20)) {
      std::fclose(fp);
      return false;
    }
    std::vector<uint8_t> control(hdr.control_sz);
    std::rewind(fp);
    if (std::fread(control.data(), hdr.control_sz, 1, fp) != 1) { std::fclose(fp); return false; }
    uint32_t off = code_.Alloc(hdr.code_sz, DK_SHADER_CODE_ALIGNMENT);
    if (off == UINT32_MAX) { std::fclose(fp); Log("shader code pool full"); return false; }
    if (std::fread(code_.cpu + off, hdr.code_sz, 1, fp) != 1) { std::fclose(fp); return false; }
    std::fclose(fp);
    DkShaderMaker sm;
    dkShaderMakerDefaults(&sm, code_.block, off);
    sm.control = control.data();
    sm.programId = 0;
    dkShaderInitialize(&out, &sm);
    return true;
#endif
  }

  void LogOnce(const char* key, const char* fmt, ...) {
    if (!logged_.insert(key).second) return;
    va_list ap;
    va_start(ap, fmt);
    std::printf("[dk] ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
#ifndef __SWITCH__
    std::fflush(stdout);
#endif
  }

  DkDevice device_ = nullptr;
  DkQueue queue_ = nullptr;
  DkSwapchain swapchain_ = nullptr;
  MemBlock code_, images_, descriptors_, cmdmem_;
  uint32_t sampler_desc_offset_ = 0;
  Frame frames_[kFrames];
  uint32_t frame_ = 0;
  HostImage* swap_images_[kFrames] = {};
  DkImage const* swap_image_ptrs_[kFrames] = {};
  DkShader present_vs_shader_{}, present_fs_shader_{};
  bool present_vs_ = false, present_fs_ = false;
  bool present_target_ = false;
  HostImage* view_image_ = nullptr;  // settings menu VIEW
  uint32_t step_n_ = 0, step_draws_ = 0;  // VIEW STEP: resolves and draws so far in this frame
  int step_logged_ = 0;
  bool step_log_ = false, step_logged_once_ = false;
  bool dump_images_ = false;
  bool no_depth_copy_ = false;
  float z_bias_ = -16.f;
  float shadow_bias_ = 32.f, shadow_slope_ = 1.5f;  // shadow-pass depth bias (file zfix)
  bool no_precise_ = false;  // zfix "noprecise"
  bool overlay_on_ = true;
  HostImage* overlay_[kFrames] = {};  // the version / fps strip, one image per frame in flight
  HostImage* menu_[kFrames] = {};     // the settings menu, the same way
  std::vector<uint32_t> menu_px_ = std::vector<uint32_t>(kMenuW * kMenuH);
  char overlay_text_[96] = {};
  uint64_t fps_tick_ = 0;
  uint32_t fps_frames_ = 0, fps_x10_ = 0;
  std::vector<uint32_t> overlay_px_ = std::vector<uint32_t>(kOverlayW * kOverlayH);
  bool always_upload_ = false;
  uint64_t meter_block_us_ = 0;  // fence wait + acquire, never reset (the CPU-work meter)
  uint64_t us_draw_ = 0, us_copy_ = 0, us_fence_ = 0, us_acquire_ = 0, us_submit_ = 0;  // ScopedUs counters of the 60-swap log
  uint32_t depth_resolves_ = 0;  // depth resolves so far (MergeDepthPieces looks again when it changed)
  std::string shader_dir_;
  std::string debug_dir_;
#ifdef __SWITCH__
  // Settings menu RECORD 5 SEC: the shown 1280x720 image of each drawn frame is copied into rec_ (one slot per frame)
  // and handed to the recorder when the frame's fence has passed (BeginFrame): the GPU thread never waits for it.
  MemBlock rec_;
  bool rec_on_ = false;
  uint64_t rec_start_tick_ = 0;
  void RecordFrame(Frame& f, DkCmdBuf cmd, HostImage* front) {
    if (!rec_on_ && hcu_settings.record) {
      if (!rec_.block && !rec_.Create(device_, kPresentWidth * kPresentHeight * 4 * kFrames, DkMemBlockFlags_CpuCached | DkMemBlockFlags_GpuCached)) {
        Log("RECORD 5 SEC: no memory block");
        hcu_settings.record = 0;
        return;
      }
      if (!hcu_rec_start((debug_dir_ + "rec/").c_str(), swaps_ + 1, kPresentWidth, kPresentHeight)) {
        hcu_settings.record = 0;
        return;
      }
      rec_on_ = true;
      rec_start_tick_ = armGetSystemTick();
    }
    if (!rec_on_) return;
    const uint64_t us = armTicksToNs(armGetSystemTick() - rec_start_tick_) / 1000;
    if (us > 5000000) {
      // Done: the frames still in flight are handed over, then the writer finishes the file.
      dkQueueWaitIdle(queue_);
      const uint32_t bytes = kPresentWidth * kPresentHeight * 4;
      for (uint32_t i = 0; i < kFrames; i++) {
        if (!frames_[i].rec_pending) continue;
        dkMemBlockFlushCpuCache(rec_.block, i * bytes, bytes);
        hcu_rec_push(rec_.cpu + i * bytes, frames_[i].rec_swap, frames_[i].rec_us);
        frames_[i].rec_pending = false;
      }
      hcu_rec_stop();
      rec_on_ = false;
      hcu_settings.record = 0;
      return;
    }
    if (!front || front->width != kPresentWidth || front->height != kPresentHeight || front->format != DkImageFormat_RGBA8_Unorm) return;
    DkImageView v;
    dkImageViewDefaults(&v, &front->image);
    DkImageRect r{0, 0, 0, kPresentWidth, kPresentHeight, 1};
    DkCopyBuf cb{rec_.gpu + uint64_t(frame_) * kPresentWidth * kPresentHeight * 4, 0, 0};
    dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
    dkCmdBufCopyImageToBuffer(cmd, &v, &r, &cb, 0);
    f.rec_pending = true;
    f.rec_swap = swaps_ + 1;
    f.rec_us = us;
  }
#endif
  uint32_t swap_interval_ = 1;
  bool skip_odd_frames_ = false;
  bool skip_busy_ = false;  // 60skip: a frame the GPU has no free slot for is not drawn (NextFrame)
  bool skip_frame_ = false;  // this frame is not drawn (NextFrame)
  bool begun_ = true;        // frames_[frame_] is ready for recording (BeginFrame ran)
  uint32_t drawn_frames_ = 0, busy_skips_ = 0, busy_in_a_row_ = 0;
  std::vector<uint32_t> page_serial_ = std::vector<uint32_t>(0x20000);  // per 4 KB physical page: the draw serial of its last write
  uint32_t serial_ = 1;  // the draw serial (SyncDirtyPages)
  uint64_t dirty_pages_ = 0, dirty_pages_at_swap_ = 0;
  std::unordered_map<uint64_t, DkShaderObj*> shaders_;
  std::unordered_map<uint64_t, HostImage*> targets_;       // EDRAM base/pitch -> render target
  std::unordered_map<uint32_t, HostImage*> guest_images_;  // guest address -> texture / resolve destination
  HostImage* bound_color_ = nullptr;
  HostImage* bound_depth_ = nullptr;
  std::unordered_set<std::string> logged_;
  std::unordered_set<uint64_t> seen_;  // integer keys of the per-draw first-time logs (no string formatting in the draw path)
  bool First(uint64_t key) { return seen_.insert(key).second; }
  uint32_t swaps_ = 0, draws_ = 0, skipped_ = 0, missing_draws_ = 0, missing_shaders_ = 0, clears_ = 0, resolves_ = 0;
  uint32_t image_count_ = 0, uploads_ = 0, reuploads_ = 0;
  uint32_t image_flips_ = 0;  // recreated + resized images, all frames (the first 40 are logged)
  uint64_t upload_bytes_ = 0;
  uint32_t frame_draws_ = 0, frame_clears_ = 0, frame_resolves_ = 0, frame_skipped_ = 0, frame_missing_ = 0, frame_reuploads_ = 0, frame_vtx_bytes_ = 0, frame_shadow_biased_ = 0;
};

class Deko3dGraphicsSystem : public GraphicsSystem {
 public:
  std::string name() const override { return "deko3d"; }
  // The backend owns the default window's swapchain; the SDK keeps no presenter.
  X_STATUS SetupPresentation(::rex::ui::WindowedAppContext*) override { return X_STATUS_SUCCESS; }

 protected:
  void CreateProvider(bool) override {}
  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<Deko3dCommandProcessor>(this, kernel_state_);
  }
};

}  // namespace

rex::system::IGraphicsSystem* CreateDeko3dGraphicsSystem() { return new Deko3dGraphicsSystem(); }

}  // namespace rex::graphics::deko
#endif  // __SWITCH__ || HCU_DK_GL
