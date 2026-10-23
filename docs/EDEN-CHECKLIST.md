# Eden checklist for the local session

Run each item with `switch/eden-deko3d.ps1 -Seconds 300 -Every 20` and relay screenshots and logs.
Plain notes: what to build, what to look at, and what to send back.

## 0. Before the run

1. `git pull` (branch main). The shared files came from cvhd-recomp 7a52b68 (`switch/CVHD_COMMIT`).
   The Switch build has **not** been compiled in the cloud. If `switch/build.sh` fails, send the first error.
2. The SDK edits changed. `switch/build.sh` resets the SDK by itself. Then run the codegen again with the host CLI
   the SDK build made: `rexglue codegen hard_corps_uprising_manifest.toml --ignore-stamp` (templates changed).
3. Delete stale debug marker files on the SD: `sdmc:/switch/hcu/dk_dump`, `dk_present_rt`, `dk_texdump`,
   `dk_reupload` (unless you test with it), `dk_nodepth`, `dk_nodepthcopy` and the `texdump/` folder. They cost speed and draw dumps on screen.
   The mirrored font atlas and the 8 to 9 fps came from such files.
4. Shaders: the stage needs 60 shaders. The old dump directory has about 45. Play stage 1 once, let the script
   copy the missing ones back, run `switch/build.sh`, run again. `docs/shader-hashes-stage1.txt` lists the known hashes.
5. Keys, stage 1 (game seconds): `START` at 60, `A` at 100 and so on as before. Do not press `START` in the stage.
   It opens the pause menu (item 5). Use `RIGHT:6000` (hold), `X` (fire), `A` (jump) to move.

## 1. Enemy sprites and the player (the main fix)

What changed: the start vertex (`VGT_INDX_OFFSET`) is now applied to vertex data.
Look at: red-hooded runners with rifles, jetpack mechs with a red pod, dynamite sticks, the player with a red scarf.
All must be whole sprites with transparent backgrounds. No 80x80 squares, no red crescent only.
Reference pictures: `docs/images/linux-test-host/hcu-linux-reference-stage.jpg` (SDK Vulkan) and `hcu-linux-deko3d-stage.jpg` (our backend on Linux).
If squares remain: send the `[dk] vertex data clamped` lines and a screenshot.

If tiles or sprite parts are stale (wrong frame, flicker) while the picture is otherwise right: create the empty file
`sdmc:/switch/hcu/dk_reupload` and run again. Textures then upload at every bind. If that fixes it, the guest wrote
texture memory without marking the page (a missing dirty mark in the store macros or a host write).

## 2. Shadows

What changed: depth resolves are copied into depth images, and three shadow cascades are joined into one texture.
Look at: dark shadow shapes under rocks, the log and the characters. They must not vanish at the far end of the view.
Log lines to send: `[dk] depth resolve EDRAM 0 pitch 2080 ...` (three lines) and `depth image at ... copied into the texture`.
If the stage is dark grey, or there is garbage: create the empty file `sdmc:/switch/hcu/dk_nodepthcopy` and run again.
Without it the depth copy is skipped (no shadows). Send both results. This marks a problem in `dkCmdBufCopyImage` with `Z24S8`.

## 3. Bloom and post effects

What changed: resolves keep the host format of their target (RGB10A2). Their texture fetches are accepted now.
Look at: a soft glow around the crashed ship and bright rocks. Log: no `unsupported texture format`.

## 3b. Colours (display gamma ramp)

What changed: the present pass applies the game's display gamma ramp (the log line `display gamma ramp` shows it: 128 -> 462).
Look at: deep blue sky, golden sand with dark shadows, not washed out. Compare `hcu-linux-deko3d-stage.jpg` (ours) and
`hcu-linux-reference-stage.jpg` (SDK Vulkan). The present shaders are rebuilt by `switch/build.sh` (`present.frag` has a uniform buffer now).

## 4. Pause menu (press START once in the stage)

Expected: a translucent dark green panel with scanlines, the scene visible through it, the score table, text rows.
Before the fix it was white and opaque. References: `docs/images/linux-test-host/hcu-linux-reference-pause.jpg` (Vulkan) and `hcu-linux-deko3d-pause.jpg` (ours).
Log line: `[dk] stencil on: func 2 fail 3 ...` (once) and a second `stencil on` line.

## 5. Edge strip, darkening, fog (watch for 3 minutes)

Look at the left screen edge (a coloured strip, flicker) and at the whole picture over time (darker, white sphere).
Neither was seen on Linux in 5 minutes. If it appears, send the screenshot, its time, and the `[dk] swap #N` line nearest.

## 6. Speed

Send the `[nx] fps` lines (stage, 60 s) and the `[dk] swap #N: ...` lines (two per 60 swaps). The second one is new:
`per frame X ms draws, ... fence wait, ... acquire, ... submit`. A long fence wait means the GPU is the limit. A long
draws time means the command processor thread is. On Linux (x86, no GPU) the draws take 1.6 to 2.4 ms per frame. A stage frame on Linux is about
250 draws, 10 clears, 31 resolves, 0 to 7 texture uploads and 3 MB of vertex data. Eden ran 30 fps at 0.122.
The MISSION 1 card with scanlines: the hologram panels have them by design. Compare with the reference.

## 7. Facts for the local assistant

- Everything under `scripts/` and `docs/LINUX-TEST-HOST.md` is for the Linux box. The Eden scripts are unchanged.
- The generic fixes are listed in `docs/for-cvhd-gh/README.md` for the Castlevania and Guardian Heroes ports.
