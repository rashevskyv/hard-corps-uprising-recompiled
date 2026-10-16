## Hard Corps: Uprising PC Port v0.6.1 - AppImage Startup Fix for Restricted Environments (Linux)

v0.6.1 is a small technical release for Linux and the AppImage. It fixes the AppImage not starting at all where the system cannot mount it through FUSE. That is the failure reported for the Steam Beta client on SteamOS (nothing happens when the AppImage is started from Steam in Game Mode or Desktop Mode, no game log is written, while starting the file directly works), and it is intended to resolve it. **The fix is reproduced and verified in a local test environment that blocks FUSE. It has not been confirmed on SteamOS hardware yet.** Please report whether the issue is resolved. Gameplay and Windows are unchanged from v0.6.0.

### What changed
- **AppImage startup fix.** Every AppImage begins with a small runtime that mounts it through FUSE before the game starts. When the mount is impossible, for example when Steam starts the AppImage somewhere the FUSE helper cannot work, the old runtime stopped there with "Cannot mount AppImage, please check your FUSE setup", before any of the game's code ran. The runtime in v0.6.1 falls back by itself: it unpacks the AppImage into a private folder in `/tmp`, runs the game from there with your arguments, returns the game's exit status and removes the folder afterwards. Nothing has to be set in Steam. A restart of the game from the launcher goes through the same steps.
- **Normal systems are unchanged.** Where FUSE works, the AppImage is still mounted, not unpacked, and starts as before. A damaged AppImage is still reported as damaged.
- **Where the runtime comes from.** It is built from the upstream AppImage type2-runtime source at a pinned revision plus one small patch kept in the repository (`patches/appimage-runtime`), instead of the released binary. The patch also fixes upstream's cleanup of unpacked files, which does nothing in the released runtime.

### Linux / Steam Deck
- Starting the AppImage directly and Steam's stable client are not affected.
- If the AppImage still does not start from the Steam Beta, set the game's **Launch Options** to `APPIMAGE_EXTRACT_AND_RUN=1 %command%` and tell us whether that changes anything. It is a troubleshooting step, not something v0.6.1 needs.
- Linux remains **experimental**. This release has not been run on SteamOS or a Steam Deck.

### Windows
No functional change from v0.6.0. The release version number is new, and there is no need to update from v0.6.0 unless you want the current release version.

### Downloads
**Windows**: **HardCorpsUprising-v0.6.1-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprisingRecompiled-v0.6.1-x86_64.AppImage**
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
- **Steam Beta report: fix verified locally, not on SteamOS.** The fix was tested by starting the AppImage in an environment where the FUSE mount fails the way Steam's log shows, with and without Launch Options. Whether the reporter's device fails in exactly that way is the part still to be confirmed on SteamOS.
- **Linux status: experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend, not a Proton or Wine build. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware, so rendering, audio, controllers and the Steam Deck flow may still have problems. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md).
- Everything else listed for v0.6.0, v0.5.0 and v0.5.1 still applies.

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card, and whether you start the AppImage from Steam.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.6.1-windows-x64.sha256` and `HardCorpsUprisingRecompiled-v0.6.1-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.6.1-windows-x64.zip` | `pending` |
| `HardCorpsUprisingRecompiled-v0.6.1-x86_64.AppImage` | `pending` |
