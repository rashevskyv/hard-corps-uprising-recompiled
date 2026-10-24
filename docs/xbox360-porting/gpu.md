# GPU (Xenos): strategies, shaders, formats, pitfalls

Tags: [CODE] read in source, [DOC] README/doc only, [GUESS] inference. Full working notes: `x360/notes_gpu.md` (temp, may be gone).

## 1. Two strategies

| | A. Emulate Xenos (ReXGlue `xenos` plugin) | B. Hook the game's D3D calls, draw natively |
|---|---|---|
| Projects | ReXGlue SDK, stuntdouble, AC6, CVHD, Xerenge "xenos" mode | UnleashedRecomp (42 hooked D3D functions, plume), Rayman Origins native renderer, Nocturne headless bridge, Xerenge plume, DR2 |
| Needs | PM4 decode, EDRAM model, resolve, RT cache, texture cache (compute untile), runtime ucode→SPIR-V, 512 MB shared-memory mirror with write tracking | Per-game hook addresses, AOT shaders (XenosRecomp), register shadow read from the D3D device struct, host RTs keyed by guest address |
| Accuracy | High (Xenia-derived) | Per game; no fallback |
| Cost on weak hosts | High. Rayman on M1: 30-55 fps, stalls 100-620 ms from runtime pipeline creation; CrownPark measured ~37 fps with GPU thread at 100% | Low. Rayman: 59 fps M1, 60 fps Galaxy S23, worst frame ~18 ms [DOC] |
| Bring-up | Days (boots almost any title) | Weeks per title |

Rule: emulate first to get a picture, go native only if the profile shows the GPU thread is the limit. CVHD's GPU thread is idle (1-5%), so for now this is not the bottleneck.
Keep the ring-buffer/fence/vblank protocol alive with a stub GPU so the game never blocks (Rayman `rexgpu-null`, Nocturne `HeadlessGpuBridge`, CVHD null backend).

ReXGlue pieces [CODE `rexglue-sdk/src/graphics/`]: `command_processor.cpp`, `register_file.cpp`, `primitive_processor.cpp`, `render_target_cache.cpp`, `texture_cache.cpp`, `shared_memory.cpp`, `pipeline_cache.cpp`, `pipeline/shader/spirv_translator*.cpp` (+ DXBC + interpreter), memexport implemented in both translators. Vulkan device needs `fragmentStoresAndAtomics`, `geometryShader` (cvar `vulkan_require_geometry_shader`, off on macOS), Vulkan 1.1+.

## 2. Shaders

- XenosRecomp (hedge-dev) [DOC]: ucode → HLSL → DXIL/SPIR-V ahead of time, cache keyed by 64-bit XXH3 of the microcode (key must match microcode LENGTH exactly: DR2 gotcha 115). VS constants 4096 B, PS 3584 B. Not implemented: memexport, point size, dynamic register indexing, integer constants, mini vertex fetch, 1D textures. "Do not expect it to work out of the box."
- Vulkan path of XenosRecomp uses push constants + buffer device address and int64. Mobile drivers lack them: Rayman and Nocturne converted to UBOs (`hlsl_ubo.py`) [DOC]. NVK on Switch has BDA (1.4) [CVHD], so the stock path can work there.
- Known shader math bug: `setp_inv`, relative constant addressing (Rayman frieze strips) [DOC].
- Vertex data is byte-swapped as 32-bit words, which scrambles 16-bit fields (fix: YXWZ order bits).
- ReXGlue translates at runtime: first use of each shader costs CPU. NVK builds pipelines in ~1 s for 12 [CVHD]; so the pipeline cost is not the first suspect there.
- Keep a disk `VkPipelineCache` and a list of used pipelines; pre-create at start (Rayman) or ship a seed (CrownPark).

## 3. Formats, tiling, endian

- Texture formats seen in a whole 2D game bundle: DXT1, DXT2/3, DXT4/5, 8888, plus 8-bit planes for movies (Rayman, 8,358 textures) [DOC]. ReXGlue table `pipeline/texture/info_formats.cpp`.
- Tiled layout (32x32 blocks, packed mips) and big-endian words: ReXGlue untiles in compute shaders (`texture_load_*_cs`); a CPU untiler with per-run offsets ran a 1280x720 k_8 plane in 0.7 ms (8.5 before) [DOC Rayman].
- `k_8` as `R8_UNORM` with RRRR swizzle: a quarter of the bytes of RGBA8.
- Quad lists → triangles; rect lists → 2 triangles (geometry shader or VS expansion); point sprites need index expansion (ReXGlue `primitive_processor.cpp`). XenonRecomp-style hosts expand quads on the CPU (6 indices per quad).
- Alpha test is fixed-function on Xenos: emulate in the pixel shader (spec constant).

## 4. EDRAM, resolve, render targets

- EDRAM is 10 MB. 1280x720 colour+depth does not fit: games tile (DR2 Case Zero renders in two 640-wide tiles; 930 wasted draws before that was known) [DOC].
- A resolve copies EDRAM → memory; treat it as a texture keyed by destination address (DR2 gotchas 112-114, 119: region = `PA_SC_WINDOW_SCISSOR`, not surface; do not cache on the fetch constant alone). Rayman water refraction was black until the resolve address was right.
- ReXGlue render target paths: Vulkan `fbo` (host FBs, default) vs `fsi` (fragment shader interlock, "much slower"; CrownPark: FSI was 25% of an MCLA frame, but Split/Second and Ridge Racer Unbounded need it for the colour grade). D3D12: ROV vs host RTs.
- Native ports skip EDRAM: host RTs by guest address.
- memexport: ReXGlue implements it; native ports so far did not need it.

## 5. 2D XBLA checklist

1. Quad/rect/point → triangles. 2. DXT + 8888 + 8-bit + 16-bit textures. 3. Resolve-as-texture for glow/blur/refraction. 4. Alpha test in PS and exact blend state. 5. Viewport/scissor from registers. 6. Fill rate: many alpha layers; measure GPU ms with timestamp queries, not only fps. 7. Movies (Bink or WMV) as updating 8-bit planes. 8. Pre-built shader/pipeline cache. 9. 2-3 frames in flight, no fence wait at frame end.

## 6. Shared memory and write tracking (relevant for Switch)

ReXGlue mirrors 512 MB of guest RAM in a Vulkan buffer (`shared_memory.cpp`, `kBufferSizeLog2=29`), tracks one valid bit per host page, and arms write watches by page protection plus a fault handler. Horizon cannot re-protect pages, so CVHD marks dirty 4 KB pages in the store macros. CrownPark's fork shows how expensive the tracking is and how to cut it: lock-free page-validity bitmap (45 → 3.7 µs/check), batched deferred uploads, hot-page blocks re-uploaded once per frame, invalidation widening bounds (1.7 MB/s of guest writes had become 110 MB/s of uploads). Nocturne reads vertex data by raw SSBO loads from a 512 MB host-visible mirror.
