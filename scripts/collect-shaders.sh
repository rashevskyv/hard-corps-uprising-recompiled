#!/bin/bash
# Collects the game's Xenos shaders on Linux (trace backend: 40 to 60 swaps per second, nothing drawn) while a key
# script walks from the title into stage 1 and plays (runs right, shoots, jumps), then translates them:
#   scripts/collect-shaders.sh [dump dir, default /home/user/hcuroot/shaders_trace] [play seconds, default 240]
# The dump dir is what switch/build.sh turns into DKSH (HCU_SHADER_DUMPS, /root/hcu/shaders_trace on the PC).
# Needs the Linux test host (docs/LINUX-TEST-HOST.md), Xvfb, xdotool and a PulseAudio null sink.
set -eu
ROOT=${HCU_ROOT:-/home/user/hcuroot}
DUMPS=${1:-${HCU_SHADER_DUMPS:-$ROOT/shaders_trace}}
PLAY=${2:-240}
mkdir -p "$DUMPS"
cd "$(dirname "$0")/.."
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/xdg-runtime}
# Title, save dialog, main menu, character select and the briefing: Start and A every 6 s, as in stage1.sh.
STEPS="wait 30;"
for i in $(seq 1 14); do STEPS="$STEPS key Return; wait 2; key space; wait 4;"; done
# In the stage START is the pause menu: the stick (d), X (l) and A (space) only.
for i in $(seq 1 $((PLAY / 8 + 1))); do STEPS="$STEPS key d+l 6000; key d+l+space 400; key l 1500;"; done
scripts/linux-drive.py --root "$ROOT" --out "$ROOT/runs/shaders-$(date +%H%M%S)" --max $((PLAY + 400)) --steps "$STEPS end" -- \
    --gpu_backend=trace --dump_shaders="$DUMPS"
mkdir -p "$ROOT/glsl"
python3 tools/xenos2glsl.py --all "$DUMPS" --out-dir "$ROOT/glsl"
cp src/gpu/shaders/present.vert "$ROOT/glsl/present.vert.glsl"
cp src/gpu/shaders/present.frag "$ROOT/glsl/present.frag.glsl"
echo "shaders: $(ls "$DUMPS"/*.ucode.vert "$DUMPS"/*.ucode.frag | wc -l) in $DUMPS, GLSL in $ROOT/glsl"
