## Hard Corps: Uprising PC Port v0.5.2 - Linux Startup and AppImage Fixes

v0.5.2 is a small technical patch release focused on Linux and AppImage compatibility. Gameplay is unchanged, and the features and performance of v0.5.1 are unchanged.

### Highlights
- **Linux graphics failure window.** When the Vulkan graphics system cannot start, Linux now shows a self-contained error window created directly with SDL.
- **Recompiled Linux branding.** The Linux desktop and AppImage name is now **Hard Corps: Uprising Recompiled**, and the download is `HardCorpsUprisingRecompiled-v0.5.2-x86_64.AppImage`.

### Linux / Steam Deck
- **Graphics failure window.** Previously the error was shown with `SDL_ShowSimpleMessageBox`, which could depend on `zenity`. In some X11 and Xvfb-style environments that left the process running for more than 30 seconds with no visible window. The error window is now created by the game itself, so no `zenity`, GTK or shell helper is needed, and it can be shown even after Vulkan graphics initialization has failed. Dismissing it exits the game cleanly with a nonzero result.
- **Name and file names.** The desktop entry is now named Hard Corps: Uprising Recompiled. The AppImage is `HardCorpsUprisingRecompiled-v0.5.2-x86_64.AppImage` and its checksum file is `HardCorpsUprisingRecompiled-v0.5.2-x86_64.sha256`. The v0.5.1 asset, `HardCorpsUprising-v0.5.1-x86_64.AppImage`, is unchanged and keeps its name.
- **Compatibility baseline.** As in v0.5.1, the AppImage is built on Ubuntu 20.04 (glibc 2.31) and its ELF files need `GLIBC_2.30` or older. The system's glibc is still used; it is not bundled.
- Linux support remains experimental.

### Windows
The Windows build has the same gameplay and features as v0.5.1. These Linux changes do not affect it; the only change that reaches it is the version number, so there is no need to update from v0.5.1 unless you want the current release version.

### Downloads
**Windows**: **HardCorpsUprising-v0.5.2-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprisingRecompiled-v0.5.2-x86_64.AppImage**
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
- Everything else listed for v0.5.0 and v0.5.1 still applies.

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.5.2-windows-x64.sha256` and `HardCorpsUprisingRecompiled-v0.5.2-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.5.2-windows-x64.zip` | `baeeb1e67adbcbc38257807adfeabea093a4aca6a35257fb5be0c29a7312469f` |
| `HardCorpsUprisingRecompiled-v0.5.2-x86_64.AppImage` | `fd85c154b835389a16d631c1d6a3c7adb533bd46ade2686eb261d49fbafa8e77` |
