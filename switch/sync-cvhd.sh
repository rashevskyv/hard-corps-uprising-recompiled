#!/bin/sh
# Copies the files shared with cvhd-recomp (our Castlevania: Harmony of Despair port) at a pinned
# commit: the Switch host layer, the SDK edits, the trace and deko3d GPU backends, the shader translator.
# Renames cvhd -> hcu (sdmc:/switch/hcu/, hcu_log.txt, namespaces), then applies switch/cvhd-local.patch
# (the HCU changes to those files). HCU's own SDK edits are switch/sdk-edits-hcu.py, not shared.
#   switch/sync-cvhd.sh [commit]   (default: switch/CVHD_COMMIT; run from Git Bash or WSL)
# Do not edit the shared files by hand: change them in cvhd-recomp (or in cvhd-local.patch) and sync.
set -e
cd "$(dirname "$0")/.."
CVHD=${CVHD_DIR:-../cvhd-recomp}
COMMIT=${1:-$(cat switch/CVHD_COMMIT)}
FILES="switch/host switch/sdk-edits.py switch/eden-shot.ps1 src/gpu tools/xenos2glsl.py tools/nro_nacp.py"
rm -rf switch/host src/gpu
git -C "$CVHD" archive "$COMMIT" $FILES | tar -xf -
for f in $(grep -rli cvhd $FILES); do
    sed -i -e 's/cvhd-recomp/@@KEEP@@/g' -e 's/cvhd/hcu/g' -e 's/CVHD/HCU/g' -e 's/@@KEEP@@/cvhd-recomp/g' "$f"
done
git -C "$CVHD" rev-parse --short=7 "$COMMIT" > switch/CVHD_COMMIT
if [ -s switch/cvhd-local.patch ]; then
    git apply switch/cvhd-local.patch
fi
echo "synced cvhd-recomp $(cat switch/CVHD_COMMIT): $FILES"
