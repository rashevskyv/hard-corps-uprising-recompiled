# 11 Offline services, profile and achievements

Scope: how Hard Corps: Uprising Recompiled answers the Xbox Live and system-UI calls of a Live Arcade title while offline,
how the local player identity and license reach the game, and how achievements and scores persist. Everything that names an
address (`sub_82...`), a view ID, a menu or a game flag is `HARD_CORPS_SPECIFIC` and `GAME_ADAPTATION`. The structure of the
code (a hook module, a pure data layer, a thin app bridge) is `REUSABLE_INFRASTRUCTURE`. Storage locations are in
[10-user-data-settings-migration.md](10-user-data-settings-migration.md); the modal overlay base class is described in
[03-launcher.md](03-launcher.md) section 7; hook mechanics are in [01-rexglue-core.md](01-rexglue-core.md) and
[16-reusable-game-features.md](16-reusable-game-features.md). Player-side documentation: `docs/offline-live.md`.

Branch note: `git branch --no-merged HEAD` lists `debug/full-gameplay-telemetry` and `investigate/controller-rumble`. By file list both are
debug/telemetry branches (guest-state tracing; a rumble probe) and neither changes the Live, stats or achievement files; nothing here is based on them.

## 1. What exists and what does not

Implemented at HEAD (commit `f3ac390`, 2026-10-11, shipped from v0.3.2; it is not in v0.3.1 by ancestry check):

| Capability | Where |
| --- | --- |
| Local scores and leaderboard answers for the one local player | `src/local_stats.*`, `src/live_stats.*`, hooks in `src/live_fix.cpp` |
| Sign-in appearance for the game's own "is Live" checks, only inside narrow scopes | `src/live_fix.cpp` (`LiveScope`, `sub_824F45E0` and the menu hooks) |
| Empty friends list | `sub_827CB240` hook |
| Achievement 0 alias workaround | `src/achievement_alias.*`, `sub_825702A0` hook |
| In-game achievements overlay and F7 | `src/achievements_overlay.*`, `sub_82570B68` hook, `CreateAchievementsOverlay` override |
| Download-content note instead of the Marketplace | `src/marketplace_rules.h`, `src/notice_dialog.*`, `sub_82570B70` hook |
| Launcher achievements viewer | `src/launcher_achievements.*`, launcher Achievements page |

Not implemented (and not in the repo): a profile creation or selection UI, a gamertag editor, profile settings or
title-settings emulation, real network Live, multiplayer, cloud saves, global or friends leaderboards, avatar, messaging, a
Marketplace or any download of content. Hard Corps has no profile first-run fix; if another project needs one, this repo
has no reusable implementation (see section 3).

## 2. APIs emulated and how (GAME_ADAPTATION)

The title calls the XAM exports directly and through its statically linked Live library, so the app hooks the recompiled guest
functions (`REX_EXTERN(__imp__sub_XXXXXXXX)` plus `REX_HOOK_RAW(sub_XXXXXXXX)`) instead of the SDK exports. Hooks chosen by
address because the library code wraps the kernel imports. Original code is called through `__imp__sub_...` first or last
depending on whether the port wants to decide before or amend the result.

| Guest function (address) | Role | Port behavior |
| --- | --- | --- |
| `sub_824F45E0` | game's `IsLive(players, index)` | returns 1 for user 0 only while a `LiveScope` is open on the thread |
| `sub_824F6870` | main menu item enable | forces the Live flag so LEADERBOARDS is enabled; other online items stay greyed because the runtime denies the multiplayer privilege |
| `sub_82534AC8` | leaderboard screen update | opens a `LiveScope` so the sign-in popup is not shown |
| `sub_825164F0`, `sub_82516608` | pause menu build / update | `LiveScope` enables the Leaderboards item |
| `sub_82545728` | score submission job after a finished game | `LiveScope`, so the game's own write job runs |
| `sub_827CC008` | `XSessionWriteStats` | calls the original, then records the parsed views into `LocalStats` when the call succeeded and the XUID is the local user |
| `sub_825705F0`, `sub_82570650` | `XUserCreateStatsEnumeratorByRank` / `ByXuid` | answered by the port (registry handle) |
| `sub_827CB240` | `XFriendsCreateEnumerator` | returns an enumerator handle with no friends (SDK fails it and leaves the handle unwritten) |
| `sub_82570E78` | `XamEnumerate` | serves the port's own handles, else original |
| `sub_82570518` | `XUserReadStats` | calls the original, then fills the results buffer from the local file |
| `sub_825702A0` | `XUserWriteAchievements` | swaps achievement id 0 for an alias around the call |
| `sub_82570B68` | `XamShowAchievementsUI` | opens the overlay, returns success |
| `sub_82570B70` | `XamShowMarketplaceUI` | shows a note, returns success, broadcasts `XN_SYS_UI` closed |

Design points worth reusing (REUSABLE_INFRASTRUCTURE, CONFIRMED by `tests/live_tests.cpp`):

- **Narrow Live scope.** The title checks Live state in many places, some for online play. Instead of making the console
  appear signed in everywhere, only the specific functions that run local features open a thread-local scope in which
  `IsLive` is true. The class that opens the game's UDP socket for online play reads the state outside the scope and still sees
  an offline console. A source-level test (`TestLiveGateStaysNarrow`) forbids network/sign-in symbols
  (`NetDll`, `socket`, `XamUserCheckPrivilege`, `XamUserGetSigninState`, ...) inside the new Live code.
- **Data layer separate from guest memory.** `LocalStats`, `LocalLeaderboard`, `EnumeratorRegistry` and the buffer writers
  work on explicit big-endian guest buffers (`guest_memory.h`: `LoadBE32`, `StoreBE32`) so they are testable without a running
  guest: `TestEnumerationBufferAndOverlapped`, `TestResultsNeverRunPastTheBuffer`, `TestReadStats`.
- **Handles.** Port-owned enumerator handles start at `0xEC000000`, away from the runtime's `0xF8xxxxxx` object handles, are
  single-use, and at most 64 unserved ones are kept (`EnumeratorRegistry`). `XOVERLAPPED` is honored when passed:
  the result is written into it and `ERROR_IO_PENDING` is returned.
- **Stay the judge.** The game reads the stored result with `XUserReadStats` before submitting and submits only a better
  result. The port therefore stores the latest submission per user and view and answers the read truthfully; it never
  decides what is a high score. A view the player never played has no row, which makes the first result count.
- **Guard installing.** `Install` is called once from `InstallLiveServices` in `OnPostSetup` after the kernel exists. If the
  kernel has no user profile at that point the function returns and nothing is installed (so no scores and no alias).
  Pitfall: that makes those features depend on an SDK default profile existing (section 3).

Views and boards are `HARD_CORPS_SPECIFIC`: view 3 (Arcade, single player) was confirmed in a played run; views 4, 2 and 1
(Arcade two players, Rising single, Rising two players) are inferred from the game's code and not played
(`docs/offline-live.md`). Higher is better on every board; a submission writes int32 columns `0x10000002`, `0x10000003` and the
int64 rating `0x20000001`, the screen reads columns 1 and 2. These are properties of this title's leaderboard definition.

### Score store (`src/local_stats.cpp`)

TOML file at `<user data>/<TITLEID>/live/stats.toml`, `version = 1`, tables per user XUID and view, `rating` plus
`col_<id> = [type, value]` (types 1 int32, 2 int64, 3 double, 5 float; floating values stored as raw bits). Limits: 1 MiB file,
8 users, 64 views per user, 16 columns per view; ratings of zero or less are ignored. Save writes `stats.toml.tmp`, flushes and
renames over the original; a file that cannot be parsed is renamed to `stats.toml.corrupt` and the store starts empty; if a
save fails the score is kept in memory for the session and a warning is logged. Tests: `TestCleanFileAndFirstValue`,
`TestRestartKeepsValues`, `TestSubmissionReplacesAndViewsAreIndependent`, `TestRejectedSubmissions`, `TestMalformedFiles`,
`TestAtomicReplacement`, `TestPathSafety` (all in `tests/live_tests.cpp`, `hcu_live_tests`). Automated, CONFIRMED. The
format is reusable for any title that submits leaderboard views; the view semantics are not.

## 3. Offline profile, local identity and first run

What the repo shows (CONFIRMED by code reading):

- The app does not create or edit a profile. `InstallLiveServices` reads the profile the SDK already provides:
  `kernel->user_profile()` gives `xuid()` and `name()`, which become the `LocalUser` for stats. The stats file is keyed by that
  XUID. The sample XUID in `docs/offline-live.md` and `tests/live_tests.cpp` (`B13EBABEBABEBABE`) is a fixture value; whether
  it is the SDK's default local profile ID is LIKELY but not proven by anything in this repo (UNCERTAIN). Treat any profile ID
  or gamertag as HARD_CORPS_SPECIFIC test data, not a constant to reuse.
- Because the XUID comes from the SDK at runtime, scores and achievements follow whatever profile the runtime presents. The
  stats file allows up to 8 users, so a profile ID change would create a second entry rather than lose the first. That is
  reasoning from the format limits, not a tested scenario.
- Achievements are written by the SDK's achievement store to `<user data>/achievements/<TITLEID>.toml`
  (`docs/offline-live.md`), i.e. not per XUID in the file name as seen in the docs. The port does not own that file.
- "First-run profile" fixes (a profile file, a gamertag prompt, XAM profile settings) are **not** present in this project.
  Any statement about `NO_VALUE` title-settings behavior of the SDK cannot be sourced here: no hook, patch, doc or test in this
  repo mentions it (`grep` for the term over `src/`, `docs/`, `patches/`, `.github/` finds nothing), and no patch filename among the 34 in
  `patches/rexglue-sdk` names a profile or settings change (inventory in [01-rexglue-core.md](01-rexglue-core.md)). For another
  title that reads profile settings or title settings, treat the SDK's behavior when a setting has no value as a generic
  investigation point and verify it with the target game; do not assume it is a no-op.

Porting: inspect-first. Check what your title does when the profile has no gamertag, no settings blob or no signed-in user,
and whether the SDK in your version supplies a default. This project avoided the question because the title never needed more
than a signed-in local user.

## 4. License behavior

Hard Corps handles the license with one configuration value, not a hook:

- `config/hard_corps_uprising.toml` (shipped next to the executable) sets `license_mask = 1`. The comment in the file says it is
  the "full (purchased) license for `XamContentGetLicenseMask`; the SDK default 0 means trial", and `docs/rexglue-feasibility.md`
  states the SDK function returns this cvar. Commit `0b6b5a9` (2026-10-10) introduced it with the codegen setup, before any
  launcher existed.
- The title package carries one license entry with an unrestricted ID and license bits `0x00000001` (`docs/package-analysis.md`),
  so `1` equals what the retail package grants. That is the only evidence for choosing `1`; there is no recorded run in the
  repo that shows a trial screen with `0` (UNCERTAIN why it was needed; CONFIRMED that the value is set and shipped). The game
  also contains trial-related GUI assets (`1280x720_OnlineNoticeTrial.bin` in the package analysis), which is the likely reason
  to prefer a full license.
- DLC: the package reader collects license bits from the package header's license entries that have flags set
  (`stfs::Package::license_mask`) and the DLC installer appends them to the content `.header` it writes
  (`WriteContentHeader`), the same way the SDK's content manager expects. See [03-launcher.md](03-launcher.md) section 3.4.
  `ListInstalledDlc` reads the mask back (`DlcInfo::license_mask`).

Porting: **do not assume a universal mask.** Read the target package's license entries (a package analysis step), then set
the cvar to what the retail package grants and test the title's trial behavior. Whether a title checks the mask at all is
unknown until tested. Classification: the cvar mechanism is GENERIC_REXGLUE; the value is HARD_CORPS_SPECIFIC.

## 5. Achievements

- Source of the list: the XDBF inside `default.xex` (12 achievements, 200 gamerscore, strings in the language set by the
  runtime's `user_language`); icons come from the same XDBF. The game's own `XUserWriteAchievements` unlocks them in the SDK
  store, which saves the file and shows the stock unlock notification.
- **Achievement id 0 problem** (`f3ac390`): the game numbers achievements 0 to 11; the SDK store refuses id 0 (its startup
  log line is `Achievement store: refusing achievement with ID 0`), so the first achievement ("Let's do this thing right!", 5 G)
  could never unlock. Fix: `RegisterZeroAchievementAlias` re-registers it under alias id `0x10000` with the XDBF metadata at
  startup, and the `XUserWriteAchievements` hook rewrites entries that name 0 to the alias just before calling the original and
  restores them afterwards (`AliasZeroAchievementIds` / `RestoreZeroAchievementIds`, bounded to 64 entries). The list order
  maps the alias back to number 0 (`GameAchievementNumber`, `ListAchievements`). The SDK's warning line stays because it is the
  SDK's. Tests: `TestZeroAchievementAlias`, `TestLauncherAchievementList`. Classification: the id-0 refusal is a generic SDK
  behavior (GENERIC_REXGLUE finding, LIKELY relevant to any title that uses id 0); the alias values and the hook address are
  HARD_CORPS_SPECIFIC. Only an SDK change could remove the warning.
- **In-game overlay** (`AchievementsOverlay`, a `ModalDialog`): opened from the game's Achievements menu item (hook above), the
  pause menu or **F7**; F7 works through ReXApp's `CreateAchievementsOverlay` virtual which this app overrides to toggle its
  own overlay (ownership is the app's, not ReXApp's). It lists unlocked count and gamerscore, a scrollable list (D-pad, stick,
  Up/Down), close with B, Escape or F7, an X button and a B Back footer (`274ee61`). Window size ~70% of the viewport, no
  larger than 560x460, from `FitWindowExtent` in `src/overlay_rules.h`. It uses the SDK's `AchievementIconCache`.
  The modal base consumes input until what was pressed is released so B does not also back out of the game's menu (fixed in
  the same family of commits, v0.3.2 release notes). Tests: `TestOverlayRules`, `TestCloseGate`, `TestModalOwnership`.
- **Launcher viewer** (`LauncherAchievements`): the SDK store needs the title's XDBF, which is only available through a runtime.
  The launcher therefore builds a throwaway `rex::Runtime` in tool mode with no graphics, audio or input, reads the list and the
  unlock file, and releases the runtime before Play builds the real one (`StartGame` resets it). It is read-only. The class
  tears down icons before the runtime (`~Runtime`) because icons are textures that read the XDBF. REUSABLE pattern for any
  title with an XDBF; cost: constructing a runtime to render a list (UNCERTAIN on load time; not measured in the repo).
- Persistence and migration: the SDK's store file lives in the user data root and moves with it
  ([10](10-user-data-settings-migration.md)).

## 6. Download content (Marketplace) note

The menu item calls `XamShowMarketplaceUI` and does not wait. Before the call the game announces `XN_SYS_UI` (opened) to its own
listeners, which clears an engine event; the console's closed notification sets it again. The SDK's stub never sent the closed
notification, so the engine stayed held and the game slowed to a crawl. The port returns success at once, opens the note
("The Xbox Live Marketplace is not part of the PC version. Additional characters are installed from the PC launcher...") and
broadcasts `XN_SYS_UI` closed (`kSystemUiNotification = 9`, data 0, `OfflineMarketplaceOutcome`, test
`TestMarketplaceCompletes`). Classification, kept apart:
- Generic investigation point (`GENERIC_REXGLUE`, observed behavior, CONFIRMED for this SDK version): the SDK's
  Marketplace stub does not emulate the whole system-UI lifecycle (it never sent the closed notification). Whether that
  matters depends on the title.
- Need for a workaround (`GAME_ADAPTATION`): synthesizing the close notification is required only if the target title
  waits on an opened/closed pair. Hard Corps does; other titles are unproven.
- Hard Corps specifics (`HARD_CORPS_SPECIFIC`): the menu hook (`sub_82570B70`), the notification wiring and constants,
  `marketplace_rules.h` and the local notice text.
Cross-project instruction: inspect how the new title handles `XamShowMarketplaceUI` and other system-UI stubs, determine
whether it waits for an opened and closed notification lifecycle, and reproduce this completion behavior only if it does.

## 7. Initialization and lifetime

- `hcu::live::Install` runs from `OnPostSetup`; it registers the alias, constructs a `Services` object holding the stats store,
  leaderboard view and enumerator registry, and loads the file. `Services` is allocated and never freed on purpose: hooks may
  still be running when the process exits. Global state is `std::atomic` pointers and a thread-local scope counter.
- Overlays are created on the UI thread (`CallInUIThreadDeferred`) because the hooks run on guest threads; the `open_*`
  callbacks in `AppBridge` carry that.
- `OnShutdown` deletes the open overlays before the window goes away.

## 8. Limitations and pitfalls

- No online anything; boards contain the local player only; no friends; at most 8 users and 64 views per user in the file.
- Only the Arcade single-player board has been played; the other three are inferred.
- A score that cannot be written is kept for the session only.
- The startup SDK warning about achievement id 0 is expected.
- The Live scope hooks depend on exact guest addresses of one game version (`default.xex` CRC pinned, see
  [03](03-launcher.md) section 3.1); a different build needs a re-derivation.
- Pitfall: when the profile is missing at `Install` time everything in this document silently turns off.
- Do not make the console appear signed in globally; online-only code paths in the title then try to use the network.

## 9. How to verify

Automated: `cmake --build <dir> --target hcu_live_tests` then run `hcu_live_tests` (stats store, enumerators, buffers, id 0
alias, Marketplace outcome, overlay and close rules, launcher list, Live-gate source check, modal ownership). No game files are
needed. Manual (per `docs/offline-live.md`): finish an Arcade single-player run, open LEADERBOARDS from the main menu and the
pause menu, unlock an achievement, press F7, choose Download Content. Pending/unproven: boards 1, 2, 4.

## 10. Porting summary

| Item | Verdict |
| --- | --- |
| `LocalStats` format and atomic save | copy |
| Enumerator registry, buffer writers, overlapped completion | copy and re-verify struct sizes against your title's XDK |
| Narrow Live scope pattern and its source-level test | adapt (addresses and menu list differ) |
| Achievement id 0 alias | adapt only if the title uses id 0 |
| XamShowMarketplaceUI close notification | inspect first; reproduce only if the title waits on an opened/closed lifecycle (`GAME_ADAPTATION`) |
| Overlay base and close gate | copy |
| Launcher achievements viewer | adapt (needs XDBF) |
| `license_mask` value and profile data | inspect first, do not copy |
| Views, columns, addresses, strings | HARD_CORPS_SPECIFIC |

Commits: `f3ac390` (all of the above), `274ee61` (overlay close button, footer), `0b6b5a9` (`license_mask`), `27e26ea`
(storage location).
