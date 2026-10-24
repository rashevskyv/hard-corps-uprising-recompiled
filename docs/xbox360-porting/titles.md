# Next titles: what to use for which (XBLA, ReXGlue, Switch)

Facts about the games come from searches and the other ports' docs (2026-10-22). Check each claim against your own dump before you rely on it.

## Decision list

1. **Always start from ReXGlue 0.10.0 + the stuntdouble SDK patches (pointer scans, noreturn) + the Konami-port fixes below.** Reason: five Switch ports and two Konami XBLA ports already ship on it.
2. **Copy the Horizon layer ideas from sa2-nx / usf4-nx / futari-nx** (aliasing recipe, priority scheme, exit, profiler). Do not copy files (GPL-3.0).
3. **Set the codegen flags on day one in a scratch copy, enable them one by one after the first boot** (performance.md section 1). `skip_msr = true` removes a global mutex; `[rexcrt]` memcpy/memset; `non_volatile_as_local` last.
4. **Fix the GPU wait machinery before profiling anything else**: WAIT_REG_MEM blind sleep, guest fence wake, vblank wake (performance.md section 8a).
5. **Renderer: stay on the SDK's Xenos/Vulkan path until a profile says the GPU thread is the limit.** A 2D sprite game with a few hundred draws fits NVK (usf4 evidence). Only then consider a native renderer or deko3d (sa2: NVK per-call costs forced it at ~1,400 draws).
6. **Test order**: Linux/PC with the null GPU → Eden null GPU gate → console. Every translation bug is easier on the PC.

## Per title

| Title | ID / facts | Closest prior art | Notes |
|---|---|---|---|
| **Castlevania: Harmony of Despair** (2010, Konami; developer not verified) | 58410A7A; XEX 3.4 MB; STFS LIVE; TU xexp 720 KB; 34,336 functions; image 0x82000000-0x82960000 | Hard Corps: Uprising (Konami XBLA, 58410A18, ReXGlue, 38 SDK patches), Nocturne (SotN XBLA, rexcrt), futari/guwange (2D XBLA on Switch) | Up to 6 players; HD sprites; The HCU wait patches (0003/0006/0009/0025) fix SDK-wide behaviour, not Konami-specific code [GUESS that they apply]: read them, then apply the Switch priority rules |
| **Guardian Heroes** (2011, Treasure/Sega XBLA, 16:9 "enhanced" graphics, online co-op) | title ID unknown to me; no existing recomp found | futari-nx (2D XBLA), sa2-nx (Sega XBLA) | Sega XBLA: likely XACT or own audio; check imports for XMA, XAudio, Bink/WMV. Expect STFS LIVE and a TU. Start with rexglue init, then pointer scan |
| **"Contra Uprising" = Hard Corps: Uprising** (Feb 2011, Arc System Works/Konami, 2D HD sprites) | 58410A18, content 000D0000, media 3C5663CB, version 0.0.0.3, default.xex 12,955,648 B SHA-256 530EDE8E…D2069DC | saulob/hard-corps-uprising-recompiled (PC; manifest + overrides.toml + 38 runtime patches) | Re-read its manifest overrides for function ends (12 entries), its patch table, DLC handling (content type 2). Same publisher as CVHD: the same fixes may apply |

## Order of work for a new title

1. Dump (STFS) → extract (`stfs_extract.py` or `rexglue extract`) → note title ID, XEX facts, TU.
2. `rexglue init` + `codegen`; fix Validate errors; add pointer scans; set setjmp/longjmp.
3. Linux boot to title with `--gpu_backend=null` and with Vulkan (lavapipe).
4. Add the codegen flags and `[rexcrt]`. Compare fps with the null backend (guest speed only).
5. Switch build with the layer; Eden null-GPU gate (swaps growing, clean exit).
6. Console: profiler (PC sampler with counts, link map), then waits/priorities/log writer, then renderer.
7. Shipping: browser or PC installer that builds shaders and game pack from the user's files; no game data in the repo.
