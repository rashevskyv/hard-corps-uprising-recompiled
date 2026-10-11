# Linux and Steam Deck

> **Experimental.** The Linux build is a native x86_64 program, not a Windows build under Proton or Wine. It is
> compiled and packaged, but it has not yet been played on a real Linux PC or Steam Deck. Reports are welcome.

The Linux build uses the same code as the Windows build: ReXGlue SDK 0.10.0's Linux runtime, the Xenos GPU
plugin's Vulkan backend, and the same TRG launcher. It ships as one AppImage, which is also the Steam Deck build.

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
| Settings, saves, shader cache | `$XDG_DATA_HOME/hard_corps_uprising`, normally `~/.local/share/hard_corps_uprising` |

The settings and save folder is ReXGlue's own Linux location. A build run outside an AppImage keeps `game` and
`logs` next to the program instead. `--game_data_root` and `--user_data_root` override the folders, as on Windows.

## Known limits

- Not tested on real hardware yet: rendering, audio, controllers and performance are unconfirmed.
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

It needs `clang-20`, `clang++-20`, CMake 3.25+, Ninja, rsync, wget or curl, unzip or python3, binutils, the
GCC 13 or newer C++ runtime and headers, and the X11/Wayland client libraries (`libx11-6`, `libx11-xcb1`,
`libxcb1`, `libwayland-client0`), which the SDK's `rexglue` code generator loads.

The AppImage needs the glibc of the machine that built it, so releases are built on Ubuntu 22.04 (glibc 2.35, the
same baseline as the ReXGlue Linux libraries). The script warns when the build machine's glibc is newer. On
Ubuntu 22.04, clang 20 comes from [apt.llvm.org](https://apt.llvm.org) and the GCC 13 runtime from the
`ubuntu-toolchain-r/test` PPA, as `libstdc++-13-dev`. CMake 3.25+ comes from Kitware's release, because 22.04
ships 3.22.

The script copies the sources to `~/hcu-linux-build` (set `HCU_WORK` to change it), downloads the pinned ReXGlue
SDK, appimagetool and AppImage runtime with their SHA-256 checks, builds the `linux-amd64-release` preset, checks
that the AppDir holds no game files, packages, saves, logs or local paths, and writes
`dist/HardCorpsUprising-v<version>-linux-x86_64.AppImage` with its `.sha256`. It publishes nothing.

For a plain build without packaging, `cmake --preset linux-amd64-release` and
`cmake --build --preset linux-amd64-release` (also `linux-amd64-debug` and `linux-amd64-relwithdebinfo`) build
into `out/build/<preset>`, with `tools/rexglue/linux-amd64` holding the SDK.
