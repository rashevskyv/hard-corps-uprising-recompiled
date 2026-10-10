# First Boot

## Starting point

Initial fatal, about 0.3 s after the guest thread starts:

`[FATAL] Call to invalid or unregistered function at guest address 0x825C7ED0` (process exits with `0xC0000409`).

## Runtime fixes

### Indirect-only functions (`config/overrides.toml`)

Cause: a ReXGlue v0.10.0 analysis gap, not memory corruption. `DecodedBinary::computeCodeRegions` only ends a code region at two or more consecutive null words, and GapFill (`phase_gapfill.cpp`) splits a region only on `blr` or a `b` to a known function. A small function placed straight after another function that ends in `bctr`, or in an in-function `b`, therefore lands in a segment that starts inside the previous function. GapFill drops that segment. Functions referenced only from a vtable or callback pointer are never registered, so the first indirect call to one is fatal.

Fix: one `[functions]` entry for each address the runtime actually hit, added in the order hit:

| Address | End | Reference | Shape |
|---|---|---|---|
| `0x825C7ED0` | `0x825C7EEC` | vtable `0x8201C56C` | vcall thunk after the `bctr` of `sub_825C7EC0` |
| `0x825CBC28` | `0x825CBC50` | vtables `0x82015798`, `0x820158A0` | null-checked vcall tail after a `bctr` |
| `0x825C26F0` | `0x825C26FC` | vtable `0x8201D940` | 3-instruction setter after a `bctr` |
| `0x825F8350` | `0x825F835C` | vtable `0x8201ABF0` | getter right after the `.pdata` function `0x825F8168`–`0x825F8350` (ends in `b`) |
| `0x825DEAE0` | `0x825DEB0C` | `lis/addi` at `0x82634478` (callback argument) | leaf after a `bctr` |
| `0x82585980` | `0x8258598C` | vtable `0x8200CE8C` | setter after a `bctr` |
| `0x8257DE40` | `0x8257DE4C` | vtable `0x82012340` | setter + tail call after a `b` |
| `0x8257DE38` | `0x8257DE40` | vtable `0x8201233C` | tail-call thunk after a `b` |
| `0x826ED8D8` | `0x826ED8E8` | `0x8203B5D0`, `0x8203B5D4`, `0x8203B5D8` | argument-shuffle tail call after a `b` |
| `0x825251A0` | `0x825251BC` | `0x823CCBB8` | vcall thunk after a `bctr` |
| `0x82521F28` | `0x82521F78` | same slot of vtables `0x823CCA3C`, `0x823CCB7C`, `0x823CD8DC` | guarded vcall tail (`bctr` at `0x82521F70`) after the `bctr` of `sub_82521EB8`; the trailing `blr` at `0x82521F74` is unreferenced padding |
| `0x8291BE58` | `0x8291BE70` | vtable `0x82339BBC` (`.rdata`), next to `sub_8291BE40` in slot `0x82339BB8` | 6-instruction vcall thunk (`lwz 72(r3)`, `+64`, slot 20, `bctr`) after the `bctr` of `sub_8291BE40`; ends at the `.pdata` start of `sub_8291BE70` |

Evidence required for each entry:

- The previous word is a terminator (`bctr`, `b` or `blr`).
- The range ends in a terminator, followed by a zero pad word or the next generated function.
- Every outgoing branch goes to a generated function.
- There are no undecodable words, and no `bctr` other than the last instruction.
- The address is not a `loc_` label of a generated function.
- A data pointer or a `lis/addi` pair references the address.

After codegen, every entry is registered (`hard_corps_uprising_register.cpp`), no other function starts inside any entry's range, and the neighbouring functions are unchanged.

Not registered speculatively: 83 data-referenced addresses directly follow a `bctr` (for example `0x825C8030`, in the same vtable as `0x825C7ED0`). Add them only when the runtime reaches them.

## GPU setup

| Item | Value |
|---|---|
| Plugin | ReXGlue Xenos: `rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)` in `CMakeLists.txt`, as in Outpost Kaloki X, Earthworm Jim HD, Aegis Wing and GoldenEye |
| Config | `gpu_plugin = "xenos"` in `config/hard_corps_uprising.toml` (copied to `build/`). The runtime loads `rexgpu-xenos<suffix>.dll` from the exe directory. A load failure is fatal (no headless fallback). |
| Required DLLs (RelWithDebInfo) | `rexruntimerd.dll`, `rexgpu-xenosrd.dll`, `TracyClientrd.dll`. Release uses `rexruntime.dll` and `rexgpu-xenos.dll`. |
| Staging | The SDK's `rexglue_configure_target` copies the plugin next to the exe after linking (the plugin is loaded at runtime, so `TARGET_RUNTIME_DLLS` misses it). `tools/build.ps1` checks that all of these files exist. |
| Shaders | Translated by the Xenos plugin at runtime. There is no build step. |

Before Xenos, the guest ran past the boot fixes. It stayed alive with an SDL window titled `hard_corps_uprising [rexglue-v0.10.0-RelWithDebInfo]`, initialized audio, and logged `VdInitializeRingBuffer: no GPU emulation loaded`.

First Xenos run: the RTX 4070 was selected, D3D12 initialized, and shaders and pipelines were created from storage. The window was black and fullscreen, then the guest hit the unregistered `0x82521F28` (fixed above).

## Development window

ReXGlue's `fullscreen` cvar defaults to `true` (borderless desktop fullscreen). That was the cause of the fullscreen start. The window settings use only the official v0.10.0 cvars in `config/hard_corps_uprising.toml`:

| Setting | Effect |
|---|---|
| `fullscreen = false` | normal resizable window |
| `window_width = 853`, `window_height = 480` | logical size, so Windows scaling applies. At ~337% on the 3840×2160 display this is about 2880×1620 physical. ReXApp's 1280×720 logical default would be larger than the screen. |
| `monitor = 1` | `SDL_WINDOWPOS_CENTERED_DISPLAY` on the first (primary) display |
| `video_mode_width = 1280`, `video_mode_height = 720` | pins the guest video mode, which otherwise follows `window_width/height` once they are set |

Known v0.10.0 risk (Outpost Kaloki X, Earthworm Jim HD): guest callbacks that run on host threads can unmask host FP exceptions (`0xC000008F`). The graphics interrupt callback `0x82556300` has no direct `enableFlushMode` in its static call tree, but it makes indirect calls. No guard has been added.

## Result

Highest confirmed milestone: **FIRST VISIBLE FRAME**. With the `0x82521F28` fix and the windowed config, the game opened a centered 853×480 window and Xenos rendered the KONAMI logo. About 19 s after start, the guest hit the unregistered `0x8291BE58` (fixed above).

`0x8291BE58` has no `.pdata` entry. Its sibling `sub_8291BE40` was found only because the preceding `sub_8291BD70` ends in a `b` to `__restgprlr`, which GapFill splits on. `0x8291BE58` follows a `bctr`, which GapFill does not split on. The fix added one `SetFunction`. Codegen rewrote 4 sources (`init`, `register`, `funcs.h`, `recomp.124`) and changed no other function.

## Remaining blockers

- Runtime test of this build: execution past `0x8291BE58`, or the next fatal.
- `game:\builddate.bin` open fails (`0xC000000F`). The file isn't in the package, and the game continued past it.
