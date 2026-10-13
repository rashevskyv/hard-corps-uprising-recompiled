# ReXGlue runtime patches

Hard Corps: Uprising ships ReXGlue **v0.10.0** (tag `v0.10.0`, commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`) with
local patches to the runtime library (`rexruntime.dll` / `librexruntime.so`) and, on Windows, to the Xenos GPU plugin
(`rexgpu-xenos.dll`). Everything else in the SDK is the stock release. The Linux build applies patches 0001 and 0010
only and ships the stock GPU plugin.

| Patch | Library | Change |
| --- | --- | --- |
| [0001-physical-release-order](../patches/rexglue-sdk/0001-physical-release-order.patch) | runtime | `PhysicalHeap::Release` in `src/system/xmemory.cpp` |
| [0002-gpu-register-write-bulk-path](../patches/rexglue-sdk/0002-gpu-register-write-bulk-path.patch) | GPU plugin | register writes in `src/graphics/command_processor.cpp` |
| [0003-gpu-waits-and-submissions](../patches/rexglue-sdk/0003-gpu-waits-and-submissions.patch) | GPU plugin | waits, submissions and occlusion queries of the command processor, resolve preflight (`src/graphics`) |
| [0004-gpu-draw-path](../patches/rexglue-sdk/0004-gpu-draw-path.patch) | GPU plugin | draw extent estimation, primitive cache threshold, shared memory request allocation |
| [0005-render-target-transfer-performance](../patches/rexglue-sdk/0005-render-target-transfer-performance.patch) | GPU plugin | render target ownership transfers in the D3D12 render target cache, vertex positions in the draw extent estimator |
| [0006-cpu-timers-and-waits](../patches/rexglue-sdk/0006-cpu-timers-and-waits.patch) | runtime | Windows sleeps, the guest vblank thread, the timer queue thread, guest critical sections and a perf counter (`src/core`, `src/graphics/graphics_system.cpp`, `src/kernel/xboxkrnl/xboxkrnl_rtl.cpp`) |
| [0007-render-target-stencil-regions](../patches/rexglue-sdk/0007-render-target-stencil-regions.patch) | GPU plugin | per-tile stencil bit tracking of depth render targets, stencil runs in depth transfers and clears, transfers and ownership claims that skip the rows above a draw (D3D12 render target cache, render target cache base, draw extent estimator) |
| [0008-edram-dump-dirty-tiles](../patches/rexglue-sdk/0008-edram-dump-dirty-tiles.patch) | GPU plugin | per-tile tracking of EDRAM tiles that the host render targets changed since they were dumped, so resolves dump only those (render target cache base and D3D12 render target cache) |
| [0009-guest-fence-wake](../patches/rexglue-sdk/0009-guest-fence-wake.patch) | GPU plugin | wakes host threads blocked on a guest memory word after the command processor writes it (event write, memory write, conditional write, register to memory); the game's GPU fence wait blocks on the word |
| [0010-renderdoc-detect-loaded-only](../patches/rexglue-sdk/0010-renderdoc-detect-loaded-only.patch) | runtime | `RenderDocAPI::CreateIfConnected` only uses a RenderDoc library the process already loaded (`src/ui/renderdoc_api.cpp`, `DynamicLibrary::LoadExisting` in `src/core/dynlib_*.cpp`) |

Pins: [patches/rexglue-sdk/baseline.env](../patches/rexglue-sdk/baseline.env) holds the tag, the commit and the stock
file hashes.

## Why 0010

`RenderDocAPI::CreateIfConnected` is meant to find a RenderDoc that is already attached to the process, and is called
when the Vulkan instance is created and by the GPU debug marker check. It looked for it with `DynamicLibrary::Load`,
which is `dlopen("librenderdoc.so")` on Linux and `LoadLibraryW("renderdoc.dll")` on Windows. Those also load the
library when the loader can find it, and loading RenderDoc's library in-process is what activates it: the reporter
of [issue #6](https://github.com/saulob/hard-corps-uprising-recompiled/issues/6) on SteamOS got `RenderDoc API
initialized` in the log and RenderDoc's overlay without having attached anything.

- **Lookup.** `DynamicLibrary::LoadExisting` takes a reference to a library the process has already loaded and never
  loads one: `dlopen` with `RTLD_NOLOAD` on POSIX (null if the library is not loaded), `GetModuleHandleExW` without
  `GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT` on Windows (it adds a reference). Both leave the object owning one
  reference, which the existing `Close` (`dlclose`/`FreeLibrary`) releases. `Load` is unchanged.
- **Caller.** `CreateIfConnected` uses `LoadExisting`, so it returns `nullptr` when RenderDoc is not in the process,
  and still returns the API when RenderDoc was injected or preloaded. macOS keeps returning `nullptr` before any
  lookup. This does not change what the GPU does, and nothing here is evidence about CPU or GPU load.
- **Test.** `tests/renderdoc_detect_tests.cpp` (CMake target `hcu_renderdoc_detect_tests`, Linux, not part of the
  default build) puts a fake `librenderdoc.so` on the loader path and checks that `CreateIfConnected` returns
  `nullptr` without loading it, that it finds the API when the library was loaded on purpose, and that the library
  unloads once its last owner is gone.

## Why 0009

The game's main thread spent its time in a loop polling the GPU fence word of the device. The command processor writes
that word with an event write packet on its own thread, so a waiter can sleep on the word's address instead.

- **Wake.** After each guest memory write by the command processor (event write, memory write, conditional write,
  register to memory) `WakeGuestWordWaiters` calls `WakeByAddressAll` on the written address. Nothing else changes in
  the packet handlers. A full fence precedes the wake so the store is ordered before the wake looks for waiters.
- **Wait.** `src/gpu_fence_wait.cpp` in the game hooks the begin and poll helpers of that one loop and calls
  `WaitOnAddress` on the same physical-view address with the value it last saw, bounded by 2 ms. A write after the
  value was read makes the wait return at once, so no write is slept through.
- **Safety.** Any other caller, a stopped device, an unaligned or unmapped word, or a word whose two views differ keeps
  the guest's own polling. After 8 waits that ended by timeout with the word already changed, blocking is turned off.

## Why 0008

A resolve first dumps the host render targets owning its source tiles into the EDRAM buffer, then copies from there.
At 4x the dump was repeated for every resolve over the whole source rectangle, even for tiles no draw touched since
the last dump.

- **Dirty tiles.** One bit per EDRAM tile says the owning host render target may differ from what the EDRAM buffer
  last got from it. It is set when ownership of a tile changes (any claim, including the transfer and the host depth
  store into it), for the tiles of a resolve clear, and for the tiles a draw can write: the claimed range of each
  color target with a nonzero write mask, and of the depth target when depth or stencil can be written. Dumping a
  tile clears it. Everything starts dirty, and the render target cache reset sets everything dirty.
- **Dump.** Each dump dispatch is shrunk to the smallest rectangle containing its dirty tiles, and dropped if there
  are none. The guest-visible resolve copy is unchanged: it still reads the same EDRAM tiles, which are current.
- **Not done.** Skipping the resolve copy itself (the destination would need a proof against CPU writes, other
  resolves and the scaled resolve memory), a lazy dump, and a copy-free resolve (no way to prove every reader of the
  EDRAM buffer).

## Why 0007

The heavy 4x scenes spent most ownership transfer draws on stencil bits: one depth render target mask that only ever
gained bits, so a single color to depth transfer made every depth to depth transfer after it draw eight bit passes.

- **Stencil bits per tile.** A depth render target keeps the bits that may be set per EDRAM tile. Draws mark only the
  tiles they claim and only with the bits their stencil state can write (a failing or never-writing stencil state
  marks nothing); clears assign the cleared value. A transfer takes the bits of its source tiles, so the transferred
  range is split into runs of equal bits, drawn once per bit that run has, and the destination stencil is cleared only
  where it may hold something. Where a transfer replaces the destination it assigns the source bits instead of
  adding them.
- **Stencil through color.** A depth to color transfer packs the stencil into the low byte of 8_8_8_8, 2_10_10_10 and
  16_16 targets, so such a color target keeps the bits of the tiles it received. Any draw or clear that writes color
  there makes them unknown again, and a transfer back to depth then uses all bits.
- **Rows above a draw.** The draw extent estimator also returns a conservative first row (scissor, viewport, or the
  vertex positions when clipping is disabled). The render target cache claims ownership only from that row down, in
  whole tile rows, so the tiles above are neither transferred nor claimed. The claim stays a single range, so
  columns are not narrowed.

## Why 0006

Windows only; the Linux build does not apply it.

- **Profiling counter.** Builds other than Release (RelWithDebInfo, the default of `tools\build.ps1`) compile the
  recompiled game with `REXGLUE_ENABLE_PERF_COUNTERS`, so every guest indirect call (every virtual call) ran
  `rex::perf::IncrementCounter(kFunctionsDispatched)`: a call into the runtime and a locked add on one shared counter.
  With several guest threads running at once, which is what loading is, they all fought for that one cache line.
  The counter is now added to a per-thread count that is published in batches of 1024. Release has no such call.
- **Sleeps.** `Sleep` and the alertable sleep lasted up to a timer tick longer than asked (a 1 ms sleep took 1.8 ms
  on the test machine). From 1 ms up they now wait on a high-resolution waitable timer, as King Kong Recompiled's
  frame limiter does. Waits with a timeout (`WaitForSingleObject` and friends) still follow the system timer.
- **Guest vblank thread.** It polled with `Sleep(1)` (about 1.8 ms), so a vblank could come up to two milliseconds
  after its time; it now sleeps until the next vblank is due. It runs one priority level above normal, since it sleeps
  almost all the time and a late vblank delays the guest's frame.
- **Timer queue thread.** The thread behind `HighResolutionTimer` (the 1 ms `KeTimeStampBundle` updater is the only
  user on Windows) waited with disruptorplus' spin wait strategy. Each wait began with about 4000 `pause`
  instructions and 19 `SwitchToThread` calls before a 1 ms sleep, so the thread woke about 500 times a second and
  spun for a good part of each wake-up for as long as the game ran. It now blocks on a condition variable that
  `QueueTimer` signals. disruptorplus' own blocking strategy passes its predicate and timeout to
  `std::condition_variable` in the wrong order and cannot wait with a timeout, so the patch carries a small one.
  Callbacks run up to a timer tick late.
- **Guest critical sections.** A thread that finds a guest critical section taken now polls the lock for up to 256
  pauses before it registers as a waiter and sleeps in the kernel. Only a free lock without waiters is ever taken, so
  a thread that already waits is not overtaken.

## Why 0003

- **Occlusion queries.** Ending a guest query ended the D3D12 submission and waited for the GPU to finish all work
  queued before it, on every query. The result is now written to the guest once the submission that holds the
  `ResolveQueryData` has completed, and the guest sees the query as unfinished until then (its `0xFFFFFEED` markers
  stay in place), as with a GPU running behind the command processor. The command processor polls for completed queries
  while it waits for commands or for a `WAIT_REG_MEM` condition, and ends the submission holding them when it has
  nothing more to record. Ending a submission in the middle of a query no longer happens, because it lost the query and
  turned host queries off for the rest of the session.
- **Submissions.** Submitting at the end of every primary buffer (`d3d12_submit_on_primary_buffer_end`) now only
  happens while fewer than three submissions are in flight, which is when it keeps the GPU fed. With more queued, the
  commands stay in the open submission until the swap, or until the GPU queue gets short again.
- **Waiting for commands.** The idle command thread spun through 500 `SwitchToThread` calls before sleeping on the
  write pointer event; it now yields 32 times and sleeps (The Simpsons Game Recomp). `WAIT_REG_MEM` treats the guest's
  wait value as a poll interval, re-polls 16 times before sleeping and sleeps 1 ms at most (the Simpsons fork caps
  it at 500 us), instead of sleeping the whole value, so the ring does not stay blocked after the condition is met.
- **Resolves.** The `direct_host_resolve` preflight only computed the dump rectangles that `DumpRenderTargets` then
  computed again before doing the same dump, so it is skipped in the D3D12 backend.

## Why 0005

Ownership transfers (copies of the old contents of an EDRAM range when another host render target takes it over) were
the main GPU cost at high resolution scales: their count did not grow with the scale, their pixels did.

- **Stencil bits.** Without pixel shader stencil reference output (NVIDIA), a depth transfer copies the stencil one
  bit at a time: a stencil clear over the rectangle, then up to eight full-rectangle draws. Each depth render target
  now tracks which stencil bits may be set anywhere in it. It is created zeroed, and gains bits from draws whose
  stencil operations can write (the bits of the write mask, or of the reference for replace), from the stencil values
  of resolve clears, and from transfers (all bits when the source is a color render target). A transfer draws only the
  bits that may be set in its source, and clears the destination stencil only if the destination may hold any. A game
  that never writes the stencil no longer pays for the eight draws.
- **Draws that replace their target.** A rectangle list draw (what a Direct3D 9 clear or a full-screen pass is) that
  covers a rectangle with the depth and stencil tests always passing, no blending, all components written and no
  pixel kill or depth output replaces the old contents there, so they are not transferred (the transfer is cut around
  the rectangle, as for the clear of a resolve). The vertex shader runs on the CPU for its three vertices, so any
  shader the interpreter handles counts, not only the XDK clear shader. The rectangle is shrunk by a pixel on every
  side, so it is covered at every resolution scale and sample count. A draw that replaces only the depth keeps the
  stencil transfers. Adapted from The Simpsons Game Recomp's Vulkan render target cache, which does it for its clears.
- **Clears and transfers** otherwise as before: the clear rectangle of a resolve was already cut out of the transfers.

## Why 0004

- **Draw extent.** `execute_unclipped_draw_vs_on_cpu` is on, as in Xenia (the SDK release has it off). A draw with
  clipping disabled and an 8192 scissor, which is what a Direct3D 9 clear is, was taken to cover the whole EDRAM, so
  it moved ownership of render target ranges that had nothing to do with it, and moved it back afterwards. The
  vertex shader of such a draw now runs on the CPU to find the rows it really covers. The Simpsons Game Recomp turned
  it on for the same reason and measured about 2 ms per frame of such transfers on the Steam Deck.
- **Frame-end page state.** `clear_memory_page_state` is off (the SDK release has it on). With it on, every frame end
  forgets which guest pages the GPU copy is valid for, so the next frame uploads every vertex and index buffer it uses
  again, and each upload batch is a barrier of the shared memory buffer between draws. Write watches already
  invalidate the pages the guest changes, as in Xenia, which has no such option. The Simpsons Game Recomp turned it
  off and measured about 4 ms of GPU time per frame at 2x on the Steam Deck. If a buffer or texture ever shows stale
  data, `clear_memory_page_state = true` in `settings.toml` brings the old behavior back.
- **Primitive processor.** `primitive_processor_cache_min_indices` is 4096, Xenia's default (the SDK release has 0):
  converted index buffers under that size cost more in the cache lookup and the memory watch than the conversion.
  The cache's invalidation test also compared a cached entry's end with the end of the written range instead of its
  start, so a write that covered a whole entry, the guest rewriting an index buffer in place, did not invalidate it
  (the same fix is in The Simpsons Game Recomp's SDK fork and matches Xenia).
- **Shared memory.** `SharedMemory::RequestRanges` reuses a member vector instead of allocating one on every call,
  including the single-range `RequestRange` made for every draw's indices.

## Why 0002

The GPU command thread looked every register write up in `RegisterFile::GetRegisterInfo`, a switch over the whole
register table, only to decide whether to log an unknown register; the patch checks a table built once. Runs of
render-state registers (`0x2000`-`0x3FFF`) are copied as a block, as the constant registers already were, and a type-0
packet of three or more registers that stays within one block-copyable span (render state, float constants, fetch
constants, bool/loop constants) is applied as one range instead of one virtual `WriteRegister` call per register
(measured: the block path costs about the same as three single writes and stays flat beyond that). The registers with
side effects (scratch, coherency status, gamma ramp) are all below `0x2000`, and shorter packets keep the ordered
path. Both backends take the block path through the base class. The same two changes are in The Simpsons Game Recomp's
SDK fork.

## Why 0001

The guest's physical memory (`0xA0000000`, `0xC0000000` and `0xE0000000`) is tracked twice: in the shared parent
heap of physical pages and in the page table of each of those views. `PhysicalHeap::Release` freed the parent pages
first and cleared the view's entries last, and the callbacks in between give up the global lock. A second thread could
allocate the just-freed pages and pin them in the view over the stale entries, and the first release then erased the
new allocation's entries. The new owner's own release later failed with
`BaseHeap::Release failed because address is not a region start`, and the view's page table no longer matched the
memory in use. The patch releases the view first and the parent pages last, so the pages cannot be handed out again
before the view has dropped them. The callbacks still run first, while the region is intact, and `out_region_size`
still reports the parent's size. This is the same fix as MindOver-Recomp's `0008-physical-release-order.patch`.

Reported in [issue #6](https://github.com/saulob/hard-corps-uprising-recompiled/issues/6) on SteamOS, where the logs
showed these errors around exiting the game. The code is the same on Windows; the error needs two threads to hit the
window between the two releases, so how often it happens depends on timing.

## Building the patched runtime

On Windows (Visual Studio with the LLVM/Clang component, and git), after `tools\codegen.ps1` has put the SDK in
`tools\rexglue`, nothing else is needed:

```powershell
.\tools\build.ps1            # or .\tools\build.ps1 -Run
```

Before configuring, `tools\build.ps1` checks the installed runtime and GPU plugin against the current baseline and
patches (see below). If they do not match it runs `tools\build-patched-rexruntime.ps1` itself, checks again, and stops
with the reasons if they still do not match; if they match, ReXGlue is not rebuilt. Adding, removing, renaming,
editing or reordering a patch, or changing `baseline.env` or the build settings in `tools\rexruntime-patch.ps1`,
therefore makes the next `build.ps1` rebuild ReXGlue (the first time it fetches the v0.10.0 source, which needs
network access, and a build takes a few minutes). The Debug configuration uses the stock `rexruntimed.dll` and
`rexgpu-xenosd.dll` and skips the check.

`tools\build-patched-rexruntime.ps1` stays available as a manual command:

```powershell
.\tools\build-patched-rexruntime.ps1                 # fetch v0.10.0 (network), patch, build, install
.\tools\build-patched-rexruntime.ps1 -Restore        # put the stock runtime and GPU plugin back
```

It applies every patch in `patches\rexglue-sdk` in name order, builds the RelWithDebInfo and Release runtime and GPU
plugin into `out\rexglue-sdk-src` (use `-WorkDir` for a shorter path) and replaces them in `tools\rexglue\win-amd64`.
The Debug files stay stock. After `-Restore`, the next `build.ps1` patches them again.

Linux (Ubuntu 22.04, see [linux.md](linux.md)): `tools/package-linux.sh` runs `tools/build-patched-rexruntime.sh`
itself the first time; it applies the patches listed in `PATCHES` in that script (0001 and 0010) and builds only the
runtime. Besides the packages in linux.md it
needs git, network access and the SDK's build
dependencies: `libgtk-3-dev libx11-dev libx11-xcb-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev
libxfixes-dev libxrender-dev libxss-dev libvulkan-dev libwayland-dev libwayland-bin wayland-protocols
libxkbcommon-dev libdecor-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libgl1-mesa-dev libudev-dev
pkg-config`.

Both scripts check the source is exactly the pinned commit, stop if a patch no longer applies, compile with
`-ffile-prefix-map` so no build folder ends up in the library, and refuse a result that equals the stock file.

## What the build verifies

The scripts record the installed files in `rexruntime-patch.txt` next to the SDK: the source commit, the fingerprint
of everything the files are built from (the SDK version, tag and commit, the compiler flags, targets and
configurations in `tools\rexruntime-patch.ps1`, and every patch's name and hash in name order), and the SHA-256 of
each installed file, the runtime and the GPU plugin included. The check (`Test-PatchedRuntime`) fails if the
fingerprint differs from the current one, if any recorded file does not match, or if one of them is the stock release
file; it names what changed, for example `patches added: ...`. Timestamps are not used.

`tools\build.ps1` runs that check before the build and updates the runtime when it fails. After the build it copies
`rexruntime*.dll` and `rexgpu-xenos*.dll` of the configuration (`d`, `rd` or no suffix) from `tools\rexglue\win-amd64\bin`
next to the executable when they differ, then fails unless the executable exists, both DLLs next to it are
byte-identical to the installed ones and the stamp matches. The same check runs once more before `-Run` starts the
game, and a failure prints the expected and the actual file.

`tools/package-release.ps1` builds through `build.ps1`, then accepts the runtime and the GPU plugin only if the check
passes; the packaged DLLs must also be identical to the installed ones. With `-SkipBuild` it does not update the
runtime and fails when it is out of date. `tools/package-linux.sh` checks the runtime against the Linux patch list and
compares `librexgpu-xenos.so` with the stock release file. On Linux the stamp (`rexruntime-patch.txt`) records the
commit, the patch names and one hash over every patch's name and hash in order, and the check fails if the list or the
hash differs, if the library does not match the stamp or if it is the stock file. The AppDir check also fails on any
file named like RenderDoc or a Vulkan layer, any JSON file or symbolic link, and any of the three binaries linking
RenderDoc, besides the exact file list it already enforces.

## Allocator stress test

`tests/physical_heap_stress.cpp` (CMake target `hcu_physical_heap_stress`, not part of the default build) has 8 threads
allocate and release physical memory at once on the `0xA0000000` and `0xE0000000` views, and passes only if no release
fails. With the stock runtime it fails; with the patched one it passes.

```powershell
.\tools\build.ps1 -Config Release -BuildDir build-release
.\tools\build-patched-rexruntime.ps1 -StressTest -SkipBuild    # test the installed runtime
```

On Linux: `tools/build-patched-rexruntime.sh --stress <sdk dir> <build dir>`, with a configured project build in
`<build dir>`. To compare, run `-Restore` first and repeat the test with the stock runtime.

## RenderDoc detection test

On Linux, with a configured project build in `<build dir>` and the patched runtime installed:

```bash
tools/build-patched-rexruntime.sh --loader-test <sdk dir> <build dir>
```

It prints one `ok`/`FAIL` line per check and `PASS` at the end.
