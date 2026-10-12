## Hard Corps: Uprising PC Port v0.3.2 - Gameplay and Launcher Improvements

This release brings together the gameplay, offline Live, portability and launcher improvements added since v0.3.1.

### New: cheats
- Press **F10** in game to open the cheat menu. Up and Down (or the D-pad) select, Enter (or A) toggles, F10, Escape or B closes.
- **Infinite Health**, **Infinite Lives**, **Rapid Fire**, **Jump Height 2x**, **Keep Weapons on Hit**, **Movement Speed 2x** and **Super Armor**.
- All cheats are off by default. They are also set on the launcher's **Cheats** page before Play. Changes made in the in-game menu apply to the current session only and are not saved.
- Cheats apply to every character and to both players in local two-player.

### New: offline Live features
Everything stays offline: there is no Xbox Live connection, Microsoft sign-in or online player.
- **Achievements**: the game's 12 achievements unlock locally with the usual notification and are kept between runs. They have an in-game list (**F7** or the menu items) and a launcher **Achievements** page.
- **Leaderboards**: the game's own Leaderboards screen shows your own scores, kept in a local file on this PC.
- **Download Content** shows a short note instead of the Xbox Marketplace. Extra characters are installed from the launcher's DLC page.

### New: portable user data
Saves, settings and player data now live in a `user_data` folder beside the program (beside the AppImage on Linux). Copying the program folder to another PC carries them along. Data from the previous system location (`Documents\hard_corps_uprising` on Windows, `~/.local/share/hard_corps_uprising` on Linux) is moved into `user_data` automatically at the first start. If the move fails, or the program folder is not writable, the old location is used for that run and nothing is lost. Details are in [docs/user-data.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/user-data.md).

### New: FPS counter
- A frame counter in the top-right corner shows the frame rate and frame time (for example `60 FPS  16.7 ms`).
- Enable it from the launcher's **Gameplay** page. It is hidden by default. **F2** shows or hides it for the current session without changing the saved setting.
- **Frame rate in title** is a separate, independent option that shows the frame rate in the window title.
- The counter stays readable together with the F10 cheat menu.

### Launcher and window improvements
- The launcher remembers its position and size, and whether it was maximized or in its own F11 borderless fullscreen.
- **F11** works in both places: in the launcher it controls the launcher's window, and in game it controls the game's Display mode. The two are independent, and the game's Display mode does not change the launcher's state.
- A saved position that is no longer on any screen, for example after unplugging a monitor, is recovered onto a visible screen.
- During focused fullscreen gameplay the mouse cursor hides after 5 seconds without movement and returns as soon as the mouse moves.

### Achievement overlay improvements
- A visible **X** close button.
- A clearer, larger **B Back** prompt.
- One **B** press now closes only the achievements overlay, instead of also reaching the menu underneath.

### Existing fixes included
v0.3.2 includes the v0.3.1 memory allocator fix: ReXGlue 0.10.0 with the `PhysicalHeap::Release` patch, which releases physical pages only after the game's own memory view has let go of them. Details are in [docs/rexglue-runtime-patch.md](https://github.com/saulob/hard-corps-uprising-recompiled/blob/main/docs/rexglue-runtime-patch.md).

### Downloads and install
**Windows**: **HardCorpsUprising-v0.3.2-windows-x64.zip**
1. Unzip it anywhere you can write to.
2. Run **hard_corps_uprising.exe**.
3. On the launcher's **Play** page click **Install from package...** and pick your own Hard Corps: Uprising Xbox Live Arcade package (not needed again if the game is already installed).
4. Press **PLAY**.

**Linux and Steam Deck (experimental)**: **HardCorpsUprising-v0.3.2-linux-x86_64.AppImage**
1. Put it in a folder you can write to and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it and install your own package from the **Play** page, as on Windows. The game files go in a `game` folder next to the AppImage.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

**No game files are included, and no DLC files are included.** You need your own package, this exact game version:

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
SHA-256 files are published with the release for both downloads: `HardCorpsUprising-v0.3.2-windows-x64.sha256` and `HardCorpsUprising-v0.3.2-linux-x86_64.sha256`.

| File | SHA-256 |
| --- | --- |
| `HardCorpsUprising-v0.3.2-windows-x64.zip` | `f3e99954f31244898f25dd5d9222922f93f99b660b6ade4949fb0caf04a10fc4` |
| `HardCorpsUprising-v0.3.2-linux-x86_64.AppImage` | `31f68c6484e0af72ce1dabd2de5fbd28d47a173b109c50a1da35f4657f6987a5` |
