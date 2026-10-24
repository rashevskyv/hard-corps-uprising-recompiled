# Runtime: kernel, timing, threads, audio, input, content, XBLA

Tags: [CODE] source read, [DOC] doc only. Working notes: `x360/notes_kernel.md` (temp).

## 1. XEX and containers

- XEX2 encryption type 1 means the retail key OR the all-zero devkit key; the header does not say which. Stock XenonRecomp tries only retail and returns an empty image silently. DR2 Case Zero needs devkit. [CODE DR2 `docs/xenonrecomp-upstream-bugs.md`]
- Compression: 1 = BASIC (list of data/zero-run blocks), 2 = LZX. A wrong parse gives garbage without an error. SotN XBLA is BASIC, DR2 CZ is LZX, CVHD default.xex 3.4 MB.
- Retail key `20B185A59D28FDC340583FBB0896BF91`. No signature check on the base image. Only XEXP patches are RSA-checked. [DOC Nocturne]
- Title update: `default.xexp` delta. ReXGlue applies it at run time (`XexModule::ApplyPatch`) but codegen reads the base XEX only in stock 0.10 → run-time patch changes code the recompilation does not have. Nocturne feeds the xexp to codegen (`build.py --tu`). UnleashedRecomp installer applies it offline with `XexPatcher::apply(base, patch, out)` (source not in tree). Pin ONE build: retail or TU, one region; configs are hash-specific.
- Containers: CON `0x434F4E20`, LIVE `0x4C495645`, PIRS `0x50495253`; STFS hash-block math in `UnleashedRecomp/install/xcontent_file_system.cpp:179-214`. Free60 layout. SVOD for discs.
- Extra code modules: guest DLLs loaded with XexLoadImage become `[[modules]]` in the manifest (sa2-nx; futari-nx has 8 game cores). Check the prologue before assuming code: `mflr r12` = Xenon MSVC, `mflr r0` = CodeWarrior leftovers (SA2's 76 `.rel` files were GameCube leftovers, not live). [DOC sa2-nx plan.md]
- Content: `game:\`/`d:\` → read-only extracted folder (case-insensitive lookup), `cache:\` writable (Shift 2 loops its attract movie without it), saves under XUID/titleid/00000001 folders. DLC (content type 0x2) mounted as data packages.

## 2. Kernel exports

- ReXGlue: `REX_EXPORT`, `REX_EXPORT_STUB` (logs WARN, returns), `REX_EXPORT_STUB_RETURN`; flags kImplemented/kStub/kSketchy/kHighFrequency/kBlocking. Tables `src/kernel/xboxkrnl/export_table.inc` (922 entries), `xam/export_table.inc`. [CODE]
- Exports are weak: `REX_NATIVE_HOOK(__imp__X)` in the game project overrides one without editing the SDK (CrownPark; Geometry Wars answers XGetLanguage/XGetGameRegion/ExGetXConfigSetting natively). [DOC]
- Method: weak logging stubs → run → implement by call frequency (one port needed 28 of 118 imports natively). `REX_TRACE_IMPORTS=<file>` lists imports used. Missing function mid-run: `unregistered_function_nonfatal` logs `[UNREGFN]` with caller/r3-r5 and returns null (CrownPark).
- Seen implemented/stubbed: Ke/Nt threading and events, KeTls*, KeDelayExecutionThread (APC aware), KeQueryPerformanceFrequency (50 MHz), KeInsertQueueDpc (stock 0.10 never ran the DPC! fixed in the fork), Vd* video (VdInitializeRingBuffer, VdEnableRingBufferRPtrWriteBack, VdSetGraphicsInterruptCallback, VdSwap), XAM content (XamContentCreateEx/Close), XamUserGetXUID/SigninState (answer XUSER_INDEX_ANY), XGetVideoMode, XNotify*, XMsg*, XAudio render driver, XamInput*.
- `headless = true` answers system prompts (storage device pick, sign-in) with defaults; ABC went from 11 to 68 pipelines because dialogs stopped blocking. `xbox_live` off: sockets start but no link, online menus disable themselves. Achievements: SDK `achievements.cpp`; apps stub avatar awards/marketplace (sa2-nx does in the app, not the SDK).
- CVHD run: xam ordinals 2815/2816/2827/2832 missing (harmless so far; not documented elsewhere).

## 3. Time

- Guest timebase 50,000,000 Hz (`runtime.cpp:108`); `mftb` → `REX_QUERY_TIMEBASE()` (see performance.md). CrownPark measured 49,997,146 Hz: fine.
- Vblank interval = guest_tick_frequency / refresh_hz (`graphics_system.cpp:156-159`). The vsync worker must have a working host clock (CVHD: host tick freq 0 = no vblank = hang).
- Nocturne steady pacer: a host thread drives the game's fixed-step clock because vblank jitter made fps hover at 58-59. Windows sleep granularity 15.6 ms needs timer resolution raised.
- 60 fps patches: Unleashed clamps delta time with mid-asm hooks (`patches/fps_patches.cpp`); stuntdouble rewrites the D3D present interval; MCLA overrides two timing functions. Busy-spin rate limiters become near-free loops after recompiling; only hooks remove them.

## 4. CPU semantics worth knowing

- Reservations `lwarx/stwcx` emitted as atomics with byteswap (`builders/memory.cpp:208-214, 411-421`). `dcbz` zeroes 32/128 bytes (`system.cpp:150-167`). `sync/lwsync/eieio` need real fences on ARM; x86 ports got them for free. Relaxed CAS between compiler barriers is cheaper (futari-nx research).
- FPSCR: host FP exception masks must stay masked on EVERY thread that runs guest code (SIGFPE / 0xC000008F in two projects). FP flush mode set per instruction; FPCR writes cost 3-6% of the game thread on ARM (futari research).
- ARM64 only: XenonRecomp `HostToGuest` rounding table is x86-ordered, so `mffs` may swap +inf/-inf rounding on ARM [CODE read, not run]. VMX pack ops need care (`vpkuhus128` used signed input → 0x9000 saturated to 0; `vpkuhus` alias fix).
- Guest threads become host threads or fibers (CRT fibers via `ConvertThreadToFiber`; Bionic and newlib have no ucontext: write an aarch64 context switch).
- setjmp/longjmp: declare their addresses before the first run.

## 5. Audio

- XMA: FFmpeg `AV_CODEC_ID_XMAFRAMES` (Xenia fork) in `src/audio/xma_context.cpp:68`; 128 KB packet buffer; games use XMA* calls or write the 64-byte context directly, both must work. devkitPro `switch-ffmpeg` has an XMA decoder [CVHD note], GPL concerns for the whole package: check before linking.
- Audio worker waits on 64-sample semaphores and calls the guest render callback; it sleeps 500 ms when idle (so a 49% core means the multi-wait polled, not that audio is busy).
- CrownPark: pace the render callback on an even 187.5 Hz schedule (credits arrived in clumps → slow robotic audio, 69% repeats → 0%). XMA ring starvation must collapse the write offset and clear `output_buffer_valid` like xenia-canary. `audio_native_xma` (mix decoded XMA bypassing the guest mixer) produced static: per-voice state lives in the guest's FMOD. Do not try it a third time.
- `audio_channels`: 2 = ITU BS.775 fold (a naive fold clips: LFE was 27% of energy in Hydro Thunder).
- WMA/XMP playlists (EA titles): native ASF reader + FFmpeg wma decoders.

## 6. Input and UI

- Backends `xinput`, `sdl`, `mnk`, `nop`. On Switch write an `nx` pad driver (libnx hid, positional ABXY swap). `XamInputGetState` is called from any guest thread; keep the device table under a recursive mutex.
- Drive tests with in-process pad scripts, not window messages.

## 7. Process lessons that cost projects days

- Never hand-edit generated files; use midasm hooks and weak overrides.
- Declare setjmp/longjmp first. Pin one build (TU/region). Enable codegen optimisations after a working boot.
- Collect missing functions in batches (stub sweeps) instead of one rebuild each.
- A watchdog for guest waits with return addresses turns "froze" into a function name.
- Test runs must not take over the desktop: muted, minimized (sa2-nx rule).
- Build the same generated code on PC first: every translation bug is far easier to find there (sa2-nx plan).

## 8. Wait/sleep ABI traps (PrototypeRecomp boot notes, [DOC])
`KeDelayExecutionThread(mode, alertable, interval_ptr)`: the interval is the THIRD argument, in 100 ns units; negative = relative, positive = absolute; convert x100 to ns. Every Ke/Nt wait needs both forms. Mistakes make every sleep a no-op and spin the guest at 100%. ReXGlue's `KeDelayExecutionThread_entry` has the right argument order [CODE]; check any hand-written runtime.
