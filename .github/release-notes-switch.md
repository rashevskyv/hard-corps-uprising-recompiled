Hard Corps: Uprising (Xbox 360 XBLA, 2011) on Nintendo Switch, by static recompilation with the ReXGlue SDK. Built from commit `fdae3aa`.

## New since 0.297

- **Local 2-player** on the Switch, with two full controllers.
- **DLC characters** (Harley Daniels, Sayuri, Leviathan) on the Switch, from your own DLC packages.
- **OVERLAY and FPS COUNTER are independent** in the port menu: each one is shown or hidden alone.
- The port menu is smaller, and it has a new **CONTROLLERS** item (the console's controller screen).

## Read this first: experimental

- **Very little testing.** A few hours of single player on one console, and a short check of local 2-player and the DLC characters. Many stages and menus are not checked. Graphical glitches, crashes and freezes are possible, and save data may be lost.
- **Local 2-player needs two full controllers**: a Pro Controller, a Joy-Con pair, or the console in handheld mode. A single Joy-Con (one half) is not supported. The Xbox LIVE features are not available.
- **30 fps by default.** The game runs at full speed and every second frame is drawn. The 60 fps modes (FRAME RATE in the port menu) can slow the game down in busy scenes.
- Enemy cries may sound too loud.
- Quit with the game's own menu, not by resetting the console.

## Files

| File | What |
|---|---|
| `hcu-0.303.fdae3aa-switch.zip` | Switch (Atmosphère + hbmenu). Unzip to the SD root |
| `hcu-0.303.fdae3aa-windows-x64.zip` | Windows 10/11 x64, Direct3D 12 (see below) |
| `hcu-0.303.fdae3aa-linux-x86_64.AppImage` | Linux x86_64, Vulkan, glibc 2.38 or newer (see below) |
| `SHA1SUMS`, `SHA256SUMS` | checksums |

Switch NRO SHA-1: `655986f8f651647d9bf16cf0d37391ce08a6349f` (build `0.303.fdae3aa`).

## You need your own copy of the game

Extract the XBLA package (`tools/stfs_extract.py`, or the PC port's launcher) and put the extracted files (`default.xex` and the data folders) into `sdmc:/switch/hcu/game/`. Start the game from hbmenu with title takeover (hold R while starting any installed game). Install details, controls and the settings menu (L3 + R3: FRAME RATE, OVERLAY, FPS COUNTER, RUMBLE, CONTROLLERS): see the README's Switch section.

## Local 2-player (Switch)

- Connect a second controller: a Pro Controller or a Joy-Con pair. A single Joy-Con (one half) is not supported, because the game uses every button of a full controller.
- In handheld mode the console is player 1 and the wireless controller is player 2.
- Docked, or with the Joy-Cons detached, wireless controller 1 is player 1 and wireless controller 2 is player 2.
- To pair or reorder controllers without leaving the game, open the port menu (L3 + R3), pick **CONTROLLERS** and press **A**. The console's controller screen opens after a short flash.

## DLC characters (Switch)

Harley Daniels, Sayuri and Leviathan work from your own Xbox Live Arcade marketplace packages of the DLC. The release contains no DLC files. The Switch build reads DLC from `sdmc:/switch/hcu/user/`. Two ways to install it:

- **With the PC port**: install each DLC package in the PC launcher (the **DLC** page, **Install DLC...**). Then copy the folder `user_data/0000000000000000/58410A18` (next to the PC executable) to `sdmc:/switch/hcu/user/0000000000000000/58410A18`.
- **With the script**: run `python tools/stfs_extract.py --dlc <package> <SD>/switch/hcu/user` for each package, where `<SD>` is the SD card root.

Both ways end with `sdmc:/switch/hcu/user/0000000000000000/58410A18/00000002/<content id>/` and `.../58410A18/Headers/00000002/<content id>.header`. Details: the README's "DLC characters" section.

## Windows and Linux

Builds of this branch for PC. They are the upstream PC port (launcher, cheats, achievements, display options; the About page shows the upstream version 0.6.1) with one change: the recompiled code is generated with this branch's ReXGlue codegen fix (`switch/sdk-edits-hcu.py`: the vector registers stay in the context with `non_volatile_as_local`), which the intro movie needs. The PC source did not change since 0.297 (only the Switch code changed), so the program files inside both packages are byte-identical to the 0.297 ones. Tested only to the title screen: Windows on one PC, Linux in a software Vulkan driver (llvmpipe).

- `hcu-0.303.fdae3aa-windows-x64.zip`: unzip, run `hard_corps_uprising.exe`, install your package from the launcher. SHA-1 `1adf712b7c55b36727a9329a923217af33acf0c0`.
- `hcu-0.303.fdae3aa-linux-x86_64.AppImage`: mark it executable and run it. Built on Ubuntu 24.04, so it needs glibc 2.38 or newer (upstream's own builds need 2.31). SHA-1 `a0f17b1ab97ea1f8d67584395134bac0062fa97e`.

### Requirements

**Windows**
- Windows 10 or 11, 64-bit.
- A GPU with Direct3D 12 support.
- Microsoft Visual C++ Redistributable 2015–2022 (x64). It is not in the zip. Download: https://aka.ms/vs/17/release/vc_redist.x64.exe

**Linux**
- x86_64 with glibc 2.38 or newer (for example Ubuntu 24.04, Fedora 39, Debian 13 or newer).
- A GPU with a Vulkan driver.
- FUSE 2 for the AppImage (`libfuse2` / `libfuse2t64`). Without FUSE, run it with `--appimage-extract-and-run`.

Build notes: the Windows build used Visual Studio 2022 Build Tools with MSYS2's clang and the DirectX headers package (`HCU_VS_ROOT`, `HCU_LLVM_BIN`, `HCU_DX_HEADERS` in `tools/build.ps1`); the Linux AppImage was made with `tools/package-linux.sh` (`HCU_MAX_GLIBC=2.38`). Upstream's PC releases: [saulob/hard-corps-uprising-recompiled](https://github.com/saulob/hard-corps-uprising-recompiled/releases).

## Legal note

The archive contains the recompiled game program, which is a derived work of the game, and the translated shaders. It contains no game data (no graphics, sound, music or text) and no DLC. Use it only with a copy of the game you own. Hard Corps: Uprising belongs to Konami. This is an unofficial fan project, not affiliated with Konami, Arc System Works, Microsoft or Nintendo.
