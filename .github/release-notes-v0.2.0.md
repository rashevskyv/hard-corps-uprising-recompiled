## Hard Corps: Uprising PC Port v0.2.0

Play the Xbox 360 edition of Hard Corps: Uprising natively on Windows, and now on Linux and Steam Deck, with the TRG launcher and modern PC options.

### New: native Linux support (experimental)
This release adds a native Linux x86_64 build: the same recompiled game and launcher, running on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend. It is **not** a Proton or Wine build. It ships as one AppImage, which is also the intended Steam Deck build.

### Downloads and install
**Windows**: **HardCorpsUprising-v0.2.0-windows-x64.zip**
1. Unzip it anywhere you can write to and run **hard_corps_uprising.exe**.
2. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package.
3. Wait for **Ready to play**, then press **PLAY**.

**Linux and Steam Deck**: **HardCorpsUprising-v0.2.0-linux-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### Linux status
**Experimental.** The Linux build compiles, packages and passes its installer tests, but it has not yet been played on a real Linux PC or Steam Deck. Rendering, audio, controllers, the launcher and the Steam Deck flow are untested, so please report what you find. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md) for details and known limits.

### Existing features
- Native static recompilation with ReXGlue SDK 0.10.0
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
If the game does not start or crashes, please open an issue and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Requirements
- **Windows**: Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).
- **Linux**: 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.2.0-windows-x64.sha256` and `HardCorpsUprising-v0.2.0-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.2.0-windows-x64.zip` | `a3ea1c5a66c7028830e584198a1c1f6da89959b09850ea06c975e2c91733abff` |
| `HardCorpsUprising-v0.2.0-linux-x86_64.AppImage` | `d2427e38737b2e86a04b69f319856094a7772d37eae71351e91f4903d8e96ce7` |
