Hard Corps: Uprising (Xbox 360 XBLA, 2011) on Nintendo Switch, by static recompilation with the ReXGlue SDK. Built from commit `<commit>`.

## Read this first: experimental

- **Very little testing.** A few hours of single player on one console. Many stages and menus are not checked. Graphical glitches, crashes and freezes are possible, and save data may be lost.
- **One controller.** Local 2-player and the Xbox LIVE features are not available. The DLC characters are not supported.
- **30 fps by default.** The game runs at full speed and every second frame is drawn. The 60 fps modes (FRAME RATE in the port menu) can slow the game down in busy scenes.
- Enemy cries may sound too loud.
- Quit with the game's own menu, not by resetting the console.

## Files

| File | What |
|---|---|
| `hcu-<version>-switch.zip` | Switch (Atmosphère + hbmenu). Unzip to the SD root |
| `SHA1SUMS`, `SHA256SUMS` | checksums |

Switch NRO SHA-1: `<nro sha1>` (build `<version>`).

## You need your own copy of the game

Extract the XBLA package (`tools/stfs_extract.py`, or the PC port's launcher) and put the extracted files (`default.xex` and the data folders) into `sdmc:/switch/hcu/game/`. Start the game from hbmenu with title takeover (hold R while starting any installed game). Install details, controls and the settings menu (L3 + R3: FRAME RATE, OVERLAY, FPS COUNTER, RUMBLE): see the README's Switch section.

## Windows and Linux

The PC port (Windows, Linux, Steam Deck) is the upstream project: [saulob/hard-corps-uprising-recompiled releases](https://github.com/saulob/hard-corps-uprising-recompiled/releases). This release adds the Switch build.

## Legal note

The archive contains the recompiled game program, which is a derived work of the game, and the translated shaders. It contains no game data (no graphics, sound, music or text). Use it only with a copy of the game you own. Hard Corps: Uprising belongs to Konami. This is an unofficial fan project, not affiliated with Konami, Arc System Works, Microsoft or Nintendo.
