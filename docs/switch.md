# Nintendo Switch build

Experimental. A libnx NRO built from the same patched ReXGlue v0.10.0 as the PC port, with the Switch
host layer of cvhd-recomp (our Castlevania: Harmony of Despair port). The PC build is not changed:
`CMakeLists.txt` includes `switch/switch.cmake` only when `NINTENDO_SWITCH` is set.

## Layout (WSL on the build PC)

| What | Where |
| --- | --- |
| Sources | `/root/hcu/src`, rsync'd from this checkout by `switch/build.sh` |
| SDK | `/root/hcu/rexglue-sdk`: v0.10.0 (`c94f5eb`), `patches/rexglue-sdk` common and `.linux` patches (committed there as one local commit), then `switch/sdk-edits.py` |
| Host CLI | `/root/hcu/rexglue-sdk/out/linux-amd64/Release/rexglue` (`cmake --preset linux-amd64`, clang-20, target `rexglue`). It embeds the codegen templates: rebuild it after a template edit |
| Game files | `/root/hcu/src/game/extracted` (the user's own package; never committed) |
| NVK | `/root/cbfd/nvk-sdk-real/lib/libvulkan.a` (mesa-switch) |
| ELF per build | `/root/hcu/elf/hcu-<version>.elf` (addr2line for crash and sampler addresses) |

`.windows` patches are skipped. `.linux` patches are mostly the Vulkan backend, which the Switch uses.

## Steps

1. `switch/build.sh`: rsync, `sdk-edits.py`, codegen when `generated/default` is missing, configure, build.
   Writes `out/build/switch/hcu.nro` and prints the NACP (`"Hard Corps: Uprising" by hcu-switch, version
   0.<commits>.<sha7>`).
2. Eden first: `powershell -File switch/eden-run.ps1 -Minutes 3` (no-GPU mode, file `sdmc:/switch/hcu/nogpu`
   in Eden's sdmc; Eden cannot run NVK). Pass = 60 or more `[nx] gpu: swap #N`. Logs in
   `E:\conker-data\eden-runs\hcu-<version>\`.
3. Console: `switch/deploy.sh 192.168.50.69` uploads `hcu.nro.part`, compares SHA-1, renames to `hcu.nro`.
   Game data once: `game/extracted/` to `sdmc:/switch/hcu/game/` (lftp mirror with temp files).

On the console: hbmenu entry "Hard Corps: Uprising" by hcu-switch, launched with title takeover (hold R
while starting any game). Logs: `sdmc:/switch/hcu/hcu_log_<build id>.txt`, one per build (`[nx]` lines: memory, clocks, fps, cpu,
pc sampler, gpu waits) and `hcu_rex_log.txt` (the SDK log). `sdmc:/switch/hcu/nogpu` selects the null GPU
backend, `nopin` turns thread placement off.

## Switch-specific changes

- `switch/host/`, `switch/sdk-edits.py`: from cvhd-recomp (memory blocks and software fold, dirty-page GPU
  write tracking, fibers, threads, NVK `VK_NN_vi_surface`, null GPU backend, libnx pad, SD log).
- `sdk-edits.py` end section: HCU patch fixes for GCC/Horizon (0006 `yield`, 0009 no futex, 0021
  WaitMultiple on the shared condition variable, 0003/0025 WAIT_REG_MEM statistics).
- `switch/host/include/dlfcn.h`: `RTLD_NOLOAD` for 0010.
- `src/crt_native.cpp`: the native memcpy/memset skip physical memory on the Switch (its stores must mark
  dirty pages).
- `src/gpu_fence_wait.cpp` is built but inactive on the Switch (Windows/Linux only).
- `switch/hcu_nx_main.cpp`: plain ReXApp, no PC launcher, cheats or offline Live; `license_mask=1` and the
  853x480 video mode are passed by `windowed_app_main_nx.cpp`.

## Results

| Build | Where | Result |
| --- | --- | --- |
| 0.89.e9aca07 | Eden, null GPU, 3 min | PASS: 3300 swaps; 60 fps at first, 14-20 fps later (Eden's JIT speed). 4 missing xam ordinals (2815, 2816, 2827, 2832), as in Castlevania |
| 0.92-0.95 | Eden, deko3d | `std::terminate` at the first missing shader (`Shader::DumpUcode`: `absolute()` of `sdmc:/`). cvhd-recomp 383b501 writes the dumps with stdio |
| 0.100.4a42f50 | Eden, deko3d, 120 s | runs, 12-23 fps; 11 missing shaders dumped (all 11 translate with xenos2glsl + uam) |
| 0.102.a1ec3d6 | Eden, deko3d, 150 s | **first picture**: the intro movie (police car scene) at 12-15 fps in Eden; 0 missing shaders, 2 draws per frame |

| 0.106.a08f9a3 | Eden, deko3d | the intro plays to its end, then the game hangs: main thread in a condition-variable wait, the Audio Worker gone (a stale "audio worker leaves" SDK insert from an older sdk-edits.py next to the new nop audio driver). build.sh now resets the SDK when the edit scripts change |
| 0.111.dd75cc2 | Eden, deko3d, 210 s, pad.txt START/A | **past the intro**: title screen, "No save data found" dialog, CHARACTER SELECT at **60 fps** in Eden, 113 draws per frame; 2 more shaders dumped |

| 0.113.d9cc178 | Eden, deko3d, 300 s | START/A to the first stage: the HUD draws, the scene is grey; 32 stage shaders dumped |
| 0.116.bc9e4d8 | Eden, deko3d, 320 s | 45 of 45 shaders (xenos2glsl: `maxas` and `c[N+a0]` added); stage: HUD, 1P marker and a light-column effect draw, the level and the player do not. 235 draws, 31 resolves, 10 clears per frame, 0 skipped, 0 without shader; `unsupported texture format k_24_8` (a depth texture sampled) |

| 0.116.bc9e4d8 | console | pushed (NRO SHA-1 899d5ebd, 73 DKSH, `deko3d` marker); replaces the NVK 0.89, which died with `out of host memory` in NVK and `std::bad_alloc` |
| 0.119.223193d | Eden, deko3d | resolves clear depth now (`resolve clears depth at EDRAM 720 pitch 1280 ... to 1`); the stage still grey |
| 0.120.793fc06 | Eden, deko3d, `dk_nodepth` | with the depth test off the level appears: depth is the cause |
| 0.122.458c237 | Eden, deko3d, 280 s | **first stage renders** (rocks, wreck, airships, depth order right), 30 fps in Eden. Cause: `CompareOp` passed the Xenos compare function through, but Xenos counts from kNever = 0 and deko3d from Never = 1 (LEQUAL was EQUAL). The player sprite looks missing (only a red shape) |

| cloud, Linux | deko3d backend over OpenGL (`docs/LINUX-TEST-HOST.md`) | stage 1, the menus, the pause menu and the character select match the SDK's Vulkan picture. Fixes: start vertex (`VGT_INDX_OFFSET`), depth resolves (shadow maps), stencil, resolve formats. See `docs/PROGRESS-cloud.md`; Eden checks in `docs/EDEN-CHECKLIST.md` |

NVK builds are not run on the console (user decision, 2026-10-23): the renderer is deko3d, Eden is the gate.

## deko3d renderer

The backend is cvhd-recomp's (`src/gpu/deko3d_backend.cpp`, docs/native-renderer-plan.md section 8 there),
shared through `switch/sync-cvhd.sh` (now at 7a52b68; `switch/cvhd-local.patch` holds the HCU changes, regenerate it with
`bash switch/make-local-patch.sh`; the generic fixes are in `docs/for-cvhd-gh/`). The file `sdmc:/switch/hcu/deko3d` selects it. Shaders: the backend
dumps the ucode of each shader without a DKSH into `sdmc:/switch/hcu/shaders/missing/`;
`switch/fetch-missing-shaders.sh <console IP>` (or `switch/eden-deko3d.ps1` for Eden) copies them to `/root/hcu/shaders_trace/`,
and the next `switch/build-base.sh` (or `switch/build.sh`) translates them (`switch/stage-shaders.sh`: `tools/xenos2glsl.py`,
uam) into `<build dir>/stage/shaders/`. `switch/build-base.sh` also stages every pixel shader of the game files and the XEX
image (`tools/shader_scan.py`); vertex shaders come only from dumps (the D3D runtime binds them to each vertex declaration).

Eden: `powershell -File switch/eden-deko3d.ps1 -Seconds 150 -Every 25 [-Keys "160:M"]` (Eden keys: C = A,
X = B, V = X, Z = Y, M = +, N = -, WASD = left stick, arrows = d-pad). Screenshots and logs in
`E:\conker-data\eden-runs\hcu-<version>-deko3d[-tag]\`.

## Player features (0.243 on)

- **Switch button icons (BUTTON ICONS, `button_glyphs` in `settings.txt`, default SWITCH).** The pad maps by
  position (`switch/host/input/nx_input_driver.cpp`: Switch B = Xbox A at the bottom). The game's 19 prompt images
  `Gui/Img/guidebutton_<NAME>.bin` (32x32, 8-bit paletted, Xenos tiled) are replaced by our own icons: the solid
  style of the switch-glyphs set (`tools/make_glyphs.py` -> `src/button_glyphs.h`, no Nintendo file). The SDK's
  `HostPathFile::ReadSync` calls `src/button_glyphs.cpp`, which writes the icon over the file's bytes (same size).
  By position: A -> B, B -> A, X -> Y, Y -> X, LB/RB -> L/R, LT/RT -> ZL/ZR, START/BACK -> +/-, the d-pad and
  stick icons -> the Switch ones. XBOX keeps the game's icons with A<->B and X<->Y swapped in the SDK's
  `VirtualFileSystem::OpenFile`. Not replaced: the How to play controller drawing (`htp_cntrl_00.bin`), the Guide
  ring of the player name window (`gmtg_cntrl_*.bin`) and the baked "Press START" / "Xbox 360 controller" texts.
- **Rumble.** `SetDeviceVibration` sends the 360's two motors to HD rumble (left = 160 Hz band, right = 320 Hz
  band) on handheld, a Joy-Con pair or a Pro Controller.
- **Settings menu.** Both stick clicks (L3 + R3) open and close it (`src/settings_menu.cpp`, drawn by the
  deko3d backend with the strip's 5x7 font). Up/down, left/right or A to change, B to close; closing saves
  `sdmc:/switch/hcu/settings.txt`. The game keeps running behind it. Lines: frame rate (30 / 45 / 60; the old
  `fps_mode=3`, 30 half speed, is removed and loads as 30), performance strip, rumble, shadow bias, colour bias, shadow map, view (live), precise
  shaders (next start). VIEW shows the frame's scene copy before the post effects, or the shadow map (the depth
  texture the receivers sample), instead of the front buffer: which one has the console hatching names the pass. There is no texture
  filter line any more: forced linear filtering blends the 8-bit palette indices of the sprites (wrong colours,
  green specks at their edges in 0.243-0.246); the file `sfix` still forces it for A/B tests. Each change is a log line `[nx] menu: ...`; each new bias value logs its first
  draw. SHADOW MAP OFF leaves out the shadow pass (an A/B of the console rings). The marker files `fps`,
  `overlay_off`, `norumble`, `zfix`, `sfix` still work and win over `settings.txt` (console A/B tests).
- **Exit.** HOME -> Close and the game's own quit end the process at once (`rex_nx_quick_exit` in
  `switch/host/switch_nx.cpp`): the log is written and the clocks go back, the runtime teardown is not run.
  On the ultimate base (0.402) the game's quit waits up to 2 s for the guest threads (`XThread::StopTitleThreads`);
  if one still runs, it ends through `rex_nx_quick_exit` in `switch/hcu_nx_base.cpp` (switch/sdk-edits-base.py).
  Before, a bare `std::_Exit` went back to hbloader with live threads, and hbloader aborted (2347-0024, 0.400).
- **GPU queue errors.** When deko3d puts the queue in its error state (a fault in an earlier list), the backend logs
  the swap, the live settings and each nvdrv channel's error notification (type 31 = MMU fault, 8/24 = timeouts,
  25 = illegal method), then ends the process the same way, instead of the deko3d abort.

## Build on the ultimate base's SDK (branch renut-ultimate)

The branch `renut-ultimate` builds the same game on the SDK of
[renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base) (GPL-3.0, used with the author's
permission). The branch `renut-base` keeps the old build above as the fallback.

| What | Where |
| --- | --- |
| Sources | `/root/renut-hcu/src`, rsync'd by `switch/build-base.sh` |
| SDK | `/root/renut-ult/sdk`: the base's SDK, then `switch/sdk-edits-base.py` (committed there) |
| Base tools | `/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base`: the toolchain file, `tools/store_cards.py`, `tools/pgo_summary.py` |
| Build folder | `/root/renut-hcu/src/out/build/switch-base` (`HCU_BUILD_DIR` for another) |
| PGO profile | `/root/renut-hcu/pgo` (`HCU_PGO_DIR`) |

- `switch/build-base.sh [cmake -D...]`: rsync, SDK edits (the build stops if they change the SDK), codegen and the
  store cards when the manifest changed, configure with the base's toolchain file, build. Under
  `D:\git\dev\renut-ref\build.lock`.
- The base owns `main`, threads, memory and the exit. `switch/hcu_nx_base.cpp` adds the SD log, `args.txt`, the
  console defaults of the cvars (`--wrap=main`), the quick exit and the clocks: `clocks=1` in `settings.txt` sets the
  memory to 1600 MHz and the CPU / GPU to the governor's first rung (below) through clkrst at start, sets them again
  when the system lowers them (checked every 2 s, then 4, 8 .. 32 s after each re-set; a rate at or above the game's
  is never set again), and puts the old rates back on exit. Default 0: the game touches no clock. The base itself
  never sets clocks (up to 0.398 this build had no clock code: `clocks=1` did nothing). With `clocks=1` a governor
  runs by default (`clocks_auto=0` turns it off and keeps CPU 1581 / GPU 384; `clocks_cpu` / `clocks_gpu` or
  `clocks_ladder` turn it off too, unless `clocks_auto=1`).
- The governor's ladder (`switch/clock_ladders.h`) follows the frame-rate mode and the console mode. The first rung
  is the start and the floor; the memory stays 1600 MHz on every rung. CPU/GPU in MHz:

  | Console, FRAME RATE | Rungs (first = start and floor) | Source of the first rung |
  | --- | --- | --- |
  | Handheld, 30 | 1122/384, 1224/384, 1428/384, 1581/384, 1683/460.8, 1785/768 | the user's guess, not measured |
  | Handheld, 45 | 1326/460.8, 1428/460.8, 1581/460.8, 1683/460.8, 1785/768 | the user's guess, not measured |
  | Handheld, 60 | 1683/691.2, 1785/768, 1785/844.8, 1785/921.6 | the user's choice; by hand, stock CPU with GPU 690 held 60 fps |
  | Docked, every mode | 1683/844.8, 1785/844.8, 1785/921.6 | the user's guess ("1700 and 800"), not measured |

  The rates are steps of the pcv table (sys-clk's list). 844.8 and 921.6 are asked, a read-back from a console is not
  seen yet. Erista (spl `HardwareType` 0 or 1, read once at start; a failed read
  counts as not Erista): the game holds the GPU at 460.8 MHz on every rung of every ladder, handheld and docked, and
  also lowers a higher rate to it (`hcu_clock::for_soc`; log `[clock] erista, GPU held at 460.8 MHz`). The CPU steps
  stay. A module sys-clk sets and `clocks=0` are still left alone.
- The governor runs every 2 s. The ladder is picked at start from `settings.txt` `fps_mode` and the console mode
  (`appletGetOperationMode`). The menu's FRAME RATE (in level play) and a dock or undock (at any time) switch the
  ladder live: the new ladder starts on its first rung, and that sample and the next one are dropped (the backend
  takes a new FRAME RATE within 60 swaps). A module sys-clk hands back is taken at the current rung. The speed: the level swaps (the
  `[speed]` meter's counts) against 60 guest frames a second;
  in 45 and 60 also the drawn level swaps against 45 or 60, the lower of the two counts (those modes skip a frame the
  GPU is busy for, so the game speed alone stays 100 % while fewer frames are shown). In 30 the game speed alone
  counts (its 30 Hz pacing reads about 100 %). One rung up below 97 % (two below 90 %), one down after 5 samples at
  99 % or more (the wait doubles to 64 when a lower rung fails at once), never below the first rung. Log lines:
  `[clock] auto: mode 30 fps (fps_mode 0), handheld, ladder 30 (6 rungs), start rung 0 (CPU 1122, GPU 384 MHz; the floor)`
  at start and at a switch, `[clock] auto: speed ..` at each move. sys-clk's modules are left alone. The
  `clocks_gpu` and `clocks_ladder` keys still stop at GPU 768 MHz (460.8 on Erista).
- sys-clk has priority, per module (0.400, `switch/sysclk_owner.h`). The game asks sys-clk's IPC (`sys:clk`, API 4
  or 5) every 2 s: a module with a sys-clk override, title profile or global profile rate is sys-clk's, and the game
  makes no pcv call for it; a module in "Do not override" (or sys-clk disabled) the game sets. A change in the
  overlay takes effect at the next check. Horizon OC (`hoc:clk`), sys-clk-OC (`sysclkOC`), an unknown sys-clk API
  or no answer: the game touches no clock. The game holds no per-module clkrst session: each read or set opens one
  and closes it (the clkrst service session stays open from the game's first set to exit). Why: 0.399 held three
  module sessions and the service session for the whole run, and with sys-clk running pcv aborted (2011-0102, hipc
  out of session memory) at least 8 s after the raise. Log lines: `[clock] CPU: sys-clk override 1785 -> game leaves it`,
  `[clock] GPU: sys-clk Do not override -> game sets 768`.
- GPU writes: the base's store cards (one byte per 4 KB page) replace the dirty-page bytes. The deko3d backend pulls
  the cards at a texture bind, a vertex copy and a resolve (`PullCards`). The shaders are the same as the old build's:
  this build copies none, the ones in `sdmc:/switch/hcu/shaders/` are used.
- New `settings.txt` key: `audio_threads=1` runs each audio client's callback on its own thread (the old SDK's patch);
  default 0 = the base's audio worker.
- Volume balance (live; the menu's SFX VOLUME, VOICE VOLUME and MUSIC VOLUME, 0..200 % in steps of 10, stopping at
  both ends): `music_gain=0..2` scales every stereo XMA stream (BGM_xenon.bnk; a stereo sound effect would follow it
  too), `voice_gain=0..2` every mono stream the voice table knows as a voice, `sfx_gain=0..2` every other mono stream.
  Above 2 is 2, below 0 is 0; 1 = the game's own mix (the samples stay as they are). Above 1 amplifies and saturates at
  int16 full scale, so loud passages can clip. Defaults when the key is absent: `sfx_gain=0.8`, `voice_gain=0.2`,
  `music_gain=1.0`. A menu save always writes the three keys, so a `settings.txt` saved by an older build keeps its
  values (an old file has `sfx_gain=0.4`: remove the gain lines to get the new defaults). The SDK scales each decoded
  frame after the frame cache (switch/sdk-edits-base.py), so a change acts at the next frame. The guest's FMOD still
  applies its own volumes after this.
- The voice table (`src/xma_voice_table.cpp`): voices and effects are both mono 48 kHz XMA, so only the sound's first
  2048-byte XMA packet tells them apart. Before the base's main, the NRO reads every `*.bnk` (FSB4) in
  `<game_data_root>/DataXENON/Sound/` (default `sdmc:/switch/hcu/game/DataXENON/Sound/`): the sample headers and the
  first packet of each mono sample (1209 samples, 962 distinct first packets), not the sample data. A sample is a voice
  when its bank is a `Voice_*` bank or its name starts with one of the voice prefixes. Default prefixes:
  `voice_prefixes=NR_,BH_001,CL_001,HD_001,HT_001,LV_001,DWN_,LV_Enemy_,DM_,DMG_VC,TB_,BREATH,ZOMBIE,VOMIT` (read at
  start; a menu save keeps the line as it is; an empty value = only the `Voice_*` banks). Samples that share a first
  packet: a `Voice_*` bank's sample makes it a voice (SE_Common's LV_001); otherwise different classes make it sfx
  (1UP and SE_Stage05's DM_010_2 share their whole first packet and size, so DM_010_2 plays at sfx_gain). Log line:
  `[audio] voice table: 1209 mono samples of 16 banks in ..., 962 keys (653 voice, 1 voice/sfx conflicts as sfx), 14
  voice prefixes, 2.9 ms` (2.9 ms on the PC with the files cached; the console reads about 1200 small pieces of the
  card). No banks: `: no table, every mono sound is sfx`.
- The class is latched per XMA context at the sound's start, in the SDK's `XmaContext::Decode`: when PrepareDecoder
  returns 1 (a fresh codec: a new sound, `fresh`), and at the buffer's first frame with the codec kept. A fresh start
  looks its raw guest packet up: a hit takes the table's class, a miss is sfx. A kept-codec buffer start keeps the
  class on a miss (a stream's next buffer) or on the same key (a loop); a known first packet with a new key is a new
  sound the game started without a clear (`unprepared`) and takes its class. Stereo is always music. Per frame: one
  member read. Start log lines (at most 20 a second, 200 a run):
  `[audio] start class=voice hit name=SE_Common_xenon/NR_001.wav key=ad355f0fecd05f04 head=400001000649fc03 pkt=0 bit=32 fresh`.
  `name` ends in `+` when other samples share that first packet; a miss has `name=?`. `head` = the packet's first 8
  bytes, `pkt` / `bit` = the packet index and the read offset in bits: with `pkt=0 bit=32` a miss is not a sample's
  first packet (or the banks on the card differ). Many misses with `pkt` above 0 mean the start is not at the first
  packet (the table cannot work there).
- The `[audio]` line, once a second:
  `[audio] peak/rms: mono 0.912/0.104 voice 0.300/0.050 stereo 0.500/0.120 c0 0.950/0.210 c1 0.020/0.003; sfx_gain 0.80 voice_gain 0.20 music_gain 1.00; starts 5 hit 4 miss 1 unprepared 0 next_miss 2`.
  Each pair is peak / RMS of that second, 1.000 = full scale. `mono` (the effects) / `voice` / `stereo`: the XMA
  streams after the gain, before FMOD's volumes. `c0`..`c3`: each render client after the 5.1 fold, before the sum
  and the clamp (a peak above 1 there can clip). A slot with no samples is left out. The counters: fresh `starts`
  (`hit` / `miss` of the table), `unprepared` starts, and `next_miss` = kept-codec buffer starts not in the table
  (streams' next buffers); no starts this second: `no starts`.
- Input record and replay (A/B runs without a hand; `switch/host/input/nx_input_driver.cpp`; keys saved only when on):
  `input_record=1` writes player 1's pad state at each poll to `sdmc:/switch/hcu/input.rec` (28 bytes: poll, the
  strip's swap number `#`, its `t`, the state the game got), from the first swap to the exit, flushed every second.
  `input_replay=1` (wins over `input_record`) gives the game, at each poll, the last record whose swap is at or below the
  strip's swap now; the pads are ignored (player 2 not connected) except L+R+ZL+ZR+Plus, which quits through
  `rex_nx_quick_exit`. After the last record's swap the pads play again. `bench_seconds=N`: N s (system clock) after the
  replay end, the game quits the same way. Log: `[input] record start / stop N polls`, `[input] replay start N polls`,
  `[input] replay end`, each with the swap number and t. Not exact: the swap count of a load or a movie depends on the
  card and the build's speed, so later inputs can land a few swaps off. Check: the swap at the replay end (the last `[speed]` line:
  about #15000 to #15150 for the 2026-11 recording) and the picture at the end.
- Cheats (every NRO; all keys 0 = off; the settings menu's line CHEATS > opens their page, B goes back):
  `cheat_god`, `cheat_lives`, `cheat_health`, `cheat_keep_weapons`, `cheat_rapid_fire`, `cheat_stage` (1..8),
  `cheat_log`. `cheat_weapon_a` / `_b` lock slot A / B to a weapon: 1 MG, 2 Spread, 3 Ripple, 4 Heated Plasma,
  5 Laser, 6 Crush. `cheat_level_a` / `_b` lock its level: 1..3 = level 0..2 (key - 1). The locks apply every frame
  (`src/cheats.cpp`). `cheat_stage` sets the stage of a new game; a change of START STAGE in the menu also jumps to
  that stage when the menu closes, if a stage is being played (else the log shows `[cheat] jump dropped`).

### PGO

1. `HCU_BUILD_DIR=/root/renut-hcu/src/out/build/switch-pgogen switch/build-base.sh -DHCU_PGO=generate`: an
   instrumented NRO (slower; never a speed test).
2. Play one session on the console. The NRO writes `pgo/banjo_pgo_<N>.stream` next to itself every 2 minutes, at HOME
   and at exit. Copy that one stream to the PC.
3. `switch/pgo-merge-base.sh <folder with the stream>`: the `.gcda` files in `/root/renut-hcu/pgo`.
4. `HCU_BUILD_DIR=/root/renut-hcu/src/out/build/switch-pgouse switch/build-base.sh -DHCU_PGO=use`.
