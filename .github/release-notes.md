## Hard Corps: Uprising PC Port v0.1.0

Play the Xbox 360 edition of Hard Corps: Uprising natively on Windows, with the TRG launcher and modern PC options.

### Install
1. Unzip **HardCorpsUprising-v0.1.0-windows-x64.zip** anywhere you can write to and run **hard_corps_uprising.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package.
3. Wait for **Ready to play**, then press **PLAY**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### What's in it
- Native Windows static recompilation with ReXGlue SDK 0.10.0
- The TRG launcher, with Play, Display, Graphics, Gameplay, Controls and About pages
- Package installer with Title ID and game version checks, and **Reinstall from package...**
- Windowed or Borderless Fullscreen, switched while playing with F11
- Window size, monitor and VSync choices, and Letterbox 16:9 or Stretch
- Render resolution up to 4x (5120×2880)
- FXAA and FXAA Extreme, 2x MSAA, texture filtering up to 16x
- Xbox 360 controller support with no setup, and keyboard play
- Local two-player with two controllers
- Optional frame rate in the window title
- Settings saved between sessions

Online multiplayer is not supported.

### Reporting problems
If the game does not start or crashes, please open an issue and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program, with a line about what you were doing.

### Requirements
Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

The ZIP's SHA-256 is published with the release as `HardCorpsUprising-v0.1.0-windows-x64.sha256`.
