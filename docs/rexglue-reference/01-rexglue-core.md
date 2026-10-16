# 01 ReXGlue core: SDK patches and app integration

Authoritative patch inventory for this repository. Other reference files cite patches by number and link here instead
of re-explaining them. Per-patch narrative (why, how, what it does not do) also lives in the project's own long-form
document `docs/rexglue-runtime-patch.md`; this file is the porting view and was re-checked against the patch files
themselves, not against their names.

Tags: classification `GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`,
`GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC`; confidence `CONFIRMED` / `LIKELY` / `UNCERTAIN`. Validation is stated per item
as automated (test name), manual (repo evidence), inferred or pending. No benchmark numbers are invented: where the
repository has no measurement the item says "unmeasured".

## 1. Baseline and patch mechanism (summary; build detail in [02-build-system.md](02-build-system.md))

| Piece | File | Role |
| --- | --- | --- |
| Pin | `patches/rexglue-sdk/baseline.env` | SDK version 0.10.0, tag `v0.10.0`, commit `f5337cdc947f...` (full hash in the file), repo URL, and SHA-256 of every stock runtime / GPU plugin file per OS (Release, RelWithDebInfo `rd`). Used to prove an installed file is not the stock one. |
| Patch series | `patches/rexglue-sdk/NNNN-name[.windows\|.linux].patch` | `git apply`-able diffs against the pinned tag. Applied in ordinal name order; later patches are diffed against the result of earlier ones. |
| Naming rule | `tools/rexruntime-patch.ps1` (`$RuntimePatchNamePattern`, `Get-RuntimePatchFiles`) and `tools/build-patched-rexruntime.sh` | Four-digit number, unique per series; a name that does not match, or a repeated number, aborts. Only the current platform's patches enter the fingerprint, so editing a `.linux` patch does not stale the Windows runtime. There is no separate manifest file (the file name is the manifest). |
| Fingerprint and stamp | `tools/rexruntime-patch.ps1` (`Get-RuntimeInputsSha256`, `Write-RuntimeStamp`, `Test-PatchedRuntime`), `rexruntime-patch.txt` beside the SDK | Fingerprint over SDK version/tag/commit, platform, clang and MSVC versions, compiler flags (`-march=x86-64-v2`), targets (`rexruntime`, `rexgpu-xenos`), configurations (RelWithDebInfo, Release) and every patch's name and hash. The stamp stores it plus the SHA-256 of each installed file. The check fails on a stale fingerprint, a file differing from the stamp, or a file equal to the stock hash, and names what changed. |
| Builder | `tools/build-patched-rexruntime.ps1` / `.sh` | Fetches the pinned tag, verifies the commit, applies the patches, builds only the runtime and the Xenos GPU plugin, installs them over the SDK folder, refuses a result equal to stock. Debug files stay stock. |
| Build integration | `tools/build.ps1`, `tools/build.sh`, `tools/package-*`, CMake targets `hcu_rexglue_check` and `hcu_rexglue_staging`, `LINK_DEPENDS` on the SDK libraries in `CMakeLists.txt` | Rebuild when the check fails; check again after staging the DLLs / shared objects next to the executable. |

Classification: the whole mechanism is `REUSABLE_INFRASTRUCTURE` and the most portable part of this repository.
Origin: `ab4f7b7` (v0.3.1: first patch, builders, stamp, stress test), generalised by `0ec7ded` (patch workflow, build.ps1
auto-update), `dc6feb8` (per-platform split), `430fc32` (stronger fingerprint, staged-copy validation, Linux developer
build). Confidence `CONFIRMED` (read in repo). Why it exists: a stale or stock DLL next to the game silently reintroduces
a fixed bug, which is invisible in a log; the stamp turns it into a build failure.

### ABI rule for patches (`GENERIC_REXGLUE`, `CONFIRMED` by the patch comments)

The game and `share/rexglue/rex_app.cpp` are compiled against the stock SDK headers, while the runtime and GPU plugin are
the patched build. A patch must not move virtual functions or data members of classes the game inlines or calls through
(`Window`, public `Presenter`, `KernelState`, `XThread`, ...). Patch 0029 adds `Window::GetDisplayRefreshRate` as the last
virtual, with a comment saying why. 0003 adds the virtual `CommandProcessor::PollBackgroundWork` among the existing virtuals of a
class the game code does not use (not verified beyond a search of `src/`; the rule that matters is "audit every class the
app touches"). Verify by running the unpatched-header
game against the patched library, which is what every build of this repository does.

## 2. Patch inventory (37 patches)

37 patch files exist: numbers 0001 to 0038 with 0020 absent. 0020 never appears in this repository's git history
(`git log --all` over `patches/rexglue-sdk`): it was never committed, so what it contained is `UNCERTAIN`. The only
removed numbered patch in history is `0010-debug-saulo-gpu-thread-stats` (added `e950ca3`, deleted `bbd02f2`, numbering
reused by the RenderDoc patch in `3020c32`), a debug-instrumentation patch; a debug patch is the most likely identity of
the missing 0020 (inferred from that precedent, not proven).

Counts, verified by listing the directory: 37 files = 18 unsuffixed (0001-0010, 0022, 0025, 0026, 0028, 0029, 0030,
0033, 0036) + 9 `.windows` (0011-0015, 0023, 0027, 0031, 0037) + 10 `.linux` (0016-0019, 0021, 0024, 0032, 0034, 0035, 0038). A Windows
build therefore applies 18 + 9 = 27 patches and a Linux build 18 + 10 = 28. Do not add the families by hand elsewhere;
re-derive from `Get-RuntimePatchFiles` (Windows) or `tools/build-patched-rexruntime.sh --list` (Linux) when a count is needed.

Platform column: B both, W Windows (D3D12 / Win32), L Linux (Vulkan / POSIX). Subsystem: RT runtime library
(`rexruntime`), GPU Xenos plugin (`rexgpu-xenos`). "Class" is the classification of the change; performance patches are
`GENERIC_REXGLUE` only in the sense that the SDK code is generic: their benefit was observed with this game, see
[12-performance.md](12-performance.md).

| No. | File | Subsystem | P | Purpose | Class | Deps / notes |
| --- | --- | --- | --- | --- | --- | --- |
| 0001 | `0001-physical-release-order.patch` | RT, `PhysicalHeap::Release` | B | Release the view first, the parent pages last (allocator race) | GENERIC_REXGLUE, correctness | none; test `physical_heap_stress` |
| 0002 | `0002-gpu-register-write-bulk-path.patch` | GPU, command processor | B | Known-register table instead of a switch per write; block copy of render-state runs; 3+ register type-0 packets applied as one range | GENERIC_REXGLUE, perf | none |
| 0003 | `0003-gpu-waits-and-submissions.patch` | GPU, command processor (+header) | B | Idle command thread yields 32 times then sleeps; `WAIT_REG_MEM` re-polls then sleeps at most 1 ms; new virtual `PollBackgroundWork` | GENERIC_REXGLUE, perf | base of 0011, 0016, 0019, 0025 |
| 0004 | `0004-gpu-draw-path.patch` | GPU, shared memory, primitive processor, draw extent | B | Cvar defaults: `execute_unclipped_draw_vs_on_cpu` on, `clear_memory_page_state` off, `primitive_processor_cache_min_indices` 4096; cache-invalidation comparison fix; `RequestRanges` reuses a vector | GENERIC_REXGLUE, perf + one correctness fix | none |
| 0005 | `0005-render-target-transfer-performance.patch` | GPU, common render target cache, draw extent estimator | B | Draw-overwrite proof (`GetDrawOverwrittenRenderTargets`), vertex positions from the estimator | GENERIC_REXGLUE, perf | used by 0012, 0017 |
| 0006 | `0006-cpu-timers-and-waits.patch` | RT, `perf/counter.cpp`, `timer_queue.cpp`, `xboxkrnl_rtl.cpp`; GPU `graphics_system.cpp` | B | Batched perf counter, timer-queue thread blocks on a condition variable, guest critical-section spin, vblank thread waits until due (Win32: above-normal priority) | GENERIC_REXGLUE, perf; mixed library | Windows sleep half is 0015 |
| 0007 | `0007-render-target-stencil-regions.patch` | GPU, render target cache base, estimator | B | Used tile ranges, first row of a draw, so ownership claims skip rows above it | GENERIC_REXGLUE, perf | after 0005; twins 0013 / 0017 |
| 0008 | `0008-edram-dump-dirty-tiles.patch` | GPU, render target cache base | B | One dirty bit per EDRAM tile | GENERIC_REXGLUE, perf | twins 0014 / 0018 |
| 0009 | `0009-guest-fence-wake.patch` | GPU, command processor, new `guest_wake.{h,cpp}` | B | Wake host waiters on a guest word after the command processor writes it (`WakeByAddressAll` / futex) | GENERIC_REXGLUE (wake side) + GAME_ADAPTATION (the waiter, `src/gpu_fence_wait.cpp`) | useless alone; Linux branch added in `dc6feb8` |
| 0010 | `0010-renderdoc-detect-loaded-only.patch` | RT, `dynlib`, `renderdoc_api.cpp` | B | `DynamicLibrary::LoadExisting` (`RTLD_NOLOAD` / `GetModuleHandleExW`); detection never loads RenderDoc | GENERIC_REXGLUE | test `renderdoc_detect_tests` (Linux) |
| 0011 | `0011-d3d12-submissions-and-occlusion.windows.patch` | GPU, D3D12 | W | Submission limit (<3 in flight), deferred occlusion results, resolve preflight skipped | GENERIC_REXGLUE, PLATFORM_WINDOWS | needs 0003 |
| 0012 | `0012-d3d12-render-target-transfers.windows.patch` | GPU, D3D12 | W | Draw-overwrite cutouts and stencil bit mask in D3D12 transfers | PLATFORM_WINDOWS | needs 0005 |
| 0013 | `0013-d3d12-stencil-regions.windows.patch` | GPU, D3D12 | W | Per-tile stencil bits and runs, first-row claims | PLATFORM_WINDOWS | needs 0007, 0012 |
| 0014 | `0014-d3d12-edram-dump-dirty-tiles.windows.patch` | GPU, D3D12 | W | Dump dispatches shrunk to dirty tiles | PLATFORM_WINDOWS | needs 0008 |
| 0015 | `0015-windows-high-resolution-sleep.windows.patch` | RT, `threading_win.cpp` | W | Per-thread high-resolution waitable timer for `Sleep`/`AlertableSleep` of 1 ms or more | PLATFORM_WINDOWS, GENERIC_REXGLUE | split out of 0006 in `dc6feb8` |
| 0016 | `0016-vulkan-submissions-and-occlusion.linux.patch` | GPU, Vulkan | L | Vulkan twin of 0003/0011: submission limit, deferred occlusion queries, `PollBackgroundWork` override | PLATFORM_LINUX | needs 0003 |
| 0017 | `0017-vulkan-render-target-transfers.linux.patch` | GPU, Vulkan | L | Resolve preflight removed, overwrite cutouts, per-tile stencil bits | PLATFORM_LINUX | needs 0005, 0007 |
| 0018 | `0018-vulkan-edram-dump-dirty-tiles.linux.patch` | GPU, Vulkan | L | Dirty-tile dumps | PLATFORM_LINUX | needs 0008 |
| 0019 | `0019-linux-command-thread-wait.linux.patch` | GPU, command processor | L | Idle command thread wait no longer alertable (alertable POSIX waits poll in 1 ms slices) | PLATFORM_LINUX | context from 0003 |
| 0020 | (absent) | | | never committed | UNCERTAIN | see above |
| 0021 | `0021-posix-wait-multiple-contention.linux.patch` | RT, `threading_posix.cpp` | L | `WaitMultiple` sleeps 100 us on lock contention, one lock vector, clock read only with a timeout | PLATFORM_LINUX | none |
| 0022 | `0022-resolve-copy-into-textures.patch` | GPU, render target + texture cache bases, `draw.h` | B | Copy a resolved single-sample render target straight into the texture that samples it; cvar `resolve_copy_into_textures` (default on) | GENERIC_REXGLUE, perf; game-pattern dependent | twins 0023/0024 |
| 0023 | `0023-d3d12-resolve-copy-into-textures.windows.patch` | GPU, D3D12 | W | The `CopyTextureRegion` copies | PLATFORM_WINDOWS | needs 0022 |
| 0024 | `0024-vulkan-resolve-copy-into-textures.linux.patch` | GPU, Vulkan (both libraries' headers) | L | `vkCmdCopyImage` in the deferred command buffer, device function entry | PLATFORM_LINUX | needs 0022 |
| 0025 | `0025-wait-reg-mem-vblank-wake.patch` | GPU, command processor, `graphics_system.cpp` | B | `WAIT_REG_MEM` past its yields waits for an event the vblank thread sets | GENERIC_REXGLUE, perf; pacing | after 0003/0006 |
| 0026 | `0026-vulkan-device-preference.patch` | RT, `vulkan_provider.cpp` | B (code is Vulkan) | Prefer discrete GPU among equally capable devices unless an environment variable chose one; log the choice | GENERIC_REXGLUE | none |
| 0027 | `0027-d3d12-adapter-preference.windows.patch` | RT, `d3d12_provider.cpp` | W | `EnumAdapterByGpuPreference`; power saving only if the player chose it in Windows graphics settings | PLATFORM_WINDOWS | none |
| 0028 | `0028-msaa-2x-host-log.patch` | GPU, both render target caches | B | Logs whether guest 2x MSAA targets are host 2x or 4x, and why | GENERIC_REXGLUE, diagnostics | none |
| 0029 | `0029-ui-repaint-with-guest-frames.patch` | RT, `presenter.cpp`, `window_sdl.cpp`, `window.h` | B | UI-only repaints wait for the next guest frame; display-refresh limit otherwise; new last virtual `Window::GetDisplayRefreshRate` | GENERIC_REXGLUE, pacing | ABI rule applies |
| 0030 | `0030-resolve-deferred-writeback.patch` | GPU, render target + texture cache bases | B | With resolution scaling, skip the EDRAM dump and memory write of a resolve copied into a texture; write it later; cvar `resolve_defer_writeback` (default on) | GENERIC_REXGLUE, perf; high risk if wrong | needs 0022 |
| 0031 | `0031-d3d12-resolve-deferred-writeback.windows.patch` | GPU, D3D12 | W | D3D12 deferral and later writes | PLATFORM_WINDOWS | needs 0030, 0023 |
| 0032 | `0032-vulkan-resolve-deferred-writeback.linux.patch` | GPU, Vulkan | L | Vulkan deferral and later writes | PLATFORM_LINUX | needs 0030, 0024 |
| 0033 | `0033-stop-title-threads.patch` | RT, `kernel_state`, `runtime.cpp`, `xobject`, `xthread` | B | Stop every title guest thread before teardown; parked, never killed | GENERIC_REXGLUE, correctness | test `title_thread_shutdown_tests`; see [13](13-crash-hardening-threading-memory.md) |
| 0034 | `0034-spirv-texture-exp-adjust.linux.patch` | GPU, SPIR-V translator | L | `exp_adjust` read from fetch constant word 3 instead of word 4 | GENERIC_REXGLUE, PLATFORM_LINUX | none; no automated test |
| 0035 | `0035-cache-vulkan-swapchain-paint-pipeline.linux.patch` | RT, `vulkan_presenter.cpp` | L | Store the swapchain format after creating the paint pipeline so it is reused | GENERIC_REXGLUE, PLATFORM_LINUX | upstream rexglue-sdk issue 507 |
| 0036 | `0036-internal-render-scale.patch` | GPU, render target cache base, texture cache base, `draw.cpp`, DXBC and SPIR-V translators | B | `hcu_internal_render_scale` and the split of the render resolution into the host render target scale (quarters of a guest pixel) and the whole scale of the EDRAM, resolves and textures | GAME_ADAPTATION | tests `hcu_internal_render_scale_tests` |
| 0037 | `0037-d3d12-internal-render-scale.windows.patch` | GPU, D3D12 | W | Host-scale render targets, transfers, clears, viewport and the EDRAM dump in the D3D12 render target cache and command processor | PLATFORM_WINDOWS | needs 0036 |
| 0038 | `0038-vulkan-internal-render-scale.linux.patch` | GPU, Vulkan | L | The same for the Vulkan render target cache and command processor | PLATFORM_LINUX | needs 0036; not run on a Linux GPU |

Library column note: 0006, 0010, 0015, 0021, 0026, 0027, 0029, 0033 and 0035 change files of `rexruntime`; the others
are in `rexgpu-xenos`. Headers shared by both targets (0024, 0033) change both libraries. This matters because the
game links the runtime and loads the plugin separately, and both must come from the same build (stamp covers both).

## 3. Patch families

1. **Correctness / lifetime**: 0001, 0033 (and the app-side shutdown order, see section 5.1). Highest portability.
2. **Detection / safety of tooling**: 0010.
3. **Shader translation**: 0034.
4. **Presenter / UI**: 0029, 0035 (and 0026/0027 device choice).
5. **GPU command processor performance (common)**: 0002, 0003, 0004, 0009, 0025. Waits and timers: 0006, 0015, 0019, 0021.
6. **Render target / EDRAM performance**: 0005, 0007, 0008, then twins (D3D12 0011-0014, Vulkan 0016-0018); resolve copy
   0022 + 0023/0024; deferred writeback 0030 + 0031/0032. The twin pattern: one common patch holds backend-agnostic
   state (dirty bits, proofs, copy eligibility); one `.windows` and one `.linux` patch consume it. Port the common
   patch only together with the backend you build; skipping it keeps the stock behaviour.
7. **Diagnostics**: 0028 (host MSAA log), 0026 (logs the Vulkan device).

Cross-references: performance rationale and expectations in [12-performance.md](12-performance.md); presenter and frame
pacing effects of 0025 and 0029 in [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md); Vulkan items
(0016-0018, 0024, 0026, 0032, 0034, 0035) in [06-graphics-vulkan.md](06-graphics-vulkan.md); Linux specifics in
[05-platform-linux.md](05-platform-linux.md); Windows specifics in [04-platform-windows.md](04-platform-windows.md).

## 4. Significant changes, one by one

### 4.1 Physical heap release order (0001) -- `GENERIC_REXGLUE`, `CONFIRMED`
- Problem and symptoms: log lines `BaseHeap::Release failed because address is not a region start` and
  `PhysicalHeap::Release failed due to parent heap failure`, then a view page table inconsistent with the memory in use.
  Reported externally around exiting the game on SteamOS (issue 6).
- Root cause: guest physical memory (views at `0xA0000000`, `0xC0000000`, `0xE0000000`) is tracked in a shared parent heap
  and in each view's page table. `PhysicalHeap::Release` freed the parent pages first and cleared the view last, while
  `TriggerCallbacks` drops the global lock in between. Another thread could allocate the just-freed pages, pin them in the
  view over stale entries, and the first release then erased the new allocation's entries.
- Solution: release the view first (`BaseHeap::Release(base, nullptr)`), the parent last; callbacks still run first while
  the region is intact; `out_region_size` is still the parent's.
- Files: `patches/rexglue-sdk/0001-physical-release-order.patch` (`src/system/xmemory.cpp`, `PhysicalHeap::Release`).
- Origin: `ab4f7b7` (v0.3.1). The project doc records the same fix existing in another ReXGlue recompilation project.
- Validation: automated, `tests/physical_heap_stress.cpp` (CMake target `hcu_physical_heap_stress`): 8 threads x 20000
  allocate/release on the `0xA0000000` and `0xE0000000` views must see zero failed releases. Per the project doc it fails with the stock runtime
  and passes patched (documented in the repo, not re-run for this reference). Was it the cause of the reported exit crash?
  `UNCERTAIN`: the v0.3.1 release notes say the report had not been re-tested.
- Porting: copy directly; first `git apply --check` against your SDK version in case upstream already fixed it.
- Pitfall: the race needs two threads in a narrow window, so the original symptom is timing dependent; write the stress
  test first. Full story and sequence in [13](13-crash-hardening-threading-memory.md).

### 4.2 Stop title threads before teardown (0033) -- `GENERIC_REXGLUE`, `CONFIRMED` mechanism
- Problem: `Exit Game` ends in `XamLoaderLaunchTitle`, which runs `KernelState::TerminateTitle` on the main guest thread.
  It woke other guest threads, gave them 200 ms to exit, reset its flag and dropped stragglers from the thread list.
  The game's polling worker sleeps 500 ms per round in `KeDelayExecutionThread`, so it outlived the window and kept
  running while `Runtime::Shutdown` destroyed kernel state and unmapped guest memory.
- Symptoms: SIGSEGV on Linux a few seconds after `Execution complete` under CPU load; Exit Game hangs (issue 9, Game Mode).
- Solution: a guest thread counts as running from start to exit except while inside a kernel wait (`XObject::Wait`,
  `WaitMultiple`, `SignalAndWait`, `XThread::Delay`, via the `XThread::GuestWait` scope). `XThread::StopTitleThreads` waits
  until none runs, then closes a gate: a thread leaving a wait afterwards parks forever. `TerminateTitle` keeps its flag
  set and retains the object of any thread that did not exit. `Runtime::Shutdown` terminates the title if needed, stops the
  threads, then tears down. After 2 s with a thread still in guest code the process ends with `std::_Exit(EXIT_FAILURE)`
  (no safe way to kill it). Nothing is force-killed because `TerminateThread` would orphan host locks.
- Files: `0033-stop-title-threads.patch` (`kernel_state.{h,cpp}`, `xthread.{h,cpp}`, `runtime.cpp`, `xobject.cpp`).
- Origin: `abd31d2` (v0.5.0). Companion app change `418174b`.
- Validation: automated, `tests/title_thread_shutdown_tests.cpp` (`hcu_title_thread_shutdown_tests`): runs the real runtime
  with a worker in a 500 ms delay, one on a cancelled periodic timer, one running guest code, terminates the title from the
  main thread, tears the runtime down, and requires no guest rounds afterwards; a last case with a thread that never waits
  must end the process. Real SteamOS Game Mode confirmation: pending (v0.5.0 notes: reporter had not confirmed).
- Dependencies: ABI rule (touches `xthread.h`, `kernel_state.h`). Interacts with `ReXApp::OnClosing`, which hard-exits
  without teardown (window close path).
- Porting: copy directly; copy the app-side input shutdown (section 5.1, `OnShutdown`) together with it.
- Pitfalls: threads blocked in host waits that are not counted (custom kernel exports) are treated as running; the 2 s
  rule then kills the process, which looks like a crash. `GuestWait` must wrap every new blocking primitive you add.
- Superseded approach: stock 200 ms cooperative drain (removed by this patch).

### 4.3 RenderDoc loaded only when already present (0010) -- `GENERIC_REXGLUE`, `CONFIRMED`
- Problem: `RenderDocAPI::CreateIfConnected` used `DynamicLibrary::Load`, i.e. `dlopen("librenderdoc.so")` / `LoadLibraryW`,
  which loads the library when the loader can find it. Loading RenderDoc in-process activates it: a SteamOS reporter
  (issue 6) got `RenderDoc API initialized` and the overlay without having attached anything.
- Solution: `DynamicLibrary::LoadExisting` (`dlopen` with `RTLD_NOLOAD`, `GetModuleHandleExW` without the unchanged-refcount
  flag), used by `CreateIfConnected`; `Load` unchanged. Normal injected/pre-loaded RenderDoc still works.
- Origin: `3020c32`; supersedes nothing. Package guards in `tools/package-linux.sh` reject any RenderDoc file or layer JSON.
- Validation: automated, `tests/renderdoc_detect_tests.cpp` (Linux target `hcu_renderdoc_detect_tests`, fake library from
  `tests/fake_renderdoc.cpp`): null and nothing loaded when absent; null when a fake library is on the loader path but not
  loaded; found when loaded on purpose; unloads when its last owner goes. `build-patched-rexruntime.sh --loader-test`
  also runs it against the stock runtime, which must fail. Whether it fixed the tester's visible overlay: pending.
- Porting: copy directly. Details in [05](05-platform-linux.md) and [15](15-debugging-logging.md).

### 4.4 SPIR-V exponent bias (0034) -- `GENERIC_REXGLUE`, `PLATFORM_LINUX`, `CONFIRMED` bug, visual effect `LIKELY`
- Problem: the texture fetch result exponent bias (`exp_adjust`, signed 6 bits at bit 13) is in fetch constant word 3. The
  SPIR-V translator read bits 13:18 of word 4, which hold the LOD bias, scaling fetches by a wrong power of two where they
  differ (darker images, lost glow). The DXBC (D3D12) translator already read word 3.
- Solution: load word 3 for the exponent bias; word 4 stays for LOD bias. Linux only because Windows does not build the
  SPIR-V translator. Origin `430fc32`.
- Validation: no automated test (not testable without a GPU); manual confirmation on a real Linux PC / Deck pending per
  `docs/linux.md`. Port: adopt for Vulkan targets after checking upstream (compare your SDK first; the same fix exists in a sibling
  project, section 8).

### 4.5 Cached swapchain paint pipeline (0035) -- `GENERIC_REXGLUE`, `PLATFORM_LINUX`, `CONFIRMED`
- Problem: `VulkanPresenter::PaintAndPresentImpl` checked which swapchain format the effect pipeline was created for but
  never stored it after creation, so every guest frame waited for the previous frame on the GPU, destroyed the pipeline and
  recreated it. Reported upstream as rexglue-sdk issue 507.
- Solution: store `swapchain_format` when creation succeeds (one-line functional change). Origin `3b4a229`.
- Kind: presenter performance (a per-frame GPU-wait stall), not a correctness fix; output was never wrong. Contrast 0034, which is a correctness bug.
- Validation: inferred from code reading; unmeasured, no test. Port: high value for Vulkan targets; check whether upstream already fixed issue 507 before carrying the patch.

### 4.6 Command processor CPU work (0002-0004, 0006, 0009, 0015, 0019, 0021, 0025)
Not crash fixes; see [12](12-performance.md) for effect. Key portability notes for this inventory:
- 0004 changes cvar defaults. Anything relying on `clear_memory_page_state` or `primitive_processor_cache_min_indices` must
  be re-tested; the project doc says `clear_memory_page_state = true` in the settings file restores the stock behaviour.
  It also fixes a real bug: the primitive cache invalidation compared an entry's end with the written range's end instead of
  its start, so a write covering a whole entry did not invalidate it (`CONFIRMED` in the patch; the correctness fix is
  `GENERIC_REXGLUE` in isolation).
- 0006 mixes unrelated changes in one patch (counter, timer queue, critical sections, vblank thread). The timer queue part
  works around a defect in the third-party disruptor wait strategy (predicate and timeout passed to
  `std::condition_variable` in the wrong order), so the patch carries its own blocking strategy.
- 0009 only helps together with a game-side waiter; see section 7 and [12](12-performance.md). Its safety design (waiter
  re-checks the word, bounded 2 ms wait, self-disabling) is in [13](13-crash-hardening-threading-memory.md).
- 0019 and 0021 exist because POSIX alertable waits are polled in 1 ms slices and `WaitMultiple` polls: `CONFIRMED`
  mechanism, effect unmeasured here (the project doc quotes another project's profile for 0021, not this game's).
- 0025 helps only when the game's present uses `WAIT_REG_MEM` on a word the vblank handler clears; verify per game.

### 4.7 Render target, resolve and EDRAM series (0005, 0007, 0008, 0011-0014, 0016-0018, 0022-0024, 0030-0032)
These are the highest-risk patches for a new project: they change what the GPU emulation writes and when. Each states a
proof condition (draw replaces its target; resolve copies a whole single-sample target from origin into the same bits;
deferred memory is written before any reader). Inferred risk: a wrong proof corrupts the picture rather than crashing.
- Toggles: `resolve_copy_into_textures`, `resolve_defer_writeback`, plus `native_2x_msaa` (stock) for 0028's log.
- Validation: manual only (play-through on D3D12 and Vulkan); no automated test exists for any of them. Do not copy blindly
  to another game without a visual regression pass on the new game.
- Porting: concept first; take the common patch plus the backend you ship, in number order.

### 4.8 Adapter and device preference (0026, 0027)
`GENERIC_REXGLUE` with `PLATFORM_*` code. Problem: dual-GPU machines picked the integrated GPU. 0027 uses
`EnumAdapterByGpuPreference` (high performance, or minimum power if the per-application Windows graphics setting says so,
read from the `UserGpuPreferences` registry key); explicit `d3d12_adapter` still wins. 0026 prefers a discrete Vulkan device
unless `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT` or `__NV_PRIME_RENDER_OFFLOAD` is set; explicit
`vulkan_device` still wins. The game also exports the Optimus / PowerXpress symbols (app side, see
[04](04-platform-windows.md)). Validation: manual only; no per-GPU matrix in the repo. Origin `cf6eee1`.

### 4.9 UI repaint with guest frames (0029)
Problem: while any ImGui dialog exists the presenter paints from the UI thread and requests a repaint after every frame;
each guest frame was presented as it arrived and again at the next vertical blank (about 120 presents per second for 60
guest frames per the project doc; on Linux without vblank ticks the repaint ran as fast as the GPU allowed).
Solution: a `ui_repaint_thread_` schedules UI-only repaints that wait up to 50 ms for the next guest frame, or the display
refresh interval from the new last virtual `Window::GetDisplayRefreshRate` (SDL display mode). The thread is joined in
`~Presenter`. Resizes, surface changes and OS repaints still paint at once. Classification `GENERIC_REXGLUE`; ABI-sensitive.
Validation: manual. See [07](07-display-window-frame-pacing.md).

### 4.10 Superseded and branch-only items
- `0010-debug-saulo-gpu-thread-stats.patch` (earlier numbers 0006 and 0008 first) was debug instrumentation; deleted in
  `bbd02f2` before release. Not part of HEAD.
- `investigate/controller-rumble` (not merged into HEAD): `66e229d` adds `patches/rexglue-sdk/0036-debug-saulo-rumble.patch`
  (touches `input_system.cpp`, the XInput driver and `xam_input.cpp`) plus `src/debug_saulo_rumble.cpp`. Branch-only,
  experimental diagnostics; not HEAD functionality and not counted in the 34.
- `debug/full-gameplay-telemetry` (not merged): `6806845`, `4a8f369`, `dd633c8` add `src/debug_saulo*.cpp` diagnostics and
  small hooks in `hard_corps_uprising_app.h`, `window_host.cpp`, `gpu_fence_wait.cpp`. Branch-only, HARD_CORPS_SPECIFIC.
- `git log --all` also lists tooling checkpoint and stash commits (for example `123015f`, `f6fb51a`, `cb72ff5`, reachable only from a non-branch ref or the stash). They are not branch history; `123015f` appears to "add" every patch only because it is a snapshot of the working tree. Do not cite them as origins.

## 5. ReXApp lifecycle integration (`src/hard_corps_uprising_app.h`)

`HardCorpsUprisingApp` subclasses `rex::ReXApp` and is created by `REX_DEFINE_APP` in `src/main.cpp` (which also includes the
generated init header). The SDK order, verified in the staged `share/rexglue/rex_app.cpp`: configure paths, logging, setup
presentation (window, drawer), `OnPreSetup`, optionally `OnFinalizePaths` (which may return an empty optional to defer
runtime construction until a `resume` callback), runtime construction, `LaunchModule`, `OnPostSetup`, then on teardown
`OnDestroy` which calls `OnShutdown` first, then unregisters overlays, drops ImGui and drawers, joins the module thread,
destroys the window, and finally destroys the runtime. `ReXApp::OnClosing` (window close) is different: it terminates the
title and calls `std::_Exit(0)` without subsystem teardown. These two exit paths are the key to
[13](13-crash-hardening-threading-memory.md).

### 5.1 Reusable lifecycle pattern (`REUSABLE_INFRASTRUCTURE`)

| Hook | Pattern to reuse | Evidence |
| --- | --- | --- |
| `Create` | Set the application identity (Wayland app id / X11 class) before any window exists | `hcu::SetApplicationId` |
| `OnConfigurePaths` | Set `game_data_root` to a game folder next to the program; run the user-data decision (portable vs legacy, migration, see [10](10-user-data-settings-migration.md)); derive `cache_root` from the chosen user data unless explicitly set; redirect `log_file` to a writable folder when the executable folder is not the port folder (AppImage mount is read-only) | `ChooseUserData`, `hcu::NextLogFile` |
| `SetupPresentation` override | Call the base; on failure show an error that needs no GPU, because the SDK only logs the failure | commit `6cf59bd`, `hcu::ReportGraphicsFailure` (Linux only; no-op on Windows) |
| `OnPostInitLogging` | Log the user-data decision; load player settings over shipped config after logging exists | `hcu::DescribeUserData`, `LoadPlayerSettings` |
| `OnConfigureFonts` | Launcher fonts scaled by window DPI | optional |
| `OnPreSetup` | Capture config baselines for the launcher and decide whether the launcher shows before the runtime exists | `game_settings_.Capture`, `ShouldShowLauncher` |
| `OnFinalizePaths` | Set title/icon, decide launcher vs direct start; the launcher's Play callback calls `resume(defaults)` on the UI thread | pattern detail in [03-launcher.md](03-launcher.md) |
| `OnPostSetup` | Attach window-level services (cursor, F11), overlays, input gating callback replacing the SDK's own, then project services needing the profile | `AttachWindowHost`, `SetActiveCallback` |
| `OnShutdown` | Shut the input system down first, then detach everything that holds the window or ImGui drawer; this is the only hook that runs before the window is destroyed | `418174b`; see [13](13-crash-hardening-threading-memory.md) |
| `CreateAchievementsOverlay` | Own the overlay in the app and return null so the SDK's does not stack | optional |

Ordering rules that were learned the hard way: (1) anything registered on the window must be detached in `OnShutdown`, not
in a destructor that runs after `window_.reset()`; (2) `OnShutdown` must not assume the runtime is still alive in the
Exit-Game path only because it is: the runtime is destroyed after the window; (3) state read by guest-thread hooks
(`g_services`, `g_active` flags, `window_host` statics) should be immortal or flag-guarded because hooks can run while the
process exits (`src/live_fix.cpp`, `src/window_host.cpp`).

### 5.2 Project-specific parts (do not copy)
`HARD_CORPS_SPECIFIC` / `GAME_ADAPTATION`: `ApplyMultisampling` (this game's MSAA option), `InstallLiveServices` (offline Live
bridge, profile id, stats file), `OpenDownloadContent` (DLC notice text), cheat menu, achievements alias, `kWindowTitle`,
the `trg::` helpers, `TuneProcessScheduling` (Windows). See [16](16-reusable-game-features.md) and
[11](11-offline-services-profile-achievements.md).

## 6. SDK behaviours discovered (evidence in this repository)

| Behaviour | Where evidenced | Point to |
| --- | --- | --- |
| The SDK achievement store refuses achievement ID 0 (`Achievement store: refusing achievement with ID 0`); the port registers an alias | `docs/offline-live.md`, `src/achievement_alias.cpp` | [11](11-offline-services-profile-achievements.md) |
| `native_2x_msaa = false` does not turn multisampling off: guest 2x targets get host 4x MSAA | patch 0028, `src/multisampling.h` | [06](06-graphics-vulkan.md) |
| ReXGlue writes logs beside the executable; read-only in an AppImage | `OnConfigurePaths`, `docs/linux.md` | [05](05-platform-linux.md), [15](15-debugging-logging.md) |
| Cache folder follows the system root unless `cache_root` is set | `ChooseUserData` | [10](10-user-data-settings-migration.md) |
| `ReXApp::OnClosing` hard-exits; `OnDestroy` runs full teardown; their divergence hides teardown bugs from window-close testing | staged `rex_app.cpp` | [13](13-crash-hardening-threading-memory.md) |
| Windows sleeps last up to a timer tick longer than asked; POSIX alertable waits poll in 1 ms slices; `WaitMultiple` polls | patches 0015, 0019, 0021 | [12](12-performance.md) |
| SDK defaults changed here: `execute_unclipped_draw_vs_on_cpu` (off in the release), `clear_memory_page_state` (on), `primitive_processor_cache_min_indices` (0) | patch 0004 | [12](12-performance.md) |
| SDL's message box runs zenity first when zenity is installed, and a stalled zenity hides the window | `ReportGraphicsFailure` uses an SDL surface window instead | [05](05-platform-linux.md) |

NO_VALUE profile settings and license-mask behaviour are not evidenced in this repository's patches or app code (the
offline-Live work is documented in [11](11-offline-services-profile-achievements.md)); nothing is claimed here.

## 7. Patch-adjacent app code that supports the SDK patches

- `src/gpu_fence_wait.cpp`: the game-side waiter for 0009. It hooks two guest helper sites (fixed guest addresses),
  resolves the device's fence word, and waits on its host address with a 2 ms bound; it disables itself after 8 missed
  wakes or any failed wait. `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC` addresses; the wait/wake mechanism is the reusable idea.
- `src/crt_native.cpp`: native `memcpy`/`memset` hooks that decline overlapping forward copies and ranges touching the GPU
  register window or the offset physical view. `GAME_ADAPTATION`.

## 8. Cross-project mapping to the Geometry Wars reference (read-only comparison)

The sibling project's `patches/rexglue-sdk` was compared byte-for-byte (MD5) with this repository's directory. Result:
33 of the 34 files are byte-identical with a file there under a different number; the one difference is this repository's
`0034-spirv-texture-exp-adjust.linux.patch` vs the sibling's `0002-spirv-texture-exp-adjust.patch` (same fix; differs in
a comment line, hunk offsets and the platform suffix). Equal numbers across projects mean nothing: 0001, 0021, 0022-0033 and 0035 happen to coincide
only because of how the sibling renumbered the series.

| This repository | Sibling file | Notes |
| --- | --- | --- |
| 0001 | 0001 physical-release-order | identical |
| 0002 | 0004 gpu-register-write-bulk-path | identical |
| 0003 | 0005 gpu-waits-and-submissions | identical |
| 0004 | 0006 gpu-draw-path | identical |
| 0005 | 0007 render-target-transfer-performance | identical |
| 0006 | 0008 cpu-timers-and-waits | identical |
| 0007 | 0009 render-target-stencil-regions | identical |
| 0008 | 0010 edram-dump-dirty-tiles | identical |
| 0009 | 0011 guest-fence-wake | identical |
| 0010 | 0003 renderdoc-detect-loaded-only | identical |
| 0011-0014 | 0012-0015 (d3d12 series) | identical |
| 0015 | 0016 windows-high-resolution-sleep | identical |
| 0016-0018 | 0017-0019 (vulkan series) | identical |
| 0019 | 0020 linux-command-thread-wait | identical |
| 0021-0033, 0035 | same numbers | identical |
| 0034 | 0002 spirv-texture-exp-adjust | same fix, not byte-identical |

Interpretation: the sibling inherited the series; nothing in this table says the patches behave equally in two games.
The sibling additionally uses a `patches.txt` manifest; this repository uses file-name rules only. Treat shared patches as
"already validated on two games" only if the sibling's own reference says so for that patch.

## 9. Porting notes and verification

1. Start from the mechanism (section 1), not from the patches. Without the stamp you will not know which runtime you ran.
2. Apply correctness and hardening first, each after confirming the pinned SDK still has the bug: 0001, 0033, 0010 where
   RenderDoc matters, 0034 for Vulkan. Then presentation, device selection and performance, each only when relevant:
   0035 (Vulkan presenter pipeline cache, high value for Vulkan), 0026/0027 (hybrid GPU selection, validate on suitable
   hardware), 0029 (when permanent ImGui dialogs reproduce the double present). The remaining performance series
   (0002-0009, 0011-0019, 0021-0025, 0030-0032) is investigate-first: profile your own game, then adopt per group.
3. Keep numbering stable; renumbering invalidates fingerprints and your own history, as this repository's `0010` reuse shows.
4. Verify each patch with the narrowest check: allocator stress for 0001, title-thread test for 0033, loader test for 0010;
   build-and-run smoke for GPU patches (not automated here). Run the whole test set only for the release review.
5. Never rely on filenames: read each patch's diff stat (`git apply --stat`) to see which library it changes.
