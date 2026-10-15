# 12 Performance

Scope: every meaningful CPU, GPU, synchronization and build-level performance change in this project, first as a chronology, then as a technical catalogue. The patch inventory itself (what each patch touches, ordering, fingerprinting) is [01-rexglue-core.md](01-rexglue-core.md); do not look for it here. Presentation pacing is also covered in [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md); Vulkan specifics in [06-graphics-vulkan.md](06-graphics-vulkan.md); crash-related runtime work in [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md); build and package mechanics in [02-build-system.md](02-build-system.md) and [14-packaging-release.md](14-packaging-release.md).

Conventions: classification `GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC`; confidence `CONFIRMED` / `LIKELY` / `UNCERTAIN`; validation `automated` (test name) / `manual` / `inferred` / `pending`. Adopt-or-investigate: adopt directly / investigate first / adapt / concept only.

## 0. How to read the numbers (important)

- The repository contains **no benchmark harness and no stored profiling logs**. The only quantitative statements are in `.github/release-notes-v0.4.0.md` and the patch descriptions in `docs/rexglue-runtime-patch.md`:
  - Windows test system, author-reported, one machine: main thread about 98-100% of one core in demanding gameplay before the fence-wait work and about 14-20% of one core in representative heavy gameplay after; previously demanding scenes sustained about 59-60 FPS including at a high internal render scale. The release notes themselves say "results vary with the PC". Treat as qualitative evidence from one system, not a guarantee.
  - The patch text quotes figures measured by **other** projects for the same kind of change (for example "about 2 ms per frame of such transfers on the Steam Deck" and "about 4 ms of GPU time per frame at 2x on the Steam Deck" for the draw-path options, "42% of the process's CPU time on Linux" for the POSIX wait loop). Those are inherited from the sibling recompilations and were **not** measured on Hard Corps; they justify the change, not its effect here.
  - A single measured fact local to the repo: a 1 ms `Sleep` took about 1.8 ms on the test machine (0006/0015).
- Everywhere else this document says: quantitative impact unmeasured.
- Linux: v0.4.0 and v0.5.0 notes both say Linux performance was **not measured on real gameplay**; the Linux stack passed automated build/packaging/regression checks only.
- Development validation was focused (the changed area); full regression only in release reviews. The author's workflow used temporary `DEBUG_SAULO` instrumentation (below) and removed it before each commit.

## 1. Chronology

| Date | Commit | Release | What |
| --- | --- | --- | --- |
| 2026-10-10 | `8421905`, `e412e6a` | pre-0.1.0 | first rendered frame with Xenos enabled; baseline stock ReXGlue 0.10.0 |
| 2026-10-12 | `ab4f7b7` | v0.3.1 | 0001 physical release order (a correctness fix reported from SteamOS; not a speed change) |
| 2026-10-12 | `0ec7ded` | v0.4.0 | GPU runtime optimizations begin: 0002 register-write bulk path, 0003 waits and submissions, 0004 draw path, 0005 render-target transfer performance; plus a temporary debug patch with GPU-thread statistics |
| 2026-10-12 | `b681177` | v0.4.0 | CPU pass: 0006 timers and waits, 0007 stencil regions; debug patch grows |
| 2026-10-12 | `e950ca3` | v0.4.0 | 0008 EDRAM dirty tiles, 0009 guest fence wake, and the game hook `src/gpu_fence_wait.cpp` |
| 2026-10-13 | `bbd02f2` | v0.4.0 | debug instrumentation removed from the tracked patches; the fence-wait hook reduced to the production version |
| 2026-10-13 | `3020c32` | v0.4.0 | 0010 RenderDoc detection (not performance; avoids RenderDoc's overhead being activated unintentionally on Linux) |
| 2026-10-13 | `dc6feb8` (merge `a9816b1`) | v0.4.0 | platform split into `.windows` / `.linux` patches (0011-0019), Vulkan parity |
| 2026-10-13 | `81444f0` | v0.4.0 tag | release |
| 2026-10-14 | `cf6eee1` | v0.5.0 | stage 2 frame pacing: 0021-0032, `src/crt_native.cpp`, ThinLTO, adapter preference exports |
| 2026-10-14 | `430fc32`, `3b4a229` | v0.5.0 | 0034 SPIR-V exponent bias (correctness), 0035 Vulkan swapchain pipeline cache |
| 2026-10-14 | `a0189f4` | v0.5.0 tag | release |

The telemetry branch `debug/full-gameplay-telemetry` (`6806845`, `4a8f369`, `dd633c8`; **TEMPORARY DIAGNOSTICS**, not merged) added gameplay markers and render/window metrics for measuring the above and is not part of HEAD. Earlier debug patches (a "gpu-thread-stats" family) existed only between `0ec7ded` and `bbd02f2`; the patch number 0020 is absent from the tree and never appears in `git log --all`; the reason is not recorded (UNCERTAIN, probably a local debug patch that was never committed), and it is not a missing feature.

## 2. Technical catalogue

Each entry: Before / Problem / Change / Why it helps / Side effects / How validated / Adopt-or-investigate. "Validated" always names automated tests where they exist; most performance validation is manual (the author running the game) and is marked as such. Quantitative impact is unmeasured unless stated in section 0.

### 2.1 CPU: waiting for the GPU (the largest CPU win)

**Guest fence wake, 0009 + `src/gpu_fence_wait.cpp`** (`e950ca3`, refined in `dc6feb8`/`bbd02f2`)
- Before: the game's main thread spun in a loop polling the GPU fence word of its device after submitting a frame.
- Problem: near 100% of one core burned waiting for a word the command processor writes on another thread.
- Change: (SDK) after each guest memory write by the command processor (event write, memory write, conditional write, register-to-memory) `WakeGuestWordWaiters` wakes waiters of that address: `WakeByAddressAll` on Windows, `futex(FUTEX_WAKE_PRIVATE)` on Linux, with a full fence before. (Game) hooks on the begin and poll helpers of that single wait loop block on the same physical-view address with the last seen value, bounded to 2 ms: `WaitOnAddress` / `FUTEX_WAIT_PRIVATE`. The kernel compares and sleeps atomically, so a write between read and sleep is never lost.
- Safety: only the one call site (matched by return address), only when the device is not stopped, the word is aligned and both views of it agree. Blocking is turned off after 8 waits that timed out with the word already changed, or after a failed wait.
- Why it helps: the thread sleeps instead of spinning; latency stays bounded by the 2 ms cap even if a writer forgets to wake.
- Side effects: a futex/WakeByAddress syscall per fence write (a few per frame); a worst case of one 2 ms stall if a wake is missed.
- Validation: manual (Windows heavy gameplay; the 98-100% to 14-20% statement in 0); no automated test of the wait loop. Confidence CONFIRMED for mechanism, LIKELY for magnitude on other PCs.
- Classification: SDK half `GENERIC_REXGLUE` + `PLATFORM_*` implementations; game half `GAME_ADAPTATION` + `HARD_CORPS_SPECIFIC` (return addresses, device field offsets, physical-address arithmetic).
- Port: **investigate first**. Do not install 0009 automatically: the SDK wake half is useless without a game-side waiter, so first prove the title has the busy-wait on a fence word. SDK half generic; game half **concept only**.
- Pitfall: the physical view offset (`PhysicalHostOffset`) differs per host; both views of the word must be compared before trusting it.

**GPU command thread idle wait and `WAIT_REG_MEM`, 0003 + 0019** (`0ec7ded`, Linux 0019 in `dc6feb8`)
- Before: the idle command thread spun through 500 `SwitchToThread` calls before sleeping; `WAIT_REG_MEM` slept the guest's whole wait value.
- Change: yield 32 times then sleep on the write pointer event; `WAIT_REG_MEM` treats the value as a poll interval, re-polls 16 times, sleeps at most 1 ms; both waits call a new virtual `PollBackgroundWork` (backends use it to finish deferred occlusion queries and to end an open submission). On Linux the idle wait is made non-alertable because an alertable POSIX wait is polled in 1 ms slices (1000 wakeups per second while idle).
- Classification: `GENERIC_REXGLUE`; 0019 `PLATFORM_LINUX`. Confidence CONFIRMED. Validation manual. Port: **investigate first**: the gain depends on workload and SDK version, so measure idle-thread and `WAIT_REG_MEM` CPU in the new title before carrying 0003/0019 (the idea is borrowed from The Simpsons Game recomp). 0019 only matters on Linux.
- Side effect: a new virtual must go after existing ones so the game's compiled headers keep their layout ("must not move virtual functions" rule in `docs/rexglue-runtime-patch.md`).

**Vblank-woken present wait, 0025** (`cf6eee1`): the command processor's wait for the word cleared by the guest vblank handler now waits for an event signaled after the interrupt instead of a blind 1 ms sleep (bounded to the same 1 ms). Fixes up to 1 ms late resume. Conditional: **investigate first**; only relevant where the title actually waits on a vblank-cleared guest word. Pacing/performance, not a correctness baseline item. Validation manual. See [07](07-display-window-frame-pacing.md#4-frame-pacing-work-that-is-in-head).

**0006 CPU timers and waits** (`b681177`)
- Profiling counter: non-Release builds compile the game with `REXGLUE_ENABLE_PERF_COUNTERS`, so every guest indirect call executed a locked add on one shared counter; many threads (loading) fought for the cache line. Now a per-thread count published in batches of 1024. Release never had the cost, so this helps the RelWithDebInfo default only.
- Guest vblank thread: sleeps until the next vblank due instead of `Sleep(1)`; one priority level above normal on Windows only.
- Timer queue thread: waited with a spin strategy (about 4000 `pause` plus 19 `SwitchToThread` per wait, 500 wakeups per second, spinning for as long as the game ran); now blocks on a condition variable signaled by `QueueTimer`. The patch carries its own small blocking strategy because the library's one passes predicate and timeout in the wrong order.
- Guest critical sections: spin up to 256 pauses on a taken lock before registering as a waiter, never overtaking an existing waiter.
- Classification: `GENERIC_REXGLUE` (priority part `PLATFORM_WINDOWS`). Confidence CONFIRMED for the code; benefit LIKELY (not measured per item). Validation manual. Port: **investigate first** per item (the timer queue and spin counts are specific to the pinned SDK version).
- Side effect: timer callbacks run up to a timer tick late.

**0015 Windows high-resolution sleep** (`dc6feb8`): sleeps of 1 ms and up use a high-resolution waitable timer; waits with a timeout still follow the system timer. `PLATFORM_WINDOWS`. A measured fact: 1 ms became about 1.8 ms. Not needed on Linux (`nanosleep`).

**0021 POSIX `WaitMultiple` contention back-off** (`cf6eee1`): a multi-handle wait that found a handle's mutex held used to yield and retry at once, spinning as long as the signaler was busy (the SDL audio callback releasing audio worker semaphores), allocated a vector and read the clock on every pass. Now sleeps 100 microseconds before retrying, keeps one vector and reads the clock only with a timeout. `PLATFORM_LINUX`. The ported source reported 42% process CPU in that loop on Linux (other project; not measured here). The 1 ms polling and timeouts are unchanged; a notification scheme is a runtime redesign not done.

**0033 stop title threads** is a stability change that also removes shutdown-time CPU use; see 13.

### 2.2 CPU: command processing

**0002 register-write bulk path** (`0ec7ded`): every register write was a lookup in a switch over the whole register table only to decide whether to log an unknown register. Now a table built once; runs of render-state registers `0x2000-0x3FFF` are copied as a block; type-0 packets of three or more registers inside one block-copyable span apply as one range. Registers with side effects (scratch, coherency status, gamma ramp) are all below `0x2000`, and short packets keep the ordered path. The patch notes the block path costs about the same as three single writes and stays flat beyond that (a local micro-measurement quoted in the doc). `GENERIC_REXGLUE` code; **adopt after profiling** (no reusable benchmark shows every title benefits; the quoted micro-measurement is local). Validation manual. Same change is in the Simpsons recomp SDK fork.

**0004 GPU draw path** (`0ec7ded`)
- `execute_unclipped_draw_vs_on_cpu` on (SDK default off): a draw with clipping disabled and an 8192 scissor (what a Direct3D 9 clear is) was assumed to cover the whole EDRAM, moving ownership of unrelated render target ranges and moving it back. Running the vertex shader on the CPU for such a draw finds the rows it really covers.
- `clear_memory_page_state` off (SDK default on): at every frame end the GPU forgot which guest pages its copy was valid for, so each frame uploaded every vertex and index buffer again, each batch a barrier. Write watches already invalidate changed pages. Rollback: `clear_memory_page_state = true` in `settings.toml` if stale data ever appears.
- `primitive_processor_cache_min_indices` 4096 (SDK default 0); and an invalidation test fixed to compare the cached entry's end with the written range's start (a rewrite covering a whole entry did not invalidate it).
- `SharedMemory::RequestRanges` reuses a member vector.
- Classification: `GENERIC_REXGLUE` (cvar defaults are title-dependent: **investigate first** per title). Confidence CONFIRMED for the code; benefit LIKELY. Validation manual. Also a correctness fix (the cache invalidation).

### 2.3 GPU: render target ownership transfers, stencil and EDRAM

Background: the Xenos EDRAM is emulated by host render targets; when ownership of an EDRAM range changes, the old contents are transferred. At higher resolution scales the count of transfers did not grow with the scale, their pixels did, so they dominated.

**0005 + 0012 (D3D12) + 0017 (Vulkan) draw-overwrite proof and stencil bits** (`0ec7ded`, platform halves `dc6feb8`)
- Rectangle-list draws (Direct3D 9 clears and full-screen passes) that cover a rectangle with depth/stencil tests always passing, no blending, all components written, no pixel kill and no depth output replace the old contents, so the transfer is cut around the rectangle (shrunk one pixel on each side so it holds at every scale and sample count). Same address alone is never treated as proof. A draw replacing only depth keeps the stencil transfers.
- Without pixel-shader stencil reference output (NVIDIA D3D12; Vulkan without `VK_EXT_shader_stencil_export`) a depth transfer copies the stencil one bit at a time (a stencil clear plus up to eight full-rectangle draws). Each depth render target now tracks which stencil bits may be set; transfers draw only those bits.
- Classification: common part `GENERIC_REXGLUE`; backends `PLATFORM_WINDOWS` / `PLATFORM_LINUX`. Adapted from The Simpsons Game Recomp's Vulkan cache. Confidence CONFIRMED (code); effect not measured per change.
- Side effects: complexity risk in ownership logic; a wrongly proven overwrite would be a visual bug. Mitigation: the proof is conservative. Validation: manual visual and performance checking in the heavy 4x scenes. Port: **investigate first**; this is the highest-risk family to port blindly.

**0007 + 0013 + 0017 stencil regions** (`b681177`): the heavy scenes spent most transfers on stencil bits because one depth mask only ever gained bits (a single color-to-depth transfer made every depth-to-depth transfer draw eight passes). Now per-tile stencil bits (draws mark only claimed tiles with bits their stencil state can write; clears assign; transfers take the source tiles' bits and split into runs of equal bits), and a conservative first row of each draw (scissor, viewport, or vertex positions if clipping is off) so ownership is claimed only from that row down in whole tile rows. Columns are not narrowed. On Vulkan images start with all bits possible (images are not created zeroed) and with the extension the tracking has no effect. Validation manual.

**0008 + 0014 + 0018 EDRAM dump dirty tiles** (`e950ca3`): a resolve first dumped the host render targets that own its source tiles into the EDRAM buffer. At 4x the dump was repeated over the whole source rectangle, even for tiles no draw touched since the last dump. Now one bit per EDRAM tile; set by ownership claims, resolve clears and by tiles a draw can write; cleared by a dump; each dump dispatch shrinks to the smallest rectangle containing its dirty tiles, or is dropped. Everything starts dirty. Resolve copy semantics unchanged. Not done (deliberately): skipping the resolve copy, a lazy dump, a copy-free resolve, because there was no way to prove every reader of the EDRAM buffer.

**0011 / 0016 submissions and occlusion queries** (`dc6feb8`, 0011 is the D3D12 half of 0003): ending a guest occlusion query ended the submission and waited for the GPU on every query; results are now written to the guest once the submission containing the resolve has completed (the guest sees the query as unfinished until then, as with a lagging GPU). Early submit at the end of every primary buffer happens only while fewer than three submissions are in flight. Never ends a submission in the middle of a query (that lost the query and turned host queries off for the session). The `direct_host_resolve` preflight, which computed the dump rectangles twice, is skipped.

**0022 / 0023 / 0024 resolve copy into textures** (`cf6eee1`): a resolve writes a render target to memory and a texture sampling that memory is then loaded from it again (compute decode plus copy at the resolution scale). When one render target owns every resolved tile from the resolve origin with the same pitch and a same-family format (`k_8_8_8_8` or `k_2_10_10_10`), the texture is copied from the render target (`CopyTextureRegion` D3D12, `vkCmdCopyImage` Vulkan) and the load is skipped; the resolve still writes memory. Reproduces the half-pixel offset fill with extra copies; red/blue swap is tracked and reversed in views. Anything else (MSAA, depth, other formats, exponent bias, offsets, aliasing, mipmapped) is resolved and loaded as before. Switch: `resolve_copy_into_textures`. Validation manual.

**0030 / 0031 / 0032 deferred writeback** (`cf6eee1`): with resolution scaling, a resolve copied into the one texture that samples exactly its rectangle still dumped the render target and wrote the scaled resolve memory that nothing read while the resolve repeated. `resolve_defer_writeback` skips both and keeps the texture as the only copy (not evictable), and writes memory before a texture load reads it, before another resolve partly overwrites it, and when the resolve is not repeated next frame (through the render target's own dump shader into EDRAM, then the saved resolve). Without resolution scaling it does not defer, since other GPU work reads the resolved memory and readback needs it at once. Side effect: higher complexity and memory held by the kept texture; v0.5.0 notes call it "often never" written.

### 2.4 Presentation

0029 UI repaint with guest frames (double present, about 120 presents per second for 60 guest frames, Linux unbounded) and 0035 Vulkan swapchain paint pipeline cache (destroyed and recreated each frame after a GPU wait) are described in [07](07-display-window-frame-pacing.md#4-frame-pacing-work-that-is-in-head). Both are `cf6eee1` / `3b4a229`, validation manual (build and package checks automated). 0029 is high value when the application keeps permanent ImGui dialogs or otherwise reproduces the double-present condition; not every application needs it. 0035 is a presenter performance fix (a per-frame GPU wait), not a correctness fix. The 0035 root cause is a never-stored format; it is upstream ReXGlue issue 507, so check for an upstream fix before carrying it.

### 2.5 Adapter selection

- 0026 (Vulkan): prefer a discrete GPU among equally capable devices; honors `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD` and `vulkan_device`; logs the chosen device.
- 0027 (D3D12, Windows): enumerate by high-performance preference, or minimum power when the user chose power saving for the game in Windows; `d3d12_adapter` overrides. The game executable exports `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance` (`src/platform.cpp`, in `cf6eee1`).
- Why: on hybrid laptops the first enumerated device was the integrated GPU. Classification `GENERIC_REXGLUE` (+ `GAME_ADAPTATION` for the exports). Port: **investigate first**: evaluate or adopt when hybrid adapter selection matters for your audience, and validate on suitable hardware. This repository records no hybrid-GPU run; that is missing evidence, not evidence against the patch. Validation: manual (log line names device); no automated test.

### 2.6 Multisampling

0028 only logs whether the title's 2x MSAA render targets are host 2x or 4x and why. The performance finding behind it: `native_2x_msaa = false` does not disable multisampling, it makes the game's 2x targets 4x, which costs more GPU time. v0.5.0 therefore removed the "Off" choice and offers "2x MSAA" (default, fastest) and "4x MSAA (Compatibility)". Pure logic in `src/multisampling.h`; automated: `hcu_multisampling_tests`. Classification `GAME_ADAPTATION` (setting) over `GENERIC_REXGLUE` (log). A saved "Off" is replaced by 2x at the next start.

### 2.7 Game-side CPU paths

**Native `memcpy` / `memset`, `src/crt_native.cpp`** (`cf6eee1`)
- The game's C runtime `memcpy` and `memset` are replaced by host routines when safe. Guards: forward copies only when the destination is not inside the source (otherwise the guest `memcpy` repeats bytes, which `memmove` would not), and `IsNativeRange` requires one host pointer to cover the range (the 0xE0000000 physical view is offset on some hosts) and not to touch the GPU register window, which plain stores would bypass. The `memcpy` hook also stores the destination at `r1 - 8` as the original does.
- Why it helps (inferred, not profiled in the repo): the recompiled guest routines go through the guest-memory byte-swapping accessors, while the host routines are single optimized library calls over the same bytes.
- Classification: `GAME_ADAPTATION` + `HARD_CORPS_SPECIFIC` (the two function addresses, and any change in the guest's `memcpy` contract). The range-safety predicates are `REUSABLE_INFRASTRUCTURE` ideas. Confidence CONFIRMED (code); benefit LIKELY, unmeasured. Validation: manual only.
- Pitfall: any guest range that is write-watched by the GPU memory tracking must still use the guest path (`IsNativeRange` excludes only the register window; see the 0xE0000000 reasoning in the code comment). Port: **adapt**, per title; verify with a diff of generated code.

**Build flags: ThinLTO** (`cf6eee1`): `hcu_enable_thinlto` links the game and the generated recompiled library with ThinLTO in Release and RelWithDebInfo (Windows lld-link, Linux lld; Debug stays `-O0`). The prebuilt SDK runtime and plugin are not LTO. Applied to test executables that compile game code. Quantitative impact unmeasured. Classification `REUSABLE_INFRASTRUCTURE` (CMake function), `PLATFORM_*`. Port: **adopt**, but measure compile time; the Linux and Windows link flags differ.

**Frame counter cost**: the title-bar sampler and F2 counter add a mutex and one clock read per counted swap. Unmeasured; negligible by inspection. In an experimental stash the swap path also did pacing (see 07, EXPERIMENTAL BRANCH).

### 2.8 Logging overhead

- Debug-only instrumentation (`DEBUG_SAULO`) was removed before release (a debug-stats patch existed in history between `e950ca3` and `bbd02f2`); no per-frame logging remains in HEAD for performance paths. Deduplicated logging ("only on change") is the convention for diagnostics that must stay cheap (see [15-debugging-logging.md](15-debugging-logging.md)).
- `RenderDoc` detection (0010) prevents a library load that activated RenderDoc's overlay on a SteamOS tester without the user attaching it. No performance number; it prevents an accidental overhead source.

## 3. Release notes summary

### v0.4.0 ("Major Performance Improvements", tag `81444f0`)
Windows and Linux. Claims: far less busy waiting while the game waits for the GPU and timers; GPU fence wait blocks and wakes when the GPU finishes; faster register handling and command processing; better command submission and non-stalling occlusion queries; fewer render target ownership transfers (proof-based, never same-address alone); stencil/depth transfers limited to used tiles and rows; per-tile EDRAM dirty tracking; cheaper draw path; Linux parity via the Vulkan/POSIX equivalents; patch tracking in the build so stale runtime DLLs are not used. Measured claim limited to the single Windows test system quoted in 0. Linux: no measurement.

### v0.5.0 (tag `a0189f4`)
Stability, frame pacing and Linux rendering. Claims: each game frame presented once (0029); the present wait wakes at the vertical blank (0025); resolves copied straight into textures (0022-0024) and less write-back above Original resolution (0030-0032); native memcpy/memset; ThinLTO; better adapter selection; the Vulkan presenter no longer rebuilds its pipeline every frame (0035). The notes explicitly give no new frame-rate or CPU figures.

## 4. What was not done (decisions with reasons)

- No frame limiter in HEAD; an experimental 120/144 FPS design exists only as a stash ([07](07-display-window-frame-pacing.md#5-fps-unlock-120--144-fps-with-fixed-60-hz-logic-experimental-branch-stash)).
- Skipping the resolve copy, lazy dump, copy-free resolve: no proof that every reader of the EDRAM buffer is covered.
- POSIX multi-handle wait notification scheme and APC delivery to blocked POSIX waiters: a runtime synchronization redesign.
- Shader preparation / pipeline cache: an audit concluded the caches are tiny and preloaded and async compilation is left at the SDK default; runtime tests were not run (noted in project notes, UNCERTAIN, no repo artifact).
- Direct host resolve in this SDK is stubbed; the preflight was removed rather than tuned.

## 5. Adoption order for a new project

This is an order of consideration, not an install list. Each step is conditional on the evidence named in it; the per-patch
sections above stay authoritative.

1. Fingerprint patches per platform first (see 02) so a stale runtime is never mistaken for a regression.
2. Correctness/hardening (0001, 0033, 0010, 0034 on Vulkan) only after confirming the pinned SDK still contains each bug.
3. Profile the new title (CPU threads, command processor, present count, GPU time) before carrying any performance patch.
4. 0009 only if the title has a game-side fence busy-wait (the SDK wake half is useless without the game waiter).
5. 0003/0019 and 0006 (and 0015 on Windows) only when profiled CPU waiting behavior justifies them; investigate per subsystem.
6. 0002 only after profiling shows command/register traffic matters.
7. 0025 only if the title uses the vblank-cleared `WAIT_REG_MEM` path.
8. 0029 only when the double-present condition exists (permanent ImGui dialogs or the same presentation architecture).
9. 0035 for Vulkan after confirming upstream has not fixed issue 507.
10. The transfer/stencil/EDRAM families only after profiling shows ownership transfers dominate GPU time; they carry the most semantic risk.
11. Measure every group on the target game with your own tool; this repository gives no numbers you can reuse.

Cross-links: [01-rexglue-core.md](01-rexglue-core.md), [06-graphics-vulkan.md](06-graphics-vulkan.md), [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md), [13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md), [18-change-history.md](18-change-history.md).
