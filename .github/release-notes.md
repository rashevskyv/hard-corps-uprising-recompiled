## Hard Corps: Uprising PC Port v0.5.1 - Linux Compatibility Update

v0.5.1 is a small technical patch release. Gameplay, the launcher and the performance work from v0.5.0 are unchanged. Its purpose is better Linux and AppImage compatibility.

### Highlights
- **Broader Linux compatibility.** The Linux release is now built on Ubuntu 20.04 (glibc 2.31) instead of Ubuntu 22.04, so the AppImage no longer requires glibc 2.35.
- **New AppImage file name.** The Linux download is now named `HardCorpsUprising-v0.5.1-x86_64.AppImage`.
- **Visible error when graphics cannot start.** On Linux, a failure to start the Vulkan graphics system now shows an error window instead of the game exiting immediately.

### Linux / Steam Deck
- **Older glibc.** The release is built on Ubuntu 20.04 (glibc 2.31) and the build checks every packaged library against that baseline. The ELF files in the AppImage need `GLIBC_2.30` or older, so it should run on more Linux distributions than the v0.5.0 AppImage, which needed glibc 2.35. The system's glibc is still used; it is not bundled with the AppImage.
- **File name.** The v0.5.0 AppImage was named `HardCorpsUprising-v0.5.0-linux-x86_64.AppImage`. From v0.5.1 the name has no `-linux-` part: `HardCorpsUprising-v0.5.1-x86_64.AppImage`. The v0.5.0 release files are unchanged.
- **Graphics startup error.** Previously the game could exit at once, with only a log entry, when the Vulkan graphics system could not be set up. It now shows a window saying that the graphics system could not start, that a working Vulkan driver (Mesa for AMD and Intel, or the NVIDIA driver) is needed, and that the log folder next to the AppImage has the details. This covers a failure of the graphics system to start; other startup problems are still only logged.
- Linux support remains experimental.

### Windows
The Windows build has the same gameplay and features as v0.5.0. The only change that reaches it is the version number, so there is no need to update from v0.5.0 unless you want the current release version.

### Downloads
**Windows**: **HardCorpsUprising-v0.5.1-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.5.1-x86_64.AppImage**
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
- Everything else listed for v0.5.0 still applies.

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.5.1-windows-x64.sha256` and `HardCorpsUprising-v0.5.1-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.5.1-windows-x64.zip` | `6a48f5057d30e7ec29bbb0612d8989f38800faf8c0c7e61e9c57d5c15f33a23c` |
| `HardCorpsUprising-v0.5.1-x86_64.AppImage` | `51a9e8c491b5625fa03c162330aeb630559950546288f16bdaf91c2adea29047` |
