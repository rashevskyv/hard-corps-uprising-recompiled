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
