# Input

## Path

```
Xbox controller (SDL)  -> ReXGlue InputSystem, user 0 -> XamInputGetState -> sub_82B71170 -> game
Keyboard (mnk_mode)    -> synthetic device, user 0 ---^
```

- SDL driver (`rexglue-sdk/src/input/sdl/sdl_input_driver.cpp`) maps SDL buttons to XINPUT bits through a lookup table in `SDL_GamepadButton` order.
- Slot: connection order 0 becomes guest user 0 (`SlotAssignment`). The keyboard device is synthetic. It targets user 0 but takes no ordinal, so it cannot displace the pad.
- Merge: `InputSystem::GetState` ORs the buttons and keeps the larger value of each stick axis and trigger. The pad stays active with `mnk_mode` on.
- Nothing in Hard Corps touches SDL or XAM. The only Hard Corps code on the path is the temporary diagnostic below.

## Xbox controller

| Control | SDL source | XINPUT bit |
|---|---|---|
| Menu / Start | `SDL_GAMEPAD_BUTTON_START` (6) | `0x0010` `X_INPUT_GAMEPAD_START` |
| Back | `BACK` (4) | `0x0020` |
| A / B / X / Y | `SOUTH` / `EAST` / `WEST` / `NORTH` | `0x1000` / `0x2000` / `0x4000` / `0x8000` |
| D-pad | `DPAD_UP/DOWN/LEFT/RIGHT` | `0x0001` / `0x0002` / `0x0004` / `0x0008` |
| LB / RB | `LEFT/RIGHT_SHOULDER` | `0x0100` / `0x0200` |
| Stick clicks | `LEFT/RIGHT_STICK` | `0x0040` / `0x0080` |
| LT / RT | analog axes (`>> 7`) | byte at state+6 / state+7 |
| Left / right stick | analog axes, Y inverted | X at state+8 / state+12, Y at state+10 / state+14 |

Standard Xbox 360 semantics, no remapping. Guide is ignored (`guide_button` is off).

## Keyboard

Plain arrow keys are the right stick, not menu navigation. Menus use WASD and Shift + Arrow.

| Pad control | Keys |
|---|---|
| Start | Enter (Return) or X |
| Back | Z or Tab |
| Left stick | W, A, S, D |
| D-pad | Shift + Up, Down, Left, Right |
| Right stick | Up, Down, Left, Right (plain arrows) |
| A | Space or Semicolon |
| B | Backspace or Quote |
| X | L |
| Y | P |
| LB / RB | 1 / 3 |
| LT / RT | Q or I / E or O |
| Left / right stick click | F / K |

These are ReXGlue's default `keybind_*` values, the same as Earthworm Jim HD.

## Guest state the game reads

The input consumer is `sub_826692F0`. It reads the state the wrapper filled:

- buttons: u16 big-endian at state+4. START is `& 0x0010`.
- LT / RT: bytes at state+6 and state+7. Any non-zero value counts as pressed. The game maps them to internal bits `0x0400` and `0x0800`.
- sticks: s16 at state+8 (LX), +10 (LY), +12 (RX), +14 (RY).
- The packet number (state+0) is not read there. The connection check (`sub_82669120`, `sub_82669728`) reads only the return code.

In every consumer traced here the packet number is never read, so the keyboard's per-poll `packet_number` increments do not change what those paths see. Other readers of the state were not traced.

## Wrapper hook (`src/debug_saulo_input.cpp`)

- Original (raw image at `0x82B71170`, three instructions): `or r5,r4,r4` (`mr r5,r4`), `addi r4,r0,1` (`li r4,1`), `b 0x82BD48DC` (`XamInputGetState`). No prologue, stack frame or memory access.
- The hook is `REX_HOOK_RAW(sub_82B71170)`. It saves the user index (r3) and state pointer (r4), calls the generated original `__imp__sub_82B71170` with the same `ctx` and `base`, then reads r3 and, only when r3 is 0, the state. It writes nothing.
- Proven equivalent: the original's effects are the same call with the same arguments. The hook changes no argument, return value, state byte or user index. The only addition is host-side logging.
- It replaces the generated weak symbol, the same mechanism as Earthworm's `sub_82BEFD18` hook. The three direct call sites and the function table all reach it.

## Keyboard listener

- `KeyboardLog` is a `WindowInputListener` at z-order 1000, attached in `OnPostSetup` (`src/hard_corps_uprising_app.h`).
- It never sets `handled`, so the event still reaches MnK at z-order 0 and the game. Auto-repeat is skipped.

## Diagnostic lines

Only changes are logged.

```
[DEBUG_SAULO][Input] keyboard Return down
[DEBUG_SAULO][Input] user 0 connected
[DEBUG_SAULO][Input] user 1 not connected, result 0x48F
[DEBUG_SAULO][Input] user 0 START pressed buttons 0x0010
[DEBUG_SAULO][Input] user 0 LS (1,0)
```

- `result 0x0` is connected. `0x48F` is not connected.
- Sticks print -1, 0 or 1 past a deadzone of 7849.

## `hid_mappings_file = ""`

Kept. Verified in `sdl_input_driver.cpp:99`: an empty value skips the only load of `gamecontrollerdb.txt`. Nothing else reads the cvar, and SDL's built-in mappings are unchanged. The warning for a missing file would read as an input fault during the test, which is why the empty value stays. Delete the line to restore the warning. No database is shipped or downloaded.

## Test order

Controller first:

1. Wait for "Press START Button".
2. Press Xbox Menu/Start once. Confirm the title advances.
3. Navigate with the D-pad.
4. Navigate with the left stick.
5. A confirms. B backs out.
6. Start a game and reach gameplay.
7. Move, then press A, B, X, Y, LB, RB, LT and RT.

Then keyboard, controller still connected:

8. Enter (or X) for Start.
9. WASD for navigation and movement.
10. Shift + Arrow for the D-pad.
11. Space for A.
12. Backspace for B.
13. L for X.
14. P for Y.

## Reading the log

- A host key line followed by a `[DEBUG_SAULO][Input] user 0 ...` line means the guest received the input.
- Pad presses show only as guest lines. The host SDL layer lies inside the prebuilt SDK, so Hard Corps can't observe it directly.
- A press with no guest line means the input never reached the guest. Investigate SDL or the merge.
- A guest line without a game reaction means the input arrived and the title logic ignored it. Investigate the game's title code.
- Any `DEBUG_SAULO` line appearing while the game behaves differently means the diagnostic itself is at fault. Remove it.

Remove the diagnostic at final review: delete `src/debug_saulo_input.cpp` and `src/debug_saulo_input.h`, the `OnPostSetup` override and include in `src/hard_corps_uprising_app.h`, and the source entry in `CMakeLists.txt`.
