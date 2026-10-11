# Offline Xbox Live services

The game's achievements, leaderboards and download content entries work offline for the one local player. Nothing is sent anywhere: there is no Xbox Live connection, no Microsoft sign-in, no remote player and no online multiplayer. The port keeps the player's scores in a local file and answers the game's Xbox Live calls from it.

## What the game uses

The game calls the Xbox services directly (the XAM imports) and through the statically linked Live library. The parts that matter here:

| Purpose | Game call | Handled by |
|---|---|---|
| Sign-in state | `XamUserGetSigninState`, behind the game's own `IsLive` check | the port, only where scores and leaderboards run |
| Stats session | `XSessionCreate`, `JoinLocal`, `Start`, `End`, `Delete` | ReXGlue (accepts them) |
| Stats write | `XSessionWriteStats` | the port records it |
| Stats read | `XUserReadStats` | the port answers from the local file |
| Board reads | `XamUserCreateStatsEnumerator` (by rank, by XUID), `XamEnumerate` | the port |
| Friends | the Live library's friends enumerator | the port: an enumerator with no friends |
| Achievements | `XUserWriteAchievements` | ReXGlue, plus the port's achievement 0 alias |
| Achievements screen | `XamShowAchievementsUI` | the port opens its achievements overlay |
| Download content | `XamShowMarketplaceUI` | the port shows a note |

The game has the network imports `NetDll_*` (sockets and XNet), but none of the paths below uses them. Only the leaderboard screen, the main menu's LEADERBOARDS item and the score submission see the Live state; the online items of the main menu need the multiplayer privilege, which the runtime denies, so they stay greyed.

## Scores and leaderboards

The game keeps no scores in its save: it submits each finished game to a leaderboard view with `XSessionWriteStats` and reads the boards back from Live. The port keeps what the game submits per user and view, and the game stays the judge: before it writes, it reads the stored result with `XUserReadStats` and compares, so only a result that beats it is submitted. A view the player never played has no row, which is what makes the first result count.

A submission is the view's rating (the last 64-bit property, which ranks the player) and its columns (`col_<id>`, the low 16 bits of the property id). The rating is also a column, so a read that asks for column 1 gets it. The file is `<user_data_root>/58410A18/live/stats.toml`:

```toml
version = 1

[users.B13EBABEBABEBABE.view_3]
rating = 123456
col_1 = [1, 2]
```

A column is `[type, value]`: type 1 int32, 2 int64, 3 double, 5 float; floating values are stored as their raw bits. The file is capped at 1 MiB and 8 users, 64 views per user and 16 columns per view. It is written as `stats.toml.tmp`, flushed and renamed over the original, so an interrupted save leaves the previous file. A file that cannot be parsed is moved to `stats.toml.corrupt` and the scores start empty. Deleting `<user_data_root>/58410A18/live` removes every score.

| Screen | Answer |
|---|---|
| All, and My Score (by rank, by XUID) | the player's row at rank 1 when the view has a result, else no rows |
| Friends | the friends list is empty; the player's own row is added by the game and read back from the stats |

The handles the port gives out for these enumerations start at `0xEC000000`, away from the runtime's `0xF8xxxxxx`, are used once and then dropped, and at most 64 are kept unserved. The game's calls are synchronous (no `XOVERLAPPED`); with one, the result is written into it and the call returns `ERROR_IO_PENDING`.

## Boards

The port answers for the view ids the game asks about and does not depend on their labels. A submission writes the int32 columns `0x10000002` and `0x10000003` and the int64 rating `0x20000001`; the screen reads columns 1 and 2, so it shows the rating and the first int32. Higher is better on every board. The tabs are Friend (the friends list, then the stats of those users), My Score (around the player's XUID) and All (by rank).

| View id | Board | Status |
|---|---|---|
| 3 | Arcade Mode, single player | confirmed in a played run: submitted, stored and shown at rank 1 |
| 4 | Arcade Mode, two players | inferred from the game's code, not played |
| 2 | Rising Mode, single player | inferred from the game's code, not played |
| 1 | Rising Mode, two players | inferred from the game's code, not played |

The game submits by itself when a run ends; the port never triggers a submission. Its calls reach the port through the game's own write job (`sub_82545728`), which creates, joins, starts, ends and deletes a stats session before `XSessionWriteStats`. ReXGlue accepts those session calls locally, so no real session or connection exists. The job only runs for a player the game believes to be signed in to Live, which is why it runs inside the scope described below.

## Sign-in

The main menu greys LEADERBOARDS unless `IsLive` is true and shows "You must sign in to Xbox LIVE" when it is chosen anyway; the pause menu does the same for its Leaderboards item (index 3), through its own `IsLive` calls when it is built and while it is open. The port tells the main menu to enable the item, and opens a scope around the pause menu's build and update, the leaderboard screen's update and the score submission in which user 0 reads as Live. Both menus lead to the same ranking screen. Everywhere else the player stays signed in locally, as ReXGlue reports. The class that opens the game's UDP socket for online play (`sub_82541A38`) reads the sign-in state and the multiplayer privilege itself, outside this scope, so it still sees a local sign-in and a denied privilege.

## Achievements

The 12 achievements (200 gamerscore) come from the XDBF inside `default.xex`: names, descriptions, gamerscore and icons, in the language ReXGlue is set to. The game's `XUserWriteAchievements` unlocks them in ReXGlue's store, which saves `<user_data_root>/achievements/58410A18.toml` and raises the stock unlock notification.

The game numbers its achievements 0 to 11 and ReXGlue's store refuses ID 0; its startup line `Achievement store: refusing achievement with ID 0` is that refusal, and achievement 0 ("Let's do this thing right!", 5G) could never unlock. The port registers it under the alias `0x10000` with its own metadata and swaps the alias in while the game's write is delivered. The warning line itself is the SDK's and stays.

The launcher's Achievements page lists the same store. The store needs the game's XDBF, so the page loads `default.xex` in a runtime with no graphics, audio or input, reads the list and the unlock state file, and drops that runtime before Play builds the game's own. It is read-only.

Choosing Achievements in the main menu or the pause menu, or pressing **F7**, opens the overlay: the achievements in a window centred on the screen, about 70% of the viewport and no larger than 560 by 460, with the unlocked count and gamerscore, a scrollable list (D-pad, left stick or Up/Down) and a B Back footer. B, Escape and F7 close it.

## Download content

The menu item calls `XamShowMarketplaceUI` without waiting for anything. Before the call the game announces the system UI to its listeners (`XN_SYS_UI`, opened), which clears an engine event; the console's closed notification sets it again. ReXGlue's stub never sent that, so the event stayed clear and the game crawled. The port answers at once with success, shows a note that extra characters are installed from the launcher's DLC page, and sends `XN_SYS_UI` closed.

## Input

The overlays (cheat menu, achievements, download content note) share one base. At most one is open. While it is, and until what was pressed in it is released, the game reads neutral input, so B or Escape closing an overlay never also backs out of the game's menu. A: OK, B or Escape: close.

## Known limitations

- The startup line `Achievement store: refusing achievement with ID 0` comes from ReXGlue's store when it loads the title's list. It has no effect: achievement 0 is registered under the alias right after and unlocks normally. It could only be removed by changing the SDK.
- Only the Arcade single-player board has been played. The other three views are mapped from the game's code.
- There are no global rankings, other players or friends; the boards hold the local player alone.
- A score that cannot be saved to disk is kept until the game exits, and a warning is logged.
- Download Content shows a note only; there is no Marketplace.

## Tests

`cmake --build build --target hcu_live_tests`, then `build\hcu_live_tests.exe`: the stats file (saving, restart, corruption, limits, interrupted saves), the enumerator and read answers with their guest buffers, the achievement 0 alias, the Marketplace outcome, the overlay and close rules, the launcher's achievement list, and source checks that the Live scope stays narrow and that overlays cannot overlap. No game files are needed.
