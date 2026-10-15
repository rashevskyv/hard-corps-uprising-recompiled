# 18 Change history

Curated engineering history of the repository (HEAD `f96c66e`, 70 commits on HEAD, 60 non-merge; 2026-10-09 to
2026-10-15). Only changes that explain a reusable system, a known failure or a superseded approach are listed; README
polish, badge edits and merge commits are omitted. Hashes were checked with `git cat-file`. Release tags: `v0.1.0`
(on `c2f80af`), `v0.2.0` .. `v0.5.0` (see the release commits below). `main` is at `a0189f4` (v0.5.0); HEAD is four
commits later and unreleased.

Branches not in HEAD (not documented as implemented): `investigate/controller-rumble` (`66e229d`) and
`debug/full-gameplay-telemetry` (`6806845`, `4a8f369`, `dd633c8`). See [08](08-input-gamepad-mouse.md) and
[15](15-debugging-logging.md).

## 1. Bring-up (2026-10-09 to 2026-10-10)

| Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- |
| `839e51b` | Initialize recompilation project | project | Repository root | [20](20-source-map.md) |
| `6b99a71` | Package analysis | analysis | STFS layout, title facts used later by the installer | [03](03-launcher.md) |
| `0b6b5a9` | ReXGlue codegen setup | codegen | Manifest, `license_mask`, first generated sources | [02](02-build-system.md), [11](11-offline-services-profile-achievements.md) |
| `99dc9f7` | Minimal native build | build | First linkable executable on SDK 0.10.0 | [02](02-build-system.md) |
| `e412e6a`, `8421905` | Enable Xenos, first rendered frame | boot | Path from first boot to first frame | [02](02-build-system.md) |
| `5b2c010`, `2f385e1` | Register missing function, batch discovery | codegen | Origin of `tools/find-missing-functions.py` and `config/discovered-functions.toml`: indirect-call targets must be registered | [02](02-build-system.md) |
| `1f2b0b2`, `8d6558b` | Keyboard/controller input, F11 and title-bar FPS | input, window | First input mapping and window toggle; a temporary input debug wrapper from `1f2b0b2` was later removed (see 15) | [08](08-input-gamepad-mouse.md), [07](07-display-window-frame-pacing.md) |

## 2. Launcher and distribution (v0.1.0 to v0.3.0)

| Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- |
| `dc89a6c`, `5ee45cf` | TRG launcher and settings UI; integration review | launcher | Deferred guest start (`OnFinalizePaths`), settings model, one-shot relaunch because the 0.10.0 window cannot be resized; on/off settings repair | [03](03-launcher.md) |
| `b31b959` | Package installer and Windows packaging | installer, packaging | STFS extraction, fingerprint, staging, verify, swap, rollback; first ZIP packager | [03](03-launcher.md), [14](14-packaging-release.md) |
| `9964f82` | Native Linux build and AppImage (experimental) | Linux | Linux platform layer, AppImage, STFS file-type fix | [05](05-platform-linux.md), [14](14-packaging-release.md) |
| `56dc167` | v0.2.0 | release | First Linux release | [14](14-packaging-release.md) |
| `f66eeca`, `d1f1a19`, `9bd071d` | DLC install/uninstall, review, iterator UB fix | installer | Marketplace content as root `dl`; test caught an undefined-behavior iterator bug | [03](03-launcher.md) |
| `9fa3986` | v0.3.0 | release | | [14](14-packaging-release.md) |
| `28c0251`, `9005c85`, `08f0fd9` | Cheat system and F10 menu | game features | Runtime-toggle architecture | [16](16-reusable-game-features.md) |
| `f3ac390` | Offline achievements, leaderboards, downloads | offline services | Local Live replacement, achievement store, modal dialog base | [11](11-offline-services-profile-achievements.md) |

## 3. Release hardening (v0.3.1 to v0.3.2)

| Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- |
| `ab4f7b7` | v0.3.1: physical heap release order | SDK patch 0001 | First SDK patch, patch/stamp/baseline mechanism, `physical_heap_stress`. Fix confirmed by the stress test; link to the reporter's crash never proven | [01](01-rexglue-core.md), [13](13-crash-hardening-threading-memory.md), [02](02-build-system.md) |
| `26bd37c` | Application icon | packaging | `.rc`/ICO pipeline, application id | [14](14-packaging-release.md), [04](04-platform-windows.md) |
| `27e26ea` | Portable user data | user data | v0.1.0 to v0.3.1 shipped data in the system folder; this commit adds the root selection, verified migration and copy fallback. Decide the root before the first release | [10](10-user-data-settings-migration.md) |
| `274ee61` | FPS counter, overlay improvements | overlay | `ModalDialog` close behavior | [16](16-reusable-game-features.md), [11](11-offline-services-profile-achievements.md) |
| `e064dd9` | Launcher window state, cursor auto-hide | launcher, window | Launcher and game display state kept separate | [03](03-launcher.md), [07](07-display-window-frame-pacing.md) |
| `09650bd` | v0.3.2 | release | | [14](14-packaging-release.md) |

## 4. Performance releases (v0.4.0 to v0.5.0)

One evolution, four commits: first the patch workflow and GPU work, then CPU waits, then transfers and fence waits.
Superseded: the first build-time debug patch (`0006-debug-saulo-gpu-thread-stats`, later renumbered) was deleted in
`bbd02f2`; the numbering was reused by the RenderDoc patch.

| Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- |
| `0ec7ded` | Optimize GPU runtime and patch workflow | patches, build | Patches 0002-0005 (as numbered then), fingerprint, `build.ps1` auto-update of the patched runtime; fixes stale-DLL root cause (build script never checked the stamp) | [01](01-rexglue-core.md), [02](02-build-system.md), [12](12-performance.md) |
| `b681177` | CPU waits and render target transfers | patches | Timer/wait rework, original sleep timer | [12](12-performance.md), [04](04-platform-windows.md) |
| `e950ca3` | GPU transfers and CPU fence waits | patches, game hook | Origin of the fence-wait pattern (`src/gpu_fence_wait.cpp`) | [12](12-performance.md), [16](16-reusable-game-features.md) |
| `bbd02f2` | Remove debug instrumentation | cleanup | Debug patch removed before release | [15](15-debugging-logging.md) |
| `3020c32` | Prevent unintended RenderDoc loading | patch 0010 | Detection used to load RenderDoc; now load-only detection | [01](01-rexglue-core.md), [05](05-platform-linux.md) |
| `dc6feb8` | Linux runtime and Vulkan parity | patches, Linux | Per-platform patch split, Vulkan twins 0016-0019, `LINK_DEPENDS` on Linux | [01](01-rexglue-core.md), [12](12-performance.md) |
| `81444f0` | v0.4.0 | release | | [14](14-packaging-release.md) |
| `50b7948` | Skip intro option | launcher, hook | Launcher option driving a game-state hook | [16](16-reusable-game-features.md) |
| `cf6eee1` | Frame pacing and GPU performance | patches, display | Patches 0021-0032 (resolve, WAIT_REG_MEM vblank wake, GPU preference, MSAA, UI repaint, deferred writeback), ThinLTO, MSAA option rework | [01](01-rexglue-core.md), [07](07-display-window-frame-pacing.md), [06](06-graphics-vulkan.md) |
| `418174b` | Shut down input before destroying the window | shutdown | Use-after-free of the destroyed window in the input driver detach (Exit Game hang) | [13](13-crash-hardening-threading-memory.md), [08](08-input-gamepad-mouse.md) |
| `abd31d2` | Stop title threads before runtime teardown | patch 0033 | Guest threads running while guest memory is unmapped | [13](13-crash-hardening-threading-memory.md), [01](01-rexglue-core.md) |
| `430fc32` | Harden patched builds, Vulkan texture exp adjust | build, patch 0034 | Stronger fingerprint, CMake check/staging targets, `build.sh`; Linux SPIR-V `exp_adjust` | [02](02-build-system.md), [06](06-graphics-vulkan.md) |
| `3b4a229` | Cache Vulkan swapchain paint pipeline | patch 0035 | Pipeline rebuilt on every paint | [06](06-graphics-vulkan.md) |
| `a0189f4` | v0.5.0 | release | Tagged release | [14](14-packaging-release.md) |

## 5. Post-v0.5.0 Linux packaging (unreleased at HEAD)

| Commit | Subject | Subsystem | Why it matters | Doc |
| --- | --- | --- | --- | --- |
| `df29fe2` | AppImage compatibility with older glibc | packaging | Build baseline moved to an older glibc (2.31) with bundled libstdc++ fallback; no evidence in the repository that this AppImage was built and run | [14](14-packaging-release.md), [05](05-platform-linux.md) |
| `536dfdf` | Artifact rename | packaging | Name required by the AppImage hub; release notes still use the old name | [14](14-packaging-release.md) |
| `6cf59bd` | Error window when Vulkan is unavailable | Linux, launcher | Message box instead of a silent exit | [03](03-launcher.md), [05](05-platform-linux.md) |
| `f96c66e` | AppImage build documentation | docs | | [14](14-packaging-release.md) |

## Superseded approaches worth remembering

- Build-script blindness to stale runtimes (before `0ec7ded`): see section 4 and [02](02-build-system.md).
- System-folder user data (v0.1.0 to v0.3.1): replaced by `27e26ea`; the migration code now has to live forever.
- RenderDoc detection by loading (before `3020c32`).
- Debug-stats patch numbered inside the production series (deleted `bbd02f2`); keep temporary patches out of the numbered series.
