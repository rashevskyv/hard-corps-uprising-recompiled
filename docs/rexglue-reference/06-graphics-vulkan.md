# 06 Graphics, Vulkan and D3D12

What the port does on the graphics side, with correctness fixes kept apart from visual options and from performance
work, and ReXGlue SDK fixes kept apart from Hard Corps game code. Patch inventory: [01-rexglue-core.md](01-rexglue-core.md).
Performance patches are only referenced here; they are explained in [12-performance.md](12-performance.md). Frame
pacing and window behavior: [07](07-display-window-frame-pacing.md). Platform specifics: [04](04-platform-windows.md),
[05](05-platform-linux.md).

## Backends

- ReXGlue's Xenos GPU plugin is a separate library (`rexgpu-xenos.dll`, `librexgpu-xenos.so`), selected in the
  executable by `rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)` and `gpu_plugin = "xenos"` in
  `config/hard_corps_uprising.toml`. A load failure is fatal. Windows uses the D3D12 backend, Linux the Vulkan one.
- The game is built against stock SDK headers, so a patch may not move virtual functions or data members the game
  code uses (`docs/rexglue-runtime-patch.md`). Patch 0029 adds `Window::GetDisplayRefreshRate()` as the last virtual
  for that reason.
- Every backend-specific patch has a twin: see "Windows versus Linux" below.
- The Vulkan render target path in use is host render targets (`Path::kHostRenderTargets`), the same ownership-transfer
  design as D3D12 (patch 0017 text).

## Correctness fixes

### 0034 SPIR-V texture exponent bias (GENERIC_REXGLUE, PLATFORM_LINUX, CONFIRMED in code)

- **Problem and symptom.** On Vulkan, texture-fetch results were scaled by a wrong power of two wherever the fetch's
  exponent bias and LOD bias differ: darker images and lost glow. Windows/D3D12 was not affected.
- **Root cause.** The fetch constant's `exp_adjust` is a signed 6-bit field at bit 13 of word 3. The SPIR-V translator
  (`src/graphics/pipeline/shader/spirv_translator_fetch.cpp`, `ProcessTextureFetchInstruction`) read bits 13:18 of word 4,
  which hold the LOD bias. The DXBC translator already reads word 3, and the Windows build does not compile the SPIR-V
  translator.
- **Solution.** Load word 3 for the bias and leave the LOD bias on word 4. File `0034-spirv-texture-exp-adjust.linux.patch`.
  Origin `430fc32`. Shipped in v0.5.0.
- **Port.** Copy as is if the pinned SDK still has the bug; relevant to any ReXGlue game on Vulkan that uses exponent bias.
  Verify by comparing D3D12 and Vulkan brightness of a glowing scene, or by RenderDoc inspection of a fetch constant.
- **Validation.** Code-level only. The v0.5.0 notes say the fix "has not been confirmed on a real Linux PC or Steam
  Deck yet", so effect on hardware is UNCERTAIN. No automated test.

### 0035 Cache the swapchain paint pipeline (GENERIC_REXGLUE, PLATFORM_LINUX, CONFIRMED in code; performance, not correctness)

- **Problem.** `VulkanPresenter::PaintAndPresentImpl` compared the swapchain format its final paint pipeline was created
  for but never stored it after creating the pipeline, so every frame looked like a format change: wait for the previous
  frame on the GPU, destroy, recreate (ReXGlue issue #507).
- **Solution.** One added assignment, `swapchain_effect_pipeline.swapchain_format = paint_context_.swapchain_render_pass_format`
  after successful creation (`src/ui/vulkan/vulkan_presenter.cpp`). A real format change still recreates once.
  Origin `3b4a229`.
- **Port.** High value for Vulkan projects, not to be copied automatically: first check whether the pinned SDK still has
  upstream issue 507 (inspect `VulkanPresenter::PaintAndPresentImpl`), and carry the patch only if it does. D3D12's
  presenter has no such cache. Verify that the per-frame GPU wait and pipeline rebuild are gone, in a profiler or by frame
  time on an otherwise light scene. No automated test.

### 0010 RenderDoc detection (GENERIC_REXGLUE, CONFIRMED by test on Linux)

Loading `librenderdoc.so` through `dlopen` activates RenderDoc, so a lookup meant to find an attached RenderDoc turned
it on. Full account, test and packaging guard in [05](05-platform-linux.md#renderdoc). Origin `3020c32`.
Relevant to any project that ships the runtime; the Windows branch (`GetModuleHandleExW`) is in the patch but has no
Windows test.

### Resolve copies into textures: 0022 to 0024 and 0030 to 0032 (GENERIC_REXGLUE with game-dependent payoff)

These are performance changes with correctness conditions, so they live in [12](12-performance.md). What matters here:
the copy is made only when it gives the same texels as the normal load (single render target owner, matching pitch and
format family, `k_8_8_8_8` or `k_2_10_10_10`, single-level tiled 2D texture, up to date), the red/blue swap of such a
resolve is undone in every view of the texture, and `resolve_copy_into_textures` turns the copy off. `resolve_defer_writeback`
(0030 to 0032) skips the dump and scaled resolve write only with resolution scaling and only while the resolve repeats.
D3D12 halves: 0023, 0031. Vulkan halves: 0024, 0032. Origin `cf6eee1`. A black or stale texture after enabling them is
the first thing to suspect when porting to a game with different resolve patterns; the cvar is the A/B switch.

## Adapter and device selection

Both are for machines with more than one GPU. Origin `cf6eee1`; both GENERIC_REXGLUE, CONFIRMED in code.

- **Vulkan, 0026** (`0026-vulkan-device-preference.patch`, `src/ui/vulkan/vulkan_provider.cpp`; applies to both
  platforms' sources but only matters where the Vulkan provider runs). Stock auto-selection scored devices by feature
  support and took the first of the equals, which is the integrated GPU on many laptops. The patch multiplies the
  feature score by 4 and adds a device-type rank below it (discrete 3, integrated 2, virtual 1, other 0), so a discrete
  GPU wins only among equally capable devices. It skips the preference when `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`,
  `VK_LOADER_DEVICE_SELECT` or `__NV_PRIME_RENDER_OFFLOAD` is set, since the loader or Mesa layer already put the
  chosen device first. It logs `Vulkan device: <name> (type N)`. The cvar `vulkan_device` still selects by index.
- **D3D12, 0027**: enumeration by `EnumAdapterByGpuPreference` (high performance, or minimum power from the Windows
  per-app graphics setting); `d3d12_adapter` still selects by index. Details and the registry value in
  [04](04-platform-windows.md). The executable additionally exports the Optimus/PowerXpress symbols.
- **Validation.** No hybrid-GPU run is recorded in the repo for either path. Pending.
- **Pitfall.** On Windows the preference applies only while `d3d12_adapter` is `-1`; on Vulkan the discrete preference
  is part of scoring, so a device that lacks a preferred feature still loses to an integrated one that has it.

## Multisampling (MSAA)

- **Fact (GENERIC_REXGLUE, CONFIRMED).** Turning `native_2x_msaa` off does not disable multisampling: the guest's 2x
  MSAA render targets get host 4x MSAA (top-left and bottom-right samples of 4x). Patch 0028 only adds a log line to both
  render target caches saying which host sample count 2x targets get and why (either "host 2x MSAA", or "host 4x MSAA,
  ... native_2x_msaa is off" / "2x MSAA is unsupported for a needed format"). Vulkan logs it only on the host render
  target path. Origin `cf6eee1`.
- **Mirror in the game.** `src/multisampling.h` holds `Native2xMsaaFor(compatibility)` (= not compatibility) and
  `HostMsaaSamples(guest_samples, native_2x_msaa, host_2x_supported)`, which mirrors the render target caches' rule:
  only a guest 2x target is promoted, to 4x, when native 2x is off or the host cannot do it. It exists so tests can pin
  the rule without a GPU.
- **Launcher bug it explains (superseded approach).** Earlier launchers labelled `native_2x_msaa = false` as "Off",
  which actually selected the heavier 4x path. v0.5.0 replaced it with two choices: "2x MSAA" (default) and "4x MSAA
  (Compatibility)", stored as the project cvar `hcu_msaa_compatibility`. `ApplyMultisampling()` (settings.cpp) writes
  `native_2x_msaa` from it at every start and replaces a stale saved value; it runs in `OnFinalizePaths` before the GPU
  starts (also on the no-launcher path). The GPU reads it at start only, so changing it applies on Play.
- **Game-specific.** That the game draws any 2x MSAA render target is taken from the project's launcher text; the
  compatibility choice is documented as for "GPUs or drivers whose 2x MSAA misrenders" (`docs/launcher.md`), but no
  misrendering driver is named in the repo (UNCERTAIN).
- **Tests (automated).** `tests/multisampling_tests.cpp` (target `hcu_multisampling_tests`): host samples for every
  guest count and launcher choice, that no choice draws fewer samples than the game asks for, and source checks of the
  cvar definition, the settings key list, the launcher row and the startup call.
- **Port lesson.** Read the SDK's render target cache before naming an option "Off".

## Visual settings (not correctness)

All are ReXGlue cvars surfaced in the launcher; the defaults equal what the game used before the launcher existed
(`docs/launcher.md`). Class GENERIC_REXGLUE mechanics, GAME_ADAPTATION for the labels.

| Option | Cvar | Notes |
| --- | --- | --- |
| Render resolution 1x to 4x | `resolution_scale` | The launcher labels (1280x720, 2560x1440, 3840x2160, 5120x2880) are fixed and come from measured presented output, independent of the guest video mode; the driver may lower a scale it cannot support and the game logs a warning. Applied at GPU start, not a relaunch trigger |
| Internal render resolution | `hcu_internal_render_scale` = 100, 75, 50 | Percent of the render resolution the host render targets are drawn at (SDK patches 0036 to 0038); see [rexglue-runtime-patch.md](../rexglue-runtime-patch.md). Applied at GPU start |
| Anti-aliasing | `swap_post_effect` = `none`, `fxaa`, `fxaa_extreme` | SDK post effect on the presented image |
| Texture filtering | `anisotropic_override` = -1 (game), 0, 2, 3, 4, 5 | launcher default `3` |
| Aspect ratio | `hcu_aspect_ratio` | 16:9, 16:10, 21:9, 32:9, 4:3 or 5:4; the presenter always letterboxes the chosen format ([07](07-display-window-frame-pacing.md)) |
| VSync | Windows: `d3d12_allow_variable_refresh_rate_and_tearing` (inverted); Linux: `vulkan_allow_present_mode_immediate` | selected by `kTearingSetting` in `src/settings.h`. VSync controls tearing only; on Linux Off allows an immediate present mode, On uses mailbox or FIFO. Guest `vsync` is untouched |
| Multisampling | `hcu_msaa_compatibility` | see above |

There is no bloom, color grading or other post-processing code in the repository beyond the SDK's FXAA.

Guest video mode is pinned at 853x480 (`video_mode_width/height`) independent of the window; Xenos presents at a
measured 1280x720 base. Both are game choices, HARD_CORPS_SPECIFIC, and a port should measure its own.

## Shaders, pipeline cache and shader preparation

- Shaders are translated at runtime by the Xenos plugin; there is no build step (`docs/first-boot.md`). The cache lives
  in `user_data/cache` ([10](10-user-data-settings-migration.md)); the first Xenos run on Windows logged shaders and
  pipelines "created from storage".
- Compilation is ReXGlue's default, `async_shader_compilation = true`, and the launcher deliberately exposes no shader
  option (`docs/launcher.md`).
- Branch `feature/shader-preparation` is merged (its tip `a0189f4` is the v0.5.0 preparation commit) and carries no
  commit of its own, so the repo holds no record of what the investigation concluded beyond the launcher decision above.
  Any claim about shader stutter or cache warmup for this game is UNCERTAIN.
- Vulkan pipeline objects other than 0035's presenter pipeline are untouched; no pipeline-cache patch exists.
- Debug branches `debug/full-gameplay-telemetry` and `investigate/controller-rumble` are not merged and are
  branch-only experimental diagnostics (render and window logging, a rumble probe), not HEAD functionality.

## Synchronization and presentation (graphics-side view)

- **0025 `WAIT_REG_MEM` vblank wake** (common): the game's present makes the GPU wait for a word its vblank interrupt
  clears; the command processor now waits on an event the vblank thread sets instead of polling in 1 ms sleeps. A signal
  set after the value was read stays pending. Origin `cf6eee1`.
- **0029 UI repaint with guest frames** (common): while any ImGui dialog exists the presenter painted from the UI thread
  after every frame and presented the same guest frame twice (about 120 presents per second for 60 guest frames; on Linux
  as fast as the GPU allowed). A UI-only repaint now waits up to 50 ms for the next guest frame. Frame-pacing side in [07](07-display-window-frame-pacing.md).
- **0009 fence wake**, **0011/0016 submission limit and deferred occlusion queries**: performance, see [12](12-performance.md).
- Occlusion queries in D3D12 and Vulkan are written back after the submission that holds them completes; the guest sees
  the query unfinished meanwhile, and a submission is never ended in the middle of a query because that turned host
  queries off for the session (`docs/rexglue-runtime-patch.md`, 0011/0016).
- **Present mode.** The D3D12 presenter always presents with sync interval 0 and uses the tearing flag for VSync Off
  (`docs/launcher.md`); frame pacing comes from the guest.
- **Stencil.** Without `VK_EXT_shader_stencil_export` (NVIDIA) the Vulkan transfers copy stencil one bit at a time;
  0017 tracks bits per tile to limit that. AMD, Intel and the Steam Deck have the extension, per the patch notes.

## Windows versus Linux

| Concern | Windows (D3D12) | Linux (Vulkan) |
| --- | --- | --- |
| Backend | D3D12 | Vulkan, host render targets |
| Texture fetch exponent bias | correct in DXBC | fixed by 0034 |
| Presenter pipeline cache | none needed | 0035 |
| GPU choice | 0027 + Optimus/PowerXpress exports | 0026 + env variables |
| Submissions/occlusion | 0011 | 0016 |
| Transfers, stencil bits | 0012, 0013 | 0017 |
| Dirty-tile EDRAM dumps | 0014 | 0018 |
| Resolve copy | 0023 (`CopyTextureRegion`) | 0024 (`vkCmdCopyImage`) |
| Deferred write-back | 0031 | 0032 |
| VSync cvar | `d3d12_allow_variable_refresh_rate_and_tearing` | `vulkan_allow_present_mode_immediate` |
| Vertical-blank tick for UI repaint | exists (DXGI tick thread) | none; display refresh interval is used (0029) |
| Failure reporting | log only | Linux error window (SDL surface) if setup fails ([05](05-platform-linux.md)) |

## Overlays and capture tools

- RenderDoc: 0010 and the AppDir guard ([05](05-platform-linux.md)).
- No RTSS, ReShade or other injection investigation exists in the repo; compatibility is UNCERTAIN
  ([04](04-platform-windows.md)).
- The port's own overlays (achievements, cheat menu, frame counter, launcher) are ImGui dialogs rendered by the
  presenter inside the same pass; they are why 0029 matters.

## ReXGlue fixes versus Hard Corps hooks

ReXGlue fixes (reusable): 0010, 0026, 0027, 0034, 0035, 0025, 0029, the render-target family and 0028's log.
Hard Corps code touching graphics: only `HostMsaaSamples` mirror and the multisampling launcher mapping
(`src/multisampling.h`, `src/settings.cpp`), the swap hook `sub_825520F8` used for the FPS counter (`src/window_host.cpp`),
the fence-wait hook (`src/gpu_fence_wait.cpp`) and the 853x480 guest video mode. No Hard Corps hook alters what is drawn.

## How to verify

1. Log: `Vulkan device:` line on Linux, the adapter line on Windows, `Guest 2x MSAA render targets:` line on both.
2. Compare a glow-heavy scene on D3D12 and Vulkan (0034).
3. Frame-time graph steady with no GPU wait spike per frame (0035).
4. Toggle `resolve_copy_into_textures` when a texture looks stale or swapped.
5. `hcu_multisampling_tests` passes.
