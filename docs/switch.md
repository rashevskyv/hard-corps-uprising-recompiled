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
while starting any game). Logs: `sdmc:/switch/hcu/hcu_log.txt` (`[nx]` lines: memory, clocks, fps, cpu,
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

NVK builds are not run on the console (user decision, 2026-10-23): the renderer is deko3d, Eden is the gate.

## deko3d renderer

The backend is cvhd-recomp's (`src/gpu/deko3d_backend.cpp`, docs/native-renderer-plan.md section 8 there),
shared through `switch/sync-cvhd.sh`. The file `sdmc:/switch/hcu/deko3d` selects it. Shaders: the backend
dumps the ucode of each shader without a DKSH into `sdmc:/switch/hcu/shaders/missing/`;
`switch/eden-deko3d.ps1` copies them to `/root/hcu/shaders_trace/`, and the next `switch/build.sh`
translates them (`tools/xenos2glsl.py`, uam) into `out/build/switch/stage/shaders/`.

Eden: `powershell -File switch/eden-deko3d.ps1 -Seconds 150 -Every 25 [-Keys "160:M"]` (Eden keys: C = A,
X = B, V = X, Z = Y, M = +, N = -, WASD = left stick, arrows = d-pad). Screenshots and logs in
`E:\conker-data\eden-runs\hcu-<version>-deko3d[-tag]\`.
