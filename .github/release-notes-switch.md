Hard Corps: Uprising (Xbox 360 XBLA, 2011) on Nintendo Switch, by static recompilation with the ReXGlue SDK. Built from commit `5be467e`.

## Read this first: experimental

- **Very little testing.** A few hours of single player on one console. Many stages and menus are not checked. Graphical glitches, crashes and freezes are possible, and save data may be lost.
- **One controller.** Local 2-player and the Xbox LIVE features are not available. The DLC characters are not supported.
- **30 fps by default.** The game runs at full speed and every second frame is drawn. The 60 fps modes (FRAME RATE in the port menu) can slow the game down in busy scenes.
- Enemy cries may sound too loud.
- Quit with the game's own menu, not by resetting the console.

## Files

| File | What |
|---|---|
| `hcu-0.297.5be467e-switch.zip` | Switch (Atmosphère + hbmenu). Unzip to the SD root |
| `hcu-0.297.5be467e-windows-x64.zip` | Windows 10/11 x64, Direct3D 12 (see below) |
| `hcu-0.297.5be467e-linux-x86_64.AppImage` | Linux x86_64, Vulkan, glibc 2.38 or newer (see below) |
| `SHA1SUMS`, `SHA256SUMS` | checksums |

Switch NRO SHA-1: `0f96ad4e7eb6cfbc28c446608fe93ae9919c2beb` (build `0.297.5be467e`).

## You need your own copy of the game

Extract the XBLA package (`tools/stfs_extract.py`, or the PC port's launcher) and put the extracted files (`default.xex` and the data folders) into `sdmc:/switch/hcu/game/`. Start the game from hbmenu with title takeover (hold R while starting any installed game). Install details, controls and the settings menu (L3 + R3: FRAME RATE, OVERLAY, FPS COUNTER, RUMBLE): see the README's Switch section.

## Windows and Linux

Builds of this branch for PC, added to this release. They are the upstream PC port (launcher, cheats, achievements, display options; the About page shows the upstream version 0.6.1) with one change: the recompiled code is generated with this branch's ReXGlue codegen fix (`switch/sdk-edits-hcu.py`: the vector registers stay in the context with `non_volatile_as_local`), which the intro movie needs. Tested here only to the title screen: Windows on one PC, Linux in a software Vulkan driver (llvmpipe).

- `hcu-0.297.5be467e-windows-x64.zip`: unzip, run `hard_corps_uprising.exe`, install your package from the launcher. SHA-1 `13205ec0a7cfc44332f2e59c82225c60fc062fd2`.
- `hcu-0.297.5be467e-linux-x86_64.AppImage`: mark it executable and run it. Built on Ubuntu 24.04, so it needs glibc 2.38 or newer (upstream's own builds need 2.31). SHA-1 `62595667edff4e42ad652877290fa30cf8e40bab`.

Build notes: the Windows build used Visual Studio 2022 Build Tools with MSYS2's clang and the DirectX headers package (`HCU_VS_ROOT`, `HCU_LLVM_BIN`, `HCU_DX_HEADERS` in `tools/build.ps1`); the Linux AppImage was made with `tools/package-linux.sh` (`HCU_MAX_GLIBC=2.38`). Upstream's PC releases: [saulob/hard-corps-uprising-recompiled](https://github.com/saulob/hard-corps-uprising-recompiled/releases).

## Legal note

The archive contains the recompiled game program, which is a derived work of the game, and the translated shaders. It contains no game data (no graphics, sound, music or text). Use it only with a copy of the game you own. Hard Corps: Uprising belongs to Konami. This is an unofficial fan project, not affiliated with Konami, Arc System Works, Microsoft or Nintendo.
