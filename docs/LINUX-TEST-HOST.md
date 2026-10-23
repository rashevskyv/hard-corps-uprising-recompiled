# Linux test host (cloud box or Ubuntu 24.04)

What it gives: Hard Corps: Uprising runs on Linux x86-64 with three GPU backends.
- `--gpu_backend=vulkan`: the SDK's Vulkan backend on Mesa lavapipe. This is the reference picture.
- `--gpu_backend=trace`: no drawing, 40 to 60 swaps per second, collects the Xenos shaders.
- `--gpu_backend=deko3d`: our deko3d backend over an OpenGL stand-in for libdeko3d (`src/gpu/dk_gl`).

The deko3d path is the Switch code. Register decoding, textures, resolves and state run unchanged.
Only the GPU calls end in OpenGL (Mesa llvmpipe, headless EGL).

## Build, from nothing

1. Packages (apt): `clang-20 lld-20 cmake ninja-build libegl-dev libgl-dev libvulkan-dev mesa-vulkan-drivers xvfb
   xdotool imagemagick glslang-tools pulseaudio python3-pil p7zip-full` and the SDK build dependencies
   (`libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev libxss-dev libxkbcommon-dev libxtst-dev
   libxfixes-dev libx11-xcb-dev libwayland-dev libdrm-dev libgbm-dev libasound2-dev libpulse-dev libdbus-1-dev
   libudev-dev libc++-20-dev`).
2. SDK: `git clone https://github.com/rexglue/rexglue-sdk`, check out `c94f5eb`, `git submodule update --init --depth 1`
   (skip moltenvk).
3. In the SDK: `git apply` every `patches/rexglue-sdk/*.patch` except `*.windows.patch`, commit them, then
   `python3 switch/sdk-edits.py <sdk>`, `python3 switch/sdk-edits-hcu.py <sdk>`, and `sdk-edits.py` once more.
   Run them on a clean SDK. `switch/build.sh` shows the order.
4. Game data: the release `gamedata` of this repo (7z volumes). `7z x hcu_live.7z.001`, then
   `python3 -I tools/stfs_extract.py hcu_LIVE_58410A18.stfs <root>/game`. The SHA-256 of `default.xex` must start 530EDE8E.
   Link it: `ln -s <root>/game game/extracted` in the repo (`game/` is ignored by git).
5. Host CLI and codegen: `cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20`
   in the SDK, then `cmake --build out/build/linux-amd64 --config Release --target rexglue -j4` (about 25 minutes on four
   cores). Then in the repo: `<sdk>/out/linux-amd64/Release/rexglue codegen hard_corps_uprising_manifest.toml` (about one minute).
6. Game: `cmake -S . -B out/build/linux-testhost -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-20
   -DCMAKE_CXX_COMPILER=clang++-20 -DREXSDK_DIR=<sdk> -DHCU_LINUX_TESTHOST=ON`, then `cmake --build out/build/linux-testhost -j4`.
   The target is a plain ReXApp (`switch/hcu_nx_main.cpp`) plus the GPU plugin with the extra backends. It takes
   about ten minutes after the SDK. A change to `src/gpu` rebuilds only the plugin: `scripts/linux-build.sh` (seconds).
7. Sound: run PulseAudio with a null sink (`XDG_RUNTIME_DIR=/tmp/xdg-runtime pulseaudio --start;
   pactl load-module module-null-sink`). Without a sink the game waits for its audio engine.

## Run

- Shaders: the trace backend writes them with `--dump_shaders=<dir>`. `python3 tools/xenos2glsl.py --all <dir>
  --out-dir <glsl>` translates them. Copy `src/gpu/shaders/present.vert` and `.frag` to `<glsl>/present.vert.glsl`
  and `.frag.glsl`. The backend dumps shaders it misses to `<glsl>/missing/`.
- `scripts/linux-drive.py --steps "wait 30; key Return; wait 5; shot a" --out <dir> -- --gpu_backend=vulkan`.
  Keys: Return = Start, space = A, `l` = X, `d` = stick right, shift+Down = d-pad down.
  `waitlog <regex>`, `shot <name>` (Vulkan only) and chords such as `d+l` exist.
- `scripts/stage1.sh <name> <seconds> [VAR=val ...]` walks to stage 1 with the deko3d backend and keeps frames.
  The guest runs at full speed with a null backend until swap `HCU_DK_START_SWAP` (default 7580). After that the
  backend draws, about 5 swaps per second. Stage 1 starts around swap 6200 on this key script.
  Do not press Start in the stage: it is the pause menu.
- Environment variables of the deko3d test host:
  - `HCU_DK_GL_DUMP=<dir>:<every N>`: frames as PPM files.
  - `HCU_DK_DUMP_DIR=<dir>` with `HCU_DK_DUMP_AT=<swaps>`: every render target and guest image at those swaps.
    Also the first 400 uploaded textures.
  - `HCU_DK_TRACE_AT=<swap or lo-hi>[,...]`: one log line per draw, clear and resolve (`T draw ...`), vertices of
    small draws, palette contents, `k_8` texture parameters.
  - `HCU_DK_TRACE_DUMP=<dir>` with a trace: the colour target after each draw (`d<swap>-<draw>.ppm`), the decoded
    texels of each bound texture (`t<swap>-<draw>-<base>-<w>x<h>-<format>.raw`) and the host image
    (`h<swap>-<draw>-<base>.ppm`).
  - `HCU_DK_GL_POISON=1`: new images start as noise (finds uninitialised memory).
- One test at a time. Never `pkill -f` with a pattern that the shell command itself contains.

## Tools that helped

- Vulkan reference: run the same key script with `--gpu_backend=vulkan` and `shot`.
- `scripts/draw-ratio.py` and `scripts/draw-diff.py`: compare the changed pixels after a draw with the non-zero texels of its
  index texture. A draw that changes far more pixels than its texture has opaque texels is wrong.
