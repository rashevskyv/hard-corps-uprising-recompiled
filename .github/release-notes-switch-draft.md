## Hard Corps: Uprising for Nintendo Switch (homebrew, experimental) - DRAFT, not published

A Switch homebrew build of the Hard Corps: Uprising static recompilation. It runs the game's own code on the console with a native renderer (deko3d). **No game data is included**: you need your own Xbox Live Arcade package of the game.

### What works
- The full game from the title screen, at a steady 30 fps with the game at full speed (every second frame is drawn; the game logic keeps 60 steps a second).
- Music, sound effects and voices.
- Button prompts show the Switch letters: the buttons work by position (the bottom button confirms, as on the Xbox), and the prompt shows the letter printed on that Switch button.
- Rumble on Joy-Con, handheld mode and the Pro Controller.
- An in-game settings menu.

### Install
1. Copy `hcu.nro` and the `shaders` folder to `sdmc:/switch/hcu/`, and create an empty file `sdmc:/switch/hcu/deko3d`.
2. Extract the game files from your own package (the PC port's launcher does this: **Install from package...**) and copy the extracted `game` folder to `sdmc:/switch/hcu/game/`.
3. Start **Hard Corps: Uprising** from the Homebrew Menu with title takeover (hold **R** while you start any game).

### Settings menu
Press **both sticks** (L3 + R3) together to open or close it. Up/down picks a line, left/right or **A** changes it, **B** closes it. Closing saves `sdmc:/switch/hcu/settings.txt`. The game keeps running while the menu is open, so open it from the game's pause screen.

| Setting | Values | When |
| --- | --- | --- |
| FRAME RATE | 30 (default), 60 SKIP WHEN BUSY, 60, 30 HALF SPEED | at once |
| PERFORMANCE STRIP | OFF, ON | at once |
| RUMBLE | OFF, ON | at once |
| SHADOW BIAS | OFF, 32 (default), 256, 1024 | at once |
| COLOUR BIAS | -16 (default), OFF | at once |
| SHADOW MAP | ON (default), OFF | at once |
| PRECISE SHADERS | OFF, ON (default) | next start |
| VIEW | GAME (default), SCENE BEFORE POST, BLOOM 480X272 | at once (not saved) |

### Known issues
- Fine stripes and rings over the picture (ground, rocks, sky) and flickering shadows, on the console only. Under investigation: SHADOW BIAS, COLOUR BIAS, PRECISE SHADERS and texture filtering do not change it; SHADOW MAP OFF and VIEW find the pass that draws it.
- Enemy cries may sound too loud.
- "60" can slow the game down in busy scenes; "60 SKIP WHEN BUSY" keeps the game speed and drops frames instead.

### Files
- `hcu.nro`: SHA-1 `<fill in at release>` (test build 0.243.554f6ab: `4d441b3f2fe3e6a196456b4cd6f95a70392e43e5`)
- `shaders/`: 121 DKSH files (88 + 33 `.np` vertex variants for PRECISE SHADERS OFF)
