## Hard Corps: Uprising PC Port v0.3.1 - Memory Release Fix

A bug fix release. Everything else is unchanged from v0.3.0, including optional DLC character support.

### Fixed
- Fixes a memory allocator race in the ReXGlue runtime that could corrupt the game's physical memory bookkeeping when it released memory while another thread was allocating. It was found while investigating a crash on exit reported on SteamOS ([issue #6](https://github.com/saulob/hard-corps-uprising-recompiled/issues/6)): the logs showed `BaseHeap::Release failed because address is not a region start`, the error this race produces. The bug exists on Windows and Linux alike.
- The release uses ReXGlue 0.10.0 with one patch to `PhysicalHeap::Release`, which now releases the physical pages only after the game's own memory view has let go of them. Details are in [docs/rexglue-runtime-patch.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/rexglue-runtime-patch.md).

The crash on exit has not yet been re-tested on SteamOS with this version, so please report whether it still happens. The frame rate overlay report in the same issue is not part of this release.

### Downloads and install
**Windows**: **HardCorpsUprising-v0.3.1-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.3.1-linux-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

### Linux status
**Experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend. It is **not** a Proton or Wine build. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware, so rendering, audio, controllers and the Steam Deck flow may still have problems. Please report what you find. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md) for details and known limits.

### Reporting problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Requirements
- **Windows**: Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).
- **Linux**: 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.3.1-windows-x64.sha256` and `HardCorpsUprising-v0.3.1-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.3.1-windows-x64.zip` | `e9b7483e2fd5f43e26cfe289b734346de89808fd3a80ab7235d86c5db842c1db` |
| `HardCorpsUprising-v0.3.1-linux-x86_64.AppImage` | `27c77966c69cfa3768df49b1cd4b50f1df744ac8af81d24dff6ec7d7320b678f` |
