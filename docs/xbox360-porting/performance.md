# Performance: what makes recompiled PPC slow or fast

Tags: [CODE] source read. [DOC] README/doc only. [MEAS] a number a project measured. [GUESS] my inference, untested.
Method first: measure before you change. A constant frame time (e.g. 478 ms every frame) is a timer or a spin, not slow code.

## 0. Decision tree for "the guest main thread is at 100% and the GPU is idle"

1. PC-sample the thread (100 Hz, `svcSetThreadActivity` + `svcGetThreadContext3` on Horizon; `perf`/`simpleperf` elsewhere).
   - Samples in `svc` stubs: it waits. Find which wait (guest wait watchdog, below).
   - Samples in `sub_XXXX` of the game, flat over thousands of functions: normal. Rayman Origins on a phone looks like this
     (largest function 1.7%) and runs 60 fps. [DOC Rayman ANDROID_PERFORMANCE.md]
   - Samples in ONE small generated function: a spin loop. Read its PPC; see section 3.
   - Samples in `Clock::QueryGuestTickCount`, `mutex`, `REX_CHECK_GLOBAL_LOCK`: timebase or lock spin (section 3).
   - Samples in memcpy-like generated functions: add `[rexcrt]` (section 2).
2. Fix the biggest bucket. Re-measure. Do not touch the renderer until the guest thread is flat.

## 1. Codegen flags (cheapest, biggest, proven)

The eight flags: `skip_lr skip_msr ctr_as_local xer_as_local reserved_as_local cr_as_local non_argument_as_local non_volatile_as_local`.
Default is false: every register lives in the `ctx` struct in memory and every `__savegprlr_N`/`__restgprlr_N` is a real call.

| Fact | Source |
|---|---|
| UnleashedRecomp (Sonic Unleashed, the reference) sets all eight to true | `UnleashedRecompLib/config/SWA.toml:8-15` [CODE] |
| hedge-dev: exe shrinks ~20 MB and frame times drop "by several milliseconds" with them | XenonRecomp README "Optimizations" [DOC] |
| CrownPark fork: `non_volatile_as_local = true` is the scaffold default; removed `__savegprlr/__restgprlr` from 9% of samples on SoulCalibur II | fork_changes.md [DOC] |
| Rayman Origins, DR2 Case Zero, speedbreaker leave all eight false and still run | their `*.toml` [CODE] |
| CVHD manifest sets none of them | `cvhd_manifest.toml` [CODE] |
| Risks: setjmp/longjmp, funclets (ReXGlue keeps funclets on ctx), games that read a callee's non-volatile regs, LR read as data. One XenonRecomp game showed green/magenta stripes with `non_volatile_as_local`; another crashed on clobbered non-volatiles | xbox360-recompilation-guide [DOC] |

Order: `cr`, `ctr`, `xer`, `reserved` first (safe), then `non_argument`, then `non_volatile`, then `skip_lr`, `skip_msr`. Boot test after each.

## 2. CRT overrides (`[rexcrt]`)

Without it the game's own memcpy/memset/strlen run as recompiled PPC: per-byte bswap, address fold, dirty-page store.
ReXGlue replaces them with host calls (`src/kernel/crt/memory.cpp`, `string.cpp`, `heap.cpp`, `file.cpp`, `threading.cpp`) [CODE].
Names: memcpy memcpy_s memmove memmove_s memset memset_vmx memchr strchr strrchr strstr strtok strncpy strncmp strcpy_s _stricmp
lstrlenA lstrcpyA lstrcatA lstrcmpiA XMemCpy XMemSet XMemSet128, heap group (RtlAllocateHeap/FreeHeap/SizeHeap/ReAllocateHeap,
all four or none), CreateFiber/SwitchToFiber..., file API (CreateFileA, ReadFile...). Set only addresses you verified.
Example (Castlevania SotN XBLA): `[rexcrt] memcpy=0x825E3300 memmove=0x825E37D0 memset=0x825E2740` [CODE nocturnerecomp_config.toml].
Find them: the most-called small leaf with a `dcbz`/`lvx`/`stvx` or byte loop; compare with a known XDK CRT build by byte pattern.
Switch caveat: if you track GPU-written pages with a dirty map, the override must mark the destination range once.

## 3. Spins, waits and time

| Pattern | What happens after recompiling | Fix |
|---|---|---|
| Rate-limiter that spins on `mftb` | The loop is now much faster than on a 3.2 GHz in-order PPC, so it spins millions of times per second ("zero-cost loop", one project's FPS bug) | Drive from a host timer; hook the wait function; or make `mftb` cheap |
| `mftb` itself | `REX_QUERY_TIMEBASE()` → `Clock::QueryGuestTickCount`: a `std::mutex` try_lock plus a 64-bit ratio multiply and divide per call [CODE `src/core/clock.cpp:67-104`]. On a spin loop that is the whole profile | Cache per vblank; use `clock_no_scaling` when host and guest frequency match; hook the caller |
| `db16cyc` spin hint | Dropped by codegen (`builders/system.cpp:51`). The spin runs at full speed on a core the other threads need | Lower to `yield()`. CrownPark: a spin held 16.5% of all CPU, yield cut total in-game CPU 26% [MEAS fork_changes.md] |
| `WaitForMultipleObjects` (`KeWaitForMultipleObjects`) | SDK polled: try-lock every handle, sleep 1 ms. Audio worker at 49% (CVHD), 73% (Rayman), a quarter of cycles (Split/Second) | One shared condvar bumped on every signal; waiters sleep on it. Same fix found independently by three projects. Wait-all locks in address order |
| Timer queue dispatch | Spun: 7.7% of cycles on MCLA | Blocking wait strategy |
| `KeInsertQueueDpc` | In stock 0.10 the DPC was queued and never drained, so DPC-driven signals were lost | Run inline under DPC impersonation (CrownPark) |
| Guest "wait for GPU to catch up" primitive (Nocturne `sub_825252C8`) | Polls a GPU-driven counter. With no GPU consumer it spins forever | Make the counter advance (RB read pointer write-back, scratch regs, fence) or hook the primitive when headless |
| Vblank | Games count vblanks for pacing. Host clock 0 Hz = no vblank (CVHD 0.51) | Drive vsync from a host timer at 60 Hz; check guest tick 50 MHz vs host ticks line in the log |
| 30 fps locked titles | `D3D` present interval = 2 | Midasm hook that rewrites the render state (stuntdouble `0x8227F670`, r10 2→1); MCLA overrode two timing functions (Xenia "60 fps" patch) |
| Audio render callback credits | Callback fired in clumps, mixer advanced once per clump: slow, robotic sound | Pace the callback evenly (187.5 Hz in CrownPark) [MEAS 69% → 0% repeats] |
| Guest wait watchdog | n/a | Record every wait when it starts; print outstanding ones with guest return address and object (CrownPark `guest_wait_report_seconds`). Turns "frozen" into a function name |

## 4. Host side bugs that look like slow guest code

(All found in ReXGlue-based Android work, Rayman Origins; check each on Horizon.)
- `AllocFixed` parsed `/proc/self/maps` on every guest commit (351 KB per call); fix: `mprotect` first. CrownPark also fixed the same parser in the write-watch fault path (2.7% → 0.16%). [DOC]
- Swapchain recreated every frame because `VK_SUBOPTIMAL_KHR` was treated as stale: 47% of game thread, 43 → 60 fps after the fix. Recreate only on OUT_OF_DATE/SURFACE_LOST. [DOC]
- CPU waited for the GPU fence at the end of every frame; two frames in flight made the wait 0.0 ms. [DOC]
- Pipelines compiled on first use (50-115 ms hitches): disk `VkPipelineCache` + a list of used pipelines pre-created at startup. CrownPark ships a seed cache (377 creations, one 1,434 ms frame → 0). [DOC]
- SDL3 `SDL_WaitEvent` spun on Android (UI thread at 100%): `SDL_POLL_SENTINEL=0`. [DOC]
- Game kept rendering behind the lock screen. [DOC]
- Movie planes (Y/U/V as `k_8`) decoded to RGBA8 texel by texel: 10 ms/frame; as `R8_UNORM` with a swizzle, plus a run-based untiler: 8.5 → 0.7 ms. [DOC]
- Per-draw CPU in a native renderer: byte-swapping whole vertex buffers (14%) and constants (5%) per draw; fix: only the used range / used constants. [DOC]

## 5. Memory access cost

- Upstream ReXGlue load: `bswap(*(volatile T*)(base + (u32)a + REX_PHYS_HOST_OFFSET(a)))`. `base` is a register argument. `rev` is one instruction. `volatile` stops the compiler merging loads. [CODE pch_h.inja:128-145]
- Windows/macOS already pay one compare for the 0xE0000000 view (+0x1000). A Switch fold of the same shape is not new in kind.
- UnleashedRecomp is the lower bound: one 4 GB `mmap` at 0x100000000, `base + addr`, page 0 PROT_NONE, nothing else [CODE kernel/memory.cpp].
- Keep the common case branch-free. Put the rare views (C0/E0/90/7F) behind ONE unlikely branch (`__builtin_expect`), not a chain. [GUESS]
- Dirty-page byte stores: one store per guest store. Acceptable if inlined; check the disassembly of one hot function.
- `REX_FUNC_PROLOGUE` asserts `base` 32-byte alignment with `__builtin_assume`; GCC turns this into `__builtin_unreachable` branches. Fine.
- `ppc_global_lock_count_()` is a function-local static (guard check per call) and `REX_CHECK_GLOBAL_LOCK` takes a recursive mutex: only for `mfmsr`-style code, count its occurrences in generated code.
- Compile flags: SDK uses `-O3` Release, `-fno-strict-aliasing`, `-ffp-model=strict` (Clang) / `-ffp-contract=off` (GCC); Unleashed `-march=sandybridge` on x86.
- 110-136 MB of generated C++ → 110 MB `.text` on Switch: instruction-cache bound on Cortex-A57 (48 KB L1I). The local-register flags shrink it. `-ffunction-sections` + `--gc-sections` helps size; CrownPark's static runtime dropped 670 of 2,864 kernel hooks (Geometry Wars 26 MB).
- GCC vs Clang for the generated code: Clang is what upstream targets. A Clang `--target=aarch64-none-elf` build of the generated files is a cheap experiment. [GUESS]

## 6. GPU CPU cost (when the guest thread is flat and the GPU thread is hot)

Emulating Xenos in software on the CPU (PM4 parse, EDRAM, shared memory) is CPU-bound. [MEAS] ~37 fps with the GPU thread at 100% in one project.
CrownPark's campaign on Midnight Club LA: pipeline state input cache (95-97% hit), texture fetch-write memoization (13-30% identical rewrites), frame-local descriptor cache, stable-binding fast path, bulk register writes (`WriteRegister` 5.9% → 0.5%), run-length constant copies, lock-free page validity bitmap (45 → 3.7 µs), hot-page upload batching (render passes 701 → 106/frame), `clear_memory_page_state=false`, host render target path instead of FSI (FSI was 25% of one frame). 2.5 → 29 fps on that title. [MEAS fork_changes.md]
For a 2D sprite game expect fill rate, descriptor churn and per-draw overhead; see gpu.md.
Native renderer (hook the game's D3D calls, draw straight to Vulkan with ahead-of-time converted shaders) is what shipped ports use in the end: Rayman Origins 60 fps on a Galaxy S23. Not needed while the GPU thread is idle.

## 7. Order of work for a slow port

1. Sampler on the guest main thread. 2. Register flags. 3. `[rexcrt]`. 4. Spin/wait/timebase fixes (db16cyc yield, mftb cost, directory listing cache for missing files). 5. Host bugs from section 4. 6. Memory fold shape. 7. Compiler/flags/Clang. 8. Renderer.

## 8. Added after reading the working Switch ports (sa2-nx, usf4-nx, futari-nx, Marathon-NX, Unleashed-NX, Hard Corps: Uprising)

### 8a. Command processor waits (stock ReXGlue) [CODE `src/graphics/command_processor.cpp:1033-1048, 217-234`]
`PM4_WAIT_REG_MEM` with `wait >= 0x100` and cvar `vsync` on: BLIND `Sleep(wait/0x100 ms)`; idle command thread waits 5 ms on an event. The guest spins on the fence while the GPU thread sleeps: "main 100%, GPU idle, constant ms per frame". Hard Corps: Uprising's patches: 0003 (idle wait, WAIT_REG_MEM polling), 0009 guest-fence-wake (wake threads blocked on a guest word when the command processor writes it), 0025 (WAIT_REG_MEM waits for the vblank interrupt), 0006 (vblank thread, timer queue, critical sections). Test: `vsync=false`, or count WAIT_REG_MEM per frame. [GUESS that it explains CVHD: unmeasured]

### 8b. Guest wait loops that must sleep (hooks) [DOC futari/sa2/usf4]
- D3D "wait for GPU" (32× db16cyc loop on the ring read pointer): sleep until the read pointer moves, timeout ~1 ms (sa2 `sa2_gpu_wait.cpp`; PC game thread 96% → 8%; usf4 70% → 55%).
- Frame-pacing loop `while (counter == last) {}` on a counter bumped by the vblank callback: sleep hook (futari: 78.6% of the console game thread).
- Job-pop / batch-wait `Sleep(0)` loops and frame limiter loops: spin 20 µs then condvar; limiter sleeps the missing time minus 1 ms (usf4 0.7.0/0.7.1: main thread 81% → 54%).

### 8c. Threads on Horizon
Guest threads 0x3B (only priority that is time-sliced); GPU ring 0x2C; audio/XMA 0x2B; vblank thread pinned. A spinning 0x3B worker starves the main thread (usf4 0.5.6: 10-12 fps, GPU 17%). `Sleep(0)`/yield only yields to same priority. [switch-horizon.md]

### 8d. Codegen items beyond the eight flags [DOC futari/sa2/usf4]
`skip_msr` removes ONE recursive global mutex taken for every interrupt-off pair (`REX_ENTER_GLOBAL_LOCK`) [usf4]. `elide_lr_stores` (futari: `ctx.lr` stores 148,273 → 10,681; masseffect main thread -10%). Direct calls `sub_X` → `__imp__sub_X` for unhooked X (≈91,000 in SA2; -41% executed calls with inline limit 64) plus an ELF check that no hook was bypassed. No-volatile guest memory REQUIRES `REX_LOOP_BARRIER()` at every loop header (GCC turned 172 polling loops into `b .` and boot hung; `find_self_loops.py`). FP mode switches: SA2 census 80,400/s; `fp_*_modeless`, lookahead: -2.6..-3.2% instructions per file; FPCR writes were 3-6% of the game thread. Relaxed `stwcx` CAS. `dcbt` → prefetch. `-fno-math-errno`, `-fomit-frame-pointer` for recompiled code, `-ffp-contract=off -fno-strict-aliasing` on EVERY target with generated code (futari forgot the 8 game cores). LTO `-flto -fipa-pta` on game code (not SDK), PGO ≈15% (profile valid only for the exact code; recollect after codegen changes), `-O3` (A57 I-cache: Unleashed uses `-O2 -fipa-pta`), `-mearly-ra=none` (GCC miscompiled a vperm helper), the generated huge register file `-O0` (GCC took 771-927 s at -O3). "Locals everywhere" was WORSE in 18,211/22,964 Marathon functions: measure per file (instructions/loads/stores with the Switch compiler).
Hand natives pay only for LEAF functions (call-heavy localized copies were +13-18% CPU; hooking a ≤12-instruction leaf makes it slower). Guard checks in `unordered_map` cost 40-70% of the audio thread: use flat buffers. Native replacements need a code-hash check + a verify mode (sampled compare of memory and r3-r10/f1-f13/v0-v13).

### 8e. Logging and SD [DOC futari/usf4]
SD `fwrite` per line = 50-150 ms (frames of 1,022 ms). Writer thread + memory buffer. Directory re-listing 0.7 ms per file; save overwrite via delete+create 18-31 ms (truncate in place). A stale SD config toml silently disabled fixes ("46 of 67 active"): compile defaults into the NRO and log the active keys at start.

### 8f. Measurement stack to copy
Sampler with per-PC counts at 1 kHz (`svcSetThreadActivity` + `svcGetThreadContext3`), linker map `-Wl,-Map`, hot-function script (self time per function and module), per-thread CPU via `InfoType_ThreadTickCount`, census counters on PC (FP switches, indirect calls, critical sections per frame), lockstep frame hashes (XXH3 per frame vs control run), `find_self_loops.py`, `check_switch_build.py`, verify mode with a single `differences: 0` line, one-file instruction/load/store A/B with the Switch compiler. Test at a fixed CPU clock; compare game-thread ms per frame, not fps at a frame cap.

### 8g. Prototype 1/2 Switch ports (fungalspores/prototype-switch, penettrator/prototype2-nx) [CODE/DOC]
- Same finding as usf4: the game brackets every `lwarx/stwcx` with `mfmsr` / `mtmsrd r13,1 ... mtmsrd rX,1` ("interrupts off"). ReXGlue maps this to ONE process-wide mutex (`REX_ENTER/LEAVE_GLOBAL_LOCK`), and `mfmsr` takes it too. They post-process the generated code to a lock per address stripe: cvar `proto_msr_lock_mode` 0 = none, 1 = per-address stripe (default), 2 = the SDK global mutex ("original, slowest"). A lock-free mode crashed Prototype 2's level loads; per-address stripes did not. Prototype 2 therefore does NOT use `skip_msr` / `reserved_as_local` (it needs the regions). Their tool refuses if an msr region is not a plain atomic. => For CVHD (885 global-lock sites): check what its msr regions guard, then choose skip_msr (if regions are plain atomics) or stripe locks. `tools/prepare_proto_codegen.py` (header comment) is the recipe.
- Codegen set: `cr/non_volatile/xer/ctr_as_local = true`, share_registers on setjmp, fiber switch and fragments reading the parent's r31; `read_before_write.py` finds functions where marking is unsafe. Build flags `P2_DIRECT_CALLS`, `P2_GUEST_LTO`, `P2_LEAF_LOCALS`, `P2_ARM_VMX`, post-codegen `prepare_no_volatile.py`, `prepare_switch_hot.py`; PGO from an instrumented run writing to the SD card.
- Four cores: Switch core 3 needs process permission (a forwarder NSP that allows cores 0-3; hbl.nsp may allow only 0-2). Their masks: guest 15, main 14, audio 8, host 6.
- Results: gameplay 25-30 fps (P1, 720p), city 25-30 fps (P2, 544p + FSR) at CPU 1785. So even working 3D ports on NVK are slow; a 2D game should be far better, and 1-2 fps is a stall bug, not a limit.

## Gotcha (Guardian Heroes port, 2026-10-23)

The codegen flags (skip_lr, skip_msr, *_as_local, ...) are read ONLY from the `[entrypoint]` table of the manifest, not from the top level. At the top level they are silently ignored. Also codegen reuses its old output when its stamp matches: pass `--ignore-stamp` (or delete the output) after changing flags, and check the generated code (counts of ctx.r / REX_ENTER_GLOBAL_LOCK) before measuring.
