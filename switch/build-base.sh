#!/bin/bash
# Builds the Switch NRO on the ultimate base's SDK in WSL (variant B, branch renut-ultimate; switch/switch-base.cmake).
#   sources  $ROOT/src (rsync'd from the git checkout $HCU_GIT_DIR)
#   SDK      $REXSDK_DIR (renut_nx_ultimate_base sdk + switch/sdk-edits-base.py, committed there; switch/prepare-base-sdk.sh)
#   CLI      $REXSDK_DIR/out/linux-amd64/Release/rexglue (the base's host codegen)
#   game     $ROOT/src/game/extracted (never committed)
#   base     $HCU_BASE_DIR (the base's tools: the toolchain file, tools/store_cards.py)
#   switch/build-base.sh [cmake -D options...]
# Run it under D:\git\dev\renut-ref\build.lock (one WSL build at a time). Every run takes the version from git
# ("0.<commit count>.<sha7>[+]", hbmenu shows it) and writes hcu_version.h, so a log and an NRO match a commit.
set -eu
ROOT=${HCU_ROOT:-/root/renut-hcu}
SRC=$ROOT/src
SDK=${REXSDK_DIR:-/root/renut-ult/sdk}
BASE=${HCU_BASE_DIR:-/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base}
GIT_DIR=${HCU_GIT_DIR:-/mnt/d/git/dev/renut-wt/hcu-ult}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export PATH=$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH
BUILD=${HCU_BUILD_DIR:-$SRC/out/build/switch-base}

CLI=$SDK/out/linux-amd64/Release/rexglue
[ -x "$CLI" ] || { echo "no host CLI: $CLI"; exit 1; }
[ -f "$SRC/game/extracted/default.xex" ] || { echo "no game files: $SRC/game/extracted/default.xex"; exit 1; }

mkdir -p "$SRC"
rsync -rlc --delete --exclude /out --exclude /game --exclude /generated --exclude /.git "$GIT_DIR/" "$SRC/"
# The SDK must hold this checkout's edits: they are idempotent, so a run changes nothing on an SDK that has them.
python3 "$SRC/switch/sdk-edits-base.py" "$SDK" > "$ROOT/sdk-edits.log"
if [ -n "$(git -C "$SDK" status --porcelain --untracked-files=no)" ]; then
    echo "the SDK edits changed $SDK (commit them there first):"; git -C "$SDK" status --short --untracked-files=no; exit 1
fi

# Codegen, then the store cards (tools/store_cards.py of the base renames the stores outside the stack to their
# card-marking forms; the deko3d renderer reads the cards). Again whenever the manifest, its config, the CLI or the
# cards tool changed.
CG_STAMP=$(cat "$SRC/hard_corps_uprising_manifest.toml" "$SRC"/config/*.toml "$CLI" "$BASE/tools/store_cards.py" \
    "$BASE/tools/pch_no_volatile.py" "$BASE/tools/loop_barriers.py" "$SRC/switch/build-base.sh" | sha1sum | cut -c1-40)
if [ ! -f "$SRC/generated/default/sources.cmake" ] || [ "$(cat "$SRC/generated/.hcu-codegen-stamp" 2>/dev/null)" != "$CG_STAMP" ]; then
    rm -rf "$SRC/generated"
    (cd "$SRC" && "$CLI" codegen --ignore-stamp hard_corps_uprising_manifest.toml > "$ROOT/codegen.log" 2>&1) \
        || { tail -20 "$ROOT/codegen.log"; exit 1; }
    python3 "$BASE/tools/store_cards.py" --gen "$SRC/generated/default" > "$ROOT/store-cards.log" 2>&1 \
        || { tail -20 "$ROOT/store-cards.log"; exit 1; }
    # stwcx. / stdcx.: the card tool knows the CAS form of the base's newer codegen (REX_GUEST_CAS); this CLI writes
    # __sync_bool_compare_and_swap. Their card goes after the store (a failed CAS marks too: one upload more).
    sed -E -i 's/^(\t[A-Za-z0-9_.]+\.eq = __sync_bool_compare_and_swap\(reinterpret_cast<uint(32|64)_t\*>\(REX_RAW_ADDR\(ea\)\), .*\);)$/\1 REX_CARD(ea);/' \
        "$SRC"/generated/default/*.cpp
    if cat "$SRC"/generated/default/*.cpp | grep '__sync_bool_compare_and_swap' | grep -vq 'REX_CARD(ea);$'; then
        echo "a CAS without its store card (switch/build-base.sh)"; exit 1
    fi
    # Item 9 of docs/bible-checklist.md: guest loads and stores without volatile, with the loop barriers they need
    # (the base's tools; without the barriers GCC turns a guest polling loop into a branch to itself). The cmake option
    # HCU_NO_VOLATILE picks the flavour: OFF (default) defines HCU_KEEP_VOLATILE, which gives back volatile and empties
    # the barriers, so the default NRO compiles the code as before and both NROs come from this one tree.
    PCH=$(ls "$SRC"/generated/default/*_pch.h)
    python3 "$BASE/tools/pch_no_volatile.py" "$PCH" > "$ROOT/loop-barriers.log" 2>&1 \
        || { cat "$ROOT/loop-barriers.log"; exit 1; }
    python3 "$BASE/tools/loop_barriers.py" "$SRC/generated/default" >> "$ROOT/loop-barriers.log" 2>&1 \
        || { cat "$ROOT/loop-barriers.log"; exit 1; }
    # After the tools (pch_no_volatile.py skips a pch that already names REX_GUEST_VOLATILE); in front of their #ifndef.
    sed -i 's|^#define REX_RAW_ADDR(x) |#if defined(HCU_KEEP_VOLATILE)  // switch/build-base.sh: HCU_NO_VOLATILE OFF\n#define REX_GUEST_VOLATILE volatile\n#define REX_LOOP_BARRIER() ((void)0)\n#endif\n&|' "$PCH"
    # This CLI's output is not the base's: no loop header found means the pass did not understand it.
    grep -qE ': [1-9][0-9]* loop headers' "$ROOT/loop-barriers.log" || { cat "$ROOT/loop-barriers.log"; exit 1; }
    echo "$CG_STAMP" > "$SRC/generated/.hcu-codegen-stamp"
fi
# Without the cards the renderer would never see a guest write: the build stops. The same for the flavour switch.
PCH=$(ls "$SRC"/generated/default/*_pch.h)
[ "$(grep -c 'HCU_KEEP_VOLATILE\|^#define REX_GUEST_VOLATILE$\|loop barrier (tools/loop_barriers.py)' "$PCH")" -ge 3 ] \
    || { echo "no volatile switch or loop barrier macro in $PCH"; exit 1; }
cat "$ROOT/loop-barriers.log"
grep -q '#define REX_CARD(ea)' "$PCH" || { echo "no store cards in $PCH (tools/store_cards.py)"; exit 1; }
# The stores left without a card (each should go through the stack pointer) and the ones with it.
echo "stores: $(cat "$SRC"/generated/default/*.cpp | grep -oE '\b(REX_STORE_(D_)?U(8|16|32|64)|REX_STVLX|REX_STVRX|REX_STORE_(D_)?F32ANY)(_C)?\(' \
    | sed 's/($//' | sort | uniq -c | tr '\n' ' ') CAS with card: $(cat "$SRC"/generated/default/*.cpp | grep -c 'REX_CARD(ea);$')"

# A git worktree made on Windows has "gitdir: D:/..." in its .git file, which WSL's git cannot open.
WIN_GITDIR=$(sed -n 's|^gitdir: \([A-Za-z]\):/|/mnt/\L\1\E/|p' "$GIT_DIR/.git" 2>/dev/null || true)
git_src() { git -c safe.directory="*" -C "$GIT_DIR" ${WIN_GITDIR:+--git-dir="$WIN_GITDIR" --work-tree="$GIT_DIR"} "$@"; }
COUNT=$(git_src rev-list --count HEAD 2>/dev/null || echo 0)
SHA=$(git_src rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
DIRTY=$(git_src -c core.autocrlf=true status --porcelain --untracked-files=no 2>/dev/null | grep -q . && echo "+" || true)
VERSION="0.$COUNT.$SHA$DIRTY"
STAMP=$(date -u +%Y-%m-%dT%H:%MZ)
BUILD_ID="$VERSION-${STAMP//[-:]/}"
mkdir -p "$BUILD"
printf '#define HCU_VERSION "%s"\n#define HCU_BUILD_UTC "%s"\n#define HCU_BUILD_ID "%s"\n' "$VERSION" "$STAMP" "$BUILD_ID" > "$BUILD/hcu_version.h.new"
if ! cmp -s "$BUILD/hcu_version.h.new" "$BUILD/hcu_version.h" 2>/dev/null; then
    mv "$BUILD/hcu_version.h.new" "$BUILD/hcu_version.h"
else
    rm -f "$BUILD/hcu_version.h.new"
fi
echo "version $VERSION, built $STAMP"

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$BASE/tools/switch/cmake/switch-devkitA64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DREXSDK_DIR="$SDK" -DHCU_VERSION="$VERSION" "$@" > "$BUILD/configure.log" 2>&1 \
    || { tail -30 "$BUILD/configure.log"; exit 1; }
# A failed build must not leave the previous NRO behind.
rm -f "$BUILD/hcu.nro"
# -k 0: every failing file in one run (a long build should not stop at the first error).
cmake --build "$BUILD" --parallel "${HCU_JOBS:-12}" -- -k 0 > "$BUILD/build.log" 2>&1 \
    || { grep -E 'error|Error|undefined reference' "$BUILD/build.log" | head -40; tail -5 "$BUILD/build.log"; exit 1; }
ls -la "$BUILD"/hcu.nro "$BUILD"/hcu  # hcu = the ELF with symbols (addr2line)
sha1sum "$BUILD"/hcu.nro
python3 "$SRC/tools/nro_nacp.py" "$BUILD/hcu.nro" "$VERSION"
