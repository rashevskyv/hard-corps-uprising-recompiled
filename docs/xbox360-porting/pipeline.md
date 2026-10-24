# Pipeline: XEX to generated C++ (ReXGlue and XenonRecomp)

Evidence tags: [CODE] I read the source. [DOC] README or doc only. [GUESS] my inference.

## 1. Two toolchains

| | XenonRecomp (hedge-dev, MIT) | ReXGlue SDK (rexglue/rexglue-sdk, BSD-3) |
|---|---|---|
| What you get | Recompiler only. No runtime. | Codegen plus a Xenia-derived runtime (kernel, XAM, Xenos GPU, XMA, input) |
| Used by | UnleashedRecomp, Rayman Origins (codegen part), CoD3, DR2 Case Zero/West, Skate 3, L4D | stuntdouble, Nocturne (SotN), hells-gate, AC6, Xerenge, CVHD, ~25 titles of CrownPark |
| Output | `PPCContext& ctx, uint8_t* base` functions | Same shape, `DEFINE_REX_FUNC`, `ctx, base` |
| Config | `config.toml` (+ switch tables TOML from XenonAnalyse) | `<name>_manifest.toml` (`rexglue init`, `rexglue codegen`) |
| Decrypts XEX itself | needs XexTool first (or its own xex.cpp, [CODE] `XenonUtils/xex.cpp`) | yes, XEX + STFS + XEXP since v0.8 [DOC] |
| Compiler | Clang 18+ | Clang 18+ enforced by CMake; we use GCC (devkitA64) via SDK edits [CVHD] |

Choose ReXGlue unless you want to own the whole runtime. XenonRecomp is the reference for the
instruction semantics; ReXGlue's codegen is derived from it.

## 2. Steps (ReXGlue)

1. Extract: `rexglue extract` or your own STFS reader. XBLA games ship as an STFS `LIVE`/`CON`/`PIRS` file
   (Free60 layout; hash table every 0xAA blocks; contiguous files are common). CVHD has `tools/stfs_extract.py`;
   CrownPark has `stfs_extract.py` (CON/LIVE/PIRS) in `xbox360-native-ports/tools`.
2. XEX facts: retail key `20B185A59D28FDC340583FBB0896BF91`, AES-128 key at `security_offset+0x150`,
   compression BASIC (zero-run blocks) or LZX. SotN XBLA is BASIC. No signature check on the base image;
   RSA check only for XEXP patches. [DOC NocturneRecomp/docs/reverse-engineering.md]
3. `rexglue init --project-name X --xex-path game/default.xex`; fix `game_root` back to `game/` if a symlink was resolved.
4. `rexglue codegen X_manifest.toml`. Output: `generated/*.cpp`, `sources.cmake`, `X_pch.h`.
5. Fix `UnresolvedCall` and `Validate` errors with manifest entries (below). Rerun. Typical: 1-5 entries to first build.
6. Build. Run. Fix "Call to invalid or unregistered function" by adding addresses; use data/code pointer
   scans (below) to remove the whole class.

## 3. Manifest keys (ReXGlue v0.10.0) [CODE `src/codegen/config.cpp`]

`[project]`, `[entrypoint] file_path out_directory_path includes`, `[entrypoint.functions.0xADDR]`
(`name`, `end`, `size`, `noreturn` with stuntdouble's patch), `[[entrypoint.midasm_hook]]`
(`address name registers=[..] return return_on_true return_on_false jump_address jump_address_on_true jump_address_on_false`), `switch_tables`, `invalid_instructions`,
`indirect_calls`, `[analysis] max_jump_extension data_region_threshold large_function_threshold exception_handler_funcs` (the 1.3 MB "function" warning in CVHD is what `large_function_threshold` is for), `setjmp_address`, `longjmp_address`, `generate_exception_handlers`, `is_dll`,
`[rexcrt]` (CRT overrides, see performance.md), and the eight codegen flags
`skip_lr skip_msr ctr_as_local xer_as_local reserved_as_local cr_as_local non_argument_as_local non_volatile_as_local`.

## 4. Hard problems and their fixes

| Problem | Fix | Source |
|---|---|---|
| Function split wrong (shared tail, second entry into a body) | `[entrypoint.functions.0xA] end = 0xB` or a declared name; CVHD: 0x826444A8, 0x8277ECF0 | CVHD |
| Jump tables not found (stock analyzer found 0 of 173 in Rayman Origins) | custom switch-block simulator `tools/jumptables`, or write `switch_tables` by hand | RaymanOrigins [DOC] |
| Jump table function looks like tail call, mis-split | explicit `functions = [{address,size}]` (Unleashed lists ~60) | XenonRecomp README, SWA.toml |
| Indirect calls into unknown code ("unregistered function") | `dataPointerScan` (+21k functions in CVHD, +2.9k in Stuntman) and `codePointerScan` (lis+addi/ori), both from stuntdouble's SDK patches | stuntdouble NOTES, CVHD |
| `_CxxThrowException` never returns, discovery runs into EH data | `noreturn = true` on it (801 call sites in Stuntman) | stuntdouble patch 0001 |
| setjmp/longjmp | declare both addresses FIRST; else longjmp = silent return, crash far away | XenonRecomp README, guide |
| Save/restore helpers `__savegprlr_14` etc | XenonRecomp wants 8 addresses (byte patterns in its README); ReXGlue has `src/codegen/sig_scanner.cpp` for them (name only, not read in detail) | XenonRecomp README |
| MSVC exceptions / SEH funclets | `generate_exception_handlers`; funclets share registers via ctx (`sharesRegisters()`) | rexglue [CODE] |
| Many unresolved warnings emitted as `REX_FATAL` | harmless until reached; use `unregistered_function_nonfatal` in CrownPark's fork to batch-collect | fork_changes.md |
| Two modules aliasing 0x80000000/0x90000000 | snapshot each module's view right after load (CrownPark codegen fix) | fork_changes.md |
| Title update (XEXP) | codegen reads the base XEX only in stock ReXGlue 0.10; Nocturne feeds `default.xexp` as codegen input (build.py `--tu`, flips the stamp). CVHD plan: patch offline | Nocturne build.py, CVHD |
| Missing opcodes (VMX128 integer ops, lmw, fri*, popcntb...) | CrownPark fork filled ~30; validated against real PPC via qemu-user `tools/ppc_oracle` | fork_changes.md |
| `vpkuhus` alias bug (dest==src) | build result in a temp, assign once | fork_changes.md |
| `bctr` outside recovered table emitted `__builtin_trap` | fall through to a branch on CTR | fork_changes.md |
| FP exceptions unmasked on host thread -> SIGFPE/0xC000008F | keep host masks in `mtfsf` and flush helpers (stuntdouble patch 0002) | stuntdouble |

## 5. Instruction model [CODE `XenonRecomp/README.md`, `pch_h.inja`]

- Loads/stores: `bswap(*(volatile T*)(base + addr))`. `volatile` blocks reordering and merging.
- Vector registers are stored byte-reversed as whole 16-byte values (not per lane). VMX uses SIMDe on ARM64.
- FPU keeps denormals, VMX flushes them: the context stores the mode and flips MXCSR/FPCR before each instruction.
- MMIO: ReXGlue detects `lis` of the 0x7F.. upper half at codegen time and emits `REX_MM_LOAD/STORE` that call the
  MMIO handler. No page faults. XenonRecomp has only an unfinished attempt.
- Indirect calls: perfect-hash table (guest address * 2 -> host function) placed after the XEX image.
- Functions are weak: override with `PPC_FUNC_IMPL(__imp__sub_X)` + `PPC_FUNC(sub_X)`. ReXGlue: `REX_NATIVE_HOOK(__imp__KernelExport)`.
- Mid-asm hook: call inserted at an instruction address; receives chosen registers by reference; `return`/`return_on_true` ends the function.
- `mftb` becomes `REX_QUERY_TIMEBASE()` (a scaled clock call, takes a mutex try_lock; see performance.md).
- `db16cyc`, `eieio`, `cctpl`, `dcbt`, `icbi` are no-ops in ReXGlue 0.10.

## 6. Tools

| Need | Tool |
|---|---|
| Decrypt XEX | XexTool 6.3 (`xextool -c u -e u`), XexTool-RE, or ReXGlue itself |
| Jump tables, boundaries | XenonAnalyse, `rexglue codegen` scans |
| Disassembly | Ghidra (PowerPC BE) or IDA with xextool IDC; the REA MCP tools here also open PE/XEX-dumped images [GUESS] |
| Instruction ground truth | Xenia canary source; CrownPark `tools/ppc_oracle` (qemu-user) |
| Find addresses in generated code | stuntdouble `tools/find_code_constants.py`; `// ` comments in the generated .cpp are the PPC instructions |
| Crash to function | `addr2line` / `llvm-symbolizer` on the ELF gives `sub_XXXXXXXX` and the line in `generated/*.cpp` |
