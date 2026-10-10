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

### Batch discovery (`tools/find-missing-functions.py`)

Fixing one address per runtime test was replaced by one static batch after the 13th blocker (`0x82915370`).

Root cause in ReXGlue v0.10.0 (`rexglue-sdk/src/codegen`): `phase_scan.cpp` cuts code regions at every null word; `phase_gapfill.cpp` then splits a region only on `blr` or a `b` to a known function and drops any segment whose start lies inside a registered function. A function that directly follows a `bctr` (or an in-function `b`) is therefore never a segment start. The SDK's own `vtable_scanner.cpp` only finds vtables through MSVC RTTI complete object locators; this image has five `.?AV` strings, so the game was built without RTTI and the vtable pass finds nothing. The lis/addi `functionPointerScan` is disabled (`analyze.cpp`). Hand-listed `[functions]` entries are the only supported input.

Reference projects: Outpost Kaloki X ships `tools/FindMissingFuncs.cs` (data pointers and lis/addi pairs into `.text`, candidate = unregistered, not a `loc_` label, preceded by a terminator; end = next known start or first null word; 81 entries in a generated `missing_funcs.toml` listed in the manifest `includes`). Earthworm Jim HD has the Python port `find_missing_funcs.py` plus thunk, veneer and tail-call seeders using the same include mechanism. The Simpsons Arcade (XenonRecomp) `find_missing_vtable_funcs.py` uses pointer clusters of 2+ entries. Aegis Wing, GoldenEye, Nocturne and afterburner only hand-list `[functions]` entries found at runtime.

Hard Corps uses the Outpost Kaloki X / Earthworm Jim mechanism with stricter classification, since the SDK composes several include files deterministically (`config.cpp`, same key last wins):

- References: aligned pointers in `.rdata` and `.data` into `0x82400000`–`0x82BD3CEC` (import thunks excluded), and lis/addi pairs in code. C++ EH tables (`FuncInfo`, unwind/try/IP maps, C scope tables) are parsed and tagged `eh`; pointer runs that contain a generated `loc_` label are tagged `jumptable`.
- HIGH requires all of: previous word is `bctr`/`blr`/`b`; previous function (or previous HIGH candidate) ends exactly at the candidate; not inside any `.pdata` extent; every instruction decodes; the CFG from the entry only leaves through `blr`, `bctr` or branches to registered functions; no direct branch from outside enters the body; nothing but a trailing `blr`/`nop` between the walked end and the next boundary; at least one vtable, data-pointer or lis/addi reference; the referencing table is not loaded as a switch table (`lis/addi` + `lbzx/lwzx/add` + `bctr`) by the previous function. Anything weaker is MEDIUM, LOW or EH and is left alone.
- Calibration: all 13 hand-made overrides above re-classify as HIGH with identical ends when removed from the config.
- Output: `build/missing-functions-report.txt` (ignored) and, with `--apply-high`, `config/discovered-functions.toml` (committed, fully regenerated, second entry of the manifest `includes`). `--verify` checks registration, generation and overlap against `generated/default` after codegen.

Result (2026-10-10): 75,640 references to 16,634 code addresses, 730 unregistered. HIGH 122, MEDIUM 0, LOW 459, EH 149. All 122 HIGH entries applied, including `0x82915370`–`0x82915378` (this-adjusting thunk, vtable `0x82339614`, 4th in a chain of 8-byte thunks after `0x82915368`). Codegen rewrote 70 files: 122 new functions, 0 existing function bodies changed, 0 partition moves, 0 overlaps, 0 duplicate registrations, 0 warnings. Incremental build: 43 objects, 0 warnings, 0 errors.

Runtime skipping of unregistered calls was rejected: `ResolveIndirectFunction` in the prebuilt `rexruntime.dll` returns a fatal trap, so a "log and return" mode would need an SDK fork, and a skipped function may leave registers, condition codes or object state wrong and move the failure somewhere meaningless.

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

The following test reached about 19 s again and hit `0x82915370`, the 13th function of the same class. It is covered by the batch above.

## Remaining blockers

- Runtime test of the batch build: execution past the KONAMI logo, or the next fatal. A new unregistered address means a candidate the rules left MEDIUM/LOW/EH (check `build/missing-functions-report.txt`) or a shape the scanner does not model.
- `game:\builddate.bin` open fails (`0xC000000F`). The file isn't in the package, and the game continued past it.
