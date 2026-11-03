#!/bin/bash
# Packs the release (WSL): hcu-<version>-switch.zip, the PC builds when given (HCU_LINUX, HCU_WINDOWS), SHA1SUMS and
# SHA256SUMS into /root/hcu/release/<version>/.
#   scripts/package-release.sh [switch stage dir, default /root/hcu/src/out/build/switch/stage]
# Build first (switch/build.sh). The version comes from the build's hcu_version.h ("0.<count>.<sha7>").
# To add files to a published release: HCU_SWITCH_ZIP=<the published zip> keeps its hash in the sums.
# The archive holds the recompiled program and the translated shaders only; the script refuses game files.
set -eu
STAGE=${1:-/root/hcu/src/out/build/switch/stage}
GIT_DIR=${HCU_GIT_DIR:-/mnt/d/git/dev/hcu-switch}
V=$(grep -o '"0\.[0-9]*\.[0-9a-f]*+\?' "$(dirname "$STAGE")/hcu_version.h" | tr -d '"')
case "$V" in *+) echo "dirty build $V: commit first" >&2; exit 1;; esac
[ -f "$STAGE/hcu.nro" ] && [ -f "$STAGE/deko3d" ] && [ -d "$STAGE/shaders" ] || { echo "no stage in $STAGE" >&2; exit 1; }
grep -q "deko3d backend up" "$STAGE/hcu.nro" || { echo "the NRO has no deko3d backend" >&2; exit 1; }
OUT=/root/hcu/release/$V
rm -rf "$OUT"; mkdir -p "$OUT/pkg"; cd "$OUT"

# Switch: unzip to the SD root.
S=pkg/switch/switch/hcu; mkdir -p $S/game
cp "$STAGE/hcu.nro" "$STAGE/deko3d" $S/; cp -r "$STAGE/shaders" $S/
echo "Put default.xex and the data folders from your own copy of the game here." > $S/game/PUT_GAME_FILES_HERE.txt
cp "$GIT_DIR/README.md" "$GIT_DIR/LICENSE" pkg/switch/; echo "$V" > pkg/switch/VERSION
bad=$(find pkg -type f \( -iname '*.xex' -o -iname '*.xexp' -o -iname '*.dds' -o -iname '*.arc' -o -iname '*.xma' \
      -o -iname '*.xwb' -o -iname '*.bin' -o -iname '*.pak' -o -iname '*.jpg' \)) || true
[ -z "$bad" ] || { echo "game data in the package: $bad" >&2; exit 1; }
# HCU_SWITCH_ZIP: the already published Switch zip of this version (a new zip never has the same hash).
if [ -n "${HCU_SWITCH_ZIP:-}" ]; then cp "$HCU_SWITCH_ZIP" "hcu-$V-switch.zip"; else (cd pkg/switch && zip -qr "../../hcu-$V-switch.zip" .); fi
rm -rf pkg
# The PC builds, when given: HCU_LINUX = the AppImage of tools/package-linux.sh, HCU_WINDOWS = the zip of
# tools/package-release.ps1 (both refuse game files themselves). They take the release's names.
[ -z "${HCU_LINUX:-}" ] || cp "$HCU_LINUX" "hcu-$V-linux-x86_64.AppImage"
[ -z "${HCU_WINDOWS:-}" ] || cp "$HCU_WINDOWS" "hcu-$V-windows-x64.zip"
sha1sum hcu-$V-* > SHA1SUMS
sha256sum hcu-$V-* > SHA256SUMS
echo "NRO SHA-1: $(sha1sum "$STAGE/hcu.nro" | cut -c1-40)  version $V"
ls -la; cat SHA1SUMS SHA256SUMS
