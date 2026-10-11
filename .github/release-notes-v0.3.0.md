## Hard Corps: Uprising PC Port v0.3.0 - DLC Character Support

This release adds optional DLC character support for **Harley Daniels**, **Sayuri** and **Leviathan**. The launcher has a new **DLC** page, directly after **Play**, where you install your own Xbox 360 DLC packages, see which characters are installed, and remove or reinstall them later.

### New: DLC characters (optional)
- Click **Install DLC...** and pick your own Hard Corps: Uprising DLC package. The launcher checks it before installing.
- Each character shows **Installed** or **Not installed**.
- **Remove** on an installed character removes only that package, after a confirmation. The game and the other DLC are not touched, and you can install the package again later from your own copy.
- Reinstalling the game does not remove installed DLC. It is stored in your user data folder, not in the game folder.
- The DLC page is available on Windows and in the experimental Linux build.

**No game or DLC files are included, and the port does not download any.** You need your own Xbox 360 DLC package for each character. The launcher accepts a package only if it is a Hard Corps: Uprising Marketplace package:

| | |
| --- | --- |
| Title ID | `58410A18` |
| Marketplace content type | `00000002` |

It refuses the game package itself and packages from other games.

This was requested in [issue #1](https://github.com/saulob/hard-corps-uprising-recompiled/issues/1) and implemented in [pull request #4](https://github.com/saulob/hard-corps-uprising-recompiled/pull/4). v0.3.0 is the first release with DLC character support.

### Downloads and install
**Windows**: **HardCorpsUprising-v0.3.0-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package.
4. Optional: on the **DLC** page click **Install DLC...** for each character you own.
5. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.3.0-linux-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage. DLC is optional and installed from the **DLC** page.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

The launcher checks the Title ID when you pick the package and checks `default.xex` after extracting it. It explains if your package is a different version.

### Linux status
**Experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend. It is **not** a Proton or Wine build. It compiles, packages and passes its installer tests, including the DLC ones, but it has not yet been played on a real Linux PC or Steam Deck, so rendering, audio, controllers, the launcher (including the DLC page) and the Steam Deck flow are untested. Please report what you find. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md) for details and known limits.

### Features
- Native static recompilation with ReXGlue SDK 0.10.0
- The TRG launcher, with Play, DLC, Display, Graphics, Gameplay, Controls and About pages
- Package installer with Title ID and game version checks, and **Reinstall from package...**
- DLC installation and removal for Harley Daniels, Sayuri and Leviathan
- Windowed or Borderless Fullscreen, switched while playing with F11
- Render resolution up to 4x (5120×2880)
- FXAA and FXAA Extreme, 2x MSAA, texture filtering up to 16x
- Xbox 360 controller support with no setup, and keyboard play
- Local two-player with two controllers
- Optional frame rate in the window title
- Settings saved between sessions

Online multiplayer is not supported.

### Reporting problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For DLC problems, say which character and whether the launcher showed an error. For Linux reports, please also say your distribution and graphics card.

### Requirements
- **Windows**: Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).
- **Linux**: 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.3.0-windows-x64.sha256` and `HardCorpsUprising-v0.3.0-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.3.0-windows-x64.zip` | `55b137f29bcb411e91b6445245932999db6eab2048a96035c35e993f8b92718a` |
| `HardCorpsUprising-v0.3.0-linux-x86_64.AppImage` | `a899b75cc8978f5afe9e6e6ae9f04be75c856230f72a3b504e9e1c4c564ca3fd` |
