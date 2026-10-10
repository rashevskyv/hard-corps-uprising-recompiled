# ReXGlue Feasibility

## Toolchain

| Item | Value |
|---|---|
| ReXGlue | v0.10.0 (`rexglue --version`), the version pinned by `outpost-kaloki-x-recompiled` v1.1.0 and `earthworm-jim-hd-recompiled` v0.9.5 |
| Source | Official GitHub release asset `rexglue-sdk-0.10.0-win-amd64.zip` (44,751,910 bytes, SHA-256 `AD4485DA853FBE932E9F5DBB643F9B5FE85DC73775EBBAD4DF6879FE6DF0C11B`, matches the release digest), cached in the Git-ignored `tools/rexglue/` |
| Runtime DLL | Official `rexruntime.dll` (SHA-256 `E359209F…1B7A`). Windows Defender raised no detection during download, extraction or codegen. |
| Input | `game/extracted/default.xex` (raw XEX2). Codegen decrypts and decompresses it with the SDK's own XEX loader. `game/analysis/default.image.bin` was only used to verify the output. |
| Input SHA-256 | `530EDE8ED5271051A2AB19C077A97FB167E4F30B094C292E8F59D37D0D2069DC` (checked before every run) |

Values reported by codegen: title `58410A18`, media `3C5663CB`, version 0.0.0.3, image base `0x82000000`, image size `0xD00000`, code `0x82400000` + `0x7D49CC`, entry point `0x82571AD0` (`xstart`).

## Code generation

Run: `tools/codegen.ps1` (`rexglue codegen hard_corps_uprising_manifest.toml`). Full generation takes 16.8 s.

| Result | Value |
|---|---|
| Functions recompiled | 28,511 (plus 206 import thunks provided by the SDK: 28,717 mappings) |
| `.pdata` functions | 19,353 of 19,353 are generated entry points |
| Function hints | 1 (`config/overrides.toml`) |
| Jump tables | 477 found, 477 emitted as `switch`. 372 absolute tables match the image entry for entry. The 105 compact (byte/halfword offset) tables match their `cmplwi` bound where one exists, and none was truncated. |
| Unresolved jump/switch tables | 0. The other 31 indexed `bctr` sites are function-pointer or vtable tail calls, not switches. |
| Veneers | 0 long-branch veneers (Earthworm Jim HD has 117,430) |
| Thunks | 238 this-adjusting thunks and 45 vtable-call thunks, all emitted as functions |
| Unsupported instructions / decode failures | 0 |
| Analysis errors | 2 before the hint (see below), 0 after |
| Analysis warnings | 0 (no `[functions]` hint was outranked or split) |
| Indirect call sites | 23,767 `REX_CALL_INDIRECT_FUNC`, resolved at runtime |
| Overlaps | 716 instructions emitted by more than one function. They are all multi-entry fall-through chains: 228 groups in the register save/restore helpers and 32 shared tails in XDK/CRT code (317 instructions). Duplicating them is correct. |
| Output | `generated/default/`: 271 C/C++ files (135 `.cpp`, 136 `.h`), 4.93 M lines, 144,273,424 bytes, plus `sources.cmake` and stamp files. `generated/rexglue.cmake` is also written. |

Hint evidence: the first run reported `Unresolved conditional branch to 0x825DA8E8` from `0x825DA914` and `0x825DA938` and emitted `REX_FATAL` at both sites. Analysis had started the function at `0x825DA8F0`. The real entry is `0x825DA8E0` (`cmplwi r4,0; bne; li r3,0; blr`), which comes right after the 16-byte vcall thunk `0x825DA8D0`, is referenced only by vtable slot `0x8201C6E8`, and ends at `0x825DA984`, just before the `.pdata` function at `0x825DA988`. `0x825DA8F0` is reached only by the in-function `bne` at `0x825DA8E4`. Outpost Kaloki X needed the same `{ end = ... }` fix for its comparator `0x82073660`.

## Required configuration

| File | Purpose |
|---|---|
| `hard_corps_uprising_manifest.toml` | ReXGlue manifest (SDK 0.10.0, `game_root = "game/extracted"`, output `generated/default`). It sits at the repository root because codegen writes `generated/rexglue.cmake` next to the manifest. |
| `config/overrides.toml` | The single function-boundary hint above |
| `config/hard_corps_uprising.toml` | Runtime cvars: `license_mask = 1`. The SDK's `XamContentGetLicenseMask` returns this cvar (default 0, trial). The app reads `<exe dir>/hard_corps_uprising.toml`, so the build phase must copy it there. Codegen does not use it. |
| `tools/codegen.ps1` | Downloads and verifies the pinned SDK, checks the `default.xex` SHA-256, runs codegen, and logs to `generated/codegen.log` |
| `.gitignore` | Adds `/tools/rexglue/` |

No `setjmp`/`longjmp`, switch-table, veneer, thunk or mid-asm configuration was needed. All other codegen options are left at their SDK defaults, the same as Outpost Kaloki X.

## Problems found

None of these block codegen. They matter at runtime:

- **Indirect-only code not emitted as functions**: 278 data words point to 117 code ranges with no generated function. 85 are referenced from `.rdata` vtables and pointer tables, and 3 from `.data`. If those are ever called through `REX_CALL_INDIRECT_FUNC`, the runtime will fail with "no function registered". Outpost Kaloki X hit the same issue (81 `missing_funcs.toml` entries) and used a scanner plus pruning. Each candidate needs that per-case check, so no speculative hints were added.
- **SEH handlers not generated**: 29 of those ranges are `__try/__except` filter and handler code referenced from the scope tables at `0x823CE0A0`–`0x823CE4D8`. `generate_exception_handlers` is off by default, as in all the references. A guest exception that reaches one of these scopes will not be handled.
- **Other unemitted `.text`**: 450 inline jump tables (data), 98 unreferenced single `blr`s and 14 unreferenced code fragments.
- **Data pointing mid-function**: 228 `.rdata`/`.data` words point inside generated functions and are not switch labels. Many look like coincidental values (for example `0x82811400`). Not investigated.
- **Runtime log noise**: `Achievement store: refusing achievement with ID 0`, logged while the tool-mode runtime loads the title resource.
- **Known SDK v0.10.0 runtime issues** (from Outpost Kaloki X): the FPSCR/MXCSR initialization bug in host-thread callbacks, and the antivirus false positive on `rexruntime.dll` (rexglue/rexglue-sdk#485; not triggered here).

## Verdict

**READY FOR MINIMAL BUILD**

ReXGlue v0.10.0 processes the unmodified `default.xex` with no unsupported instructions, no decode failures and no unresolved branch or jump-table targets. It needed one boundary hint, which the binary's control flow and vtable data confirm. The output agrees with every `.pdata` record, and every jump table checked against the image matches. The remaining gaps are indirect-only functions and SEH handlers, which show up at runtime, not at compile or link time. Handle them when they are hit during first boot, as Outpost Kaloki X did.
