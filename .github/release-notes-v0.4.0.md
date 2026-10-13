## Hard Corps: Uprising PC Port v0.4.0 - Major Performance Improvements

v0.4.0 is a major cross-platform performance release, bringing extensive CPU, GPU and runtime optimizations to both Windows and Linux. Windows received substantial improvements to D3D12 rendering, synchronization and CPU wait behavior, while Linux now includes equivalent Vulkan/POSIX optimizations for the applicable runtime and rendering paths. The result is significantly lower CPU overhead, reduced render-target and EDRAM transfer work, improved GPU synchronization and more efficient command processing, without changing gameplay behavior.

### Major performance improvements
This is the largest performance-focused release so far. The work covers the whole path between the game and the graphics card:
- **CPU wait behavior**: far less busy-waiting while the game waits for the GPU and for timers.
- **GPU synchronization**: the game's GPU fence wait now blocks efficiently and wakes the moment the GPU is done.
- **GPU register handling and command processing**: faster handling of the stream of register writes and commands the game sends to the graphics card.
- **Command submission**: how work is handed to the GPU was tuned to cut CPU overhead.
- **Render-target ownership, stencil and depth handling**: fewer transfers between render targets, and transfers limited to the area that is actually used.
- **EDRAM tracking and dirty tiles**: the emulated Xbox 360 EDRAM is tracked per tile, so only tiles that changed are copied.
- **Resolve behavior**: less preparation work before a resolve.

Each platform uses its own native mechanisms where that is the right way to do it. The results are equivalent behavior, not identical source code.

### CPU and synchronization improvements
- A GPU fence polling loop that kept the game's main thread spinning was replaced with an efficient blocking wait. It is bounded, so the game always keeps making progress.
- Less unnecessary CPU busy-waiting, more efficient GPU fence synchronization and lower main-thread CPU usage.
- Timer, vertical-blank and guest critical-section waits were reworked to avoid unnecessary wakeups and spinning.
- **On the Windows test system**, the game's main thread used approximately 98-100% of one CPU core in demanding gameplay before this change and approximately 14-20% of one core in representative heavy gameplay after it. Results vary with the PC.

### GPU / rendering performance
- Faster handling of GPU register writes and a more efficient command processor.
- Better command submission, and occlusion queries that no longer stall the CPU.
- Fewer render-target ownership transfers. A transfer is skipped only when the game is proven to overwrite the area anyway; the same EDRAM address alone is never treated as enough.
- Less stencil and depth transfer work: transfers cover only the tiles and rows that are used.
- Dirty-tile EDRAM tracking, and a cheaper draw path.
- **On the Windows test system**, scenes that were previously demanding were able to sustain approximately 59-60 FPS, including at a high internal render scale during validation. This is a result from one system, not a guarantee for every PC or scene.

### Linux / Vulkan performance parity
v0.4.0 extends the applicable performance work to Linux instead of leaving it on the older runtime and rendering paths.
- Linux now uses the project's patched ReXGlue runtime and its patched Vulkan GPU plugin.
- Command submission is optimized, and occlusion/query handling avoids unnecessary CPU stalls.
- The render-target transfer optimizations were ported to Vulkan, including stencil/depth region tracking and dirty EDRAM tile handling.
- GPU fence waits use efficient futex-based blocking.
- The Linux command thread no longer wakes in short polling slices while it is idle, where applicable.

The Linux runtime and the AppImage passed automated build, packaging and regression validation. **Linux performance has not been measured on real gameplay yet**, so no frame rate or CPU figures are given for it. Reports from real Linux PCs and Steam Deck are welcome.

### Runtime and build improvements
The build workflow now tracks the active ReXGlue patch set per platform and refreshes the patched runtime and GPU plugin when required, reducing the chance of accidentally using stale runtime binaries. This applies to both Windows and Linux.

### Linux RenderDoc fix
Fixed unintended RenderDoc activation on Linux. ReXGlue now detects RenderDoc only when it is already intentionally attached instead of loading librenderdoc.so during ordinary startup. Normal RenderDoc debugging support is preserved, with focused loader and packaging checks to prevent the issue from returning.

### Included from earlier releases
All gameplay, launcher, offline Live, portability and quality-of-life improvements from v0.3.2 remain included: the cheat menu (**F10**), offline achievements and leaderboards, portable `user_data`, the FPS counter (**F2**) and the launcher and window improvements. So does the v0.3.1 memory allocator fix; details are in [docs/rexglue-runtime-patch.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/rexglue-runtime-patch.md).

### Downloads and install
**Windows**: **HardCorpsUprising-v0.4.0-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.4.0-linux-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included, and no DLC files are included.** You need your own package, this exact game version:

| | |
| --- | --- |
| Title ID | `58410A18` (Xbox Live Arcade, content type `000D0000`) |
| Game version | `default.xex` 0.0.0.3, 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

### Requirements and platform notes
- **Windows**: Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).
- **Linux**: 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04 or later), a Vulkan driver, and an X11 or Wayland desktop.
- **Linux status: experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend. It is **not** a Proton or Wine build. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware, so rendering, audio, controllers and the Steam Deck flow may still have problems. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md) for details and known limits.
- Linux packaging now checks the libraries' search paths and rejects build-machine paths in the packaged binaries.
- The ReXGlue runtime and GPU plugin are ReXGlue SDK 0.10.0 built with project-maintained compatibility and performance patches; the rest of the SDK is the unmodified release.

### Reporting problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.4.0-windows-x64.sha256` and `HardCorpsUprising-v0.4.0-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.4.0-windows-x64.zip` | `740069c868444e5937e7f510434af5a5594b49ef4cebade8e1cb101320c16a9d` |
| `HardCorpsUprising-v0.4.0-linux-x86_64.AppImage` | `0c9a9e2f7762a37f84dc45db3a9a6e2aa821f9debcdff1bace494a5867647ede` |
