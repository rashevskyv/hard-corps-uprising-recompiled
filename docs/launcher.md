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
- **Package reader** (`src/stfs.cpp`): read-only STFS (`LIVE`, `PIRS`, `CON `) with consecutive files, which is what Xbox Live Arcade packages are. Every entry name is checked before anything is written: one printable ASCII path component, no `..`, separators, drive or stream markers, device names, or trailing dot or space; parents must be directories and nesting is limited. Extraction also checks that each destination stays inside the staging folder. Block hashes and the Microsoft signature are not verified.
- **Tests:** `cmake --build build --target hcu_installer_tests` builds `tests/installer_tests.cpp`. It builds synthetic packages (with a `default.xex` of the supported size and CRC32 made from generated bytes), so it needs no game files. `hcu_installer_tests --package <file> --reference <extracted folder>` also extracts a real package once into a temporary folder and compares it.

## Pages

| Page | Setting | Cvar |
| --- | --- | --- |
| Play | Install / Reinstall from package | (game files, see below) |
| | Show this launcher | `hcu_launcher` |
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

## Not offered yet

| Option | Reason |
| --- | --- |
| Frame-rate cap | Earthworm's cap is its own limiter. ReXGlue has no generic one. |
| Language | The data has EN, JP, DE, FR, ES and IT folders, and the game imports `XGetLanguage`, but the mapping from `user_language` is not verified. |
| Key rebinding | TRG captures one key. ReXGlue's defaults use alternatives (`X,Return`) and combinations (`Shift+Up`). |
| Deadzone, vibration, stick inversion, button remap | Earthworm-specific guest hooks. |
| Render presets | Earthworm-specific logic over `resolution_scale`. |
| Present effect (FSR/CAS) | Not exposed by Earthworm either. Needs a restart. |
| Cheats, achievements, updates, drag and drop of the package | Out of scope. |
