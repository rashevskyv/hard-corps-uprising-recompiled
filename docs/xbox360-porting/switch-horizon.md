# Nintendo Switch / Horizon for a ReXGlue (or XenonRecomp) port

Tags: [CVHD] our own hardware result (D:\git\dev\cvhd-recomp\CLAUDE.md). [CODE]/[DOC] from other ports. Several of the ports below are GPL-3.0: take ideas, not files.
Working notes (temp, may be gone): `x360/notes_sa2_cpu_switch.md`, `notes_usf4_renderers_nx.md`, `notes_futari_xbla.md`.

## 0. Prior art: ports that already run on the Switch

| Port | Toolchain | GPU API | State |
|---|---|---|---|
| sa2-nx (ChanseyIsTheBest) Sonic Adventure 2 XBLA | ReXGlue fork + devkitA64 GCC | deko3d (Renut), was NVK until 1.0.2 | 60 fps at stock clocks |
| usf4-nx (Thorhax) Ultra Street Fighter IV | same SDK fork | native Vulkan on NVK | fights 59-60 fps at 540p, CPU 1785/GPU 768 |
| futari-nx, guwange-nx, peggle1-nx, peggle2-nx, bejeweled3-nx (same author) | same SDK fork | NVK or deko3d | 2D XBLA; futari 59-60 fps |
| MarathonRecomp-NX, UnleashedRecomp-NX | XenonRecomp + devkitA64 | NVK through plume | work; Unleashed 48-60 fps with CPU rounds |
Same-author Switch ports of other games (Angry Birds, Bad Piggies, ...) share the Horizon layer. Their `mesa-switch` repo builds NVK.
Our CVHD port is built independently on stock ReXGlue 0.10 + `switch/sdk-edits.py`. Read `sa2-nx/sdk/src/core/guest_memory_switch.{h,cpp}`, `exception_handler_switch.cpp`, `threading_switch.cpp`, `sdk/src/ui/switch_perf.cpp`, `windowed_app_main_switch.cpp` before inventing more.

## 1. Guest memory

| | sa2-nx / usf4-nx / futari-nx [CODE] | CVHD [CVHD] |
|---|---|---|
| Window | 4.5 GB: `virtmemFindAslr` + `virtmemAddReservation` (0x120000000) | same reservation |
| Backing | `memalign` 2 MB chunks (64 KB granules hit kernel limit 0xCE01) | `aligned_alloc` per block (256+128+64+512+...) |
| Aliases | Yes: chunk gets a code alias (`svcMapProcessCodeMemory`), made RW (`svcSetProcessMemoryPermission`), then `svcMapProcessMemory` maps the alias into each of 9 views, LAZILY on first fault (exception handler maps and retries) | None: probe `svcMapProcessMemory` on heap memory = 0xE401 |
| Mirrors (C0/E0/90/7F) | Come free from the view mapping; `PhysicalHostOffset` = 0 | Software fold per access |
| Write watch | `Perm_R` on watched pages, fallback "unmap" for the session; a read of a watched page is emulated through the shadow alias (~9 µs each; first read unwatches) | Not possible: `svcSetMemoryPermission` 0xD401; dirty byte per 4 KB page set by every store |
| Dirty/“store cards” | 1 byte/4 KB page set by generated stores (SA2_GEN_STORE_CARDS) | same idea |
| Faults in steady state | one per (view, chunk) first touch + watched pages | none |
| RAM | 3,281 of 3,285 MB used (title takeover) | same |
Facts to check: the heap-source error code differs in notes (0xD401 in sa2-nx comment, 0xE401 in the CVHD probe). The sa2 recipe goes through CODE memory first. CVHD probe tested heap directly, then CodeMemory owner/slave (2 views max). Re-test: `svcMapProcessCodeMemory` of a heap range to a window address, then `svcMapProcessMemory` of that alias to another address.
Applet mode (album etc.) has too little memory; the 0x120000000 reservation fails. Use title takeover (hold R) or a forwarder.
Exception handler: no signals. libnx has one handler stack: use slots taken with `ldaxr/stlxr`. Returning from the handler kills the process; use `svcReturnFromException(0)` path. ESR has no register info for some aborts: decode the instruction. NEON state can be lost on emulated reads (fix at handler entry). Eden: `cpuopt_ignore_memory_aborts` hides bad accesses; Ryujinx does not deliver data aborts at all.

## 2. Threads and priorities (the rule that fixed usf4's 10-12 fps)

- Horizon time-slices ONLY priority 0x3B (10 ms slices). Other priorities are cooperative. `svcSleepThread(0)` yields only to same-priority threads.
- Scheme used by Marathon/usf4/sa2: guest threads 0x3B; GPU ring/render 0x2C; audio output and XMA worker 0x2B; vblank thread pinned to a core. Host service threads must be numerically lower so they always preempt guest spinners.
- usf4 0.5.6: 4 job workers spinning in an XDK job-pop at 0x3B → "10-12 fps, cores at 100%, GPU 17%". Fix: yield/sleep when empty (0.5.7), then 60 fps.
- `svcSetThreadCoreMask` on a running thread can hang; each thread sets its own mask. Pinning a thread can starve it if another thread saturates the core.
- Spin locks: spin, then `svcSleepThread(10 µs)`, not yield. Critical sections: waiter counting, signal only if someone waits (kernel notify = `svcSignalProcessWideKey` syscall).
- `-mtp=soft` makes every `thread_local` a call to `__aarch64_read_tp`; keep it out of loops. Read `tpidrro_el0` for a thread id.
- Guest thread stacks of 16 MB fragment the heap (55 threads at a stage load): pool them. Marathon: 1 MB host stacks with ENOMEM retry (2 MB failed at ~23 live threads). Close thread handles (reaper).
- Fibers: hand-written aarch64 switch (x19-x30, sp, d8-d15), 24-word context. TPIDR is shared.

## 3. Time

- Host tick: `armGetSystemTick()` at 19.2 MHz (`armGetSystemTickFreq()`); NOT `CLOCK_MONOTONIC` (libnx `clock_getres(CLOCK_MONOTONIC_RAW)` is unsupported → frequency 0 → no vblank [CVHD 0.51]). Scale to the guest 50 MHz.
- Vblank: best source is the display vsync event (`viGetDisplayVsyncEvent`); a second `viOpenDefaultDisplay` fails while libnx's default window holds the display, so use the default window's display for all threads (futari). Fall back to a 60 Hz timer.
- Frame pacing: games run per frame; below 60 fps the game slows down. sa2 "relaxed vsync" shows a late frame at once but never sooner than 1/60 s after the last.

## 4. Clocks, power, thermal

- CPU 1020 MHz stock; 1785 MHz is the usual boost used by the Xbox ports ([CVHD] clkrst 1785/768/1600 with a sys-clk guard). sa2 keeps CPU stock for A/B runs and raises only the handheld GPU 307.2 → 460.8 MHz with `apmSetPerformanceConfiguration(0x92220008)` plus a watchdog that reverts the memory clock. Compare runs at equal clocks (a log can show 1020 then 1785 in one session).
- No project handles thermals in the Switch layer. Resolution 540p was the usf4 lever.

## 5. Process and files

- Exit: set `__nx_applet_exit_mode = 1` and call `__libnx_exit` after stopping own threads; returning from main or `svcExitProcess` can break hbmenu or show "closed because an error occurred". [CVHD] Probe: exit by + crashed with live threads.
- IPC buffers must not point into code-memory mappings (fs service jams): bounce buffer [CVHD]. `fsFileRead` can return short on large reads: loop in 1 MB pieces.
- SD card: every open/stat is slow (≈0.7 ms per file in a listing; 18-31 ms for save overwrite via FAT delete+create; use truncate in place). Many small files: `game_root.nxpack` + `.nxidx` single pack (FAT32 4 GB file limit). Cache directory listings in RAM; answer "file not found" from the cache.
- Logging: never `fwrite` per line on the hot thread. newlib FILE buffer is 1 KB, an SD write costs 50-150 ms (futari frames of 1,022 ms). Writer thread, 4 MB memory buffer, one write per second, drop lines when full. A sampler must not hold a lock while writing to SD.
- NACP/hbmenu: `nx_create_nro` ignores NAME/AUTHOR/VERSION unless you generate the NACP with `nx_generate_nacp` [CVHD].
- `std::filesystem::absolute("sdmc:/x")` prepends "/" on newlib and `status()` throws [CVHD]. No `copy_file`; copy file by file with stdio.
- Eden: runs the NRO with no argv; no NVK (Vulkan submits never complete): use a null GPU backend for the gate; clock getres bug and unfolded GuestPtr were found only there [CVHD]. Ryujinx runs deko3d NROs.

## 6. Renderer on Switch

- NVK costs on the A57: ~3 µs per pipeline bind, 2.4-2.9 µs per draw, 1.3-1.6 µs per descriptor update (banjo-nx measurement via sa2 docs). 1,300 draws/frame → ~25 ms ring time at 1020 MHz → sa2 moved to deko3d (render thread 17.6 → 11.1 ms for ~1,400 draws).
- usf4 holds 60 fps on NVK with 400-500 draws (ring 9-10 ms per swap, 21-24 µs per draw) after: one 1280x720 EDRAM tile, merged copies, vertex range cache, narrow sampler cache invalidation, exact constant extent, pipelines saved to SD 15 s after a new one, 4096-call deferred queue, async submit.
- A 2D sprite game with a few hundred draws should fit on NVK. Measure draws/frame and µs/draw first (the sa2 numbers are for 3D).
- Marathon on NVK: MSAA off (hangs the GPU channel), shadow maps capped at 4096 (an 8192x4 depth texture is 2 GB), NVK reports ~5 GB VRAM so force low-end settings, scale viewports for oversized targets.
- Non-sparse 512 MB shared-memory buffer is real RAM [CVHD]: shrink to the game's physical use if memory is tight.
- Pipeline cache: disk `VkPipelineCache` + list of used pipelines, pre-warm; validate the file (XXH3 header, fsync before rename) so a crash cannot leave a poisoned cache ("stuck on Now Loading" in usf4).
- Disable game-play video recording at start (`appletSetGamePlayRecordingState(false)`): the capture button crashed usf4.

## 7. Xbox 360 specifics found on Horizon

- XMA: the game's audio server decodes XMA on the kicking thread (17.3% of a core, 14 ms peaks). SDK option `audio_xma_in_worker` runs a worker at 0x2B. FFmpeg built minimal with only XMAFRAMES (Marathon `tools/build-switch-ffmpeg.sh`).
- Audio output: 4 guest frames per audout buffer (1024 samples), 3 buffers, output thread 0x2B, pump every 5.333 ms with absolute deadlines. 80-115 ms latency; add a declick stage for steps on 256-sample frame edges (futari).
- Input: positional ABXY swap; number players by first real press; HID style 0 drops with an overlay open.
- Shaders: ship no game shaders in the NRO; build them on the user's PC/in the browser installer from their own files; pin SHA-256 of inputs.
