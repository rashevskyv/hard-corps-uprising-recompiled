# Cloud session progress (Hard Corps: Uprising, deko3d backend)

Plain notes. Each item says what is fixed, what Linux verified, and what needs an Eden check.
The Eden checks are in `docs/EDEN-CHECKLIST.md`. The Linux test host is in `docs/LINUX-TEST-HOST.md`.

## What the cloud box can do now

- The game runs on Linux with the SDK's Vulkan backend (reference), the trace backend and our deko3d backend.
- The deko3d backend runs the Switch code over OpenGL (`src/gpu/dk_gl`). Frames, textures and draws can be dumped.
- Stage 1 is reached by a key script. The deko3d picture on Linux now matches the Vulkan reference.
- Not verified here: the real deko3d driver, Eden, speed on the console, the Switch build itself.

## Fixed and verified on Linux

| fault seen in Eden | cause | state |
|---|---|---|
| enemy sprites as 80x80 squares, player as a red crescent | `VGT_INDX_OFFSET` was ignored. HCU draws every sprite quad from one vertex buffer, each with its own start vertex. All quads read the first four vertices. | fixed: the start vertex shifts the vertex data. Player, soldiers, mechs and dynamite draw whole on Linux. |
| dark grey stage, no cliffs or sky (after the sync) | the level pixel shader samples a depth texture (`k_24_8`, a shadow map). Depth resolves were ignored and draws with a missing texture are skipped. | fixed: a depth resolve copies the depth image into a depth image at the destination address. |
| shadows only near the camera | three shadow cascades are resolved to three addresses and sampled as one 2048x2048 texture | fixed: the texture takes the other depth images at their rows (`MergeDepthPieces`). |
| pause menu panel white and opaque | the UI clips its panels with the stencil buffer. The backend never enabled stencil. | fixed: stencil test, ops, reference and masks. The pause menu matches the reference. |
| post effects and blur draws missing | resolves of 2_10_10_10 targets were read back as `k_2_10_10_10_AS_16_16_16_16`, an unsupported format. Their draws were skipped. | fixed: resolves keep the target's host format. The bloom glow draws. |
| picture too bright and flat against the Vulkan reference | the game sets a non-linear display gamma ramp (128 maps to 462 of 1023). The SDK applies it at the swap, our present pass did not. | fixed: the present shader looks every channel up in the 256-entry ramp (`present.frag`, a uniform buffer filled at the swap). The sky and sand colours match the reference. |
| blur chain and bloom read back as black or noise at times | a resolve destination was re-uploaded from guest memory when the guest wrote it after the resolve. The guest runs ahead of the command processor. | fixed: a resolve destination bound in the swap that resolved it is never re-uploaded. |
| blend factors with a constant colour | `RB_BLEND_RED..ALPHA` was never set | fixed: set when a constant factor is used. HCU does not use one yet. |
| compare function off by one, depth clears in resolves | known from the earlier session | kept (the sync from cvhd-recomp had dropped them). |
| debug dumps and per-frame re-uploads | debug experiments 0.125, 0.126 | gone: the sync replaced the backend. Dumps need marker files or env variables now. |

## Probably fixed, needs an Eden look

- Colour strip at the left edge, flicker: not seen on Linux. New images start cleared now. Sprite quads with a wrong start vertex are fixed.
- Screen darkens over time, white fog sphere: not seen in 5 minutes of play on Linux. Targets start cleared.
- MISSION 1 card with scanlines: the hologram panels have scanlines by design (the Vulkan reference shows them). Compare with the reference picture in the checklist.
- Mid-ground parallax with hard steps: smooth on Linux.
- 8 to 9 fps with a font atlas dump: the dumps were the experiments. Delete stale marker files on the SD (see the checklist).

## Needs an Eden check (cannot be verified here)

- The display gamma ramp in the present pass (new uniform buffer in `present.frag`; `switch/build.sh` compiles it with `uam`).
- Depth copy between two `Z24S8` images with `dkCmdBufCopyImage`, and sampling a depth image as a texture.
- `dkCmdBufSetStencil` with the UI's stencil use.
- Raw image copies between two `RGB10A2` images.
- Speed. A stage frame has about 250 draws, 31 resolves, 10 clears and 3 MB of vertex data. Uploads are few.
- The Switch build after the sync (host layer and SDK edits from cvhd-recomp 7a52b68). Nothing was built for the Switch here.

## Shaders

- A key script run with the trace backend found 60 shaders (39 vertex, 21 pixel). `docs/shader-hashes-stage1.txt` lists their hashes.
- All translate with `tools/xenos2glsl.py` and pass `glslangValidator`. `uam` was not available here.
- The ucode dumps are game-derived and are not in git. The Eden loop (`switch/eden-deko3d.ps1`) collects missing ones.

## Shared with the sibling ports

`docs/for-cvhd-gh/README.md` lists the generic backend fixes. The patch applies to cvhd-recomp 7a52b68.

## Speed notes (Linux, x86, no GPU involved in these numbers)

- A stage frame has about 250 draws, 10 clears and 31 resolves. Vertex data is 3 MB. Uploads are 0 to 7 small tiles.
- Recording the draws takes 1.6 to 2.4 ms per frame on this box (the guest memory hash of the test host excluded).
  The Switch CPU is slower. Expect 3 to 5 times that. The 60-swap log line shows the real number.
- The 60-swap log has `per frame ... draws / fence wait / acquire / submit`. A long fence wait means a GPU limit.
- Cheaper draws were done: integer keys for logs, one binding per fetch constant, only the used float constants.

## Not done

- The piecewise linear gamma ramp for 2_10_10_10 front buffers (HCU uses the 256-entry table).
- Shaders for later stages and bosses. The key script reached the first stage and played into it only.
- MSAA 4x draws (`msaa 2`) are drawn at the doubled size and resolved from a different image. Not seen in stage 1 pictures.
- A real measurement of the console speed, and the dirty-page path of the Switch (Linux hashes memory instead).
