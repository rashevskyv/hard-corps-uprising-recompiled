#!/bin/bash
# Builds the Switch NRO in WSL.
#   sources /root/hcu/src (rsync'd here from the git checkout), SDK /root/hcu/rexglue-sdk (ReXGlue v0.10.0
#   + patches/rexglue-sdk common and .linux patches + switch/sdk-edits.py), host CLI built from that SDK
#   (out/linux-amd64/Release/rexglue), game files in /root/hcu/src/game/extracted (never committed),
#   NVK /root/cbfd/nvk-sdk-real/lib/libvulkan.a, devkitPro /opt/devkitpro.
#   switch/build.sh [ninja targets...]
# Every run reconfigures with the version from git ("0.<commit count>.<sha7>", the NACP's display
# version, hbmenu shows it) and writes hcu_version.h (the same string + the build time, the first
# log line), so a log and an NRO can always be matched to a commit.
set -eu
ROOT=${HCU_ROOT:-/root/hcu}
SRC=$ROOT/src
SDK=${REXSDK_DIR:-$ROOT/rexglue-sdk}
NVK_SDK=${NVK_SDK:-/root/cbfd/nvk-sdk-real}
GIT_DIR=${HCU_GIT_DIR:-/mnt/d/git/dev/hcu-switch}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
BUILD=$SRC/out/build/switch

[ -f "$NVK_SDK/lib/libvulkan.a" ] || { echo "no NVK: $NVK_SDK/lib/libvulkan.a (cbfd-switch switch/build-nvk.sh)"; exit 1; }
CLI=$SDK/out/linux-amd64/Release/rexglue
[ -x "$CLI" ] || { echo "no host CLI: $CLI"; exit 1; }
[ -f "$SRC/game/extracted/default.xex" ] || { echo "no game files: $SRC/game/extracted/default.xex"; exit 1; }

mkdir -p "$SRC"
rsync -a --delete --exclude /out --exclude /game --exclude /generated --exclude /.git "$GIT_DIR/" "$SRC/"
python3 "$SRC/switch/sdk-edits.py" "$SDK"
python3 "$SRC/switch/sdk-edits-hcu.py" "$SDK"
# The CLI embeds the codegen templates: a template edit needs `cmake --build ... --target rexglue`.
if [ ! -f "$SRC/generated/default/sources.cmake" ]; then
    (cd "$SRC" && "$CLI" codegen hard_corps_uprising_manifest.toml > "$ROOT/codegen.log" 2>&1)         || { tail -20 "$ROOT/codegen.log"; exit 1; }
fi

COUNT=$(git -c safe.directory="*" -C "$GIT_DIR" rev-list --count HEAD 2>/dev/null || echo 0)
SHA=$(git -c safe.directory="*" -C "$GIT_DIR" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
DIRTY=$(git -c safe.directory="*" -c core.autocrlf=true -C "$GIT_DIR" status --porcelain 2>/dev/null | grep -q . && echo "+" || true)
VERSION="0.$COUNT.$SHA$DIRTY"
STAMP=$(date -u +%Y-%m-%dT%H:%MZ)
mkdir -p "$BUILD"
printf '#define HCU_VERSION "%s"\n#define HCU_BUILD_UTC "%s"\n' "$VERSION" "$STAMP" > "$BUILD/hcu_version.h.new"
if ! cmp -s "$BUILD/hcu_version.h.new" "$BUILD/hcu_version.h" 2>/dev/null; then
    mv "$BUILD/hcu_version.h.new" "$BUILD/hcu_version.h"
else
    rm -f "$BUILD/hcu_version.h.new"
fi
echo "version $VERSION, built $STAMP"

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DREXSDK_DIR="$SDK" -DNVK_SDK="$NVK_SDK" -DHCU_VERSION="$VERSION" \
    -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=ON > "$BUILD/configure.log" 2>&1 \
    || { tail -20 "$BUILD/configure.log"; exit 1; }
# A failed build must not leave the previous NRO behind for deploy.sh.
rm -f "$BUILD/hcu.nro"
cmake --build "$BUILD" --parallel "${HCU_JOBS:-12}" ${1:+--target "$@"}
ls -la "$BUILD"/hcu.nro "$BUILD"/hcu.elf 2>/dev/null || true
sha1sum "$BUILD"/hcu.nro 2>/dev/null || true
# What hbmenu will show; the NACP version must be this build's (nx_create_nro ignored it until 0.55).
python3 "$SRC/tools/nro_nacp.py" "$BUILD/hcu.nro" "$VERSION"

# --- deko3d renderer staging (shared with cvhd-recomp): the present shaders and the game's shaders as
# DKSH next to the NRO in $BUILD/stage/ (switch/eden-deko3d.ps1 copies it to Eden's SD). The game's
# shaders come from the trace backend's --dump_shaders directory ($HCU_SHADER_DUMPS, default
# /root/hcu/shaders_trace) through tools/xenos2glsl.py + uam.
STAGE=$BUILD/stage
rm -rf "$STAGE"
mkdir -p "$STAGE/shaders"
for s in vert frag; do
    uam -s $s -o "$STAGE/shaders/present.$s.dksh" "$SRC/src/gpu/shaders/present.$s"
done
SHADER_DUMPS=${HCU_SHADER_DUMPS:-$ROOT/shaders_trace}
if [ -d "$SHADER_DUMPS" ]; then
    rm -rf "$BUILD/glsl"
    python3 "$SRC/tools/xenos2glsl.py" --all "$SHADER_DUMPS" --out-dir "$BUILD/glsl" --check | tail -1
    for f in "$BUILD"/glsl/*.glsl.dksh; do
        [ -f "$f" ] || continue
        b=$(basename "$f" .glsl.dksh)
        cp "$f" "$STAGE/shaders/$b.dksh"
    done
fi
cp "$BUILD/hcu.nro" "$STAGE/hcu.nro"
touch "$STAGE/deko3d"
echo "stage: $STAGE ($(ls "$STAGE/shaders" | wc -l) shaders)"
