#!/bin/bash
# PGO step 2 of 3 in WSL (the base's tools/pgo_merge.py runs only on Windows with devkitPro there; the streams of the
# instrumented NRO name the WSL profile folder, so the merge runs here).
#   step 1: switch/build-base.sh -DHCU_PGO=generate -> play ONE session on the console -> copy sdmc:/switch/hcu/pgo/
#   step 2: switch/pgo-merge-base.sh <folder with banjo_pgo_*.stream>   (one stream, or --all for several sessions)
#   step 3: switch/build-base.sh -DHCU_PGO=use   (a different build folder: HCU_BUILD_DIR=...)
set -eu
ROOT=${HCU_ROOT:-/root/renut-hcu}
BASE=${HCU_BASE_DIR:-/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base}
TOOL=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-gcov-tool
OUT=$ROOT/pgo
STREAMS=$1
shopt -s nullglob
files=("$STREAMS"/banjo_pgo_*.stream)
[ ${#files[@]} -gt 0 ] || { echo "no banjo_pgo_*.stream in $STREAMS"; exit 1; }
[ ${#files[@]} -eq 1 ] || [ "${2:-}" = "--all" ] || { echo "${#files[@]} streams: keep one session, or pass --all"; exit 1; }
mkdir -p "$OUT"
ls "$OUT"/*.gcda > /dev/null 2>&1 && { echo "$OUT already has .gcda files: move them aside (merge-stream adds)"; exit 1; }
for f in "${files[@]}"; do "$TOOL" merge-stream "$f"; done
python3 "$BASE/tools/pgo_summary.py" "$OUT" "${#files[@]}"
echo "$(ls "$OUT"/*.gcda | wc -l) .gcda files in $OUT"
