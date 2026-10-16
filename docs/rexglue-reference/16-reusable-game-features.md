# 16 Reusable game features: cheats, runtime menu, overlays, skip intro, FPS options

Scope: the game-level features built on top of ReXGlue in this project, separated into **reusable patterns** and **Hard Corps specific addresses**. Nothing in this file is a generic SDK capability. Every guest address, vtable, struct offset, script-command id, state number and tuned constant below is `HARD_CORPS_SPECIFIC` and must be rediscovered for another game. Related: [07-display-window-frame-pacing.md](07-display-window-frame-pacing.md) (aspect ratio, FPS unlock), [08-input-gamepad-mouse.md](08-input-gamepad-mouse.md) (hotkeys, input gating, rumble), [03-launcher.md](03-launcher.md) (settings UI), [10-user-data-settings-migration.md](10-user-data-settings-migration.md) (persistence), [11-offline-services-profile-achievements.md](11-offline-services-profile-achievements.md) (achievements data), [15-debugging-logging.md](15-debugging-logging.md).

Tags: `GENERIC_REXGLUE`, `REUSABLE_INFRASTRUCTURE`, `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `GAME_ADAPTATION`, `HARD_CORPS_SPECIFIC`; `CONFIRMED` / `LIKELY` / `UNCERTAIN`; validation `automated` / `manual` / `inferred` / `pending`. Status labels for anything not in HEAD: HEAD IMPLEMENTATION / HISTORICAL IMPLEMENTATION / EXPERIMENTAL BRANCH / TEMPORARY DIAGNOSTICS.

## 0. Status table

| Feature | Label | Origin |
| --- | --- | --- |
| Seven cheats (health, lives, rapid fire, jump, keep weapons, movement speed, super armor) | HEAD IMPLEMENTATION | `08f0fd9`, `9005c85`, `28c0251` (Super Armor and Movement Speed 2x arrived by `28c0251` per `src/cheats.h` history) |
| F10 in-game cheat menu with session overrides | HEAD IMPLEMENTATION | `28c0251` |
| Achievements overlay on F7 and from the game's own menu item; download-content notice | HEAD IMPLEMENTATION | `f3ac390`, `274ee61` |
| FPS counter (title bar, F2 overlay) | HEAD IMPLEMENTATION | `8d6558b`, `274ee61` |
| Skip intro | HEAD IMPLEMENTATION | `50b7948` |
| Multisampling choice (2x / 4x compatibility) | HEAD IMPLEMENTATION | `cf6eee1` |
| Native aspect ratio (16:10 / 21:9 / 32:9 / 4:3 / 5:4) | HEAD IMPLEMENTATION (`release/next`) | see 07 |
| FPS unlock 120/144 with interpolation | EXPERIMENTAL BRANCH (stash `404bc74`; not in HEAD) | see 07 |
| Rumble | none in HEAD; diagnostics only on `investigate/controller-rumble` (`66e229d`), TEMPORARY DIAGNOSTICS | see 08 |
| Full-gameplay telemetry (F9 markers etc.) | TEMPORARY DIAGNOSTICS, branch `debug/full-gameplay-telemetry` | `6806845`, `4a8f369`, `dd633c8` |

Branch caution: `feature/high-frame-rate` is an ancestor of HEAD but its tip contains none of this work (CONFIRMED with `git merge-base --is-ancestor` plus reading the tip); the code is in a stash.

## 1. Hook patterns used throughout (`REUSABLE_INFRASTRUCTURE`)

All gameplay features are guest function hooks declared in game source, not SDK patches, compiled into the executable with the recompiled game:

1. **Pass-through hook.** `REX_EXTERN(__imp__sub_XXXXXXXX); REX_HOOK_RAW(sub_XXXXXXXX) { ...; __imp__sub_XXXXXXXX(ctx, base); ... }`. Code before the call edits arguments in `ctx` (PPC registers: integer in `ctxr3.u32`, floats in `f1.f64`); code after edits the result or state. `base` is the guest memory base, accessed through byte-swapping helpers (`src/guest_memory.h`: `LoadBE16/32/64`, `StoreBE*`).
2. **Replacement hook.** Return without calling `__imp__` (e.g. Infinite Lives skips the stock decrement).
3. **Caller-keyed hook.** Read `ctx.lr` to decide the call site matters (the fence wait hook only acts on one return address; `src/gpu_fence_wait.cpp`).
4. **Scoped thread-local flags.** A hook on a script-command handler sets a `thread_local` flag/target while it runs so that a *shared* low-level setter (jump, velocity, cooldown) only changes values when called from that command (rapid fire, movement speed, super armor use this). It is why the same setter used by enemies is not affected.
5. **Class gate.** Only act when the object's vtable equals the player class vtable (`cheats::IsPlayer`); enemies use other classes.

Pitfall for porting: hooks reference generated function names (`sub_8240D8D0`) that change if the XEX is re-analysed or the codegen configuration changes. The same addresses can also be wrong for a different title version; this port is pinned to one executable (CRC32 `3DF903B9`, see `docs/launcher.md`). Porting rule: **concept only** for every address in this file.

Validation approach: pure logic (the numeric rules of each cheat) is in headers without game memory and is automated (`hcu_cheats_tests`, `tests/cheats_tests.cpp`: player classification, life delta, stock value, rapid-fire cooldown, jump speed, keep weapon, movement speeds, run speeds, super armor, super-armor independence, hit grace, session cheats). The hooks themselves were verified in the running game by the author (manual; these runs are not reproducible from the repo).

## 2. Cheats architecture (HEAD IMPLEMENTATION)

Files: `src/cheats.h` (pure rules, no SDK), `src/cheats.cpp` (hooks, cvars, session layer), `src/cheat_menu.*` (F10 UI), `tests/cheats_tests.cpp`.

### 2.1 Layers

1. **Saved value**: seven `REXCVAR_DEFINE_BOOL(hcu_cheat_*)`, default false, written to `settings.toml` only by the launcher's Cheats page (see 03, 10).
2. **Session override layer** (`cheats::SessionCheats`): per cheat a tri-state in `std::atomic<uint8_t>` (follows saved / forced off / forced on). `Effective(cheat, saved)` is what hooks call through `cheats::Enabled`. Setting a cheat to the saved value clears the override. In memory only; gone at exit.
3. **Hooks** read `Enabled(...)` every time they run, so turning a cheat off restores the game's behaviour with no state to undo.

Why a separate layer instead of changing the cvar: ReXGlue's own F4 settings overlay has "Save to config", which calls `rex::cvar::SaveConfig` and writes **every modified cvar** to the shipped TOML. A menu that flipped the cvar would be persisted the next time anyone pressed that button, silently turning session cheats into saved ones. Classification: `REUSABLE_INFRASTRUCTURE`; confidence CONFIRMED (documented in `docs/launcher.md`, structure visible in code). Validation: automated `session cheats`. Porting: **adopt directly** for any runtime menu that must not write config.

### 2.2 Individual cheats (mechanism and why it is safe)

| Cheat | Mechanism | Safety rule | Notes |
| --- | --- | --- | --- |
| Infinite Health | hook both player add-life methods; clamp negative deltas to zero (`ProtectedLifeDelta`) | player vtable check | healing unchanged; the kill path that sets life to zero directly is not hooked, so scripted deaths still happen |
| Infinite Lives | skip the stock decrement on respawn; clamp a set-stock call so it cannot lower the stock | player gate | game clamps the stock to 99 (`kMaxStock`); co-op continue lowers the partner stock through the set-stock call, hence both hooks |
| Rapid Fire | halve the script-set shot cooldown (frames); `carry` alternates rounding so the average is exactly half | only while the script command that sets cooldown runs (thread-local flag), because the per-frame countdown calls the same setter | weapons keep relative speeds; one character's attacks share the cooldown |
| Jump Height 2x | multiply the launch speed of the shared jump function by sqrt(2) | gravity is constant, so apex scales with speed squared; hook applies to players only | moves that set a velocity without calling jump are not scaled |
| Keep Weapons on Hit | disarm the drop flag that a hit arms (`ArmWeaponDrop`) | a hit that leaves life at zero still drops the weapon (`KeepsWeaponOnHit(life)`) | independent from Infinite Health |
| Movement Speed 2x | double the horizontal speed that character scripts set for ordinary movement: walking, air steering and run states | only calls made by the two velocity script commands on the player's own object; a table of known locomotion speeds plus a set of run-state ids avoids doubling dashes/slides | state numbers and speeds are tuned per character; run doubles horizontal only so run jumps are not raised |
| Super Armor | take damage without push or the hit state: hooks on the hit handler, the accumulated-damage getter (reports a value below the stagger threshold, so the handler skips the state change like an enemy armor hit) and the push predicate (returns no); a 1.0 s per-victim grace window (`HitGrace`) ignores further hits of a volley | living players only; lethal hit stays the game's own | the grace window exists because, without the hit state, nothing stops the next hit of a volley or of sustained contact (code comment); no earlier design is recorded in committed history |

Every numeric constant (cooldown rule, `0.656` speeds, state ids, offsets such as life and stock) is `HARD_CORPS_SPECIFIC`. The *shapes* (clamp a delta, clamp a stock, scale a shared setter only inside a command, grace window against volleys) are reusable ideas. Confidence CONFIRMED for the code; "works in game" per author testing (manual). Quantitative balance: untuned beyond author testing.

### 2.3 What to build first in a new project

1. Locate the player object and its class (vtable) by tracing the first hook you can verify (life or score).
2. Build the **pure rule** in a header and unit-test it.
3. Hook as narrowly as possible (call-site and class gated).
4. Add the session layer before the menu.

## 3. In-game menu: F10 cheat menu (HEAD IMPLEMENTATION)

- A `ModalDialog` (project subclass of `rex::ui::ImGuiDialog`) on the ImGui drawer ReXApp already owns; it adds no second ImGui, backend or window. Fonts and the accent are shared with the launcher (`LauncherRegularFont`).
- Opened and closed by F10 through an input listener at z-order 1000, so the key is consumed before the guest; Escape closes via the shared `EscapeKey` listener; Up/Down or D-pad moves, Enter or A toggles, B closes, mouse works.
- While open, `CheatMenu::GameInputActive` returns false and is installed as the input system's active callback, so the guest sees an untouched controller, and keys/buttons pressed in the menu keep the game blocked until released (max 1 s, `CloseGate`) so the press used to toggle is never seen by the game as a held button.
- Rows are a static table; `static_assert` ties the row count to `kCheatCount`.
- Classification: `REUSABLE_INFRASTRUCTURE`. Porting: **adopt directly**; replace the row table. Validation: manual and `hcu_live_tests` for the overlay rules. Details in [08](08-input-gamepad-mouse.md#3-launcher-versus-game-navigation-gamepad-into-imgui).

## 4. Runtime toggles and game-state detection

- **Runtime toggles with hot application**: the frame counter (`hcu_show_frame_counter`, F2 flips the cvar through `rex::cvar::SetFlagByName`, not saved), the cursor (always on), cheats (session layer). The pattern for toggles that must apply immediately: read the cvar/override inside the hook or draw callback every time.
- **Settings that apply only at start** (cannot be hot): window size, monitor, VSync (relaunch), render resolution (GPU start), multisampling (`ApplyMultisampling` before emulation starts), skip intro (read when the boot director starts), aspect ratio (video mode at start). Pattern: `GameSettings::Capture` records what the process started with and `Apply` sets cvars when Play is pressed. See [03](03-launcher.md), [10](10-user-data-settings-migration.md).
- **Game-state detection in HEAD** is deliberately small: UI state is detected by `ModalDialog::AnyOpen()`; gameplay objects by vtable; the "launcher or game" split by `show_launcher_`. There is no general "is in stage" detector in HEAD.
- **Game-state detection in the FPS experiment** (EXPERIMENTAL BRANCH, stash `404bc74`): a `GameplayGate` combining "stage scene ran", "ticked unpaused for 30 consecutive iterations" and the game's pause flags (world paused, pause menu open, task manager paused; fixed global plus offsets +104/+108/+150). Pattern `REUSABLE_INFRASTRUCTURE`, flags `HARD_CORPS_SPECIFIC`. See [07](07-display-window-frame-pacing.md#5-fps-unlock-120--144-fps-with-fixed-60-hz-logic-experimental-branch-stash).
- Persistence of toggles: only launcher-saved settings persist (`settings.toml`, schema list `kGameSettingKeys`); in-game toggles never write.

## 5. In-game overlays

### 5.1 Achievements overlay (HEAD IMPLEMENTATION, `f3ac390`, improved in `274ee61`)
- ReXGlue's achievements list restyled and sized for the viewport (about 70% of the viewport, clamped, `FitWindowExtent`), with an X close button, a B Back footer, D-pad/stick scrolling with a dead zone (`ScrollDirection`), and the same release-before-close gate.
- Opened by F7 (the app overrides `CreateAchievementsOverlay` so F7 toggles the *project's* overlay, and the SDK's own would not own it) and by the game's own Achievements menu item through `live::Install` bridge `open_achievements`, posted to the UI thread with `CallInUIThreadDeferred`. The Download Content menu item opens a notice dialog instead (the Marketplace is not part of the PC version).
- Offline achievements data and alias handling are in [11](11-offline-services-profile-achievements.md); here only the overlay pattern matters. Validation automated for the layout/scroll/close rules in `hcu_live_tests`.
- Classification: `REUSABLE_INFRASTRUCTURE` for the dialog and gate, `GAME_ADAPTATION` for the menu-item bridge. Porting: **adapt**.

### 5.2 Frame counter (HEAD IMPLEMENTATION)
- Title bar: one-second sampler from a pass-through hook on the title's swap routine. Overlay: `FrameCounterOverlay` draws a rounded panel at the top right on the foreground draw list ("60 FPS  16.7 ms"), so it stays above the cheat menu; text built by header-only `FrameCounterText` (automated: `hcu_frame_counter_tests`).
- It is a permanent ImGui dialog, which is why SDK patch 0029 matters (see 07 and 12).
- Porting: pattern **adopt directly**; the swap routine hook is `HARD_CORPS_SPECIFIC` (**concept only**).

## 6. Skip intro (HEAD IMPLEMENTATION, `50b7948`)

- Problem: the startup logos, opening movie and notices cannot be skipped by the player.
- Solution: the game's boot director (a state machine with an ordered list of steps) is started at its completion step: after the original initializer returns, the hook locates the step list via an object field, finds the index of the "boot complete" step id (`BootCompleteIndex`, scans up to 64 entries) and stores it as the current step. It acts only while a "first boot" flag byte is set (`SkipsBootIntro`), so later returns to the title are not affected. The game then reaches the title screen through its own transition. Nothing is written to game files or saves.
- Setting: `hcu_skip_intro` (Gameplay page, default Off).
- Classification: pure functions `REUSABLE_INFRASTRUCTURE`; the hook, flag address and step id 11 are `HARD_CORPS_SPECIFIC`. Confidence CONFIRMED for the code and tests; the repo has no recorded runtime log of the skipped boot reaching the title, so the behaviour is UNCERTAIN beyond the unit tests and the release note; treat other boot paths as unproven.
- Validation: automated `hcu_skip_intro_tests` (default, persistence and reset, gameplay row, boot gate, completion step, hook logic over fake memory).
- Porting: **adapt** if the title has a step-based boot director; otherwise investigate first.

## 7. Aspect-ratio hooks (HEAD) and FPS options (not in HEAD)

Documented in [07](07-display-window-frame-pacing.md#6-aspect-ratio-and-ultrawide-head-implementation) and [07 section 5](07-display-window-frame-pacing.md#5-fps-unlock-120--144-fps-with-fixed-60-hz-logic-experimental-branch-stash). For feature planning only:

- Aspect ratio is three hooks (display-mode copy, render-surface setup, canvas factor) plus guest video mode and presenter letterbox. Reusable idea: a table of formats to guest widths and video modes, pure and unit-tested. Hooks: `HARD_CORPS_SPECIFIC`. Status: implemented and played by the author at 16:9, 21:9 and 4:3.
- FPS options (120/144 labelled Experimental) need a fixed-step gate, an interpolation layer, a limiter and a present-interval override. Reusable: limiter schedule, gate, lerp-with-restore, tested without game memory. Specific: accumulator and camera/sprite offsets, pause flags. Status: runtime-proven by the author on one machine, user test pending, not shipped.
- Conditional features: frame-rate hooks, game-speed logic and cheats are never generic; a port should list them as optional work after boot, render and input are stable.

## 8. Rumble event architecture

Not implemented in HEAD (see [08](08-input-gamepad-mouse.md#6-rumble-complete-history)). The only evidence is the diagnostic branch `investigate/controller-rumble`, which shows the **shape** of a possible implementation: the game computes per-motor strengths on an object (motor 0/1), pushes them through its own XInput wrapper, the SDK routes `XamInputSetState` to `InputSystem::SetState` and to the driver's `SetDeviceVibration`. A port wanting rumble needs: (a) the game-side event sites if the game cannot already drive the pad, (b) confirmation that the active driver supports vibration, (c) a user-facing toggle. The Hard Corps addresses in that branch are `HARD_CORPS_SPECIFIC`, TEMPORARY DIAGNOSTICS, and not a reference implementation. Outcome of the investigation: not recorded (UNCERTAIN).

## 9. Persistence and settings integration summary

- Every user-visible option of these features is a `REXCVAR` in the project's cvar group, listed in `kGameSettingKeys`, rendered by the launcher page, and applied by `GameSettings::Apply` when Play is pressed.
- Retired settings must be removed deliberately: the aspect ratio change removes the old `present_letterbox` line with a byte-preserving `RemoveSettingLines`; the multisampling change replaced a stored "Off" with the default and overwrote an old `native_2x_msaa = false`. Pattern `REUSABLE_INFRASTRUCTURE`; see 10.
- Legacy repair: an unreleased build wrote `on/off` values that TOML rejects; they are repaired before parsing, with a `.bak` copy (see 10).

## 10. Pitfalls

1. A feature menu must not write config unless the user asked (F4 trap).
2. Shared setters need command-scoped hooks; otherwise enemies, cutscenes and demos change too.
3. A cheat that removes a consequence (hit state) may need the next-hit rule too (Super Armor's grace window).
4. Speed hacks tied to frame counts drift with frame rate changes; document game-speed coupling before adding FPS options.
5. Never present an address from this file as generic; the one portable artefact is the pattern.

Cross-links: [01-rexglue-core.md](01-rexglue-core.md), [12-performance.md](12-performance.md), [17-porting-checklist.md](17-porting-checklist.md), [18-change-history.md](18-change-history.md), [19-cross-project-porting-guide.md](19-cross-project-porting-guide.md).
