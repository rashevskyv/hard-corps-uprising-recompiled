Hard Corps: Uprising (Xbox 360 XBLA, 2011) on Nintendo Switch, by static recompilation with the ReXGlue SDK. Built from commit `c728c58`. This release has the Switch build only: the PC code did not change since 0.303 (see "Windows and Linux" below).

## New since 0.303

- **Boss and enemy graphics**: 12 shaders that were missing on the console are now translated (34 files in `shaders/`). Without them some objects were not drawn, for example the cart enemy and parts of a boss cannon on stage 1.
- **Switch button icons** in the game's prompts: our own drawings of the Switch buttons (A, B, X, Y, L, R, ZL, ZR, +, -, the d-pad, the sticks, HOME) replace the Xbox icons. No Nintendo files are used.
- New port menu item **BUTTON ICONS**: SWITCH (default) or XBOX (the game's Xbox icons with the A/B and X/Y pictures swapped, as in 0.303). It applies at the next start. In `settings.txt`: `button_glyphs=switch` or `button_glyphs=xbox`.
- The log `hcu_log.txt` names each shader pair that has no translated shader (one line per pair), so a missing object can be reported with its shader numbers.

## Known gaps

- The boss and cart fix is checked on stage 1 only.
- Not replaced yet: the controller picture on the How to play screen, the Guide button ring, and the texts "Press START" and "Xbox 360 controller" that are part of the game's images.

## Read this first: experimental

- **Very little testing.** A few hours of single player on one console, and a short check of local 2-player and the DLC characters. Many stages and menus are not checked. Graphical glitches, crashes and freezes are possible, and save data may be lost.
- **Local 2-player needs two full controllers**: a Pro Controller, a Joy-Con pair, or the console in handheld mode. A single Joy-Con (one half) is not supported. The Xbox LIVE features are not available.
- **30 fps by default.** The game runs at full speed and every second frame is drawn. The 60 fps modes (FRAME RATE in the port menu) can slow the game down in busy scenes.
- Enemy cries may sound too loud.
- Quit with the game's own menu, not by resetting the console.

## Files

| File | What |
|---|---|
| `hcu-0.310.c728c58-switch.zip` | Switch (Atmosphère + hbmenu). Unzip to the SD root. To update 0.303, unzip over it: your `settings.txt`, game files and saves stay |
| `SHA1SUMS`, `SHA256SUMS` | checksums |

Switch NRO SHA-1: `9d7ba08ed2ae11218f313128fb2e6d8e25942336` (build `0.310.c728c58`).

## You need your own copy of the game

Extract the XBLA package (`tools/stfs_extract.py`, or the PC port's launcher) and put the extracted files (`default.xex` and the data folders) into `sdmc:/switch/hcu/game/`. Start the game from hbmenu with title takeover (hold R while starting any installed game). Install details, controls and the settings menu (L3 + R3: FRAME RATE, OVERLAY, FPS COUNTER, RUMBLE, BUTTON ICONS, CONTROLLERS): see the README's Switch section.

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

Not in this release. Use the 0.303 PC builds: [switch-0.303.fdae3aa](https://github.com/rashevskyv/hard-corps-uprising-recompiled/releases/tag/switch-0.303.fdae3aa) (Windows x64 zip, Linux x86_64 AppImage, requirements there). Everything new in 0.310 is Switch code, so a PC build of 0.310 would be the same program.

## Legal note

The archive contains the recompiled game program, which is a derived work of the game, and the translated shaders. It contains no game data (no graphics, sound, music or text) and no DLC. Use it only with a copy of the game you own. Hard Corps: Uprising belongs to Konami. This is an unofficial fan project, not affiliated with Konami, Arc System Works, Microsoft or Nintendo.
