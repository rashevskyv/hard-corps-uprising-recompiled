## Hard Corps: Uprising PC Port v0.5.0 - Stability, Frame Pacing and Linux Rendering

v0.5.0 focuses on Linux/Vulkan rendering, Exit Game stability and another round of frame-pacing and GPU-side optimizations. It also adds a Skip intro option and corrects the Multisampling choices. Gameplay is unchanged, and everything from v0.4.0, including its CPU and GPU performance work, is still included.

### Highlights
- **Linux/Vulkan: correct texture brightness.** Fixed incorrect texture brightness that could cause dark images and missing glow effects. Windows was not affected.
- **Exit Game fixes.** Fixed Exit Game hangs and shutdown crashes, especially visible on Linux / SteamOS Game Mode.
- **Smoother frame pacing.** Each game frame is presented once, instead of a second time by a UI repaint, and the present wait wakes at the vertical blank instead of polling in 1 ms steps.
- **Less GPU resolve work**, especially with a Render resolution above Original.
- **New: Skip intro.** A launcher option that bypasses the startup logos and intro and goes directly to the title screen. Off by default.
- **Corrected Multisampling choices**: 2x MSAA (default) or 4x MSAA (Compatibility). The old Off choice never turned multisampling off and has been removed.
- **Better automatic GPU selection** on PCs with integrated and dedicated graphics.
- **Linux: the Vulkan presenter no longer rebuilds its output pipeline and waits for the GPU on every frame** (fixes ReXGlue issue #507).

### Linux / Steam Deck
- **Texture brightness and glow.** The Vulkan shader translator read each texture fetch's exponent bias from the wrong word of the texture fetch constant (word 4, which holds the LOD bias, instead of word 3). Textures could then be scaled by the wrong power of two, which could darken images and lose glow effects. It now reads word 3, as the D3D12 renderer on Windows already did. Windows was not affected.
- **Output pipeline cache.** The Vulkan presenter checked which format its output pipeline was created for, but never stored that format after creating it. So on every game frame it waited for the previous frame on the GPU, destroyed the pipeline and created it again. The pipeline is now reused while the format stays the same, and a real format change recreates it once. This fixes ReXGlue issue [#507](https://github.com/rexglue/rexglue-sdk/issues/507) in the port's patched runtime.
- **Discrete GPU first.** Vulkan prefers a discrete GPU among the suitable devices. A device chosen with `DRI_PRIME`, `MESA_VK_DEVICE_SELECT`, `VK_LOADER_DEVICE_SELECT`, `__NV_PRIME_RENDER_OFFLOAD` or the `vulkan_device` setting still takes precedence.
- **Less CPU spinning** in a runtime wait path: a wait on several objects now backs off while another thread holds one of them, instead of spinning.
- The Exit Game fixes under **Stability** apply to Linux as well, where the hang was reported from SteamOS Game Mode.

### Performance and Frame Pacing
- **Each game frame is presented once.** A repaint for the port's on-screen overlay presented the same game frame a second time at the next vertical blank: about 120 presents a second for 60 game frames, and on Linux as fast as the GPU allowed. The overlay is now drawn with the next game frame.
- **Tighter vblank timing.** When the game's present waits for the flip, the GPU command processor now wakes on the vertical blank instead of polling in 1 ms sleeps, which could resume up to 1 ms late.
- **Resolves copied straight into textures.** When an eligible render-target resolve covers exactly what a texture samples, the texture is copied from the render target on the GPU (D3D12 and Vulkan), avoiding unnecessary decode and reload work.
- **Less resolve write-back above Original.** At render resolutions above Original, eligible resolves keep their result in the texture and write the resolve memory only when something needs it, which is often never.
- **Native memcpy/memset.** The game's C runtime `memcpy` and `memset` use the native host routines wherever the memory ranges allow it.
- **ThinLTO.** The game is linked with ThinLTO in Release and RelWithDebInfo builds.

No new frame-rate or CPU figures are given for v0.5.0: these changes are about smoother pacing and less redundant work, and the results depend on the PC, the settings and the scene.

### Stability
- **Input shuts down before the window is destroyed.** At Exit Game the window was destroyed before the input system, which then touched the destroyed window during shutdown and could hang or crash it. Input now shuts down first.
- **Game threads stop before the runtime is torn down.** A game thread could keep running while the runtime destroyed the kernel state and game memory, and crash a few seconds after the game had ended. Every game thread is now stopped before anything is torn down.

Together these fix Exit Game hangs and shutdown crashes, especially visible on Linux / SteamOS Game Mode ([#9](https://github.com/saulob/hard-corps-uprising-recompiled/issues/9)). The reporter has not confirmed the fix yet, and whether the B-button behaviour described in that report has the same cause is not known. Feedback there is welcome.

### Launcher and Graphics Settings
- **Skip intro** (Gameplay page, Off by default) starts directly at the title screen, bypassing the startup logos and intro. It only skips the presentation at startup; the game files and saves are not changed.
- **Multisampling** (Graphics page) now offers **2x MSAA**, the default and the fastest, and **4x MSAA (Compatibility)** for GPUs or drivers that show graphics problems with 2x. The old **Off** choice is gone: it never turned multisampling off, and the game's 2x MSAA render targets were drawn with 4x MSAA instead. A saved Off is replaced by the default, 2x MSAA, at the next start. If Off was working around graphics problems for you, choose 4x MSAA (Compatibility), which is what Off really did. The log now says which host MSAA mode is in use.
- **GPU selection on Windows.** The high-performance D3D12 adapter is preferred, and the game exports the NVIDIA Optimus and AMD PowerXpress high-performance hints. A GPU preference set for the game in Windows' graphics settings, or the `d3d12_adapter` setting, still takes precedence.

### Downloads
**Windows**: **HardCorpsUprising-v0.5.0-windows-x64.zip**
1. Unzip it anywhere you can write to. To keep your settings, saves and game files when updating, extract it over the previous version, or move `user_data` and `game` across.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.5.0-linux-x86_64.AppImage**
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
- **Linux**: 64-bit x86 Linux with glibc 2.35 or newer (Ubuntu 22.04 or later), a Vulkan driver, and an X11 or Wayland desktop.

### Notes / Known Limitations
- **Linux status: experimental.** The Linux build is a native x86_64 program on the ReXGlue 0.10.0 Linux runtime with its Vulkan graphics backend, not a Proton or Wine build. A SteamOS tester has run it, but it has not been broadly tested on real Linux PCs or Steam Deck hardware, so rendering, audio, controllers and the Steam Deck flow may still have problems. See [docs/linux.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/linux.md).
- The Linux texture brightness fix has not been confirmed on a real Linux PC or Steam Deck yet. Reports, ideally with a screenshot, are welcome.
- 4x MSAA (Compatibility) costs more GPU time than 2x MSAA. In a small launcher window (for example 1080p at 200% display scaling or more) its label can be cut off.
- The ReXGlue runtime and GPU plugin are ReXGlue SDK 0.10.0 built with project-maintained compatibility, stability and performance patches; the rest of the SDK is the unmodified release.

### Build / Developer Notes
- The patched ReXGlue runtime and GPU plugin are fingerprinted together with the compiler and platform, and builds, including CMake presets and IDE builds, check that the copies next to the program are exactly those files.
- New `tools/build.sh` for a normal Linux developer build. The Linux RelWithDebInfo runtime libraries are now patched too.
- Stricter package and runtime verification on both platforms.
- The patch stacks are 25 patches on Windows and 26 on Linux, listed in [docs/rexglue-runtime-patch.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/rexglue-runtime-patch.md).

### Reporting Problems
If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues) and attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program (next to the AppImage on Linux), with a line about what you were doing. For Linux reports, please also say your distribution and graphics card.

### Checksums
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.5.0-windows-x64.sha256` and `HardCorpsUprising-v0.5.0-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.5.0-windows-x64.zip` | `7fb7af5f1a2db621fa4741c7539e72da2c9e17986239cad4a39e2dbc458d5a9a` |
| `HardCorpsUprising-v0.5.0-linux-x86_64.AppImage` | `b92d2774204f4e1960df5ec9e38048bc358a001ec9a9db4e97c8b791d81fae3c` |
