#!/bin/bash
# Collects the game's Xenos shaders on Linux (trace backend, fast, nothing drawn) while a key script walks
# the game from the title into the first stage, then translates them for the deko3d backend:
#   scripts/collect-shaders.sh [dump dir, default /home/user/hcuroot/shaders_trace]
# The dump dir is what switch/build.sh turns into DKSH (HCU_SHADER_DUMPS). Needs the Linux build
# (docs/LINUX-TEST-HOST.md), Xvfb, xdotool, a PulseAudio null sink. Extra steps (more play) can be
# appended with HCU_EXTRA_STEPS="key space 300; wait 5; ...".
set -eu
DUMPS=${1:-${HCU_SHADER_DUMPS:-/home/user/hcuroot/shaders_trace}}
ROOT=${HCU_ROOT:-/home/user/hcuroot}
mkdir -p "$DUMPS"
cd "$(dirname "$0")/.."
# Title at swap ~360: START at 540; Single Player at 1260; "Start the Game" = four d-pad downs and A.
STEPS="waitlog 'swap #540' 600; key Return; waitlog 'swap #1260' 600; key space 400; waitlog 'swap #1500' 600;
key s 400; wait 2; key s 400; wait 2; key s 400; wait 2;
key space 400; wait ${HCU_STAGE_SECONDS:-120}; ${HCU_EXTRA_STEPS:-end}; end"
scripts/linux-drive.py --root "$ROOT" --out "$ROOT/runs/shaders-$(date +%H%M%S)" --max 1200 --steps "$STEPS" -- \
    --gpu_backend=trace --dump_shaders="$DUMPS"
python3 tools/xenos2glsl.py --all "$DUMPS" --out-dir "$ROOT/glsl"
cp src/gpu/shaders/present.vert "$ROOT/glsl/present.vert.glsl"
cp src/gpu/shaders/present.frag "$ROOT/glsl/present.frag.glsl"
echo "shaders: $(ls "$DUMPS"/*.ucode.vert "$DUMPS"/*.ucode.frag | wc -l) in $DUMPS, GLSL in $ROOT/glsl"
