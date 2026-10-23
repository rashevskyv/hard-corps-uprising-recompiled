# Button glyphs and confirm/cancel on the Switch (planned, not done)

Postponed by the user on 2026-10-23 (to be done later in a cloud session). Applies to all our Switch ports.

## What is wanted

1. **Positional glyph swap.** The Switch input maps positionally: Xbox A (bottom) = Switch B, Xbox B
   (right) = Switch A, Xbox X (left) = Switch Y, Xbox Y (top) = Switch X
   (`switch/host/input/nx_input_driver.cpp`). The game draws Xbox glyphs (A green, B red, X blue,
   Y yellow). On the Switch build, each glyph should show the Switch button at the same physical place:
   the game's "A" becomes a "B" glyph, "B" becomes "A", "X" becomes "Y", "Y" becomes "X".
2. **Switch-style icons** for those glyphs (not Xbox colours).
3. **`menu_confirm=nintendo|xbox`** (default `nintendo` on the Switch): in menus, Switch A (right) confirms
   and Switch B (bottom) cancels. Gameplay buttons stay as they are. The glyphs follow the active mapping.
4. **`button_glyphs=switch|xbox`** (default `switch` on Switch builds), so the PC build is unchanged.

## Where HCU draws glyphs (what is known)

- The prompts (`Select`, `OK` with a green A, `Cancel` with a red B) are 2D GUI draws, seen on the save
  dialog and the character select (Eden screenshots, build 0.111).
- The GUI images are in `game/DataXENON/Gui/Img/` (`*.bin`, per language in `DE/EN/ES/FR/IT/JP/`). The
  button icon sheet is not identified yet.

## Plan

1. Find the glyph texture: log the GUI textures bound by the prompt draws (the deko3d backend's per-draw
   texture log gives the guest address; match the address to the file the game read).
2. Swap or replace the glyph cells at load time: a hook after the GUI resource load (or on the texture
   upload in the backend) that rewrites the four cells, behind `button_glyphs`.
3. Confirm/cancel: find the menu handlers that read the confirm button (the XInput state read and the
   menu code that tests the A bit); swap A/B only there, behind `menu_confirm`. If no clean per-menu hook
   exists, a global A/B swap behind the option, with a plain note that the gameplay buttons move too.
4. Verify in Eden (private copy `E:\Switch\Eden-hcu\`) with scripted presses (`pad.txt`) and screenshots
   of the save dialog and the character select.

## Technique proven in Guardian Heroes (gh-recomp, 2026-10-23, verified in Eden)

gh-recomp `src/gpu/deko3d_backend.cpp` (`PatchGlyphs`, `SwapAtlasGlyphs`, `ReplaceTitleGlyph`, `DumpTexture`)
and its CLAUDE.md "button glyphs": dump every distinct texture as a PPM (marker `dk_dump_tex`), find the
icon cells, swap equal-sized cells in the staged (untiled) data in `UploadTexture` before the GPU copy,
guarded by format, size and the hue of each cell (A about 70 degrees, B 0, X 216, Y 37); icons baked into
artwork get a drawn replacement; cvar `button_glyphs`. HCU needs its own dump pass for addresses and cells.
