<div align="center">

# Hard Corps: Uprising — PC Port

### Hard Corps: Uprising running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher and modern PC options.

[![Latest release](https://img.shields.io/github/v/release/saulob/hard-corps-uprising-recompiled?style=for-the-badge&label=release&color=99db2e&labelColor=07101f&include_prereleases)](https://github.com/saulob/hard-corps-uprising-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/hard-corps-uprising-recompiled/total?style=for-the-badge&color=2e7d32&labelColor=07101f)](https://github.com/saulob/hard-corps-uprising-recompiled/releases)
![Platform](https://img.shields.io/badge/platforms-Windows%20x64%20%7C%20Linux%20x86__64%20(experimental)-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Hard Corps: Uprising Xbox Live Arcade package.</sub>

</div>

## What you need

- Windows 10 or 11, 64-bit, and a DirectX 12 graphics card
- The latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already have it.
- **Or** 64-bit Linux or a Steam Deck with a Vulkan driver (experimental, see [Linux and Steam Deck](#linux-and-steam-deck))
- **Your own Hard Corps: Uprising Xbox Live Arcade package**, this exact game version:

| | |
| --- | --- |
| Game | Hard Corps: Uprising, Xbox Live Arcade |
| Title ID | `58410A18` |
| Content type | `000D0000` (Xbox Live Arcade), STFS `LIVE` package |
| Media ID | `3C5663CB` |
| Game version | `0.0.0.3` |
| `default.xex` | 12,955,648 bytes, CRC32 `3DF903B9`, SHA-256 `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` |

The launcher checks the Title ID and content type when you pick the package, and checks `default.xex` after extracting it. Package containers can differ between copies, so `default.xex` is what identifies the version. Any other version is refused, because the port is built for this exact game code.

## Installing

1. Download **HardCorpsUprising-v0.3.0-windows-x64.zip** from the [latest release](https://github.com/saulob/hard-corps-uprising-recompiled/releases/latest) and extract it anywhere you can write to.
2. Run **hard_corps_uprising.exe**. The launcher opens.
3. On the **Play** page click **Install from package...** and pick your package. It is one file with a long hexadecimal name and no extension. On an Xbox 360 drive it is in `Content\0000000000000000\58410A18\000D0000\`.
4. Wait until **Ready to play** appears. The game files (about 1 GB) are copied into the `game` folder next to the program. The package file itself is not copied there.
5. Press **PLAY**.

**No game files are included** in the release.

**Reinstall from package...** repairs the game files and keeps your settings and saves.

### Optional DLC

The DLC characters are Harley Daniels, Sayuri and Leviathan. DLC is optional, and the port does not include or download DLC packages: you need your own Xbox 360 DLC package for each one.

1. Install the game first.
2. Open the **DLC** page (directly after **Play**), click **Install DLC...**, and pick your DLC package. The launcher checks that it is a Hard Corps: Uprising package (Title ID `58410A18`, content type `00000002`, Marketplace). It refuses the game package and other packages.
3. The **DLC** page shows each character as Installed or Not installed. **Remove** on an installed character removes only that package, after a confirmation. The game and the other DLC are not touched, and you can install the package again later from your own copy.

Installed DLC is stored in your user data folder (`Documents\hard_corps_uprising` on Windows, or the `--user_data_root` folder), not in the game folder. Reinstalling the game does not remove it.

### Linux and Steam Deck

**Experimental, not yet tested on real hardware.** This is a native Linux build (ReXGlue's Linux runtime with Vulkan), not Proton.

1. Download **HardCorpsUprising-v…-linux-x86_64.AppImage** from the release and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it, then install your package from the **Play** page as above. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck (intended flow, not yet tried on a Deck), do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

Settings and saves are in `~/.local/share/hard_corps_uprising`, logs in `logs` next to the AppImage. Requirements, known limits and building the AppImage are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. If you downloaded it from this repository's releases page, choose **More info**, then **Run anyway**.

## The launcher

The launcher opens before the game. Your choices are saved to `settings.toml` in `Documents\hard_corps_uprising`. To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back.

### Play
- **Install from package...** with the Title ID and game version checks, and **Reinstall from package...**
- The **Ready to play** status, and the **Show this launcher** setting

### DLC
- Harley Daniels, Sayuri and Leviathan, each shown as **Installed** or **Not installed**
- **Install DLC...** for your own package, and **Remove** for an installed character (see [Optional DLC](#optional-dlc))

### Display
- **Display mode**: Windowed or Borderless Fullscreen. Press **F11** while playing to switch between them.
- **Window size** and **Monitor**
- **VSync** on or off
- **Aspect ratio**: Letterbox 16:9 keeps the game's picture on any window shape; Stretch fills the window

Window size, monitor and VSync take effect when you press **PLAY**. If one differs from the launcher's window, the game restarts once in a new window.

### Graphics
- **Render resolution**: Original (1280×720), 2x (2560×1440, recommended for 1440p), 3x (3840×2160) or 4x (5120×2880)
- **Anti-aliasing**: Off, FXAA or FXAA Extreme
- **Multisampling**: 2x MSAA on or off
- **Texture filtering**: Game, Off, 2x, 4x, 8x or 16x

### Gameplay
- **Frame rate in title**: shows the frame rate in the window title, for example `Hard Corps: Uprising | FPS: 60.00`

### Cheats
- **Infinite Health**: normal damage no longer lowers a player's health
- **Infinite Lives**: the number of remaining lives no longer goes down
- Both apply to every character and to both players in local two-player

All cheats are off by default and can be enabled from the Cheats page before Play.

### Controls
- Xbox 360 controllers work without setup
- **Keyboard**: enabled or disabled. The keys are listed on the page; [docs/input.md](docs/input.md) has the full layout.
- Plain arrow keys are the right stick. Menus use **W A S D** or **Shift + arrow** keys.
- **Local two-player** works with two Xbox controllers. The keyboard and a connected controller both drive player 1.
- Online multiplayer is not supported.

### About
- The port version, game version and library versions
- Open the game folder, or the saves and settings folder
- **Show settings file**, and **Reset all settings**

## Screenshots

<img width="1966" height="1179" alt="title_screen" src="https://github.com/user-attachments/assets/bff13f4f-3a4e-488d-8cf0-1b6cde1c0875" />

<img width="1966" height="1172" alt="gameplay" src="https://github.com/user-attachments/assets/6029f8f7-e0a8-49af-ac51-46d6ab54926d" />

<img width="1966" height="1172" alt="dlc_harley" src="https://github.com/user-attachments/assets/ad445921-1b3d-4e4d-bca6-8496c83a0afe" />


## Reporting problems

If the game does not start or crashes, please open an [issue](https://github.com/saulob/hard-corps-uprising-recompiled/issues). Attach the newest `hard_corps_uprising_*.log` file from the `logs` folder next to the program, and say what you were doing when the problem happened.

## Building it yourself

The recompiled C++ is generated from your own game files, so the repository does not contain it. Clone with submodules, so the TRG launcher is included:

```powershell
git clone --recurse-submodules https://github.com/saulob/hard-corps-uprising-recompiled.git
```

In an existing clone, run `git submodule update --init --recursive`.

1. Install Visual Studio with the C++ and LLVM/Clang components. `tools\build.ps1` expects Visual Studio 18 Community at its default location.
2. Extract your package's files into `game\extracted` with an Xbox 360 STFS extractor, so that `game\extracted\default.xex` exists.
3. `.\tools\codegen.ps1` downloads the pinned ReXGlue SDK 0.10.0, checks `default.xex` and generates the C++ into `generated\`.
4. `.\tools\build.ps1` builds `build\hard_corps_uprising.exe`. `.\tools\build.ps1 -Run` also starts it.
5. `.\tools\package-release.ps1` makes the release ZIP in `dist\`. It publishes nothing.

The Linux AppImage is built on Linux or WSL with `tools/package-linux.sh`, after the steps above have generated the C++. See [docs/linux.md](docs/linux.md).

How the launcher, settings and installer work is described in [docs/launcher.md](docs/launcher.md). The other technical notes are in [docs/](docs/).

## Credits

- Hard Corps: Uprising by Arc System Works, published by Konami for Xbox Live Arcade (2011). This is an unofficial fan-made PC port. It is not affiliated with or endorsed by Konami or Arc System Works, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0. The referenced revision contains no explicit license file.
- The launcher and package installer follow the approach of the [Earthworm Jim HD PC port](https://github.com/TekRantGaming/earthworm-jim-hd-recompiled)
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP

## AI disclosure

AI tools were used to assist with code analysis, development, debugging and documentation. Changes were directed, tested and reviewed by the maintainer.

## License

Original code in this project is licensed under the GNU General Public License v2.0 only (GPL-2.0-only). See [LICENSE](LICENSE).

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt) and [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt).

No game files are included. Hard Corps: Uprising and its assets are not covered by this project's license.
