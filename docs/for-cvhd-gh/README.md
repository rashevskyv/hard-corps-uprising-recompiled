# Generic deko3d backend fixes from the Hard Corps port (for Castlevania HD and Guardian Heroes)

`backend-fixes-vs-cvhd-7a52b68.patch` applies to cvhd-recomp 7a52b68 (`git apply`). It covers
`src/gpu/deko3d_backend.cpp`, `src/gpu/dk_gl/dk_gl_shim.{cpp,h}` and `tools/xenos2glsl.py`.
Verified on the Linux test host with Hard Corps. The real deko3d driver is untested for items 3, 4 and 7.

1. **Start vertex.** `VGT_INDX_OFFSET` is added to every vertex index. Copy the vertex data from
   `fetch.address + offset * stride`. A 2D game that draws all sprite quads from one buffer needs it.
2. **Depth resolves.** `IsCopyingDepth()` copies the depth image into a `Z24S8` guest image at the destination address.
   A texture fetch of `k_24_8` / `k_24_8_FLOAT` samples it. The pixel shader may compare depth by hand (shadow maps).
   Draws with a missing texture are skipped now, so ignoring a depth resolve drops every draw that samples it.
3. **Shadow cascades.** Several resolves land in one texture at tile-row offsets. `MergeDepthPieces` copies them in.
4. **Stencil.** Test, ops, reference and masks from `RB_DEPTHCONTROL` and `RB_STENCILREFMASK(_BF)`.
   UI panels are clipped with it. Without it a panel draws as a full-screen quad.
5. **Resolve formats.** The destination image keeps the target's host format (RGB10A2, RGBA16F; sRGB becomes RGBA8).
   `k_2_10_10_10`, `k_2_10_10_10_AS_16_16_16_16` and `k_8_8_8_8_AS_16_16_16_16` textures map to it. The front buffer
   may have any format.
6. **Blend constants** from `RB_BLEND_RED..ALPHA` when a constant factor is used.
7. **Compare function** `+ 1` (Xenos kNever = 0, deko3d Never = 1) and depth clears in resolves (from the first patch).
8. Test host extras: `HCU_DK_TRACE_AT`, `HCU_DK_TRACE_DUMP`, ranges, per-draw dumps, GL shim `dkCmdBufSetStencil`,
   `dkCmdBufSetBlendConst`, depth copies.
9. `tools/xenos2glsl.py`: `maxas` / `maxasf` and `c[N+a0]` (address register), in the patch.
10. `tools/stfs_extract.py`: the volume descriptor offsets of the cvhd copy are off by one byte for this package
    (its loop never ended). The copy in this repo follows Free60: size, reserved, block separation, count (2), start (3).
11. Cheaper draw path (CPU, matters on the Switch): integer keys for the first-time logs (no `snprintf` per draw),
    one texture binding per fetch constant (a shader may read one constant nine times), only the float constants
    a shader reads are copied to the write-combined buffer (`constant_register_map`), depth pieces merged once per
    depth resolve.
12. **Display gamma ramp.** `gamma_ramp_256_entry_table()` (the SDK's command processor keeps what the game wrote to the
    DC_LUT registers, linear by default) is applied in the present pass: `src/gpu/shaders/present.frag` reads a 256-entry
    float table from a uniform buffer filled in `IssueSwap`. HCU's ramp is not linear. Check your game's ramp in the log line
    `display gamma ramp`. A 2_10_10_10 front buffer would use the PWL ramp (not done).
13. A resolve destination bound in the swap that resolved it is not re-uploaded from guest memory (`resolve_swap`).
14. Timing in the 60-swap log (`per frame ... ms draws`, fence wait, acquire, submit) and the `dk_reupload` / `dk_nodepthcopy`
    debug markers.
