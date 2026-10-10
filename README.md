# Hard Corps: Uprising PC Port

Hard Corps: Uprising for Xbox Live Arcade, running natively on Windows. The original Xbox 360 game code is statically recompiled to C++ with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), and a launcher lets you install the game from your own package and set up the PC options.

**No game files are included.** You need your own Hard Corps: Uprising Xbox Live Arcade package.

## Screenshots

Screenshots will be added here.

## What you need

- Windows 10 or 11, 64-bit, and a DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64), which most PCs already have
- Your own Hard Corps: Uprising Xbox Live Arcade package, this exact release:

| | |
| --- | --- |
| Game | Hard Corps: Uprising, Xbox Live Arcade |
| Title ID | `58410A18` |
| Media ID | `3C5663CB` |
| Content type | `000D0000` (Xbox Live Arcade), STFS `LIVE` package |
| Game version | `0.0.0.3` |
| `default.xex` | 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |
| Known good package | 1,036,054,528 bytes, SHA-256 `53DF4058709E7DD2156AEADABB71B9A620D47D1EA57D827567ED3F75D3A9D1DE` |

The launcher checks the Title ID when you pick the package, and checks `default.xex` after extracting it. Any other version is refused, because the port is built for this exact game code. The package file's own checksum is not checked: `default.xex` is what identifies the version.

## Installing

1. Download the Windows ZIP from the [releases page](https://github.com/saulob/hard-corps-uprising-recompiled/releases) and extract it anywhere you can write to.
2. Run `hard_corps_uprising.exe`. The launcher opens.
3. On the **Play** page, click **Install from package...** and select your package. It is one file with a long hexadecimal name and no extension; on an Xbox 360 drive it is in `Content\0000000000000000\58410A18\000D0000\`.
4. Wait while the launcher checks the package and extracts about 1 GB into the `game` folder next to the program.
5. When the Play page says **Ready to play**, press **PLAY**.

Windows SmartScreen may show a warning the first time you run the program, because the release is not digitally signed. If you downloaded the release from the GitHub releases page, choose **More info**, then **Run anyway**.

**Reinstall from package...** repairs the game files and keeps your settings and saves. Settings, saves and the shader cache are in `Documents\hard_corps_uprising`; logs are in the `logs` folder next to the program. Hold Shift while starting to bring the launcher back if you have turned it off.

## Features

- Native static recompilation with ReXGlue 0.10.0, no emulator
- TRG Launcher with Play, Display, Graphics, Gameplay, Controls and About pages
- Install from your own package, with Title ID and game version checks
- Windowed or Borderless Fullscreen, switched with F11 while playing; window size and monitor
- Render resolution up to 4x of the game's 1280×720 output
- FXAA, 2× MSAA and anisotropic filtering up to 16×
- Keyboard, and Xbox controllers with no setup
- Local two-player with two controllers
- Optional frame rate in the window title

Online multiplayer is not supported.

## Reporting problems

If the game does not start or crashes, attach the newest file from the `logs` folder to an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues).

## Building from source

The launcher is a git submodule, so clone with submodules:

```powershell
git clone --recurse-submodules https://github.com/saulob/hard-corps-uprising-recompiled.git
```

or, in an existing clone:

```powershell
git submodule update --init --recursive
```

The recompiled sources are generated from your own game and are not in the repository.

1. Install Visual Studio with the C++ and LLVM/Clang components. `tools\build.ps1` expects Visual Studio 18 Community at its default location.
2. Extract your package's files into `game\extracted` with an Xbox 360 STFS extractor, so that `game\extracted\default.xex` exists.
3. `.\tools\codegen.ps1` downloads the pinned ReXGlue SDK 0.10.0, checks `default.xex` and generates the C++ into `generated\`.
4. `.\tools\build.ps1` builds `build\hard_corps_uprising.exe`. `.\tools\build.ps1 -Run` also starts it with `game\extracted` as the game folder.
5. `.\tools\package-release.ps1` makes the Release ZIP in `dist\`. It publishes nothing.

How the launcher, settings and installer work is described in [docs/launcher.md](docs/launcher.md).

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface; its repository has no license file at the pinned version
- [Earthworm Jim HD PC port](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled), whose launcher and package installer this port follows
- The licenses of the libraries the release includes are in its `licenses` folder

Hard Corps: Uprising is the property of its owners. This project is not affiliated with or endorsed by them and contains none of their files.
