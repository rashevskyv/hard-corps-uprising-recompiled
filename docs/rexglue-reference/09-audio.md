# 09 Audio

Hard Corps: Uprising has no reusable audio implementation. Audio is entirely ReXGlue's (guest XAudio/XMA emulation, the
SDL audio driver, the audio worker). The port adds no audio hook, device selector, volume control, mute option or
compatibility fix of its own, and no audio-specific SDK patch.

## What was checked

- `src/`, `tools/`, `tests/`, `config/`, `CMakeLists.txt`, `packaging/` and the 34 patches in `patches/rexglue-sdk` were
  searched for audio, XMA, XAudio, mixer, volume, PipeWire, ALSA and Pulse terms. The only source-level matches are a
  comment in `src/launcher_achievements.h` and the use described below; no patch hunk edits audio code.
- All local branches were searched too. The unmerged `debug/full-gameplay-telemetry` branch (branch-only, experimental)
  logs an `audio_mute` cvar value in its session header with "device=not captured"; it changes nothing.
- Release notes and docs were searched; findings follow.

## Facts the repo does contain

| Item | Detail | Class | Confidence |
| --- | --- | --- | --- |
| First boot | `docs/first-boot.md`: the guest "initialized audio" during the first Xenos-less run. Nothing records that sound was audible | - | CONFIRMED (log-level only) |
| Launcher runtime without audio | `src/launcher_achievements.cpp` builds a `rex::Runtime` with `config.tool_mode = true` to read the achievement list from `default.xex`; `src/launcher_achievements.h` states this runtime has no graphics, audio or input. Reusable pattern for reading title metadata without devices | REUSABLE_INFRASTRUCTURE | CONFIRMED (code) |
| Patch 0021 `posix-wait-multiple-contention` (Linux) | `WaitMultiple` spun on lock contention while the SDL audio callback released the audio worker's client semaphores; it now sleeps 100 us between retries. The 42% CPU figure in `docs/rexglue-runtime-patch.md` was measured in another recompilation project (The Simpsons Game) and is not a measurement of this game | GENERIC_REXGLUE, PLATFORM_LINUX | CONFIRMED (code); effect here not measured |
| Patch 0033 `stop-title-threads` | explicitly leaves runtime threads (vblank, audio, kernel dispatch) to their subsystems; only guest title threads are stopped before teardown. See [13](13-crash-hardening-threading-memory.md) | GENERIC_REXGLUE | CONFIRMED (code, test `tests/title_thread_shutdown_tests.cpp`) |
| Linux build dependencies | `tools/setup-linux-build-env.sh` installs ALSA and Pulse development packages and builds PipeWire 0.3.48 headers/libraries into `/usr/local` so SDL can build its PipeWire backend; the AppImage bundles no audio library (SDL loads PipeWire at run time when present) | PLATFORM_LINUX | CONFIRMED (script) |
| Linux audio status | `docs/linux.md`: rendering, audio, controllers and performance are "largely unconfirmed" on real hardware; v0.2.0 notes list audio as untested; later notes repeat "audio ... may still have problems" | PLATFORM_LINUX | CONFIRMED statement; behavior UNCERTAIN |

No audio device selection, sample-rate, latency, crackle or sync issue is recorded anywhere, on either platform, so
nothing is claimed fixed. Whether sound works correctly on Windows is also not stated in the repo; it is simply not
documented as a problem.

## For another project

- Nothing to port. Check your first-boot log for the audio endpoint line and test audible output by hand on both
  platforms before assuming it works.
- If a Linux build shows a busy audio worker, look at `WaitMultiple` first (0021) and see [12](12-performance.md).
- Keep audio threads out of any "stop everything" shutdown logic; see 0033 in [13](13-crash-hardening-threading-memory.md).
- Linux packaging and the audio backends SDL needs at build time: [05](05-platform-linux.md).
