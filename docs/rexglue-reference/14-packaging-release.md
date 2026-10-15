# 14 - Packaging and release

Scope: how Windows and Linux release artifacts are assembled and checked, the AppImage layout and its glibc history,
icons and resource metadata, checksums, license bundles, the release notes workflow and the validation commands.
Build mechanics and the patched-runtime gates are in [02-build-system.md](02-build-system.md); the Linux platform is in
[05-platform-linux.md](05-platform-linux.md); logging for bug reports is in [15-debugging-logging.md](15-debugging-logging.md).

Tags and confidence follow the conventions of the other files in this folder (GENERIC_REXGLUE, REUSABLE_INFRASTRUCTURE,
PLATFORM_WINDOWS, PLATFORM_LINUX, GAME_ADAPTATION, HARD_CORPS_SPECIFIC; CONFIRMED / LIKELY / UNCERTAIN). No packaging
script was run while writing this document, so every "verified" below means "the script enforces it" or "the commit
records it", not "re-run now".

## 1. Verified versus claimed

| Statement | Evidence | Status |
| --- | --- | --- |
| Windows ZIP contents are exactly the 6 root files + 2 license files | enforced by `Test-ReleaseFolder` in `tools/package-release.ps1`; accept/reject cases in `tools/test-release-check.ps1` | CONFIRMED in code; the 9 cases are a manual run (script exists, not executed here) |
| v0.5.0 artifacts and their SHA-256 | listed in `.github/release-notes-v0.5.0.md`, tag `v0.5.0` exists | CONFIRMED that the notes record them; artifacts not re-hashed here (pending) |
| The AppImage now targets glibc 2.31 (Ubuntu 20.04) | `df29fe2` changes `MAX_GLIBC`, the setup script and `docs/linux.md`; `f96c66e` fixes the doc text | CONFIRMED in scripts; **no release or log in the repository shows an AppImage actually built and run on 20.04** (pending) |
| Released AppImages need glibc 2.35 | every release note up to v0.5.0 says "glibc 2.35 or newer (Ubuntu 22.04 or later)" | CONFIRMED for v0.5.0 and earlier |
| `docs/linux.md` says glibc 2.31 | HEAD docs after `df29fe2` | the release notes (`.github/release-notes.md`) were **not** updated and still say 2.35 and the old `-linux-x86_64` name; HEAD is 4 commits after `v0.5.0` (`git describe`), so the new baseline and name are unreleased |
| Linux AppImage runs on a Steam Deck | `docs/linux.md`: "intended flow ... has not been tried on a Steam Deck yet"; a SteamOS tester ran it | not hardware-verified |

## 2. Windows package

`tools/package-release.ps1 [-Clean] [-SkipBuild] [-VerifyOnly <folder>]` (PLATFORM_WINDOWS, REUSABLE_INFRASTRUCTURE,
introduced `b31b959`).

Flow:

1. Parse the project version from the `project(...)` line of `CMakeLists.txt`.
2. Unless `-SkipBuild`: run `tools/build.ps1 -Config Release -BuildDir build-release` (so the normal `build/` tree is
   untouched, and the patched-runtime update runs).
3. Gate: `Test-PatchedRuntime` on `tools/rexglue/win-amd64`; stop if stale ([02-build-system.md](02-build-system.md)
   section 8).
4. Stage into `dist/HardCorpsUprising-v<version>-windows-x64/`: `hard_corps_uprising.exe`, `rexruntime.dll`,
   `rexgpu-xenos.dll`, `hard_corps_uprising.toml`, `LICENSE`, `README.txt` (from `packaging/README.txt` with `@VERSION@`
   replaced, CRLF, no BOM) and `licenses/ReXGlue-LICENSE.txt`, `licenses/THIRD-PARTY-LICENSES.txt`.
5. Require the staged runtime DLLs to be byte-identical to the ones in `tools/rexglue`.
6. Run the forbidden-content scan on the staging folder (section 5).
7. Write the ZIP (entries sorted by path, forward slashes, optimal compression; file timestamps are the files' own, so
   byte-reproducibility is not claimed), reopen it and compare the entry
   list with the staged files, reject unsafe entry names (leading slash, `..`, `:`, backslash).
8. Write `<name>.sha256` (`<hash>  <name>.zip`, lower-case, LF, no BOM).

The package is only the executable, two DLLs and the runtime TOML. No Tracy DLL, no symbols, no game data. The
dependency on the VC++ redistributable is stated in `packaging/README.txt` rather than bundled.

Nothing is published or tagged by the script. Porting notes: copy the flow; replace `$RootFiles`, `$LicenseFiles`
and the README; the exact-file-list approach (allow list, not deny list) is the important idea.

## 3. Linux package (AppImage)

`tools/package-linux.sh [--clean] [--out-dir DIR]` (PLATFORM_LINUX, REUSABLE_INFRASTRUCTURE; introduced `9964f82`;
`--out-dir` added in `536dfdf`). Run on Linux or WSL.

Flow (in script order): read version from `CMakeLists.txt` and the application id from `src/platform.cpp`
(`kApplicationId`); verify the `.desktop` file is named after it; check tools; compare host glibc with `MAX_GLIBC`
(warn if newer); verify `game/extracted/default.xex` SHA-256; rsync sources to `~/hcu-linux-build` (`HCU_WORK`);
download and unpack the pinned SDK zip (SHA-256 checked) and check the version from `rexglueConfigVersion.cmake`
(because the stock `rexglue` binary may not run on the build distro); build the patched runtime if
`--check` fails and re-check; build `rexglue` from source if the stock one cannot run; configure and build the
`linux-amd64-release` preset; verify the staged libraries against the SDK stamp; assemble the AppDir; run the release
check; check glibc symbol versions; download `appimagetool` and the type-2 runtime (pinned versions, SHA-256 checked),
produce the AppImage, `chmod 755`, write `<name>.sha256`.

### Artifact layout (AppDir, enforced exactly)

| Path | Purpose |
| --- | --- |
| `AppRun` (from `packaging/linux/AppRun`) | launcher shim, section 3.2 |
| `io.github.saulob.hard-corps-uprising.desktop` | desktop entry; file name must equal `kApplicationId`; `StartupWMClass` must equal it too |
| `hard_corps_uprising.png`, `.DirIcon` | 256 px icon for the root and file managers |
| `usr/bin/hard_corps_uprising` (stripped), `librexruntime.so`, `librexgpu-xenos.so`, `hard_corps_uprising.toml` | the program, patched Release libraries, runtime config |
| `usr/lib/libstdc++.so.6`, `libgcc_s.so.1` | bundled GCC 13 C++ runtime, used only as a fallback |
| `usr/share/doc/hard_corps_uprising/` | `LICENSE`, `ReXGlue-LICENSE.txt`, `THIRD-PARTY-LICENSES.txt`, `THIRD-PARTY-LICENSES-LINUX.txt`, `libstdc++-copyright.txt` |
| `usr/share/icons/hicolor/<16..512>/apps/*.png`, `scalable/apps/*.svg` | icon theme |

The script compares the AppDir file list with a hard-coded `EXPECTED` list, so any extra or missing file fails the
build. HARD_CORPS_SPECIFIC names, REUSABLE technique.

### 3.1 RUNPATH / RPATH

- The game executable has RPATH `$ORIGIN` (the SDK's Linux link settings; the SDK does not stage the runtime beside the
  program on Linux, so CMake adds a POST_BUILD copy). The release check requires `R*PATH [$ORIGIN]` on the executable.
- The patched runtime and GPU plugin are built with RUNPATH `$ORIGIN:$ORIGIN/../lib`, matching the stock libraries,
  instead of the build folder CMake would embed; `build-patched-rexruntime.sh` fails if the RUNPATH is not exactly that
  (`430fc32`). The GPU plugin is found because the runtime loads it by name from its own directory.
- Test executables use `BUILD_RPATH $ORIGIN` for the same reason.

### 3.2 AppRun and the bundled C++ runtime

`AppRun` is a short POSIX shell script. It looks up the system `libstdc++.so.6` (`ldconfig -p`, then well-known paths)
and sets `LD_LIBRARY_PATH` to the bundled `usr/lib` **only if** the system copy lacks `GLIBCXX_3.4.32` (GCC 13). A
newer system copy must win because graphics drivers may need it. The script then `exec`s the binary with the original
arguments. The AppRun shell script is unchanged since `9964f82`. Class: REUSABLE_INFRASTRUCTURE. The `GLIBCXX_3.4.32`
requirement is a property of the ReXGlue Linux libraries built with GCC 13 libstdc++ headers (CONFIRMED by the
packaging script's check that the bundled copy contains that symbol version).

### 3.3 glibc compatibility history

| Step | Commit | Effect |
| --- | --- | --- |
| First AppImage | `9964f82` | build on Ubuntu 22.04 (glibc 2.35, the same baseline as the stock ReXGlue Linux libraries); script warns if the host glibc is newer; docs and release notes say 2.35 |
| Stricter compatibility | `df29fe2` | build on Ubuntu 20.04 (glibc 2.31); new `tools/setup-linux-build-env.sh`; `MAX_GLIBC=2.31`; the release check now scans the **dynamic symbols** (`readelf --dyn-syms`) of the program, both ReXGlue libraries and the two bundled C++ libraries and fails if any needs a glibc symbol version above 2.31 (previously `objdump -T` was used for three files and only printed); stock `rexglue` (needs 2.35) is replaced by a locally built one; docs now say 2.31 |
| Artifact name | `536dfdf` | `HardCorpsUprising-v<ver>-linux-x86_64.AppImage` -> `HardCorpsUprising-v<ver>-x86_64.AppImage` for AppImageHub naming compliance (reason from the commit subject; the compliance rule itself was not checked here) |
| Missing-Vulkan UX | `6cf59bd` | `ReportGraphicsFailure()` shows an error window drawn in an SDL surface when the presentation setup fails (SDL's message box is not used: it runs zenity first; see `src/platform.cpp`); documented in [15-debugging-logging.md](15-debugging-logging.md) |
| Doc/script cleanup | `f96c66e` | the warning message uses `$MAX_GLIBC`; the build-dependency list in `docs/rexglue-runtime-patch.md` now points to the setup script; also fixes a mangled `printf` line break in the glibc check introduced in `df29fe2` |

Why a build distro matters: a binary links against the glibc symbol versions available on the build machine; it runs on
that glibc or newer. So the **oldest** supported distro is the build machine. Pitfall (CONFIRMED in `df29fe2`): the
bundled libstdc++ is itself checked, since GCC 13's libstdc++ can raise the glibc floor.

Pitfall: the first design only warned when the host was newer than the baseline. The new design fails the build on the
actual symbol versions, which is the real constraint (host glibc alone does not prove that a library was built there).

Not established in this review: that the 20.04 environment builds successfully end to end, that the resulting AppImage
starts on 20.04, or what the minimum working system actually is (the script only proves the symbol floor, not the
rest of the dependency chain such as Vulkan loader and X11/Wayland client libraries).

## 4. Icons and metadata

| Item | Detail |
| --- | --- |
| Source | `packaging/icons/app-icon.svg`; PNGs 16, 24, 32, 48, 64, 128, 256, 512 and `app-icon.ico` are committed |
| Generator | `tools/make-app-icon.py` (needs Edge/Chrome); refuses an SVG that does not use the launcher accent color |
| Windows | `packaging/windows/hard_corps_uprising.rc` is one line: resource 1 is the app icon; CMake sets `OBJECT_DEPENDS` on the `.ico`; no `VERSIONINFO` resource exists (CONFIRMED) |
| Linux | icons are copied into hicolor theme sizes and the root; the app sets its window icon from the AppImage's icon file (`docs/linux.md`) |
| Desktop entry | `Type=Application`, `Exec=hard_corps_uprising`, `Icon=hard_corps_uprising`, `Categories=Game;`, `Terminal=false`, `StartupWMClass=<application id>` |
| Application id | `io.github.saulob.hard-corps-uprising` (`kApplicationId` in `src/platform.cpp`); the SDL app metadata, Wayland `app_id` and X11 class all use it |

Origin: `26bd37c` (icon and `.rc`). The `.desktop` file used to be `hard_corps_uprising.desktop` with a separate SVG
(`9964f82`); at HEAD it is named after the application id and the SVG lives under `packaging/icons`. Classification:
REUSABLE_INFRASTRUCTURE, with the application id HARD_CORPS_SPECIFIC. The script's rule that the `.desktop` file name,
`StartupWMClass` and the string inside the program must agree is a cheap, portable consistency check.

## 5. Forbidden-file scanning (technique reusable; list project-specific)

Both packagers scan the staged tree and fail on:

| Check | Windows (`Test-ReleaseFolder`) | Linux (inside `package-linux.sh`) |
| --- | --- | --- |
| Forbidden names | exact-name list covering game data, packages, install temp/backups, player data, caches, logs, settings and the title id folder | same list |
| Forbidden file patterns | `*.xex`, `*.log`, `*.dmp`, `*.pdb`, `*.xsh`, `*.xpso`, `*.bak`, `*.tmp`, `*.sav` | same |
| Magic bytes | first 4 bytes `LIVE`, `PIRS` or `CON ` (Xbox 360 STFS packages) | same |
| Local paths | repo path, both slash styles, UTF-8 and UTF-16 forms, in every file | repo path, work dir and `$HOME` in every file |
| Allow-list | exact set of root files and a `licenses` folder with 2 files | exact `EXPECTED` file list |
| Extra (Linux) | n/a | no symlinks, no PE (`MZ`) files, no file named like RenderDoc/Vulkan-layer, no `*.json`, no binary linking RenderDoc, unresolved `ldd` entries fail |

The exact names (including the title-id folder and the settings file names) are HARD_CORPS_SPECIFIC; the *categories*
(game data, package signatures, caches, logs, player data, local paths, layer/injection files) are the reusable
checklist. `tools/test-release-check.ps1` creates scratch folders with a good layout plus one bad extra each
(`user_data\`, scores, achievements, settings, cache, migration leftover, log, game file) and requires
`package-release.ps1 -VerifyOnly` to accept the fresh one and reject the others. It uses placeholder files only and
removes its scratch folder. There is no Linux equivalent test (the Linux check is inline in the packaging script).
Origin: `b31b959` for the Windows scan; `27e26ea` for the test script and the user-data categories; the Linux RenderDoc
rules came with `3020c32`/`430fc32` ([15-debugging-logging.md](15-debugging-logging.md), RenderDoc).

Pitfall: scanning for the *repo path* is only meaningful if the build removed it (Release `-fmacro-prefix-map`,
`-ffile-prefix-map` on the patched runtime, `strip` on Linux). Without those the scan fails by design.

## 6. Licenses bundle

- `packaging/licenses/ReXGlue-LICENSE.txt` (SDK license text), `THIRD-PARTY-LICENSES.txt` (Windows set) and
  `THIRD-PARTY-LICENSES-LINUX.txt` (adds Linux-only components). The repo `LICENSE` is also shipped.
- The text states that the runtime and GPU plugin are the SDK release files built with the project's patches and that the
  rest of the SDK is stock. The Linux file also covers the bundled libstdc++ under the GCC Runtime Library Exception
  with its copyright file copied from the build machine (`/usr/share/doc/libstdc++6/copyright`; the script fails if it is
  missing).
- Release commits touch these files when SDK pins change (`81444f0` is the last that did). The project license is
  GPL-2.0-only (`5e75728`).
- Class: REUSABLE_INFRASTRUCTURE for the mechanism; the contents are HARD_CORPS_SPECIFIC and must be redone from the
  components the new project actually ships. Whether the third-party list is complete was not audited here (UNCERTAIN).

## 7. Release notes workflow

1. A release commit (`release: prepare vX.Y.Z`, for example `81444f0`, `a0189f4`) bumps the `project(... VERSION ...)`
   line, updates `README.md`, writes `.github/release-notes-vX.Y.Z.md`, replaces `.github/release-notes.md` with the same
   text (at HEAD the two files are identical), and updates the version in the `package-release.ps1` help example.
2. Release notes sections used consistently: highlights, Downloads (per platform), Requirements, Notes / Known
   Limitations, Build / Developer Notes, Reporting Problems (what log to attach), Checksums (table of SHA-256 for each
   artifact).
3. The packaging scripts only build local artifacts under `dist/` ("nothing was published"); tagging and uploading are
   manual steps outside the repository. `dist/` is git-ignored.
4. Claims about what is fixed are carefully worded in the notes (for example the Linux brightness fix and the Exit Game
   fix are marked as not yet confirmed by the reporter or on real hardware); keep that practice.

Pitfall: the notes' requirement line and artifact name are hand-edited and can drift from the scripts. At HEAD they do
(glibc 2.35 and `-linux-x86_64` in the notes versus 2.31 and `-x86_64` in the scripts). Update both when the next
release is prepared, and do not state a glibc floor in release notes until an AppImage built on the new baseline has
been run.

## 8. Validation commands

Development (focused): run only the check that matches the change.

| Change | Check |
| --- | --- |
| Windows release scanner list | `tools/test-release-check.ps1` (scratch folders only; does not build or launch) |
| An already staged Windows folder | `tools/package-release.ps1 -VerifyOnly <staged folder>` |
| Runtime stamp or staging | the check-only forms in [02-build-system.md](02-build-system.md) |
| AppDir layout | build with `tools/package-linux.sh --out-dir <scratch>` so `dist/` is not overwritten (the script otherwise replaces the release AppImage in `dist/`) |

Final release review only: clean Release build on each platform through the packaging scripts, confirm the printed
contents list and SHA-256, check the glibc lines printed by the Linux script, run the allocator stress and RenderDoc
loader tests ([13-crash-hardening-threading-memory.md](13-crash-hardening-threading-memory.md)), and run each
artifact once on the target OS (by the owner; this review did not launch the game).

## 9. Porting notes

| Piece | Port as |
| --- | --- |
| Exact-allow-list staging + sorted ZIP + entry-name check + `.sha256` | copy / adapt |
| Forbidden-category scan (names, extensions, magic bytes, local path strings) | adapt (replace the lists) |
| Gating the package on the patched-runtime stamp and byte-identical DLLs | copy / adapt |
| AppDir builder with exact file list, RPATH check, `ldd` check, glibc symbol-version scan | adapt |
| AppRun libstdc++ fallback | concept (needed only if the runtime needs a newer libstdc++ than the oldest target) |
| Application-id consistency checks | adapt |
| Build-distro setup script with pinned, fingerprint-checked sources | adapt (versions change) |
| Release notes structure | adapt |

## 10. Origin commits and branches

`b31b959` Windows packaging and scanner; `5e75728` license; `9964f82` AppImage; `56dc167` first Linux release prep;
`ab4f7b7` runtime gating in both packagers; `27e26ea` scanner test script and user-data categories; `26bd37c` icon and
`.rc`; `3020c32` RenderDoc/layer rules; `430fc32` staged checks in the AppDir; `81444f0` and `a0189f4` recent release
preparation examples; `df29fe2`, `536dfdf`, `6cf59bd`, `f96c66e` glibc baseline, artifact name, missing-Vulkan
message and doc cleanup. Superseded: Ubuntu 22.04 baseline (replaced in `df29fe2`), the `-linux-x86_64` artifact name
(replaced in `536dfdf`).
