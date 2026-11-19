#!/bin/bash
# The deko3d renderer's shader set (shared with cvhd-recomp), as DKSH in <stage dir>/shaders/ (sdmc:/switch/hcu/shaders/
# on the console; switch/eden-deko3d.ps1 copies it to Eden's SD): the present pair and every game shader of the dump
# directories, through tools/xenos2glsl.py + uam. The backend loads shader_<ucode hash>.<vert|frag|geom>.dksh, the
# .vert.np.dksh variant (no precise outputs, the zfix file's "noprecise") and .frag.na.dksh (no alpha test, ALPHA
# VARIANTS); the GLSL stays in <work dir>/glsl (the Linux test host's --deko3d_shader_dir).
#   switch/stage-shaders.sh <stage dir> <work dir> <dump dir>...
# A dump dir holds shader_<hash>.ucode.<vert|frag>: the trace backend's --dump_shaders, the console's missing/ dumps
# (switch/fetch-missing-shaders.sh) or tools/shader_scan.py's output. A hash in two dirs is the same shader.
set -eu
SRC=$(cd "$(dirname "$0")/.." && pwd)
STAGE=$1
WORK=$2
shift 2
rm -rf "$STAGE/shaders" "$WORK/glsl" "$WORK/glsl_np" "$WORK/glsl_na"
mkdir -p "$STAGE/shaders" "$WORK"
for s in vert frag; do
    uam -s $s -o "$STAGE/shaders/present.$s.dksh" "$SRC/src/gpu/shaders/present.$s"
done
for d in "$@"; do
    [ -d "$d" ] || { echo "shaders: no dump dir $d"; continue; }
    LOG="$WORK/xenos2glsl-$(basename "$d").log"
    python3 "$SRC/tools/xenos2glsl.py" --all "$d" --out-dir "$WORK/glsl" --check --precise > "$LOG"
    # The vertex shaders without precise outputs, staged as shader_<hash>.vert.np.dksh.
    python3 "$SRC/tools/xenos2glsl.py" --all "$d" --out-dir "$WORK/glsl_np" --check > /dev/null
    # ALPHA VARIANTS (settings.txt alpha_variants=1): the pixel shaders without the alpha test.
    python3 "$SRC/tools/xenos2glsl.py" --all "$d" --out-dir "$WORK/glsl_na" --check --no-alpha-test > /dev/null
    FAILS=$(grep -c '^FAIL' "$LOG" || true)
    echo "shaders: $d: $(tail -1 "$LOG"), $FAILS failed (all results: $LOG)"
    grep '^FAIL' "$LOG" | head -10
    [ "$FAILS" -le 10 ] || echo "  ... $((FAILS - 10)) more in $LOG"
done
for f in "$WORK"/glsl/*.glsl.dksh; do
    [ -f "$f" ] || continue
    cp "$f" "$STAGE/shaders/$(basename "$f" .glsl.dksh).dksh"
done
for f in "$WORK"/glsl_np/*.vert.glsl.dksh; do
    [ -f "$f" ] || continue
    cp "$f" "$STAGE/shaders/$(basename "$f" .glsl.dksh).np.dksh"
done
# Only where the GLSL differs (a shader with colour outputs).
for f in "$WORK"/glsl_na/*.frag.glsl.dksh; do
    [ -f "$f" ] || continue
    b=$(basename "$f" .glsl.dksh)
    cmp -s "$WORK/glsl_na/$b.glsl" "$WORK/glsl/$b.glsl" || cp "$f" "$STAGE/shaders/$b.na.dksh"
done
# The Linux test host reads the present pair from the GLSL dir too.
for s in vert frag; do cp "$SRC/src/gpu/shaders/present.$s" "$WORK/glsl/present.$s.glsl"; done
echo "shaders: $STAGE/shaders: $(ls "$STAGE/shaders" | grep -c '\.vert\.dksh$') vertex, $(ls "$STAGE/shaders" | grep -c '\.frag\.dksh$') pixel shaders, $(ls "$STAGE/shaders" | wc -l) files"
