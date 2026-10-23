#!/bin/bash
# Incremental Linux build of the game (and the GPU plugin, with the deko3d-over-OpenGL test backend),
# then copies the plugin next to the exe (the SDK builds it in its own output directory).
#   HCU_JOBS=4 scripts/linux-build.sh
set -eu
B=${HCU_BUILD:-out/build/linux-testhost}
SDK=${REXSDK_DIR:-/home/user/rexglue-sdk}
cmake --build "$B" -j"${HCU_JOBS:-4}"
rm -f "$B/librexgpu-xenos.so"  # new inode: a running game keeps its mapping
cp -f "$SDK/out/linux-amd64/librexgpu-xenos.so" "$B/librexgpu-xenos.so"
