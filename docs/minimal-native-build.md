# Minimal Native Build

## Environment

| Item | Value |
|---|---|
| OS | Windows 11 Pro 10.0.26200 (x64) |
| Compiler | `clang++` 22.1.3 bundled with Visual Studio Community 2026 (18.10.1), target `x86_64-pc-windows-msvc`, GNU-style driver |
| Linker | `lld-link` (Visual Studio LLVM) |
| C++ runtime / SDK | MSVC toolset 14.51.36231 (STL, CRT), Windows SDK 10.0.26100.0 |
| CMake / generator | CMake 4.3.1-msvc1 and Ninja 1.13.2, both bundled with Visual Studio |
| Configuration | Single-config Ninja, `RelWithDebInfo`, C++23 |
| ReXGlue | v0.10.0, `tools/rexglue/win-amd64` (official zip, SHA-256 rechecked) |

The compiler has to be Clang. The generated code uses `__attribute__((alias))`, GNU statement expressions and `__builtin_bswap*`, which MSVC `cl.exe` rejects. All five references (Outpost Kaloki X, Earthworm Jim HD, Aegis Wing, Nocturne, Simpsons Arcade) build with Ninja + `clang++`. Outpost Kaloki X and Earthworm Jim HD also run on v0.10.0 and use the same generated `rexglue.cmake` flow as this project.

## Build structure

| File | Role |
|---|---|
| `CMakeLists.txt` | `project(hard_corps_uprising)`, C++23. Adds `tools/rexglue/win-amd64` to `CMAKE_PREFIX_PATH`, includes `generated/rexglue.cmake`, declares the `WIN32` executable, calls `rexglue_setup_target()` and copies the runtime config next to the exe. This is the SDK `rexglue init` template plus the config copy. |
| `generated/rexglue.cmake` (ignored, from codegen) | `find_package(rexglue 0.10.0)`. Compiles the 135 generated `.cpp` files as the OBJECT library `hard_corps_uprising_recomp` (PCH `hard_corps_uprising_pch.h`, `-fasync-exceptions`, `-gline-tables-only`) and links `rex::runtime`. Adds the SDK's `windowed_app_main_sdl.cpp` (`wWinMain`) and `rex_app.cpp` to the exe and stages `TARGET_RUNTIME_DLLS` after linking. Codegen runs as a build step, driven by a depfile. |
| `src/main.cpp` | `REX_DEFINE_APP(hard_corps_uprising, HardCorpsUprisingApp::Create)` |
| `src/hard_corps_uprising_app.h` | `rex::ReXApp` subclass with no overrides. The app name `hard_corps_uprising` sets the config file name (`<exe dir>/hard_corps_uprising.toml`) and the default user-data folder. |
| `.gitattributes` | Checks the manifest out with LF, the line endings codegen writes back |
| `tools/build.ps1` | Loads the VS x64 environment, puts VS's CMake, Ninja and clang ahead of the msys64 copies on `PATH`, configures `build/` and builds. Restores the caller's environment afterwards. |

Output directory `build/`:

| File | Source |
|---|---|
| `hard_corps_uprising.exe` | link output |
| `hard_corps_uprising.toml` | copied from `config/hard_corps_uprising.toml` (`license_mask = 1`) |
| `rexruntimerd.dll` | SDK `RelWithDebInfo` runtime, SHA-256 `02C4CC9F…9D02` (byte-identical to `tools/rexglue`) |
| `TracyClientrd.dll` | SDK profiler client, linked by the runtime in non-Release configs, SHA-256 `6E5AFCA2…7FE8` |

A `Release` build stages `rexruntime.dll` (the file hashed in Phase 2) and no Tracy DLL.

Game content is not copied. The runtime has no default game path, so the game is started with `--game_data_root=<repo>\game\extracted`.

Commands:

```
tools\codegen.ps1      # only if generated\ is missing or its inputs changed
tools\build.ps1        # [-Config RelWithDebInfo|Release|Debug] [-Jobs N]
build\hard_corps_uprising.exe --game_data_root=<repo>\game\extracted
```

## Build result

| Step | Result |
|---|---|
| Configure | Succeeded on the first attempt (4.7 s) |
| Compile | 135 generated TUs, the PCH, `main.cpp` and the 2 SDK host sources. 0 warnings, 0 errors. A full build with `-Jobs 8` on an i5-12400 (6C/12T) took about 2 minutes. |
| Link | Succeeded, 0 warnings |
| Executable | `build\hard_corps_uprising.exe`, 48,901,632 bytes, x64 GUI subsystem (PDB 52,482,048 bytes) |
| Imports | `rexruntimerd.dll` plus Windows/VC++ runtime DLLs (`KERNEL32`, `ole32`, `SHELL32`, `MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`, UCRT API sets) |
| Incremental no-op build | Nothing is recompiled or relinked |

The first two in-build codegen runs re-analysed the XEX and wrote 0 files each, so the generated C++ is unchanged. Both were caused by line-ending changes in fingerprint inputs. Git (`core.autocrlf=true`) had checked out the manifest and `overrides.toml` as CRLF, and `rexglue codegen` rewrites the manifest as LF after every run (`WriteSdkVersionStamp`). Later runs report `1 module(s) up to date` in 0.1 s. Ninja still runs that step on every build, because unchanged generated files keep their older timestamps.

## Required fixes

Compile and link: none. Configure, compile and link all succeeded on the first attempt with the SDK's template host. No stubs, warning suppressions, generated-code edits, manifest/override changes or extra libraries were needed.

Build hygiene: one. Because codegen rewrote the CRLF-checked-out manifest as LF, Git reported it as modified after every build. The content was identical, but the cached size differed. `.gitattributes` now pins `hard_corps_uprising_manifest.toml` to `eol=lf`, so a build leaves `git status` clean.

## Minimal launch

- Launched: yes, once, with `--game_data_root=<repo>\game\extracted --user_data_root=<repo>\build\user_data`. No GPU plugin was configured (`gpu_plugin` is empty by default).
- Observable result: the runtime mapped the guest memory arena, initialised the function table (`code=82400000-82BD49CC`, `image=82000000-82D00000`) and SDL input, loaded `default.xex` and started the guest thread. The process then exited with `0xC0000409` (fast fail).
- First runtime failure, on a guest thread about 0.3 s after the first log line:
  `[FATAL] Call to invalid or unregistered function at guest address 0x825C7ED0`

Not investigated in this phase.

## Verdict

**READY FOR FIRST BOOT**

The generated code compiles and links into a native Windows executable with no warnings and no compile or link fixes. The executable loads the XEX and runs guest code until the first unregistered indirect call target. That is the class of gap Phase 2 listed (code reached only indirectly that was not emitted as a function). It needs runtime work, not build work.
