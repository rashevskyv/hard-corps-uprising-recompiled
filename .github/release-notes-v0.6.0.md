## Hard Corps: Uprising PC Port v0.6.0 - Native Aspect Ratios and Performance Scaling

v0.6.0 is a feature release for the graphics and display options. It adds native aspect ratios, an internal render resolution option for slower GPUs, and graphics card selection on Windows. Gameplay is unchanged from v0.5.3.

### Highlights
- **Native aspect ratios.** The game now renders in the format you choose, from 4:3 and 5:4 up to 21:9 and 32:9, instead of stretching a 16:9 picture.
- **Internal render resolution.** Draw the game at 75% or 50% of the render resolution to lower the GPU work on slower graphics hardware.
- **Graphics card selection (Windows).** Pick the GPU the game uses, or leave it on Automatic.
- **Reduced resolution on Intel GPUs.** Internal render resolution now also works on Intel GPUs that need a different rendering path.

### Native aspect ratios
Choose **Aspect Ratio** on the launcher's **Display** page:
- **16:9 Original** (default)
- **16:10**
- **21:9 Ultrawide**
- **32:9 Super Ultrawide**
- **4:3**
- **5:4**

This changes the game's own rendering format rather than stretching the 16:9 image, and Window size follows the chosen format. Wider formats can show more of the sides of the scene. It also covers displays such as a 4:3 CRT or a 16:10 screen such as the Steam Deck's.

Menus and videos were made for 16:9 and are not redesigned for every format, so some HUD, menu or cutscene compositions may not look as intended outside 16:9.

### Performance options
On the launcher's **Graphics** page:
- **Render resolution** (Original, 2x, 3x, 4x) sets the target resolution of the picture, as before. It is separate from Window size, which only sets the size of the game window.
- **Internal render resolution** sets how much of that resolution the GPU actually draws before the picture is scaled up to it:
  - **100% Native** (default)
  - **75% Performance**
  - **50% Low-end**

75% and 50% reduce the internal GPU rendering resolution, which can improve performance on slower GPUs. Image quality decreases as expected. 50% is intentionally a low-end mode and can look noticeably softer or pixelated.

The internal size follows the selected aspect ratio. For example, 16:9 at 50% draws 640x360 for a 1280x720 output, and 21:9 at 75% draws 1260x540 for a 1680x720 output.

### Graphics card selection
**Windows only.** The **Graphics card** setting on the **Graphics** page offers:
- **Automatic (Recommended)**, which keeps preferring the high-performance GPU, as before
- Each compatible Direct3D 12 graphics card found on your PC

An explicit choice overrides Automatic. It is useful on systems with both integrated and dedicated graphics. It takes effect when you press **PLAY**, and the game restarts once if the choice needs a new window.

### Linux
Linux remains **experimental**. The AppImage and the Vulkan build compile and pass automated validation. The new internal render resolution path for Vulkan has not been tested in real gameplay on Linux or Steam Deck hardware for this release. Graphics card selection is Windows only.

### Downloads
**Windows**: **HardCorpsUprising-v0.6.0-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprisingRecompiled-v0.6.0-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included, and no DLC files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

Requirements:
- **Windows**: Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).
- **Linux**: 64-bit x86 Linux with glibc 2.31 or newer (such as Ubuntu 20.04 or later), a Vulkan driver, and an X11 or Wayland desktop. The AppImage is built on Ubuntu 20.04 (glibc 2.31) and its files need `GLIBC_2.30` or older.

### Notes / Known Limitations
- **Linux status: experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend, not a Proton or Wine build. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware, so rendering, audio, controllers and the Steam Deck flow may still have problems. Running on glibc 2.31 or newer is the build baseline, not a guarantee for every such system. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md).
- Aspect ratios other than 16:9 change the rendering format only. Menus and videos are not redesigned for them.
- Everything else listed for v0.5.0 and v0.5.1 still applies.

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.6.0-windows-x64.sha256` and `HardCorpsUprisingRecompiled-v0.6.0-x86_64.sha256`. The hashes are added when the release files are built.
