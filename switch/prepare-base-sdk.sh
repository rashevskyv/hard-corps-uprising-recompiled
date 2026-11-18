#!/bin/bash
# Variant B (branch renut-ultimate): the SDK of the ultimate base under HCU's app and deko3d back end.
#   switch/prepare-base-sdk.sh [OUT]   (WSL; OUT default /root/renut-ult/sdk)
# 1. `git archive` of sdk/ at switch/BASE_COMMIT from the base clone (BASE_REPO), into a fresh OUT. Only sdk/ is taken:
#    it is BSD-3-Clause (the base's THIRD_PARTY_NOTICES.md); its app/, tools/ and docs/ are GPL-3.0 and are not copied.
# 2. CRLF -> LF in every text file (the base's checkout has CRLF in 983 files outside thirdparty/; HCU's patches and
#    switch/sdk-edits*.py anchor on LF text). OUT becomes a git repo with the result as its first commit.
# 3. HCU's patches/rexglue-sdk (common and .linux; .windows skipped) in order, fuzz 3. Failed hunks are counted and
#    listed in OUT/.hcu-patch-report.txt; the script does not stop on them (probe stage).
# The edits (switch/sdk-edits*.py) are NOT run: they port the stock SDK to Horizon, and the base has its own Horizon
# layer (REXGLUE_PLATFORM_SWITCH: memory mirrors, threads, fibers, window, audout, input). See the port plan in the
# commit message that added this file.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
BASE_REPO=${BASE_REPO:-/mnt/d/git/dev/renut-ref/renut_nx_ultimate_base}
OUT=${1:-/root/renut-ult/sdk}
COMMIT=$(cat "$HERE/switch/BASE_COMMIT")
rm -rf "$OUT"
mkdir -p "$OUT.tmp"
git -c safe.directory='*' -C "$BASE_REPO" archive "$COMMIT" sdk | tar -x -C "$OUT.tmp"
mv "$OUT.tmp/sdk" "$OUT"
rmdir "$OUT.tmp"
cd "$OUT"
grep -rlIZ $'\r' . | xargs -0 -r sed -i 's/\r$//'
git init -q
git -c core.autocrlf=false -c core.safecrlf=false add -A
git -c user.email=hcu@local -c user.name=hcu commit -qm "ultimate base sdk at $COMMIT, LF"
report=$OUT/.hcu-patch-report.txt
: > "$report"
total=0
failed=0
for p in "$HERE"/patches/rexglue-sdk/*.patch; do
    case $p in *.windows.patch) continue ;; esac
    h=$(grep -c '^@@' "$p")
    out=$(patch -p1 -F3 -f --no-backup-if-mismatch -r - < "$p" 2>&1 || true)
    f=$(echo "$out" | grep -oE '[0-9]+ out of [0-9]+ hunks? FAILED' | awk '{s += $1} END {print s + 0}')
    total=$((total + h))
    failed=$((failed + f))
    echo "$(basename "$p"): $f of $h hunks failed" >> "$report"
    echo "$out" | grep -E 'FAILED|can.t find file' | sed 's/^/    /' >> "$report" || true
done
echo "patches: $failed of $total hunks failed" | tee -a "$report"
# 4. The failed hunks, merged by hand onto the base's text (patches/rexglue-sdk-base/0001): 0003+0019 (command
#    processor idle loop: HCU's yield burst, background-work polling, non-alertable wait, on every platform), 0017 and
#    0032 (Vulkan transfers and deferred resolve, the base's Switch perf lines kept where they still fit), 0033 (guest
#    waits in WaitMultiple, on the base's (handles, count) signature). Dropped because the base already has them or a
#    better form: 0004's primitive cache bound, 0034's exp_adjust word, 0006's timer queue (the base blocks on a
#    condition variable; the hunk that applied is reverted), 0021's multi-wait (the base parks on a wake point).
#    Also reverted, because fuzz 3 put them in the wrong place: 0006's critical-section spin (it landed in
#    xeTryEnterCriticalSection; the base spins in xeEnterCriticalSection, count from the cvar kernel_critsec_spin)
#    and 0035's swapchain_format line (the base already sets it, further down). 10 hunks applied with fuzz; the
#    other 8 were checked against their hunk text.
git add -A && git -c user.email=hcu@local -c user.name=hcu commit -qm "HCU patches, fuzz 3"
patch -p1 --no-backup-if-mismatch < "$HERE/patches/rexglue-sdk-base/0001-hcu-failed-hunks-on-base.patch"
git add -A && git -c user.email=hcu@local -c user.name=hcu commit -qm "HCU failed hunks merged onto the base"
echo "failed hunks merged: $(git diff --stat HEAD~1 | tail -1)" | tee -a "$report"
# 5. thirdparty/FFmpeg/config.h, the per-platform dispatcher: FFmpeg's own .gitignore hides it, so the base's repo
#    never got it and no SDK target builds without it. Same text as the stock SDK's after switch/sdk-edits.py.
cp "$HERE/patches/rexglue-sdk-base/ffmpeg-config.h" thirdparty/FFmpeg/config.h
git add -f thirdparty/FFmpeg/config.h && git -c user.email=hcu@local -c user.name=hcu commit -qm "FFmpeg config.h"
