# Linux and Steam Deck

> **Experimental.** The Linux build is a native x86_64 program, not a Windows build under Proton or Wine. It is
> compiled and packaged. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck
> hardware. Reports are welcome.

The Linux build uses the same code as the Windows build: ReXGlue SDK 0.10.0's Linux runtime and the Xenos GPU
plugin's Vulkan backend, both built with the project's patches, and the same TRG launcher. It ships as one
AppImage, which is also the Steam Deck build.

v0.5.0 fixes three problems that showed on Linux: incorrect texture brightness in the Vulkan renderer, which could
darken images and lose glow effects (Windows was not affected); Exit Game hangs (reported from SteamOS Game Mode) and
shutdown crashes; and a Vulkan presenter that rebuilt its output pipeline, waiting for the GPU, on every frame. The
brightness fix has not been confirmed on a real Linux PC or Steam Deck yet.

## Requirements

- 64-bit x86 Linux (x86_64) with glibc 2.35 or newer, such as Ubuntu 22.04 or later (`ldd --version` shows it)
- A Vulkan driver (Mesa RADV on a Steam Deck and most AMD and Intel PCs)
- An X11 or Wayland desktop (the runtime loads `libX11`, `libX11-xcb`, `libxcb` and `libwayland-client`)
- `zenity` or `kdialog` for the package picker, and `xdg-open` for the About page's folder buttons
- Your own Hard Corps: Uprising Xbox Live Arcade package (see the README for the exact version)

The AppImage carries the C++ runtime of its build machine (GCC 13 or newer). It is used only when the system's own copy
is older than GCC 13.

## Installing

1. Download **HardCorpsUprising-v…-linux-x86_64.AppImage** and put it in a folder you can write to.
2. Make it executable: right-click it > **Properties** > **Permissions** > **Is executable**, or
   `chmod +x HardCorpsUprising-v*-linux-x86_64.AppImage`.
3. Run it. The launcher opens.
4. On the **Play** page click **Install from package...** and pick your package. The game files are copied into a
   `game` folder next to the AppImage.
5. Press **PLAY**.

### Steam Deck

This is the intended flow. It has not been tried on a Steam Deck yet.

1. In **Desktop Mode**, do steps 1 to 5 above. Copy your package to the Deck first (USB stick, SD card or network).
2. In Steam: **Games** > **Add a Non-Steam Game to My Library**, and pick the AppImage.
3. Return to **Game Mode** and start it from your library.

Use the touchscreen or the trackpads in the launcher. A controller's **Start** button presses Play, and the D-pad,
left stick, A and B move through the launcher.

## Where things are

| What | Where |
| --- | --- |
| Game files | `game` next to the AppImage |
| Logs | `logs` next to the AppImage (`~/.local/share/hard_corps_uprising/logs` if that folder is not writable) |
| Settings, saves, shader cache | `user_data` next to the AppImage ([user-data.md](user-data.md)) |

Earlier versions used ReXGlue's own Linux location, `$XDG_DATA_HOME/hard_corps_uprising`; its contents move into
`user_data` at the first start. A build run outside an AppImage keeps `game`, `logs` and `user_data` next to the program instead. `--game_data_root` and `--user_data_root` override the folders, as on Windows.

- **Window icon.** The program sets its window icon from the AppImage's own icon file, so it does not depend on the
  AppImage being integrated into the desktop. A build run outside an AppImage keeps the desktop's default icon.
- **Application ID.** The program tells the desktop it is `io.github.saulob.hard-corps-uprising` (the Wayland
  app_id and the X11 window class). The `.desktop` file in `packaging/linux` has the same name and sets it as
  `StartupWMClass`, and `tools/package-linux.sh` stops if they differ.

## Known limits

- Not broadly tested on real hardware: rendering, audio, controllers and performance are largely unconfirmed.
- Holding **Shift** to bring the launcher back works only if the window gets keyboard focus while Shift is still
  held, because SDL cannot read the keyboard before the window has focus. Keep Shift held until the launcher appears.
- The VSync setting maps to Vulkan's present mode: **Off** allows immediate presentation (tearing), **On** uses
  mailbox or FIFO.
- On a Wayland desktop with display scaling above 100%, window sizes can come out larger than chosen. ReXGlue
  sizes its window this way on Linux.

## Building the AppImage

Run on Linux or WSL, from a clone with the TRG launcher submodule, after `tools/codegen.ps1` has generated
`generated/default` and with your own `game/extracted/default.xex`:

```sh
tools/package-linux.sh            # incremental Release build, then package
tools/package-linux.sh --clean    # clean Release build first
```

It needs `clang-20`, `clang++-20`, `lld-20` (the game links with ThinLTO), CMake 3.25+, Ninja, rsync, wget or curl,
unzip or python3, binutils, the GCC 13 or newer C++ runtime and headers, and the X11/Wayland client libraries
(`libx11-6`, `libx11-xcb1`, `libxcb1`, `libwayland-client0`), which the SDK's `rexglue` code generator loads.

The AppImage needs the glibc of the machine that built it, so releases are built on Ubuntu 22.04 (glibc 2.35, the
same baseline as the ReXGlue Linux libraries). The script warns when the build machine's glibc is newer. On
Ubuntu 22.04, clang 20 comes from [apt.llvm.org](https://apt.llvm.org) and the GCC 13 runtime from the
`ubuntu-toolchain-r/test` PPA, as `libstdc++-13-dev`. CMake 3.25+ comes from Kitware's release, because 22.04
ships 3.22.

The script copies the sources to `~/hcu-linux-build` (set `HCU_WORK` to change it), downloads the pinned ReXGlue
SDK, appimagetool and AppImage runtime with their SHA-256 checks, builds the project's patched ReXGlue runtime and
Vulkan GPU plugin from source the first time and whenever the patches change
([docs/rexglue-runtime-patch.md](rexglue-runtime-patch.md) lists the extra packages and the network access this
needs), builds the `linux-amd64-release` preset, checks
that the AppDir holds no game files, packages, saves, logs or local paths, and writes
`dist/HardCorpsUprising-v<version>-linux-x86_64.AppImage` with its `.sha256`. It publishes nothing.

For a plain build without packaging, with the SDK extracted into `tools/rexglue/linux-amd64`, `tools/build.sh`
(`release`, `relwithdebinfo` or `debug`) builds the patched ReXGlue libraries when they are missing or out of date and
then the `linux-amd64-<config>` preset into `out/build/<preset>`. Building a preset directly works too, but stops if
the SDK does not hold the patched libraries.
