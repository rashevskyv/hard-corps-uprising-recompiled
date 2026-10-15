## Hard Corps: Uprising PC Port v0.5.3 - Linux Launcher Compatibility Fix

v0.5.3 is a small technical patch release for Linux and the AppImage. Gameplay is unchanged from v0.5.2. The one real change is how the Linux build starts when Vulkan graphics cannot start. Windows has no functional change.

### Highlights
- **Software fallback launcher.** When Vulkan graphics initialization fails on Linux, the game now opens a 640x400 launcher window instead of showing an error dialog first. It is drawn in software with SDL, so it needs no Vulkan, GPU renderer, `zenity` or GTK.
- **Graphics error only on Play.** Pressing **PLAY** without a working Vulkan driver still shows the graphics error window, and closing it returns you to the launcher.

### Linux / Steam Deck
- **Fallback launcher.** The window shows the game name and icon, the state of your game files and of graphics (for example "Game files: Setup required" and "Graphics: Vulkan setup required"), and **PLAY** and **QUIT** buttons. It stays open normally and no error dialog appears by itself. Installing game files and changing settings still need the full launcher, which needs Vulkan.
- **The graphics error stays explicit.** The game still needs a working Vulkan driver. Pressing **PLAY** without one opens the existing error window, which says the graphics system could not start and points to the `logs` folder. Dismissing it returns to the fallback launcher.
- **Systems with a working Vulkan driver** open the normal launcher, as before.
- **Compatibility baseline.** As in v0.5.2, the AppImage is built on Ubuntu 20.04 (glibc 2.31) and its ELF files need `GLIBC_2.30` or older. The system's glibc is still used; it is not bundled.
- Linux support remains experimental.

### Windows
Gameplay and Windows runtime behavior are unchanged from v0.5.2. The release version number is new, and the game window title now reads Hard Corps: Uprising Recompiled. The Windows file names are unchanged, and there is no need to update from v0.5.2 unless you want the current release version.

### Downloads
**Windows**: **HardCorpsUprising-v0.5.3-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprisingRecompiled-v0.5.3-x86_64.AppImage**
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
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.5.3-windows-x64.sha256` and `HardCorpsUprisingRecompiled-v0.5.3-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.5.3-windows-x64.zip` | `fcae5d5a099f0107788244df0761fe9fd560f30c695bc77ffac0382b61b9db69` |
| `HardCorpsUprisingRecompiled-v0.5.3-x86_64.AppImage` | `f3a25f6b244352eee17578126e646f88a2df44aa80403a4b33253f6a2f2a5f94` |
