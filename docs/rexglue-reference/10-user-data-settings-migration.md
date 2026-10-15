# 10 User data, settings and migration

Scope: where Hard Corps: Uprising Recompiled keeps player data, how settings are stored and loaded, how data from earlier
layouts is migrated, and what a new project should decide before its first public release. The launcher UI that edits
settings is in [03-launcher.md](03-launcher.md); the scores and achievements files that live in this tree are in
[11-offline-services-profile-achievements.md](11-offline-services-profile-achievements.md); packaging exclusions are in
[14-packaging-release.md](14-packaging-release.md). Project-side player documentation: `docs/user-data.md`.

Tags: path decision, migration and settings model are `REUSABLE_INFRASTRUCTURE`. The folder layout below the user data root
is the ReXGlue SDK's own (`GENERIC_REXGLUE` convention). Title ID, key names and the DLC path are `HARD_CORPS_SPECIFIC`.

## 1. Why this exists: a migration that was forced by shipping early

The project published v0.1.0 to v0.3.1 (tags, 2026-10-10 to 2026-10-12) with player data in ReXGlue's default user data
root: `Documents\hard_corps_uprising` on Windows and `$XDG_DATA_HOME/hard_corps_uprising` (normally
`~/.local/share/hard_corps_uprising`) on Linux. Commit `27e26ea` ("add portable user data", included from v0.3.2 onward;
checked by ancestry against the tags) moved everything into a `user_data` folder next to the program and wrote a migration
for the installed base. The migration, its 17 tests and the packaging exclusions are the largest single piece of
infrastructure the project built purely because the layout was not decided before release. This is the main lesson of the
file; section 9 turns it into a checklist. Confidence CONFIRMED (tags, commit, release notes v0.3.2).

## 2. Final layout (HEAD)

```
<port folder>/
    hard_corps_uprising(.exe)   hard_corps_uprising.toml   (shipped defaults, never written by the program)
    game/                       game data (not player data)
    logs/                       logs
    user_data/
        settings.toml           the player's launcher choices
        58410A18/live/stats.toml   local scores (title-specific)
        0000000000000000/58410A18/...   installed DLC (SDK marketplace layout)
        achievements/58410A18.toml      unlocked achievements (SDK store)
        cache/                  shader cache (SDK default placement relative to the user root)
```

Everything under `user_data` except `settings.toml`, the `live` folder and the DLC install is created by the SDK in its own
layout; only the root moved. `game` is deliberately outside it so "player data" is one folder to copy, back up or delete.
Evidence: `docs/user-data.md`, `src/user_data.cpp`, `src/game_files.cpp` (DLC path), `src/local_stats.cpp`. CONFIRMED.

## 3. Path resolution

All in `src/hard_corps_uprising_app.h` (`OnConfigurePaths`, `ChooseUserData`) and `src/platform.cpp` (`PortFolder`):

- Port folder: the executable's folder; on Linux, when `$APPIMAGE` is set, the folder that contains the AppImage file (the
  mount point is read-only and disappears on exit). Outside an AppImage on Linux it is the program's folder.
  Same folder is used for `game/` and `logs/`.
- Game data root: `--game_data_root` if given, else `<port folder>/game`.
- User data root: `--user_data_root` always wins and is used as given (no migration, no `user_data` created, outcome
  `kExplicit`). Otherwise `SelectUserData` decides between the portable folder and the system folder that ReXGlue had already
  chosen.
- Cache root: when `--cache_root` is empty and the selected root differs from the SDK's, `cache_root` is set to
  `<user data>/cache`, so the shader cache follows the user data. An explicit `--cache_root` is respected.
- Logs: next to the program; for an AppImage whose folder is not writable the log goes to `<user data>/logs`
  (`NextLogFile` returns empty if the folder cannot be created, then the next candidate is tried). The SDK itself writes logs
  next to the executable, which would be inside the read-only mount.
- Development: `tools/build.ps1 -Run` passes `--game_data_root` and `--user_data_root=build/user_data`, and `-Clean` keeps
  `build/user_data`. A development run therefore never touches or migrates the real data. (CONFIRMED in `tools/build.ps1`.)

Source-folder vs installed: the project has no separate "source tree" layout. A built executable behaves the same wherever it
sits; the dev override above is what keeps a source checkout's profile separate. A new project should do the same and avoid
detecting "am I in a source folder" heuristics.

## 4. `SelectUserData` decision table

Inputs: `port_folder`, `system` (the root ReXGlue chose), `explicit_root`. Output: `UserDataSelection{outcome, root, portable,
system, detail}`. The last argument, `UserDataFileOps`, replaces `rename`, `copy_file` and `remove_all` with callables so
tests can inject failures. A directory counts as "has data" only if it contains something other than directories
(`HoldsFiles` — also true when the folder cannot be read, so an unreadable folder is never mistaken for empty).

| Situation | Outcome | Active root |
| --- | --- | --- |
| `--user_data_root` given | `kExplicit` | that folder, unchanged |
| System folder has data, portable has none | migrate (below) | portable on success, system on failure |
| Neither has data, portable writable | `kPortable` | portable |
| Both have data | `kBothExist` | portable, system left alone, one warning, never merged |
| Portable not writable (probe file) | `kPortableUnavailable` | system folder for this run |
| Migration fails | `kMigrationFailed` | system folder; retried next start |
| Migrated but old folder cannot be removed | `kMigratedLegacyKept` | portable, warning |

Migration (`MoveIntoPlace`):

1. If `user_data` exists: it must be a directory (a file named `user_data` gives a failure and nothing moves); an empty
   one (only empty sub-folders) is removed so the rename can proceed.
2. `rename(system, portable)`: atomic on the same volume. If it succeeds, done.
3. Otherwise copy into `user_data.migrating` (a stale one is cleared first), recursively, copying regular files and symlinks
   and failing on other types (`CopyTree`). The copy is verified against the original by listing both trees with type and
   file size (`ListTree`, `CopyVerified`); sizes and structure must match exactly.
4. `rename(user_data.migrating, user_data)`. On any failure `user_data.migrating` is removed and the old folder is left as it
   was.
5. Only after the portable folder is complete is the old folder removed; if that fails the portable data is still used and
   the log names the old folder.

There are no marker files. The "marker" is the staging folder name `user_data.migrating` (cleaned up on failure, rejected
from release packages) and the rule that `user_data` only appears by rename, so a half-written `user_data` cannot be
mistaken for complete data. Rollback in this design is "the source is never modified until the destination is complete and
verified"; there is no separate undo step. Failure always degrades to the old folder, so the player keeps their data.
Confidence CONFIRMED by the failure-injection tests.

Messages: `DescribeUserData` produces one log line per start (info or warning); `UserDataFolderHelp` is the About page text
that explains which folder is in use, including the failure modes (see [03](03-launcher.md)).

### Tests (`tests/user_data_tests.cpp`, target `hcu_user_data_tests`, temporary folders only, no game files)

`TestNothingExists`, `TestMigrationByRename`, `TestMigrationByCopy` (rename forced to fail, a "cross-volume" helper),
`TestStaleStagingIsReplaced`, `TestPortableDataWins`, `TestEmptyPortableFolder`, `TestPortableWithOnlyFolders`,
`TestCopyFailureKeepsSystemData` (fails the 4th file copy), `TestShortCopyIsRejected` (copy writes the wrong size),
`TestActivationFailureKeepsSystemData`, `TestPortableNameTakenByFile`, `TestLegacyRemovalFailure`, `TestPortableUnavailable`,
`TestExplicitRootIsAuthoritative`, `TestLinksAreNotFollowed`, `TestPathsFollowTheRoot`, `TestMessages`. Also
`tools/test-release-check.ps1` verifies the release scripts reject `user_data`, its contents and `user_data.migrating`.
Automated; CONFIRMED. These tests cover the logic with injected file operations; a real cross-volume migration on a real
drive pair is documented behavior but not shown as run in the repo (UNCERTAIN as a manual validation).

Porting: COPY `user_data.*` and its tests, replacing only the constant folder name and the log text. If the target has never
shipped, you only need the portable half and can skip the legacy branch (section 9).

## 5. Settings

Two files, both TOML read by the SDK's cvar loader:

| File | Role | Written by |
| --- | --- | --- |
| `<exe dir>/hard_corps_uprising.toml` | shipped defaults (license mask, GPU plugin, input mode, default window, fixed guest video mode) | the build copies `config/hard_corps_uprising.toml`; never written at runtime |
| `<user data>/settings.toml` | the player's choices | the launcher only |

Loading order (`HardCorpsUprisingApp`): `SetupEnvironment` loads the shipped file; `OnPostInitLogging` (after the user data
root is known and logged) calls `LoadPlayerSettings`, which runs `rex::cvar::LoadConfig` on the player file over it. Ordering
matters: the player file location depends on the user data decision, so it cannot be loaded earlier. The GPU plugin loads
later (in `SetupPresentation`) and its cvars take the player values from the already loaded file, which is why `GameSettings`
has a second capture step ([03](03-launcher.md) section 5).

Launcher vs game settings: the launcher is the only writer of `settings.toml` (`GameSettings::Save` writes only changed
keys; `SaveNow` writes launcher window placement). The in-game F10 cheat menu deliberately does not write cvars; its changes
are in-memory session overrides because `rex::cvar::SaveConfig` writes every modified cvar and the SDK's own F4 overlay can
call it (a leak into the shipped file otherwise; stated in `docs/launcher.md`, CONFIRMED by design). Window placement keys
(`hcu_launcher_x`, `_y`, `_width`, `_height`, `_maximized`, `_fullscreen`) live in the same file, are launcher state and are
not touched by "Reset all settings".

File handling details (`src/settings.cpp`):

- The file is created on first save with a two-line comment header. Edits go through `trg::TextFileSettings`, which
  preserves comments and unrelated lines, so players may hand-edit it.
- Value types: bools are written `true`/`false`; string cvars are quoted with escaping (`TomlValue`).
- A key is written only if its cvar is registered; otherwise `Save` fails and logs an error (never writes a key the runtime
  would ignore).
- Reset reads the shipped TOML (toml++) for each whitelisted key and falls back to the cvar default.
- `hcu_skip_launcher` is a one-shot flag that is reset on read, so it is never persisted.

### Settings evolution that needed migration code

1. **Bare `on` / `off` values** (`dc89a6c`, `src/legacy_settings.cpp`): an earlier build wrote `key = on`. TOML rejects it and
   ReXGlue then ignored the whole file (one bad value discards the file). `RepairLegacyPlayerSettings` runs before parsing:
   it rewrites only lines of the exact form `bare_key = on|off` (optional BOM, comments and tables untouched, no trailing
   junk) to `true`/`false`, writes a `.bak`, then a `.tmp`, then renames over the original. If the backup or temp write fails
   it leaves the file alone and logs an error. This is idempotent: once no bare values remain it does nothing. Classification
   REUSABLE_INFRASTRUCTURE; GENERIC lesson that the SDK discards an entire config on a single parse error. Confidence
   CONFIRMED (code; no target under `tests/` exercises `RepairLegacyToggles`, so validation is inferred, not automated).
2. **Multisampling** (`cf6eee1`): the UI first bound directly to the SDK cvar `native_2x_msaa`, with "Off" meaning false,
   which is not a meaningful way to turn off the game's own multisampling. It was replaced by a project cvar
   `hcu_msaa_compatibility` that `ApplyMultisampling` converts to `native_2x_msaa` at every start (and at Play), so stale
   `native_2x_msaa = false` lines written by older launchers are overridden. Test: `tests/multisampling_tests.cpp`.
   Lesson: expose project-owned setting names and derive the SDK cvar from them, so the SDK key can change meaning without a
   file migration.
3. **Monitor 0 / 1** are equivalent for the window-change comparison (`WindowValue`).

There is no version number in `settings.toml` and no general key-rename mechanism. Adding one before release is cheap
(section 9).

## 6. First-run behavior

- A fresh program folder has no `game`, no `user_data` and no `settings.toml`. `SelectUserData` creates `user_data` (probing
  writability with `.write_test`). The launcher forces itself open because game data is missing ([03](03-launcher.md)
  sections 2 and 3). Defaults come from the shipped file (853x480 windowed, `monitor = 1`, fixed guest video mode,
  `license_mask = 1`, keyboard mode on).
- `settings.toml` appears the first time the player presses Save or Play with a changed value.
- No profile file is created by this project. See [11](11-offline-services-profile-achievements.md) for what the SDK does.

## 7. Platform locations

| System | Port folder / `user_data` | Previous (migrated) location |
| --- | --- | --- |
| Windows | folder of `hard_corps_uprising.exe` | `Documents\hard_corps_uprising` |
| Linux AppImage | folder holding the AppImage file | `$XDG_DATA_HOME/hard_corps_uprising` |
| Linux, not an AppImage | folder of the program | same |

PLATFORM_WINDOWS: if the program is installed under a protected directory (Program Files) the portable folder is not
writable; the code then uses the system folder for that run (`kPortableUnavailable`). It does not try a third location.
PLATFORM_LINUX: AppImage in a read-only directory gives the same outcome; logs fall back to `<user data>/logs`. Steam Deck
flow: documented in `docs/linux.md` as untested on hardware (UNCERTAIN).

## 8. Packaging and updates

`tools/package-release.ps1` and `tools/package-linux.sh` refuse (fail the build) when the staging folder contains game files,
`game`, `game-install-temp`, `game-install-backup`, `user_data`, `user_data.migrating`, `content`, `cache`, `logs`,
`settings.toml`, `settings.toml.bak`, `58410A18` or save/log types. `tools/test-release-check.ps1` exercises the Windows check on
scratch folders. Update model: extract a newer ZIP over the old folder (the ZIP contains no `user_data`), or move `user_data`
and `game` to the new folder. Deleting the old program folder deletes the player's data; this is the trade-off of a portable
layout and the project documents it in `docs/user-data.md`. Details: [14-packaging-release.md](14-packaging-release.md).

## 9. What to introduce before the first public release (to avoid later migration pain)

Derived from what this project had to retrofit in `27e26ea` and the follow-up fixes. Each item is advice grounded in the
evidence above, not a claim that this project already did it.

1. **Decide the root once.** Choose portable (`user_data` next to the program) or the SDK/system root before the first
   release. Changing later required `SelectUserData`, a copy fallback, 17 tests, the log/About messaging and three packaging
   guards. If portable is chosen from the start, none of the legacy branch is needed.
2. **Keep game files and player data in different folders** from day one, and keep logs separate.
3. **Make `--user_data_root` and `--game_data_root` work from day one** and use them in your dev script, so a dev run never
   touches a real profile.
4. **Pick project-owned setting names (`<prefix>_...`) for every launcher option** and derive SDK cvars from them (the
   `hcu_msaa_compatibility` pattern). Never bind the UI to an SDK cvar whose meaning you do not control.
5. **Validate what you write.** Round-trip the file through the SDK's TOML loader in a test; one bad value discards the whole
   file (the `on`/`off` incident). Write atomically (temp plus rename) and keep a backup before rewriting.
6. **Add `version = N` to the player settings file** (this project has none) and a one-function migration hook, so a future key
   rename or semantic change has somewhere to live. UNCERTAIN whether the project would have needed it; it is a cheap
   precaution.
7. **Write other state files atomically with a size cap and a corrupt-file quarantine** (the pattern in `src/local_stats.cpp`:
   `.tmp` + rename, 1 MiB cap, bad file moved to `.corrupt`, store starts empty).
8. **Use staged writes and verified renames for any folder you replace** (`user_data.migrating`, `game-install-temp`,
   `dlc-install-temp`): the destination only appears complete.
9. **Never merge two data sets automatically.** `kBothExist` keeps the portable data and leaves the old folder in place with
   a warning.
10. **Add the packaging guard at the same time as the folder** (forbidden-name list plus a check script).
11. **Decide the title-specific subfolders** (stats, DLC, achievements) and the content-ID folder rule before shipping
    content installs; they sit in the same root.
12. **Document the data location in the app** (About page with an "open folder" button) so support requests can be answered.
13. **Plan for read-only install locations** (AppImage folder, Program Files): detect with a write probe and degrade
    gracefully rather than failing to start.

## 10. Pitfalls and confidence

- A user who had both a portable and a system folder with data will not have them merged (by design). CONFIRMED.
- Unreadable folders are treated as having data, which avoids replacing them but can produce `kMigrationFailed` repeatedly.
  LIKELY harmless.
- Migration of the shader cache is by whole-folder move; stale cache is not validated. UNCERTAIN whether it matters.
- `--user_data_root` bypasses migration entirely, so a tester using it will not see the legacy path. CONFIRMED.
- Deleting the program folder deletes the data. Documented, not mitigated.

Confidence summary: path decision, migration, packaging guards CONFIRMED (code plus automated tests); settings repair CONFIRMED
(code); manual cross-volume run UNCERTAIN; Windows protected-folder behavior LIKELY.
