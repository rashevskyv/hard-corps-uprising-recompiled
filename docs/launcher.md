# Launcher

The pre-game launcher is [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), integrated the same way as Earthworm Jim HD.

## Architecture

- Embedded, not standalone. TRG draws inside ReXGlue's own Dear ImGui frame, in the game window. There is no second window, process, SDL or ImGui.
- Dear ImGui is the copy inside `rexruntime` (1.92.5). CMake passes it to TRG as `TRG_LAUNCHER_IMGUI_TARGET`, with `TRG_LAUNCHER_STANDALONE`, `TRG_LAUNCHER_FETCH_DEPS` and `TRG_LAUNCHER_BUILD_DEMO` off, so nothing is downloaded.
- Lifecycle, from `ReXApp`:
  1. `SetupEnvironment` loads `hard_corps_uprising.toml`; `OnPostInitLogging` loads the player's `settings.toml` over it. The Xenos GPU plugin is not loaded here. ReXGlue loads it in `SetupPresentation`, and the Graphics cvars it registers take the player's values from the file.
  2. `OnPreSetup` records the saved game settings and decides whether the launcher shows. If it does, `PrepareLauncherWindow` sets the window for the launcher. Then the window settings this process creates its window with are recorded. Graphics keys are not registered yet, so they are recorded as unregistered.
  3. `SetupPresentation` creates the window and loads the GPU plugin. `OnFinalizePaths` calls `CaptureLateRegistered`, which takes the Graphics values now that they are registered, then shows the launcher and returns `nullopt`; the runtime is not built yet. A key the player has already edited in this process is never overwritten.
  4. **Play** (mouse, Enter or Xbox Start) calls `RequestPlay`, which is the only save: if it fails, the launcher stays open with "Could not save the settings." and nothing starts. On success it shows "Settings saved. Starting game..." at full brightness for 0.4 s, timed with ImGui and without sleeping. The launcher's controls are dimmed and disabled during that time, and its keyboard, gamepad and mouse input is released each frame, so Play, Save, Ctrl+S, Esc and the D-pad do nothing. Then, if the window size, monitor or VSync differs from this process's window, the game is relaunched once and this process quits (`StartGame` does not save). Otherwise the pending settings are applied and the runtime starts.
  5. **Relaunched copy**: `--hcu_skip_launcher=true` skips the launcher, and the window is created natively with the saved settings.

## Settings

| File | Written by | Role |
| --- | --- | --- |
| `<exe dir>/hard_corps_uprising.toml` | the build (copied from `config/`) | shipped defaults, never written by the launcher |
| `<user data>/settings.toml` | the launcher | the player's choices, loaded last |

- With `tools\build.ps1`, user data is `build\user_data`, which `-Clean` keeps.
- Launcher edits are pending in `hcu::GameSettings` until Save or Play. Save writes only the changed keys to `settings.toml`, through `trg::TextFileSettings`; comments and other lines are kept. A key whose cvar is not registered is refused rather than written.
- Play is blocked, and does not save, while the game files are missing or invalid or an install is running.
- Reset all settings puts the launcher's values back to the shipped ones, pending until Save or Play.
- Legacy repair: an unreleased development build wrote `key = on` and `key = off`, which TOML rejects and which made ReXGlue ignore the whole file. Before the file is parsed, each exact bare `on`/`off` value is rewritten as `true`/`false`. Nothing else changes. The original is kept as `settings.toml.bak`. Once the file holds no bare on/off, this does nothing.
- The dependency is the `third_party/trg-launcher` submodule, pinned to `b384a28` (TRG Launcher 1.1.0). After cloning, run `git submodule update --init third_party/trg-launcher`. When the submodule is not checked out, CMake falls back to the checkout named by `HCU_TRG_LAUNCHER_DIR` (CMake cache or environment). The upstream repository has no LICENSE file.

## Window

The launcher window and the game window are separate.

- **Launcher window:** Windowed, 1280×720 logical (capped at 90% of the primary display's work area), centred on the primary display by SDL (`monitor = 0`).
- **Game window:** created by the game process with the saved Window size, Monitor, VSync and Display mode.

| Setting | When it takes effect |
| --- | --- |
| Window size, Monitor, VSync | Play. If any differs from the launcher's window, the game restarts once in a new window. |
| Display mode | Play. In the launcher's window when the window already matches, otherwise in the new window. |
| Aspect ratio, graphics, frame counter, keyboard | Play, in the same window |

Why a restart: ReXGlue 0.10.0 has no API to resize or move an open window (its `window.h` lists resize as not implemented). The window's size and display are fixed when it is created. A window created with the game's size and display keeps the right Windowed size and position for F11.

- Default game window: 853×480 logical, from `config/hard_corps_uprising.toml`. A value in `settings.toml` replaces it.
- With the defaults, a normal Play from the launcher relaunches the game once. The launcher and the game have independent window sizes, so this is expected, not an error. The relaunched copy opens its window at the game size directly.
- Display mode is switched in place only when the window already matches the game's size, monitor and VSync. Otherwise it is applied by the relaunched copy, so the Windowed size F11 restores is always the game's.
- The launcher's 1280×720 target is capped to the usable desktop, so on a display where it does not fit (for example 2560×1440 at 225%, where it becomes 1024×576) the launcher is smaller.
- Sizes that cannot fit the current display at its scaling are hidden from the Window size list, except the current one.

### Guest video mode

The guest video mode is fixed at 853×480 (`video_mode_*` in `config/hard_corps_uprising.toml`). It does not follow the window size. Window size changes only the host window; the game renders the same guest picture and ReXGlue scales it into the window.

The guest video mode is not the render output. Xenos presents at a measured 1280×720 base, which Render resolution scales. Measured presented output: 3840×2160 at scale 3 and 5120×2880 at scale 4.

### Relaunch rule and loop prevention

- The comparison is between the pending settings and the window this process created. The launcher's own window is what this process created, so it counts as a mismatch when the game's settings differ from it.
- A relaunch starts a copy with `--hcu_skip_launcher=true`. That copy never shows the launcher, so it never compares or relaunches.
- Launcher hidden: the game starts directly with the saved settings.
- Shift at start shows the launcher with its own window. The relaunched copy skips it even if Shift is still held.

### Monitor numbering

- ReXGlue's `monitor = N` opens the window on `SDL_GetDisplays()[N-1]`. SDL lists the primary display first, then the others in Windows order.
- The launcher's Monitor list uses the same order (`ListMonitors` in `src/platform.cpp`). The order was checked against the SDL 3.4.10 source; the SDK ships SDL 3.5.0, whose source is not on disk.
- At startup the game logs a warning when the window did not open on the display the setting names.
- Monitor `0` (Default) is the primary display.

## Game files and install

- **Game folder:** `--game_data_root`, else `game` next to the program (`HardCorpsUprisingApp::OnConfigurePaths`). Development builds pass `game\extracted`. The folder holds `default.xex`, `DataXENON\`, `ArcadeInfo.xml` and `nxeart`, and nothing the player makes. Settings, saves and the shader cache are in the user data folder (`Documents\hard_corps_uprising`, or `--user_data_root`); logs are in `logs` next to the program.
- **One readiness check:** `CheckGameFiles` (`src/game_files.cpp`) is used by the Play page, the install verification and the clean-start decision. Ready means `default.xex` is the supported build (12,955,648 bytes, CRC32 `3DF903B9`) and `DataXENON` exists. Missing and unsupported are separate states.
- **Launcher visibility:** `LauncherWanted` (`src/launcher.h`) shows the launcher whenever the game files are not ready, whatever `hcu_launcher` or `--hcu_skip_launcher` say, so turning the launcher off cannot hide the only way to install the game. A relaunch after PLAY still skips it, because the files are ready then.
- **Install from package** (`InstallFromPackage`, on a TRG `Task`, so the UI stays responsive): reads the STFS header and requires Title ID `58410A18`, reads and validates the file table, checks free space, extracts into `game-install-temp` next to the game folder, runs `CheckGameFiles` on it, and only then swaps it in (`game` becomes `game-install-backup`, the staging folder becomes `game`, the backup is deleted). A wrong package, damaged package, bad `default.xex`, write error or Cancel leaves the existing game untouched and removes the staging folder. A folder that holds other files is never replaced.
- **DLC install** (`InstallDlcFromPackage`, on the same kind of `Task`): requires Title ID `58410A18` and content type `00000002`, rejects the game package (content type `000D0000`) and other titles, and names the folder by the package's content ID from its metadata, not by the file name. It extracts into `dlc-install-temp` under the user data folder, writes the `.header` the SDK's content manager reads, and swaps both in with the previous install kept until the swap completes. A failed, cancelled or clashing install leaves the installed DLC as it was. It also stops, changing nothing, when `dlc-install-temp` or the installed copy it would replace is not a plain folder (a junction, symbolic link or other reparse point at or inside it), and it only ever deletes `dlc-install-temp` after the same check. The layout is the SDK's marketplace content layout for this title: `<user data>\0000000000000000\58410A18\00000002\<content ID>\` with `Headers\00000002\<content ID>.header` beside it. The game folder is never touched.
- **DLC page** (sidebar, directly after Play): one card per supported character, Harley Daniels, Sayuri and Leviathan, and an "Other installed content" section for verified packages no character card shows. Each card shows Installed (green, with a Remove button) or Not installed (neutral). The state is read from the installed packages on every change, never stored. The character comes from the package's content ID when it is one of the three known IDs, else from its English display name: exactly one of the three names, optionally followed by "Additional Character" with or without parentheses; any other package is shown under Other installed content. Install DLC... runs the install above and is available whether or not the game is installed.
- **DLC removal** (`RemoveInstalledDlc`, the Remove button on a card, after a confirmation dialog with Cancel and Remove): removes only the selected package's content folder and its header, identified by content ID. Before anything is moved it requires that the package is verified (its header names this title, content type `00000002` and the same content ID), that its folder is a plain folder directly inside the DLC storage (no junction, symbolic link or other reparse point, here or inside it), and that the game folder is neither the folder nor inside it. The folder is moved aside first, then the header is deleted; if the header cannot be deleted the folder is moved back, and the result is reported as a failure. The state is read back from the installed packages before success is reported. Saves, settings, the shader cache, the game folder and other Marketplace content are never touched. Nothing is removed for a malformed content ID, a package that does not verify, or a package that is not installed.
- **Package reader** (`src/stfs.cpp`): read-only STFS (`LIVE`, `PIRS`, `CON `) with consecutive files, which is what Xbox Live Arcade packages are. Every entry name is checked before anything is written: one printable ASCII path component, no `..`, separators, drive or stream markers, device names, or trailing dot or space; parents must be directories and nesting is limited. Extraction also checks that each destination stays inside the staging folder. Block hashes and the Microsoft signature are not verified.
- **Tests:** `cmake --build build --target hcu_installer_tests` builds `tests/installer_tests.cpp`. It builds synthetic packages (with a `default.xex` of the supported size and CRC32 made from generated bytes), so it needs no game files. `hcu_installer_tests --package <file> --reference <extracted folder>` also extracts a real package once into a temporary folder and compares it.

## Pages

| Page | Setting | Cvar |
| --- | --- | --- |
| Play | Install / Reinstall from package | (game files, see below) |
| | Show this launcher | `hcu_launcher` |
| DLC | Install DLC..., Remove | (DLC packages, see below) |
| Display | Display mode | `fullscreen` |
| | Window size | `window_width`, `window_height` |
| | Monitor | `monitor` |
| | VSync | `d3d12_allow_variable_refresh_rate_and_tearing` (inverted) |
| | Aspect ratio | `present_letterbox` |
| Graphics | Render resolution | `resolution_scale` |
| | Anti-aliasing | `swap_post_effect` |
| | Multisampling | `native_2x_msaa` |
| | Texture filtering | `anisotropic_override` |
| Gameplay | Frame rate in title | `hcu_show_fps` |
| Cheats | Infinite Health | `hcu_cheat_health` |
| | Infinite Lives | `hcu_cheat_lives` |
| | Rapid Fire | `hcu_cheat_rapid_fire` |
| | Jump Height 2x | `hcu_cheat_jump_height` |
| | Keep Weapons on Hit | `hcu_cheat_keep_weapons` |
| Controls | Keyboard | `mnk_mode` |

### Graphics

| Control | Cvar | Values (default) |
| --- | --- | --- |
| Render resolution | `resolution_scale` (integer 1–8, the launcher offers 1–4) | `1` Original, `2`, `3`, `4` (`1`) |
| Anti-aliasing | `swap_post_effect` | `none` Off, `fxaa`, `fxaa_extreme` (`none`) |
| Multisampling | `native_2x_msaa` | `true` 2x MSAA, `false` Off (`true`) |
| Texture filtering | `anisotropic_override` | `-1` Game (no override), `0` Off, `2` 2x, `3` 4x, `4` 8x, `5` 16x (`3`, 4x) |

Shader compilation is not a launcher setting; the game uses ReXGlue's default, `async_shader_compilation = true`.

- **Render resolution** labels show the presented output measured at each scale: Original 1280×720, 2x 2560×1440, 3x 3840×2160, 4x 5120×2880. The labels are fixed and do not depend on `video_mode_*` or Window size. 2x is a natural match for 1440p displays. The recommendation is informational only, and the default stays Original. Higher scales cost more GPU time.
- The game takes the render resolution when its GPU starts, after Play, so a change applies to the game that Play starts. The GPU driver can lower a scale it cannot support; the game logs a warning when it does.
- Render resolution is not a relaunch trigger. It never changes the window.

- Every default is the value the game already used.
- VSync controls tearing only. The D3D12 presenter always presents with sync interval 0; frame pacing comes from the guest `vsync` cvar, which the launcher does not touch.
- Keyboard keys are listed read-only on the Controls page.
- An Xbox controller drives the launcher through XInput: Start plays; A, B, the D-pad and the left stick navigate. ReXGlue's input system only starts with the game.
- The accent is a neon green (`kAccent`) with a dark emerald for the ready card, both in `src/launcher.cpp` with the header gradient. Amber is kept only for the "game files" warning, which is a status colour.

### Cheats

The sidebar order is Play, DLC, Display, Graphics, Gameplay, Cheats, Controls, About. All five cheats default to off and are ordinary launcher settings: saved with Save or Play, and put back to off by Reset all settings.

- The hooks are in `src/cheats.cpp` and read the cvars when they run, so turning a cheat off restores the game's own behaviour with no other state to undo. Nothing is written to the game files or the save data.
- A hook acts only on an object whose vtable is the game's player class (`hcu::cheats::kPlayerVtable`), which is every character, DLC included, in both player slots. Enemies use other classes.
- **Infinite Health** turns a negative life delta into zero in the player's two add-life methods, which is how hits arrive. Healing and the death commands that set life to zero directly (`SetLife(0)`) are untouched.
- **Infinite Lives** skips the player's stock decrement on respawn and refuses a set-stock call that would lower the stock. The stock can still go up.
- **Rapid Fire** halves the shot cooldown, in frames, that the game's own script sets after each shot, so weapons keep their relative speeds. The script command and the player's cooldown setter are hooked together, because the per-frame countdown uses the same setter. Odd values alternate between rounding down and up, so the average is exactly half. Sayuri's attacks use the same cooldown.
- **Jump Height 2x** scales the launch speed of the game's jump (`Obj::Jump`, shared by every object class, so the hook acts on players only) by the square root of two. Gravity is constant (0.1419 per frame), so the apex grows with the square of the launch speed. Each character's own speed is scaled, including Sayuri's and Leviathan's special jumps. Horizontal speed is not touched, and moves that set a velocity directly instead of calling the jump are not scaled.
- **Keep Weapons on Hit** disarms the weapon drop that a hit arms. The player's on-damaged script then neither resets the weapon nor spawns the dropped item. A hit that leaves the player at zero life still drops the weapon, as in the game. It does not depend on Infinite Health.
- `hcu_cheats_tests` (`cmake --build build --target hcu_cheats_tests`) checks the player test, the life delta, the stock rule, the shot cooldown, the jump speed and the weapon-drop rule without game memory.

## Not offered yet

| Option | Reason |
| --- | --- |
| Frame-rate cap | Earthworm's cap is its own limiter. ReXGlue has no generic one. |
| Language | The data has EN, JP, DE, FR, ES and IT folders, and the game imports `XGetLanguage`, but the mapping from `user_language` is not verified. |
| Key rebinding | TRG captures one key. ReXGlue's defaults use alternatives (`X,Return`) and combinations (`Shift+Up`). |
| Deadzone, vibration, stick inversion, button remap | Earthworm-specific guest hooks. |
| Render presets | Earthworm-specific logic over `resolution_scale`. |
| Present effect (FSR/CAS) | Not exposed by Earthworm either. Needs a restart. |
| Achievements, updates, drag and drop of the package | Out of scope. |
