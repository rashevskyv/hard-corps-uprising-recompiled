# ReXGlue engineering reference

Technical migration reference for Xbox 360 native recompilation projects built on ReXGlue. It records the
infrastructure, fixes, hardening, launcher work and SDK patches implemented in this repository so another developer or
AI can understand, reproduce or port them. It is not a player README; user-facing text lives in `README.md`, `docs/` and
`.github/release-notes-*.md`.

| | |
| --- | --- |
| Source project | Hard Corps: Uprising Recompiled (`saulob`, GPL-2.0-only) |
| ReXGlue | v0.10.0, tag `v0.10.0`, commit pinned in `patches/rexglue-sdk/baseline.env` |
| Branch when written | `docs/rexglue-reference` |
| Reference HEAD | `f96c66e` Fix AppImage compatibility build documentation |
| Latest release | v0.5.0 on `a0189f4`; HEAD is 4 unreleased commits later (Linux packaging) |
| Last updated | 2026-10-15 |
| History covered | all 70 commits on HEAD (60 non-merge), `839e51b` (2026-10-09) to `f96c66e` |

Not documented as implemented (not in HEAD): branches `investigate/controller-rumble` (`66e229d`) and
`debug/full-gameplay-telemetry` (`6806845`, `4a8f369`, `dd633c8`); and the FPS-unlock and native-aspect-ratio work, which
exists only in local stashes even though branches with those names point at older commits (details in
[07](07-display-window-frame-pacing.md)). They are described only under explicit EXPERIMENTAL / TEMPORARY labels.

## How to use these documents
1. Read this README, [19](19-cross-project-porting-guide.md), then [17](17-porting-checklist.md).
2. For each item you port read the topical document, its pitfalls, then the files in [20](20-source-map.md).
3. Treat the tag on each item as a porting instruction. Never copy `GAME_ADAPTATION` or `HARD_CORPS_SPECIFIC` addresses, ids or fingerprints into another game.
4. Same-numbered files in the Geometry Wars reference are intentionally comparable; neither is authoritative for the other.

### Classification
| Tag | Meaning |
| --- | --- |
| `GENERIC_REXGLUE` | SDK/runtime behavior likely relevant to almost any ReXGlue project; used conservatively |
| `REUSABLE_INFRASTRUCTURE` | Application-side launcher, user data, packaging, utilities |
| `PLATFORM_WINDOWS` / `PLATFORM_LINUX` | Reusable platform-specific behavior |
| `GAME_ADAPTATION` | Good pattern that needs the new game's code, symbols, timing or state |
| `HARD_CORPS_SPECIFIC` | Do not copy (addresses, fingerprints, ids, rules) |

### Confidence and validation
`CONFIRMED` (code, history and where noted a test back it), `LIKELY` (consistent, not proven), `UNCERTAIN`. Validation is
stated per item as automated (test named), manual (repo evidence), inferred or pending. No benchmark numbers are
invented: the repository has almost none, and impact is described qualitatively and marked unmeasured.

Path convention: paths are repository-relative. Paths such as `src/system/xmemory.cpp` next to a patch number are inside the
ReXGlue SDK tree (tag `v0.10.0`). Patch numbers are Hard Corps numbers (34 files, 0001-0035, 0020 absent).

## Documents
| # | Document | Content |
| --- | --- | --- |
| 01 | [rexglue-core](01-rexglue-core.md) | Authoritative patch inventory, mechanism, ReXApp lifecycle, Geometry Wars mapping |
| 02 | [build-system](02-build-system.md) | CMake, presets, patched-runtime guard, codegen, Linux build env |
| 03 | [launcher](03-launcher.md) | Launcher architecture, installer, settings, restart, window state |
| 04 | [platform-windows](04-platform-windows.md) | Win32 specifics, timers, GPU preference, overlay status |
| 05 | [platform-linux](05-platform-linux.md) | SDL, AppImage, RUNPATH, glibc, tested vs untested |
| 06 | [graphics-vulkan](06-graphics-vulkan.md) | Vulkan/D3D12 fixes, MSAA, adapters, RenderDoc |
| 07 | [display-window-frame-pacing](07-display-window-frame-pacing.md) | Window modes, presentation, FPS unlock and aspect ratio status |
| 08 | [input-gamepad-mouse](08-input-gamepad-mouse.md) | Input, hotkeys, shutdown order, rumble status |
| 09 | [audio](09-audio.md) | Nothing reusable |
| 10 | [user-data-settings-migration](10-user-data-settings-migration.md) | Portable user_data, migration, settings |
| 11 | [offline-services-profile-achievements](11-offline-services-profile-achievements.md) | Offline Live, achievements, license |
| 12 | [performance](12-performance.md) | Chronological and technical performance work |
| 13 | [crash-hardening-threading-memory](13-crash-hardening-threading-memory.md) | Crash and teardown fixes |
| 14 | [packaging-release](14-packaging-release.md) | ZIP and AppImage reproduction |
| 15 | [debugging-logging](15-debugging-logging.md) | Logs, diagnostics, DEBUG_SAULO |
| 16 | [reusable-game-features](16-reusable-game-features.md) | Cheat/overlay/hook architecture |
| 17 | [porting-checklist](17-porting-checklist.md) | Phased checklist |
| 18 | [change-history](18-change-history.md) | Curated history |
| 19 | [cross-project-porting-guide](19-cross-project-porting-guide.md) | What to port first |
| 20 | [source-map](20-source-map.md) | Subsystem to files |

## QUICK START FOR A NEW REXGLUE PROJECT
1. [19](19-cross-project-porting-guide.md): priority groups and port order.
2. [01](01-rexglue-core.md): patch mechanism and per-patch inventory; [02](02-build-system.md): patched-runtime guard.
3. [10](10-user-data-settings-migration.md) before any public release.
4. [13](13-crash-hardening-threading-memory.md) and [08](08-input-gamepad-mouse.md) (shutdown order).
5. [03](03-launcher.md), [05](05-platform-linux.md), [14](14-packaging-release.md).
6. Walk [17](17-porting-checklist.md) phase by phase, using [20](20-source-map.md) to find code.

## RECOMMENDED COMMON REXGLUE BASELINE
Candidates to evaluate in every new project; none should be copied automatically.

| Item | Why | Action | Where |
| --- | --- | --- | --- |
| Patched-runtime guard (pin, fingerprint, stamp, stock-hash refusal, staging check, `LINK_DEPENDS`) | A stale or stock DLL silently reintroduces fixed bugs | adopt directly (adapt target names) | [01](01-rexglue-core.md), [02](02-build-system.md) |
| `0001` physical heap release order | Allocator race corrupts page state; stress test proves it | adopt directly after `git apply --check` | [13](13-crash-hardening-threading-memory.md) |
| `0033` stop title threads + input shutdown in `OnShutdown` | Teardown ran with guest threads alive and input attached to a destroyed window | adopt directly; verify both exit paths | [13](13-crash-hardening-threading-memory.md) |
| `0010` RenderDoc load-only detection | Detection itself loaded RenderDoc | adopt directly | [01](01-rexglue-core.md), [05](05-platform-linux.md) |
| `0034` SPIR-V exponent bias (Linux Vulkan correctness) | Wrong texture exponent scaling; visual effect unconfirmed on real Linux hardware | adopt after verifying the pinned SDK still has the bug | [06](06-graphics-vulkan.md) |
| `0035` Vulkan presenter pipeline cache (performance) | Pipeline recreated after a GPU wait every paint | high value for Vulkan; check upstream issue 507 first and carry only if the bug is still present | [06](06-graphics-vulkan.md) |
| `0026`/`0027` GPU preference | Hybrid GPU machines | investigate first (relevant on hybrid GPUs; no hybrid run recorded) | [06](06-graphics-vulkan.md) |
| `0029` UI repaint with guest frames | Double presents with permanent ImGui dialogs; ABI change | adapt when the condition reproduces | [07](07-display-window-frame-pacing.md) |
| Portable user data with verified migration | Moving data after release is costly | adapt (decide the root first) | [10](10-user-data-settings-migration.md) |
| Launcher: deferred start, settings, relaunch rule, separated window state | The SDK cannot resize an open window | adapt | [03](03-launcher.md) |
| Installer architecture (fingerprint, staging, verify, swap, rollback) | Safe data install | adapt; package format is conditional | [03](03-launcher.md) |
| Release-content scans, RUNPATH and glibc checks | Catch leaks and loader failures | adopt the technique, not the lists | [14](14-packaging-release.md) |
| Performance patches (`0002`-`0009`, `0011`-`0019`, `0021`-`0025`, `0030`-`0032`) | CPU/GPU cost in this game | investigate first; measure | [12](12-performance.md) |
| Fence-wait and CRT hooks | Game-side hot paths | not copy; game-specific | [12](12-performance.md), [16](16-reusable-game-features.md) |
| License mask, profile defaults, achievement alias | Per-title behavior | not copy; inspect first | [11](11-offline-services-profile-achievements.md) |

Concept comparison with the Geometry Wars reference: both projects share the same patch contents under different
numbering (see [01](01-rexglue-core.md#8-cross-project-mapping-to-the-geometry-wars-reference-read-only-comparison)), so the
baseline above is validated by two games for those patches only to the extent each reference says so.

## Highest-value findings
1. The patch fingerprint/stamp mechanism is the most portable piece of this repository.
2. Window close hard-exits while Exit Game runs the full teardown; teardown bugs hide behind the close button ([13](13-crash-hardening-threading-memory.md)).
3. Choose the user-data root before the first release; Hard Corps shipped three releases before moving it ([10](10-user-data-settings-migration.md)).
4. ReXGlue 0.10.0 cannot resize an open window, which drives the launcher relaunch design ([03](03-launcher.md)).
5. `native_2x_msaa = false` does not disable MSAA ([06](06-graphics-vulkan.md)).
6. One bad TOML value makes the SDK ignore the whole file ([10](10-user-data-settings-migration.md)).

## Maintenance
Re-running the generation prompt updates files incrementally: read the HEAD above, inspect `git log f96c66e..HEAD`, edit
only affected documents, and update this table and [18](18-change-history.md). Long-form project documents in `docs/`
remain the source for individual features; this reference points into them.
