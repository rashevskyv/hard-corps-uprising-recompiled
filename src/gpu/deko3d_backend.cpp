// deko3d GPU backend (--gpu_backend=deko3d, Nintendo Switch): the SDK's Xenos command processor
// parses the ring (fences, read-pointer write-back, vblank, bin masks), this backend draws with
// deko3d. No EDRAM model: a host image per render target (keyed by EDRAM base and pitch), resolves
// are image copies into images keyed by guest address, textures bound at such an address alias
// them (no readback), the swap presents the image at the front buffer's address through one
// textured triangle. Shaders are translated offline (tools/xenos2glsl.py, uam) and loaded by the
// ucode hash from sdmc:/switch/hcu/shaders/. docs/native-renderer-plan.md, M3.
// cvhd-recomp, BSD-3 like the SDK.
#ifdef __SWITCH__
#include <switch.h>
#include <deko3d.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/graphics/command_processor.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/util/draw.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/string/buffer.h>
#include <rex/system/gpu_plugin.h>

#include "ucode_hash.h"

namespace rex::graphics::deko {

namespace {

constexpr uint32_t kFrames = 2;
constexpr uint32_t kFrameDataBytes = 24u << 20;  // vertex, index and uniform data of one frame
constexpr uint32_t kFrameCmdBytes = 4u << 20;
constexpr uint32_t kStagingBytes = 24u << 20;    // texture uploads of one frame
constexpr uint32_t kImagePoolBytes = 224u << 20;
constexpr uint32_t kCodePoolBytes = 4u << 20;
constexpr uint32_t kMaxImageDescriptors = 512;
constexpr uint32_t kSamplerCount = 8;
constexpr uint32_t kPresentWidth = 1280;
constexpr uint32_t kPresentHeight = 720;
constexpr uint32_t kEdramTiles = 2048;  // 10 MB of 5120-byte tiles (80 x 16 x 32 bit)
constexpr uint32_t kMaxTargetHeight = 1088;
const char* const kShaderDir = "sdmc:/switch/hcu/shaders/";

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::printf("[dk] ");
  std::vprintf(fmt, ap);
  std::printf("\n");
  va_end(ap);
  std::fflush(stdout);
}

void DebugCallback(void*, const char* context, DkResult result, const char* message) {
  Log("deko3d %s: result %d: %s", context ? context : "?", int(result), message ? message : "");
}

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
  bool from_resolve = false;  // host-native channels: the fetch swizzle is not applied
  bool uploaded = false;
  uint32_t guest_base = 0;    // textures and resolve destinations
  uint32_t edram_base = 0, edram_pitch = 0;  // render targets
};

class DkShaderObj : public Shader {
 public:
  DkShaderObj(xenos::ShaderType type, uint64_t hash, const uint32_t* dwords, uint32_t count)
      : Shader(type, hash, dwords, count) {}
  DkShader shader{};
  bool loaded = false;
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
};

struct SysConstants {
  float ndc_scale[4];
  float ndc_offset[4];
  float alpha[4];
  uint32_t bools[8];
};

DkCompareOp CompareOp(xenos::CompareFunction f) { return DkCompareOp(uint32_t(f)); }

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

bool TextureFormatOf(xenos::TextureFormat f, DkImageFormat& out) {
  using TF = xenos::TextureFormat;
  switch (f) {
    case TF::k_8_8_8_8: case TF::k_8_8_8_8_A: out = DkImageFormat_RGBA8_Unorm; return true;
    case TF::k_8: case TF::k_8_A: case TF::k_8_B: out = DkImageFormat_R8_Unorm; return true;
    case TF::k_8_8: out = DkImageFormat_RG8_Unorm; return true;
    case TF::k_DXT1: out = DkImageFormat_RGBA_BC1; return true;
    case TF::k_DXT2_3: out = DkImageFormat_RGBA_BC2; return true;
    case TF::k_DXT4_5: out = DkImageFormat_RGBA_BC3; return true;
    case TF::k_4_4_4_4: out = DkImageFormat_RGBA4_Unorm; return true;
    case TF::k_5_6_5: out = DkImageFormat_RGB565_Unorm; return true;
    case TF::k_1_5_5_5: out = DkImageFormat_RGB5A1_Unorm; return true;
    case TF::k_16_16_16_16_FLOAT: out = DkImageFormat_RGBA16_Float; return true;
    case TF::k_32_FLOAT: out = DkImageFormat_R32_Float; return true;
    case TF::k_16_FLOAT: out = DkImageFormat_R16_Float; return true;
    case TF::k_16: out = DkImageFormat_R16_Unorm; return true;
    case TF::k_16_16: out = DkImageFormat_RG16_Unorm; return true;
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
        !descriptors_.Create(device_, DK_MEMBLOCK_ALIGNMENT * 4 * kFrames + DK_MEMBLOCK_ALIGNMENT, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached) ||
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
    // Samplers: filter (nearest, linear) x wrap x (clamp, repeat) x wrap y (clamp, repeat).
    for (uint32_t i = 0; i < kSamplerCount; i++) {
      DkSampler s;
      dkSamplerDefaults(&s);
      DkFilter filter = (i & 4) ? DkFilter_Linear : DkFilter_Nearest;
      s.minFilter = filter;
      s.magFilter = filter;
      s.mipFilter = DkMipFilter_None;
      s.wrapMode[0] = (i & 2) ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
      s.wrapMode[1] = (i & 1) ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
      s.wrapMode[2] = DkWrapMode_ClampToEdge;
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
    present_target_ = access("sdmc:/switch/hcu/dk_present_rt", F_OK) == 0;
    dump_images_ = access("sdmc:/switch/hcu/dk_dump", F_OK) == 0;  // readback dumps stall the GPU: debug only
    present_vs_ = LoadDksh(std::string(kShaderDir) + "present.vert.dksh", present_vs_shader_);
    present_fs_ = LoadDksh(std::string(kShaderDir) + "present.frag.dksh", present_fs_shader_);
    Log("deko3d backend up: %s present shaders, %u MB images, %u MB data per frame",
        present_vs_ && present_fs_ ? "with" : "WITHOUT", kImagePoolBytes >> 20, kFrameDataBytes >> 20);
    BeginFrame();
    return true;
  }

  void ShutdownContext() override {
    if (queue_) dkQueueWaitIdle(queue_);
    Log("deko3d backend down: %u swaps, %u draws, %u skipped, %u without shaders, %u clears, %u resolves, %u images (%u MB), %u uploads (%u KB)",
        swaps_, draws_, skipped_, missing_draws_, clears_, resolves_, image_count_, images_.peak >> 20, uploads_,
        uint32_t(upload_bytes_ >> 10));
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
    char name[128];
    std::snprintf(name, sizeof(name), "%sshader_%016llX.%s.dksh", kShaderDir, (unsigned long long)hash,
                  shader_type == xenos::ShaderType::kVertex ? "vert" : "frag");
    shader->loaded = LoadDksh(name, shader->shader);
    if (!shader->loaded) {
      missing_shaders_++;
      Log("no DKSH for %s %016llX (%u dwords at %08X): dumping the ucode to shaders/missing/",
          shader_type == xenos::ShaderType::kVertex ? "VS" : "PS", (unsigned long long)hash, dword_count, guest_address);
      shader->DumpUcode(std::string(kShaderDir) + "missing");
    } else {
      REXLOG_INFO("[dk] shader {} {:016X} loaded ({} dwords)", shader_type == xenos::ShaderType::kVertex ? "VS" : "PS",
                  hash, dword_count);
    }
    return shader;
  }

  bool IssueDraw(xenos::PrimitiveType prim_type, uint32_t index_count, IndexBufferInfo* index_buffer_info,
                 bool major_mode_explicit) override {
    const RegisterFile& regs = *register_file_;
    auto mode = regs.Get<reg::RB_MODECONTROL>().edram_mode;
    if (mode == xenos::EdramMode::kCopy) return Resolve();
    auto* vs = static_cast<DkShaderObj*>(active_vertex_shader());
    auto* ps = static_cast<DkShaderObj*>(active_pixel_shader());
    if (!vs || !ps) { skipped_++; return true; }
    auto mask = regs.Get<reg::RB_COLOR_MASK>();
    auto dc = draw_util::GetNormalizedDepthControl(regs);
    bool writes_color = mask.write_red0 || mask.write_green0 || mask.write_blue0 || mask.write_alpha0;
    bool writes_depth = dc.z_enable && dc.z_write_enable;
    if (!writes_color && !writes_depth && !dc.stencil_enable) { skipped_++; return true; }
    if (IsClearDraw(vs, ps, prim_type)) return ClearDraw(index_count, mode);
    if (!vs->loaded || !ps->loaded) { missing_draws_++; return true; }
    DkPrimitive prim;
    bool rects;
    if (!PrimitiveOf(prim_type, prim, rects)) { LogOnce("prim", "unsupported primitive type %u", uint32_t(prim_type)); skipped_++; return true; }
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;

    // Render targets.
    auto surf = regs.Get<reg::RB_SURFACE_INFO>();
    auto color_info = regs.Get<reg::RB_COLOR_INFO>();
    auto depth_info = regs.Get<reg::RB_DEPTH_INFO>();
    uint32_t pitch = EffectivePitch(surf);
    HostImage* color = (mode != xenos::EdramMode::kDepthOnly && writes_color)
                           ? TargetImage(color_info.color_base, pitch, ColorTargetFormatOf(color_info.color_format), false)
                           : nullptr;
    HostImage* depth = (dc.z_enable || dc.stencil_enable) ? TargetImage(depth_info.depth_base, pitch, DkImageFormat_Z24S8, true) : nullptr;
    if (!color && !depth) { skipped_++; return true; }
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
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;  // TODO(cull): cull_front/cull_back once the winding is verified on a picture
    rs.fillRectangleEnable = rects;
    (void)su;
    dkCmdBufBindRasterizerState(cmd, &rs);
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
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = depth && dc.z_enable;
    ds.depthWriteEnable = depth && dc.z_write_enable;
    ds.depthCompareOp = CompareOp(dc.zfunc);
    ds.stencilTestEnable = 0;
    dkCmdBufBindDepthStencilState(cmd, &ds);

    // Shaders.
    DkShader const* shaders[2] = {&vs->shader, &ps->shader};
    dkCmdBufBindShaders(cmd, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);

    // Vertex layout and data: one buffer per fetch constant the vertex shader uses.
    DkVtxAttribState attribs[DK_MAX_VERTEX_ATTRIBS];
    DkVtxBufferState buffers[DK_MAX_VERTEX_BUFFERS];
    uint32_t attrib_count = 0, buffer_count = 0;
    uint32_t vertex_count = index_count;
    uint32_t index_offset = UINT32_MAX;
    if (index_buffer_info) {
      if (!CopyIndices(f, *index_buffer_info, index_offset, vertex_count)) { skipped_++; return true; }
    }
    for (const auto& vb : vs->vertex_bindings()) {
      if (buffer_count >= DK_MAX_VERTEX_BUFFERS) break;
      auto fetch = regs.GetVertexFetch(vb.fetch_constant);
      uint32_t stride = vb.stride_words * 4;
      uint32_t bytes = vertex_count * stride;
      uint32_t avail = fetch.size * 4;
      if (bytes > avail) bytes = avail;
      uint32_t off = f.data.Alloc(bytes ? bytes : 16, 64);
      if (off == UINT32_MAX) { LogOnce("ring", "frame data ring full"); skipped_++; return true; }
      const uint8_t* src = memory_->TranslatePhysical(fetch.address << 2);
      texture_conversion::CopySwapBlock(fetch.endian, f.data.cpu + off, src, bytes);
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

    // Textures of the pixel shader.
    for (const auto& tb : ps->texture_bindings()) {
      auto fetch = regs.GetTextureFetch(tb.fetch_constant);
      TextureInfo info;
      if (!TextureInfo::Prepare(fetch, &info) || info.dimension != xenos::DataDimension::k2DOrStacked) {
        LogOnce("tex", "texture fetch %u not preparable or not 2D (dim %u)", tb.fetch_constant, uint32_t(fetch.dimension));
        continue;
      }
      HostImage* tex = TextureImage(f, info, fetch);
      if (!tex) continue;
      uint32_t slot = DescriptorFor(f, tex, tex->from_resolve ? 0 : fetch.swizzle);
      if (slot == UINT32_MAX) continue;
      uint32_t sampler = (fetch.mag_filter == xenos::TextureFilter::kLinear ? 4 : 0) |
                         (fetch.clamp_x == xenos::ClampMode::kRepeat ? 2 : 0) | (fetch.clamp_y == xenos::ClampMode::kRepeat ? 1 : 0);
      DkResHandle handle = dkMakeTextureHandle(slot, sampler);
      dkCmdBufBindTextures(cmd, DkStage_Fragment, tb.fetch_constant, &handle, 1);
    }

    // Uniforms: the ALU constants and the system constants, per stage.
    if (!BindUniforms(f, vs, ps, vi, regs)) { skipped_++; return true; }

    if (index_buffer_info) {
      dkCmdBufBindIdxBuffer(cmd, index_buffer_info->format == xenos::IndexFormat::kInt32 ? DkIdxFormat_Uint32 : DkIdxFormat_Uint16,
                            f.data.gpu + index_offset);
      dkCmdBufDrawIndexed(cmd, prim, index_count, 1, 0, 0, 0);
    } else {
      dkCmdBufDraw(cmd, prim, index_count, 1, 0, 0);
    }
    draws_++;
    frame_draws_++;
    return true;
  }

  bool IssueCopy() override { return Resolve(); }

  void IssueSwap(uint32_t frontbuffer_ptr, uint32_t frontbuffer_width, uint32_t frontbuffer_height) override {
    Frame& f = frames_[frame_];
    DkCmdBuf cmd = f.cmdbuf;
    HostImage* front = nullptr;
    if (guest_images_.count(frontbuffer_ptr) && frontbuffer_width && frontbuffer_height) {
      // The resolve created it with the aligned height: the first swap trims it to the front buffer's size.
      front = GuestImage(f, frontbuffer_ptr, frontbuffer_width, frontbuffer_height, DkImageFormat_RGBA8_Unorm, false);
    }
    if (present_target_) {
      // Debug (the file sdmc:/switch/hcu/dk_present_rt): show the widest colour render target itself,
      // bypassing the resolve copies and the front buffer image.
      HostImage* widest = nullptr;
      for (auto& it : targets_) if (!it.second->is_depth && (!widest || it.second->width > widest->width)) widest = it.second;
      if (widest) front = widest;
    }
    // Debug readback at two swaps: what the images hold (uniform colour = a broken stage).
    std::vector<std::pair<std::string, uint32_t>> dumps;
    if (dump_images_ && (swaps_ + 1 == 120 || swaps_ + 1 == 600)) {
      auto queue_dump = [&](const char* what, HostImage* img) {
        if (!img) return;
        uint32_t bytes = img->width * img->height * 4;
        uint32_t off = f.staging.Alloc(bytes, 0x1000);
        if (off == UINT32_MAX) return;
        DkImageView v;
        dkImageViewDefaults(&v, &img->image);
        DkImageRect r{0, 0, 0, img->width, img->height, 1};
        DkCopyBuf cb{f.staging.gpu + off, 0, 0};
        dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image);
        dkCmdBufCopyImageToBuffer(cmd, &v, &r, &cb, 0);
        char name[96];
        std::snprintf(name, sizeof(name), "%s %ux%u", what, img->width, img->height);
        dumps.emplace_back(name, off);
      };
      queue_dump("front", front);
      for (auto& it : targets_) if (!it.second->is_depth) { char n[48]; std::snprintf(n, sizeof(n), "target base %u pitch %u", it.second->edram_base, it.second->edram_pitch); queue_dump(n, it.second); }
      for (auto& it : guest_images_) if (it.second->width >= 600 && it.second != front) { char n[48]; std::snprintf(n, sizeof(n), "guest %08X%s", it.first, it.second->from_resolve ? " (resolve)" : " (upload)"); queue_dump(n, it.second); }
      dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
    }
    if (swaps_ < 5) Log("swap #%u: acquiring a swapchain image", swaps_ + 1);
    int slot = dkQueueAcquireImage(queue_, swapchain_);
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
      dkCmdBufBindShaders(cmd, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
      dkCmdBufBindVtxAttribState(cmd, nullptr, 0);
      dkCmdBufBindVtxBufferState(cmd, nullptr, 0);
      uint32_t dslot = DescriptorFor(f, front, 0);
      if (dslot != UINT32_MAX) {
        DkResHandle handle = dkMakeTextureHandle(dslot, 4);  // linear, clamp
        dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &handle, 1);
        dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
      }
    }
    dkCmdBufBarrier(cmd, DkBarrier_Full, 0);
    DkCmdList list = dkCmdBufFinishList(cmd);
    dkQueueSubmitCommands(queue_, list);
    dkQueueSignalFence(queue_, &f.fence, false);
    f.fenced = true;
    dkQueuePresentImage(queue_, swapchain_, slot);
    if (!dumps.empty()) {
      dkQueueWaitIdle(queue_);
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
    swaps_++;
    if (swaps_ <= 3 || swaps_ % 60 == 0) {
      std::printf("[nx] gpu: swap #%u\n", swaps_);  // the Eden gate's frame counter
      Log("swap #%u: front %08X %ux%u %s; frame: %u draws, %u clears, %u resolves, %u skipped, %u no-shader; data %u KB staging %u KB; images %u (%u MB)",
          swaps_, frontbuffer_ptr, frontbuffer_width, frontbuffer_height, front ? "shown" : "MISSING", frame_draws_,
          frame_clears_, frame_resolves_, frame_skipped_, frame_missing_, f.data.used >> 10, f.staging.used >> 10, image_count_,
          images_.used >> 20);
    }
    frame_draws_ = frame_clears_ = frame_resolves_ = frame_skipped_ = frame_missing_ = 0;
    frame_ = (frame_ + 1) % kFrames;
    BeginFrame();
  }

 private:
  // ----- frames ---------------------------------------------------------------------------------
  void BeginFrame() {
    Frame& f = frames_[frame_];
    if (f.fenced) {
      if (swaps_ < 6) Log("frame %u: waiting for its fence", frame_);
      DkResult r = dkFenceWait(&f.fence, 2000000000ll);  // 2 s: a hang is logged, not silent
      if (r != DkResult_Success) Log("frame %u: fence wait result %d (GPU hang?)", frame_, int(r));
      if (swaps_ < 6) Log("frame %u: fence done", frame_);
      f.fenced = false;
    }
    f.data.Reset();
    f.staging.Reset();
    f.next_descriptor = 0;
    f.descriptors.clear();
    dkCmdBufClear(f.cmdbuf);
    dkCmdBufAddMemory(f.cmdbuf, cmdmem_.block, f.cmd_offset, kFrameCmdBytes);
    dkCmdBufBindImageDescriptorSet(f.cmdbuf, descriptors_.gpu + f.desc_offset, kMaxImageDescriptors);
    dkCmdBufBindSamplerDescriptorSet(f.cmdbuf, descriptors_.gpu + sampler_desc_offset_, kSamplerCount);
    bound_color_ = nullptr;
    bound_depth_ = nullptr;
  }

  // ----- images ---------------------------------------------------------------------------------
  HostImage* CreateImage(uint32_t width, uint32_t height, DkImageFormat format, uint32_t flags, bool is_depth) {
    auto* img = new HostImage();
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
  HostImage* GuestImage(Frame& f, uint32_t base, uint32_t width, uint32_t height, DkImageFormat format, bool at_least,
                        uint32_t create_w = 0, uint32_t create_h = 0) {
    auto it = guest_images_.find(base);
    if (it != guest_images_.end()) {
      HostImage* img = it->second;
      if (img->width == width && img->height == height) return img;
      if (at_least && width <= img->width && height <= img->height) return img;
      if (at_least) {
        width = std::max(width, img->width);
        height = std::max(height, img->height);
      }
      HostImage* resized = CreateImage(width, height, img->format, DkImageFlags_UsageRender, false);
      if (!resized) return img;
      uint32_t cw = std::min(width, img->width), ch = std::min(height, img->height);
      DkImageView sv, dv;
      dkImageViewDefaults(&sv, &img->image);
      dkImageViewDefaults(&dv, &resized->image);
      DkImageRect sr{0, 0, 0, cw, ch, 1}, dr{0, 0, 0, cw, ch, 1};
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      dkCmdBufCopyImage(f.cmdbuf, &sv, &sr, &dv, &dr, 0);
      dkCmdBufBarrier(f.cmdbuf, DkBarrier_Full, DkInvalidateFlags_Image);
      resized->from_resolve = img->from_resolve;
      resized->uploaded = img->uploaded;
      resized->guest_base = base;
      Log("image at %08X resized %ux%u -> %ux%u", base, img->width, img->height, width, height);
      it->second = resized;  // the old image's memory stays allocated (bump pool)
      return resized;
    }
    HostImage* img = CreateImage(std::max(width, create_w), std::max(height, create_h), format,
                                 DkImageFlags_UsageRender, false);
    if (!img) return nullptr;
    img->guest_base = base;
    guest_images_.emplace(base, img);
    return img;
  }

  HostImage* TextureImage(Frame& f, const TextureInfo& info, const xenos::xe_gpu_texture_fetch_t& fetch) {
    DkImageFormat format;
    if (!TextureFormatOf(info.format, format)) {
      LogOnce("tfmt", "unsupported texture format %s", info.format_info()->name);
      return nullptr;
    }
    uint32_t base = info.memory.base_address;
    // TextureInfo keeps the fetch constant's fields: the size minus one.
    HostImage* img = GuestImage(f, base, info.width + 1, info.height + 1, format, false);
    if (!img) return nullptr;
    if (img->from_resolve || img->uploaded) return img;
    if (!UploadTexture(f, *img, info, fetch.endianness)) return nullptr;
    img->uploaded = true;
    return img;
  }

  bool UploadTexture(Frame& f, HostImage& img, const TextureInfo& info, xenos::Endian endian) {
    const FormatInfo* fi = info.format_info();
    const uint32_t width = info.width + 1, height = info.height + 1;  // TextureInfo keeps size minus one
    uint32_t bw = (width + fi->block_width - 1) / fi->block_width;
    uint32_t bh = (height + fi->block_height - 1) / fi->block_height;
    uint32_t bpb = fi->bytes_per_block();
    uint32_t bytes = bw * bh * bpb;
    uint32_t off = f.staging.Alloc(bytes, 0x100);
    if (off == UINT32_MAX) { LogOnce("staging", "staging ring full: %u KB texture not uploaded", bytes >> 10); return false; }
    uint8_t* dst = f.staging.cpu + off;
    const uint8_t* src = memory_->TranslatePhysical(info.memory.base_address);
    if (info.is_tiled) {
      texture_conversion::UntileInfo u{};
      u.offset_x = 0;
      u.offset_y = 0;
      u.width = bw;
      u.height = bh;
      u.input_pitch = info.pitch;
      u.output_pitch = bw;
      u.input_format_info = fi;
      u.output_format_info = fi;
      u.copy_callback = [endian](void* o, const void* i, size_t n) { texture_conversion::CopySwapBlock(endian, o, i, n); };
      texture_conversion::Untile(dst, src, &u);
    } else {
      uint32_t row = bw * bpb;
      uint32_t src_row = info.pitch * bpb;
      for (uint32_t y = 0; y < bh; y++) texture_conversion::CopySwapBlock(endian, dst + y * row, src + y * src_row, row);
    }
    DkCopyBuf cb{f.staging.gpu + off, 0, 0};
    DkImageView view;
    dkImageViewDefaults(&view, &img.image);
    DkImageRect rect{0, 0, 0, width, height, 1};
    dkCmdBufCopyBufferToImage(f.cmdbuf, &cb, &view, &rect, 0);
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
    std::memcpy(f.data.cpu + vs_off, &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X], 4096);
    std::memcpy(f.data.cpu + ps_off, &regs.values[XE_GPU_REG_SHADER_CONSTANT_256_X], 4096);
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
    std::memcpy(f.data.cpu + sys_off, &sys, sizeof(sys));
    DkBufExtents vs_bufs[2] = {{f.data.gpu + vs_off, 4096}, {f.data.gpu + sys_off, sizeof(SysConstants)}};
    DkBufExtents ps_bufs[2] = {{f.data.gpu + ps_off, 4096}, {f.data.gpu + sys_off, sizeof(SysConstants)}};
    dkCmdBufBindUniformBuffers(cmd, DkStage_Vertex, 0, vs_bufs, 2);
    dkCmdBufBindUniformBuffers(cmd, DkStage_Fragment, 0, ps_bufs, 2);
    (void)vs;
    (void)ps;
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
        depth_img = TargetImage(depth_info.depth_base, pitch, DkImageFormat_Z24S8, true);
      } else if (mode == xenos::EdramMode::kDepthOnly) {
        // Depth-only through the alias: the XDK fills a color surface's EDRAM this way too.
        auto it = targets_.find((uint64_t(depth_info.depth_base) << 32) | (uint64_t(pitch) << 1));
        if (it != targets_.end()) color_img = it->second;
        else depth_img = TargetImage(depth_info.depth_base, pitch, DkImageFormat_Z24S8, true);
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
      if (clears_ < 8) Log("clear %s%s at EDRAM %u/%u pitch %u: rect %u,%u %ux%u color %g %g %g %g z %g (alias x%u)",
                           color_img ? "color" : "", depth_img ? " depth" : "", color_info.color_base, depth_info.depth_base, pitch,
                           sc.x, sc.y, sc.width, sc.height, color[0], color[1], color[2], color[3], z, scale);
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
    if (info.IsCopyingDepth()) {
      LogOnce("depthresolve", "depth resolve to %08X ignored", info.copy_dest_base);
      return true;
    }
    uint32_t src_pitch = info.color_edram_info.pitch_tiles * 80;
    HostImage* src = TargetImage(info.color_edram_info.base_tiles, src_pitch, DkImageFormat_RGBA8_Unorm, false);
    if (!src) return true;
    uint32_t x = info.coordinate_info.edram_offset_x_div_8 * 8, y = info.coordinate_info.edram_offset_y_div_8 * 8;
    uint32_t w = info.coordinate_info.width_div_8 * 8, h = info.height_div_8 * 8;
    uint32_t dest_pitch = info.copy_dest_coordinate_info.pitch_aligned_div_32 * 32;
    uint32_t dest_height = info.copy_dest_coordinate_info.height_aligned_div_32 * 32;
    uint32_t dx = info.copy_dest_coordinate_info.offset_x_div_8 * 8, dy = info.copy_dest_coordinate_info.offset_y_div_8 * 8;
    HostImage* dst = GuestImage(f, info.copy_dest_base, dx + w, dy + h, DkImageFormat_RGBA8_Unorm, true, dest_pitch, dest_height);
    if (!dst) return true;
    dst->from_resolve = true;
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
    if (resolves_ < 8) Log("resolve EDRAM %u pitch %u rect %u,%u %ux%u -> %08X (%ux%u image) at %u,%u%s", info.color_edram_info.base_tiles,
                           src_pitch, x, y, w, h, info.copy_dest_base, dst->width, dst->height, dx, dy,
                           info.IsClearingColor() ? ", then cleared" : "");
    resolves_++;
    frame_resolves_++;
    return true;
  }

  // ----- shaders --------------------------------------------------------------------------------
  bool LoadDksh(const std::string& path, DkShader& out) {
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
  }

  void LogOnce(const char* key, const char* fmt, ...) {
    if (!logged_.insert(key).second) return;
    va_list ap;
    va_start(ap, fmt);
    std::printf("[dk] ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
    std::fflush(stdout);
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
  bool dump_images_ = false;
  std::unordered_map<uint64_t, DkShaderObj*> shaders_;
  std::unordered_map<uint64_t, HostImage*> targets_;       // EDRAM base/pitch -> render target
  std::unordered_map<uint32_t, HostImage*> guest_images_;  // guest address -> texture / resolve destination
  HostImage* bound_color_ = nullptr;
  HostImage* bound_depth_ = nullptr;
  std::unordered_set<std::string> logged_;
  uint32_t swaps_ = 0, draws_ = 0, skipped_ = 0, missing_draws_ = 0, missing_shaders_ = 0, clears_ = 0, resolves_ = 0;
  uint32_t image_count_ = 0, uploads_ = 0;
  uint64_t upload_bytes_ = 0;
  uint32_t frame_draws_ = 0, frame_clears_ = 0, frame_resolves_ = 0, frame_skipped_ = 0, frame_missing_ = 0;
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
#endif  // __SWITCH__
