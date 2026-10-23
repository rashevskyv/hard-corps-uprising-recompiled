#!/bin/bash
# Drives the Linux test host into stage 1 with the deko3d backend (GL stand-in) and keeps frame dumps.
#   scripts/stage1.sh <run name> [seconds in the stage, default 150] [extra backend env as VAR=val ...]
# HCU_DK_START_SWAP (default 7580) fast-forwards the guest with a null backend; keys as in the first stage walk.
set -eu
cd "$(dirname "$0")/.."
NAME=${1:-run}; SECS=${2:-150}; shift 2 2>/dev/null || shift $# 
OUT=${HCU_ROOT:-/home/user/hcuroot}/runs/$NAME
mkdir -p "$OUT/frames" "$OUT/dump"
export XDG_RUNTIME_DIR=/tmp/xdg-runtime
STEPS="wait 30;"
for i in $(seq 1 ${HCU_NAV_CYCLES:-14}); do STEPS="$STEPS key Return; wait 2; key space; wait 4;"; done
# In the stage START is the pause menu: only A (fire) and the stick from here on.
STEPS="$STEPS key d 2000; wait 2; key space 500; wait $SECS; end"
env HCU_DK_START_SWAP=${HCU_DK_START_SWAP:-7580} HCU_DK_DUMP_DIR="$OUT/dump" HCU_DK_GL_DUMP="$OUT/frames:${HCU_DK_FRAME_EVERY:-30}" "$@" \
  python3 scripts/linux-drive.py --steps "$STEPS" --max $((300 + SECS)) --out "$OUT" -- --gpu_backend=deko3d --deko3d_shader_dir=${HCU_GLSL:-/home/user/hcuroot/glsl}/ > "$OUT/drive.txt" 2>&1
echo "run $OUT: $(ls "$OUT/frames" | wc -l) frames"
