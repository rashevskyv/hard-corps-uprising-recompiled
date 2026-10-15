# 17 Porting checklist

Phased checklist for a new ReXGlue project, derived from how Hard Corps evolved. Each item links to the document with
the details. Items marked **inspect first** depend on the game and must not be copied from Hard Corps. During a phase
validate only what you changed (see [19](19-cross-project-porting-guide.md#validation-policy)); the comprehensive suite is
phase 12.

## PHASE 1 Initial recompilation
- [ ] Decrypt/extract the executable and record title facts ([02](02-build-system.md); project analysis in `docs/package-analysis.md`)
- [ ] Codegen manifest, then register functions reached only through pointers (`tools/find-missing-functions.py`); **inspect first**: `setjmp`/`longjmp` addresses, video mode, license mask ([02](02-build-system.md), [11](11-offline-services-profile-achievements.md))
- [ ] Pin LF line endings for the manifest, shell scripts and patches (`.gitattributes`) ([02](02-build-system.md))
- [ ] First boot to first rendered frame

## PHASE 2 Build and dependency cleanup
- [ ] Presets for Windows and Linux, SDK discovery, tests as `EXCLUDE_FROM_ALL` targets ([02](02-build-system.md))
- [ ] Decide Debug versus release configurations; remember Debug links the stock runtime ([02](02-build-system.md))
- [ ] Exe resources, icon, version metadata ([04](04-platform-windows.md), [14](14-packaging-release.md))

## PHASE 3 ReXGlue patch baseline
- [ ] Pin SDK tag and commit and stock hashes (`baseline.env`) ([01](01-rexglue-core.md))
- [ ] Patch application, fingerprint, stamp, stock-hash refusal, CMake check/staging, `LINK_DEPENDS` ([02](02-build-system.md))
- [ ] Apply `git apply --check` for each candidate patch on your SDK; skip those fixed upstream ([01](01-rexglue-core.md))
- [ ] Take correctness/hardening first, each after confirming the pinned SDK still has the bug (`0001`, `0033`, `0010`; `0034` if Vulkan) ([19](19-cross-project-porting-guide.md))
- [ ] Presentation/device/performance only when relevant: `0035` (Vulkan presenter, high value; carry only if upstream issue 507 is still present in the pinned SDK), `0026`/`0027` (hybrid GPU), `0029` (permanent ImGui dialogs); profile before carrying the rest ([12](12-performance.md))
- [ ] Respect the ABI rule: patches must not change layout of classes the game inlines ([01](01-rexglue-core.md))
- [ ] Never number temporary debug patches inside the production series ([15](15-debugging-logging.md))

## PHASE 4 Launcher integration
- [ ] Deferred guest start via `OnFinalizePaths` and `resume` ([01](01-rexglue-core.md), [03](03-launcher.md))
- [ ] Launcher/game window state kept separate; one-shot relaunch when the launcher window cannot be resized ([03](03-launcher.md), [07](07-display-window-frame-pacing.md))
- [ ] Game-data install: fingerprint, staging, verify, swap with backup, rollback; **inspect first**: package format (STFS), fingerprints, title ids ([03](03-launcher.md))
- [ ] File chooser with a fallback and a visible error when none exists ([03](03-launcher.md), [05](05-platform-linux.md))
- [ ] Error window for failures that happen before any GPU exists ([03](03-launcher.md), [05](05-platform-linux.md))

## PHASE 5 User data and settings
- [ ] Choose the user-data root before the first public release; portable vs system ([10](10-user-data-settings-migration.md))
- [ ] Verified rename migration with copy fallback and marker; test failure paths ([10](10-user-data-settings-migration.md))
- [ ] Settings file tolerant of old values; project-owned cvars derived into SDK cvars ([10](10-user-data-settings-migration.md))
- [ ] First-run behavior; **inspect first**: how the game reacts to unsaved title/profile settings, profile ids, achievement schema ([11](11-offline-services-profile-achievements.md))

## PHASE 6 Graphics and display
- [ ] GPU preference on hybrid machines when relevant, validate on suitable hardware (`0026`/`0027`) ([06](06-graphics-vulkan.md))
- [ ] Inspect first: present only once per guest frame, if the project reproduces the double present from permanent UI dialogs (`0029`) ([07](07-display-window-frame-pacing.md))
- [ ] MSAA option labels match behavior ([06](06-graphics-vulkan.md))
- [ ] Windowed/borderless/fullscreen and saved state ([07](07-display-window-frame-pacing.md))
- [ ] **Inspect first**: aspect ratio and ultrawide (does the game render a wider world, or only stretch?) ([07](07-display-window-frame-pacing.md))

## PHASE 7 Input and controller
- [ ] Input shutdown before window destruction in `OnShutdown` ([08](08-input-gamepad-mouse.md), [13](13-crash-hardening-threading-memory.md))
- [ ] Input gating while overlays/dialogs are open; cursor behavior ([08](08-input-gamepad-mouse.md))
- [ ] Hotkeys that do not collide with the SDK's own ([08](08-input-gamepad-mouse.md))
- [ ] Rumble: none at HEAD here; start from [08](08-input-gamepad-mouse.md) for what was investigated

## PHASE 8 Performance and frame pacing
- [ ] Profile before porting any performance patch; measure each group ([12](12-performance.md))
- [ ] **Inspect first**: frame-rate hooks, fixed time step, `VdSwap` or equivalent, fence-wait loops ([07](07-display-window-frame-pacing.md), [12](12-performance.md))
- [ ] Windows timer resolution and Linux wait contention patches only with a measurement ([12](12-performance.md))
- [ ] Keep logging off hot paths ([15](15-debugging-logging.md))

## PHASE 9 Crash hardening
- [ ] Allocator stress test, title-thread shutdown test, shutdown-order test ([13](13-crash-hardening-threading-memory.md))
- [ ] Test both exit paths: window close hard-exits, Exit Game runs full teardown ([13](13-crash-hardening-threading-memory.md))
- [ ] Hooks reachable at exit use immortal or flag-guarded state ([13](13-crash-hardening-threading-memory.md))
- [ ] **Inspect first**: whether the game relies on `setjmp`/`longjmp` ([01](01-rexglue-core.md))

## PHASE 10 Linux support
- [ ] Application id before window creation; native window handle fallback ([05](05-platform-linux.md))
- [ ] RenderDoc load-only detection ([01](01-rexglue-core.md), [05](05-platform-linux.md))
- [ ] RUNPATH check, no bundled Vulkan loader, glibc symbol check ([05](05-platform-linux.md), [14](14-packaging-release.md))
- [ ] State honestly what was tested (WSL, headless, real hardware) ([05](05-platform-linux.md))

## PHASE 11 Packaging
- [ ] Allow-list based Windows ZIP and AppImage packagers; forbidden-content scan (technique reusable, list per project) ([14](14-packaging-release.md))
- [ ] Verify patched runtime after staging, before packaging ([02](02-build-system.md), [14](14-packaging-release.md))
- [ ] Checksums, licenses, release notes ([14](14-packaging-release.md))

## PHASE 12 Final release validation
- [ ] Full build, zero new warnings, `git diff --check`
- [ ] All `hcu_*` style test targets for your project, patch applicability on both platforms
- [ ] Launcher and persistence, fresh and upgraded user data, DPI representative cases
- [ ] Both exit paths, repeated start/quit
- [ ] No `DEBUG_SAULO`-style temporary code, local paths or leftovers in logs and packages ([15](15-debugging-logging.md))
- [ ] Real hardware run per OS; record anything not verified in the release notes
