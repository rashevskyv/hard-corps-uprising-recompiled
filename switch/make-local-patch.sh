#!/bin/sh
# switch/cvhd-local.patch from the committed tree: sync without the patch, diff against HEAD, restore.
set -e
set -o pipefail 2>/dev/null || true
cd "$(dirname "$0")/.."
cp switch/cvhd-local.patch "${TMPDIR:-/tmp}/old.patch"
: > switch/cvhd-local.patch
sh switch/sync-cvhd.sh > /dev/null
git diff -R HEAD -- switch/host switch/sdk-edits.py switch/eden-shot.ps1 switch/eden-deko3d.ps1 src/gpu tools/xenos2glsl.py tools/nro_nacp.py > "${TMPDIR:-/tmp}/new.patch"
git checkout -q HEAD -- switch src tools
cp "${TMPDIR:-/tmp}/new.patch" switch/cvhd-local.patch
wc -l switch/cvhd-local.patch
