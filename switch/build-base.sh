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

# The flavour switches of this script, given as cmake -D options: the last -DNAME=value wins, as in CMake, and any
# value CMake reads as true (ON, 1, TRUE, YES, Y, a non-zero number; any case; also -DNAME:BOOL=...) is 1.
opt() {
    local r=$2 a v
    for a in "${@:3}"; do
        case "$a" in "-D$1="*|"-D$1:"*=*) v=${a##*=}; v=${v,,} ;; *) continue ;; esac
        case "$v" in on|true|yes|y|[1-9]*) r=1 ;; *) r=0 ;; esac
    done
    echo "$r"
}
NO_VOLATILE=$(opt HCU_NO_VOLATILE 0 "$@")
DIRECT_CALLS=$(opt HCU_DIRECT_CALLS 1 "$@")
GUEST_CAS=$(opt HCU_GUEST_CAS 0 "$@")
METER=$(opt HCU_NX_METER 0 "$@")
FLOAT_LOADS=$(opt HCU_FLOAT_LOADS 1 "$@")
COLD_TRAPS=$(opt HCU_COLD_TRAPS 1 "$@")
INLINE_MEMCALLS=$(opt HCU_INLINE_MEMCALLS 1 "$@")
FPCR_ONE=$(opt HCU_FPCR_ONE_MODE 0 "$@")
DFORM=$(opt HCU_DFORM 0 "$@")  # this script's only (no CMake option): a codegen key
RO_CONSTANTS=$(opt HCU_RO_CONSTANTS 1 "$@")
PGO=""  # HCU_PGO is a string (empty, generate or use): the last -DHCU_PGO=... wins
for a in "$@"; do case "$a" in -DHCU_PGO=*|-DHCU_PGO:*=*) PGO=${a##*=} ;; esac; done

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
# -DHCU_DFORM=ON: the base's codegen key dform_disp_split (the base sets it): a D-form access passes the register and
# the displacement apart (base + zext(rA) + D; the old form wraps rA + D at 32 bits), so neighbouring accesses share
# one add. NOT exact only for an access whose rA + D crosses 2^32 (0xFFFFxxxx+ or a negative offset from a near-null
# pointer, a guest fault on the 360 too). Its own generated tree: the key goes into the rsync'd manifest copy (the
# rsync above puts the original back every run), so the stamp below re-runs the codegen at each change of the flavour.
[ "$DFORM" = 1 ] && echo 'dform_disp_split = true  # switch/build-base.sh -DHCU_DFORM=ON' >> "$SRC/hard_corps_uprising_manifest.toml"
# The base's other codegen keys (its app/renut_config.toml sets them all), each a -D option of this script only, default
# OFF: ON puts the key into the manifest copy, as HCU_DFORM, and its suffix on the version. Class "exact": the same
# values as the default code (read in the base's builders); -DHCU_CG_EXACT=ON turns on every "exact" key ("-cgx").
# Class "lock": the same values, but the code no longer takes a lock the default code takes (exact only under the
# hypothesis written at the key). Class "fix": other values than the default code, the ones the 360 gives.
# Class "fp": fewer flush-mode switches through the SDK's any-mode float helpers (rex/ppc/diet_ops.h: the result of
# the flush-off mode in either mode, a cold path for a denormal), exact by the base's design, NOT proven for HCU.
# Only "exact" keys are in -cgx; the others take their own option and suffix.
# The audit of each is in docs/bible-checklist.md (step 4b).
#   option              manifest key        suffix  class
CG_KEYS=(
    "HCU_DCBT_PREFETCH  dcbt_prefetch       dcbt    exact"  # dcbt / dcbtst as the host's prefetch: a hint, no value
    "HCU_ROTATE_SHIFTS  rotate_word_shifts  rot     exact"  # rlwinm / rlwimi, mask mb <= me, as the plain shift (sdk/tests/rotate_word_test.cpp)
    "HCU_DIV_INLINE     div_inline          div     exact"  # divw(u) / divd(u) as sdiv / udiv: 0 for a zero divisor and INT_MIN / -1, as before
    "HCU_FCTIWZ_INLINE  fctiwz_inline       fwz     exact"  # fctiwz as fcvtzs + NaN select: simde's range test gives the same saturation
    # mfmsr / mtmsrd: no global lock around the guest's 1499 lwarx ... stwcx. regions (each one reservation, ALU
    # operations and one CAS: the CAS alone is atomic). HYPOTHESIS: no host code takes global_critical_region to guard
    # these guest words (checked: the DPC list and the threading exports only).
    "HCU_SKIP_MSR       skip_msr            msr     lock"
    # fctiw / fctid (598 sites) as frinti + fcvtzs: a tie rounds to even in the current mode, as on the 360; the default
    # code (simde's portable conversion) rounds it away from zero. CHANGES those results, toward the 360's.
    "HCU_FCTI_INLINE    fcti_inline         fcti    fix"
    # Float moves (lfd / stfd, fmr, fneg, fabs ...) switch no mode; lfs / stfs in VMX mode convert any-mode. HCU's
    # census (codegen only): switches off 22774 -> 17836, on 386 -> 253; 597 any-mode helpers.
    "HCU_FP_MOVES       fp_moves_modeless   fpm     fp"
    # fcfid / fctiwz / fctidz / fsel switch no mode; fcmpu / fcmpo / frsp in VMX mode any-mode. HCU: switches off
    # 22774 -> 22753; 183 any-mode compares (fsel's GeZeroAnyMode).
    "HCU_FP_COMPARES    fp_compares_modeless fpc    fp"
)
CG_EXACT=$(opt HCU_CG_EXACT 0 "$@")
CG_SUFFIX=""
for k in "${CG_KEYS[@]}"; do
    read -r o key suf class <<< "$k"
    if [ "$(opt "$o" 0 "$@")" = 1 ] || { [ "$CG_EXACT" = 1 ] && [ "$class" = exact ]; }; then
        echo "$key = true  # switch/build-base.sh -D$o=ON" >> "$SRC/hard_corps_uprising_manifest.toml"
        [ "$CG_EXACT" = 1 ] && [ "$class" = exact ] || CG_SUFFIX="$CG_SUFFIX-$suf"
    fi
done
[ "$CG_EXACT" = 1 ] && CG_SUFFIX="-cgx$CG_SUFFIX"

# Codegen, then the store cards (tools/store_cards.py of the base renames the stores outside the stack to their
# card-marking forms; the deko3d renderer reads the cards). Again whenever the manifest, its config, the CLI or the
# cards tool changed.
CG_STAMP=$(cat "$SRC/hard_corps_uprising_manifest.toml" "$SRC"/config/*.toml "$CLI" "$BASE/tools/store_cards.py" \
    "$BASE/tools/pch_no_volatile.py" "$BASE/tools/loop_barriers.py" "$BASE/tools/guest_cas.py" "$BASE/tools/float_loads.py" "$BASE/tools/pch_cold_traps.py" "$BASE/tools/inline_memcalls.py" "$BASE/tools/ro_constants.py" "$SRC/switch/build-base.sh" | sha1sum | cut -c1-40)
if [ ! -f "$SRC/generated/default/sources.cmake" ] || [ "$(cat "$SRC/generated/.hcu-codegen-stamp" 2>/dev/null)" != "$CG_STAMP" ]; then
    rm -rf "$SRC/generated"
    (cd "$SRC" && REXGLUE_DUMP_IMAGE="$ROOT/hcu_image.bin" "$CLI" codegen --ignore-stamp hard_corps_uprising_manifest.toml \
        > "$ROOT/codegen.log" 2>&1) || { tail -20 "$ROOT/codegen.log"; exit 1; }
    : > "$ROOT/passes.log"
    # Item 11c-4 (the base's tools/ro_constants.py; it reads only the D-form loads, so only the -DHCU_DFORM=ON tree):
    # the loads from the image's read-only data (.rdata) become REX_RO_* with the bytes there, the XEX's import records
    # excepted (--xex: the loader writes them). Exact while the game never writes its .rdata (read-only on the 360).
    # The cmake option HCU_RO_CONSTANTS=OFF (REX_RO_CONSTANTS_OFF) loads them as before. Before float_loads.py.
    if [ "$DFORM" = 1 ]; then
        python3 -I "$BASE/tools/ro_constants.py" --gen "$SRC/generated/default" --image "$ROOT/hcu_image.bin" \
            --xex "$SRC/game/extracted/default.xex" >> "$ROOT/passes.log" 2>&1 || { cat "$ROOT/passes.log"; exit 1; }
    fi
    # Item 11c (the base's tools/float_loads.py; why and the switch: below, at its volatile edit). Before the store
    # cards: its test "a float was just stored to this address" knows only the plain store names, not the card ones.
    python3 -I "$BASE/tools/float_loads.py" --gen "$SRC/generated/default" >> "$ROOT/passes.log" 2>&1 \
        || { cat "$ROOT/passes.log"; exit 1; }
    python3 "$BASE/tools/store_cards.py" --gen "$SRC/generated/default" > "$ROOT/store-cards.log" 2>&1 \
        || { tail -20 "$ROOT/store-cards.log"; exit 1; }
    # stwcx. / stdcx.: the card tool knows the CAS form of the base's newer codegen (REX_GUEST_CAS); this CLI writes
    # __sync_bool_compare_and_swap. Their card goes after the store (a failed CAS marks too: one upload more).
    sed -E -i 's/^(\t[A-Za-z0-9_.]+\.eq = __sync_bool_compare_and_swap\(reinterpret_cast<uint(32|64)_t\*>\(REX_RAW_ADDR\(ea\)\), .*\);)$/\1 REX_CARD(ea);/' \
        "$SRC"/generated/default/*.cpp
    if cat "$SRC"/generated/default/*.cpp | grep '__sync_bool_compare_and_swap' | grep -vq 'REX_CARD(ea);$'; then
        echo "a CAS without its store card (switch/build-base.sh)"; exit 1
    fi
    # Item 11 (the base's tools/guest_cas.py): every CAS through REX_GUEST_CAS; the cmake option HCU_GUEST_CAS picks the
    # form (OFF: REX_GUEST_CAS_LEGACY, the same builtin as before), so both flavours come from this one tree.
    python3 -I "$BASE/tools/guest_cas.py" --gen "$SRC/generated/default" >> "$ROOT/store-cards.log" 2>&1 \
        || { tail -5 "$ROOT/store-cards.log"; exit 1; }
    if cat "$SRC"/generated/default/*.cpp | grep -q '__sync_bool_compare_and_swap'; then
        echo "a CAS that tools/guest_cas.py did not rewrite"; exit 1
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
    # Item 11c (the base's post-codegen steps; each has a cmake option that switches it off at compile time, so every
    # flavour comes from this one tree). tools/float_loads.py: a single-precision load as `ldr s` + `rev32` in the
    # vector unit instead of `ldr w` + `rev` + `fmov` (the same four bytes). Its `ldr s` goes through REX_GUEST_VOLATILE
    # like every other guest load (the base copies with memcpy: not volatile), so the default stays volatile-exact.
    # -DHCU_FLOAT_LOADS=OFF defines REX_FLOAT_LOADS_INTEGER: the loads as before (flavour "-nofl"). The tool runs right
    # after the codegen (above); this edit after pch_no_volatile.py, which skips a pch that names REX_GUEST_VOLATILE.
    sed -i 's|^  __builtin_memcpy(&f, p, 4);  // ldr s$|  f = *(REX_GUEST_VOLATILE const float*)p;  // ldr s (switch/build-base.sh: volatile)|' "$PCH"
    # tools/pch_cold_traps.py: ppc_trap (the trap before each division and at the asserts) cold and out of line, so the
    # hot path of a dividing function is the division alone. -DHCU_COLD_TRAPS=OFF passes -DREX_TRAP_ATTR= ("-notrap").
    python3 -I "$BASE/tools/pch_cold_traps.py" "$PCH" >> "$ROOT/passes.log" 2>&1 || { cat "$ROOT/passes.log"; exit 1; }
    # tools/inline_memcalls.py: a call of the CRT memcpy (sub_82819390, first instruction `std r3,-8(r1)` as the tool
    # needs) or memset (sub_828198C0) with a constant size of 1-64 bytes copies inline; an overlap the original smears,
    # a range outside plain memory (MMIO, physical 0xA0000000 and up, as src/crt_native.cpp) or a destination over the
    # r1-8 slot calls the function as before. -DHCU_INLINE_MEMCALLS=OFF defines REX_INLINE_MEMCALLS_OFF ("-nomc").
    cat "$SRC"/generated/default/*.cpp | grep -A12 '^DEFINE_REX_FUNC(sub_82819390) {' | grep -m1 $'^\t// ' \
        | grep -qx $'\t// std r3,-8(r1)' || { echo "sub_82819390 does not start with std r3,-8(r1) (tools/inline_memcalls.py)"; exit 1; }
    python3 -I "$BASE/tools/inline_memcalls.py" --gen "$SRC/generated/default" --memcpy 82819390 --memset 828198C0 \
        >> "$ROOT/passes.log" 2>&1 || { cat "$ROOT/passes.log"; exit 1; }
    echo "$CG_STAMP" > "$SRC/generated/.hcu-codegen-stamp"
fi
# Item 11 of docs/bible-checklist.md (the base's post-codegen step tools/direct_calls.py): a call to a game function
# without a hook goes to __imp__sub_X, the same function but not a weak alias, so GCC and LTO may inline it. Every
# build, because a hook in src/ changes the list without a new codegen; --redo writes only the files that change.
# The hooked list is every 82xxxxxx address in the code of HCU and the SDK (comments not counted; direct_calls.py's
# own scan, on HCU's folders), and in config/overrides.toml (only function bounds there: safe, a few calls stay weak).
# -DHCU_DIRECT_CALLS=OFF gives back the plain calls (the A/B flavour "-nodirect").
python3 -I - "$BASE/tools" "$SRC" "$SDK" > "$ROOT/hooked.txt" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import direct_calls
s, k = sys.argv[2], sys.argv[3]
direct_calls.SOURCES = [s + '/src', s + '/switch', k + '/src', k + '/include', s + '/config/overrides.toml',
                        s + '/hard_corps_uprising_manifest.toml', s + '/CMakeLists.txt']
print('\n'.join(sorted(direct_calls.hooked(s + '/generated/default'))))
EOF
grep -q 'sub_825520F8' "$ROOT/hooked.txt" || { echo "the hooked list misses REX_HOOK_RAW(sub_825520F8) (src/window_host.cpp)"; exit 1; }
if [ "$DIRECT_CALLS" = 1 ]; then
    python3 -I "$BASE/tools/direct_calls.py" --gen "$SRC/generated/default" --hooked-list "$ROOT/hooked.txt" --redo
    python3 -I "$BASE/tools/direct_calls.py" --gen "$SRC/generated/default" --hooked-list "$ROOT/hooked.txt" --check
else
    python3 -I "$BASE/tools/direct_calls.py" --gen "$SRC/generated/default" --undo
fi
# Without the cards the renderer would never see a guest write: the build stops. The same for the flavour switch.
PCH=$(ls "$SRC"/generated/default/*_pch.h)
[ "$(grep -c 'HCU_KEEP_VOLATILE\|^#define REX_GUEST_VOLATILE$\|loop barrier (tools/loop_barriers.py)' "$PCH")" -ge 3 ] \
    || { echo "no volatile switch or loop barrier macro in $PCH"; exit 1; }
cat "$ROOT/loop-barriers.log"
grep -q '#define REX_CARD(ea)' "$PCH" || { echo "no store cards in $PCH (tools/store_cards.py)"; exit 1; }
grep -q '(tools/guest_cas.py)' "$PCH" || { echo "no REX_GUEST_CAS in $PCH (tools/guest_cas.py)"; exit 1; }
grep -q '(switch/build-base.sh: volatile)$' "$PCH" || { echo "no volatile float load in $PCH (tools/float_loads.py)"; exit 1; }
grep -q '(tools/pch_cold_traps.py)' "$PCH" || { echo "no cold ppc_trap in $PCH (tools/pch_cold_traps.py)"; exit 1; }
grep -q '(tools/inline_memcalls.py)' "$PCH" || { echo "no inline memcpy in $PCH (tools/inline_memcalls.py)"; exit 1; }
if [ "$DFORM" = 1 ]; then
    grep -q '^#define REX_ADDR_D(r, d)' "$PCH" || { echo "no D-form macros in $PCH (dform_disp_split)"; exit 1; }
    grep -q '(tools/ro_constants.py)' "$PCH" || { echo "no REX_RO_* macros in $PCH (tools/ro_constants.py)"; exit 1; }
    echo "D-form accesses: $(cat "$SRC"/generated/default/*.cpp | grep -oE '\bREX_(LOAD|STORE)_D_' | wc -l)," \
         "read-only constants: $(cat "$SRC"/generated/default/*.cpp | grep -oE '\bREX_RO_(U8|U16|U32|U64|V128)\(' | wc -l)"
fi
cat "$ROOT/passes.log"
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
# A flavour NRO names itself in hbmenu and in every log line that carries the version.
[ "$NO_VOLATILE" = 1 ] && VERSION="$VERSION-novol"
[ "$DIRECT_CALLS" = 0 ] && VERSION="$VERSION-nodirect"
[ "$GUEST_CAS" = 1 ] && VERSION="$VERSION-cas"
[ "$METER" = 1 ] && VERSION="$VERSION-meter"
[ "$FLOAT_LOADS" = 0 ] && VERSION="$VERSION-nofl"
[ "$COLD_TRAPS" = 0 ] && VERSION="$VERSION-notrap"
[ "$INLINE_MEMCALLS" = 0 ] && VERSION="$VERSION-nomc"
[ "$FPCR_ONE" = 1 ] && VERSION="$VERSION-fpcr1"
[ "$DFORM" = 1 ] && VERSION="$VERSION-dform"
[ "$DFORM" = 1 ] && [ "$RO_CONSTANTS" = 0 ] && VERSION="$VERSION-noro"
VERSION="$VERSION$CG_SUFFIX"
# The instrumented NRO is slow (never a speed test): hbmenu and its logs must say so.
[ "$PGO" = generate ] && VERSION="$VERSION-pgogen"
[ "$PGO" = use ] && VERSION="$VERSION-pgo"
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
    -DREXSDK_DIR="$SDK" -DHCU_VERSION="$VERSION" -DHCU_NO_VOLATILE=OFF -DHCU_GUEST_CAS=OFF -DHCU_NX_METER=OFF -DHCU_FLOAT_LOADS=ON -DHCU_COLD_TRAPS=ON -DHCU_INLINE_MEMCALLS=ON -DHCU_FPCR_ONE_MODE=OFF -DHCU_RO_CONSTANTS=ON -DHCU_PGO= "$@" > "$BUILD/configure.log" 2>&1 \
    || { tail -30 "$BUILD/configure.log"; exit 1; }
# A failed build must not leave the previous NRO behind.
rm -f "$BUILD/hcu.nro"
# -k 0: every failing file in one run (a long build should not stop at the first error).
cmake --build "$BUILD" --parallel "${HCU_JOBS:-12}" -- -k 0 > "$BUILD/build.log" 2>&1 \
    || { grep -E 'error|Error|undefined reference' "$BUILD/build.log" | head -40; tail -5 "$BUILD/build.log"; exit 1; }
ls -la "$BUILD"/hcu.nro "$BUILD"/hcu  # hcu = the ELF with symbols (addr2line)
# A direct call goes past a hook: every function the ELF overrides (sub_X not at the address of __imp__sub_X) must be on
# the hooked list. 0.354 overrides 3 (src/frame_meter.cpp, src/crt_native.cpp x2; the other REX_HOOK_RAW are PC-only).
MISSED=$(aarch64-none-elf-nm "$BUILD/hcu" | awk '$3 ~ /^(__imp__)?sub_8[0-9A-F]{7}$/ { n = $3; sub(/^__imp__/, "", n);
    if ($3 ~ /^__imp__/) imp[n] = $1; else s[n] = $1 } END { for (k in s) if (!(k in imp) || imp[k] != s[k]) print k }' \
    | grep -vxFf "$ROOT/hooked.txt" || true)
[ -z "$MISSED" ] || { echo "functions with a hook that direct calls skip (not in $ROOT/hooked.txt):" $MISSED; exit 1; }
sha1sum "$BUILD"/hcu.nro
# The NACP display version holds 15 characters: hbmenu shows a flavour suffix cut ("-n"); the logs carry it whole.
python3 "$SRC/tools/nro_nacp.py" "$BUILD/hcu.nro" "${VERSION:0:15}"
