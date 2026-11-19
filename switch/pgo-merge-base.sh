#!/bin/bash
# PGO step 2 of 3 in WSL (the base's tools/pgo_merge.py runs only on Windows with devkitPro there; the streams of the
# instrumented NRO name the WSL profile folder, so the merge runs here).
#   step 1: HCU_BUILD_DIR=$SRC/out/build/switch-pgogen switch/build-base.sh -DHCU_PGO=generate
#           -> play ONE session on the console -> copy <NRO folder>/pgo/ to the PC
#   step 2: switch/pgo-merge-base.sh <folder with banjo_pgo_*.stream> [<instrumented ELF or NRO>] [--all]
#           (one stream, or --all for several sessions of the SAME instrumented build; with the ELF/NRO, no stream may
#           be older than it: the base's port 122, a stream of another build loses functions on a checksum mismatch)
#   step 3: HCU_BUILD_DIR=$SRC/out/build/switch-pgo switch/build-base.sh -DHCU_PGO=use, in an empty folder (rm -rf it
#           first): ninja does not know an object depends on its .gcda, so a new profile would not recompile anything
set -eu
ROOT=${HCU_ROOT:-/root/renut-hcu}
BASE=${HCU_BASE_DIR:-/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base}
TOOL=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-gcov-tool
OUT=$ROOT/pgo
STREAMS=$1
GEN=""; ALL=0
for a in "${@:2}"; do [ "$a" = --all ] && ALL=1 || GEN=$a; done
shopt -s nullglob
files=("$STREAMS"/banjo_pgo_*.stream)
[ ${#files[@]} -gt 0 ] || { echo "no banjo_pgo_*.stream in $STREAMS"; exit 1; }
for f in "${files[@]}"; do echo "  $(basename "$f") written $(date -r "$f" '+%Y-%m-%d %H:%M'), $(stat -c %s "$f") bytes"; done
[ ${#files[@]} -eq 1 ] || [ $ALL = 1 ] || { echo "${#files[@]} streams: keep one session, or pass --all"; exit 1; }
if [ -n "$GEN" ]; then
    for f in "${files[@]}"; do [ "$f" -nt "$GEN" ] || { echo "$f is older than the instrumented build $GEN"; exit 1; }; done
fi
mkdir -p "$OUT"
g=("$OUT"/*.gcda)  # nullglob: an empty array when there is none (a bare `ls "$OUT"/*.gcda` would list the folder)
[ ${#g[@]} -eq 0 ] || { echo "$OUT already has .gcda files: move them aside (merge-stream adds)"; exit 1; }
for f in "${files[@]}"; do "$TOOL" merge-stream "$f"; done
g=("$OUT"/*.gcda)
[ ${#g[@]} -gt 0 ] || { echo "no .gcda in $OUT after the merge: the streams name another folder"; exit 1; }
python3 "$BASE/tools/pgo_summary.py" "$OUT" "${#files[@]}"
echo "${#g[@]} .gcda files in $OUT"
