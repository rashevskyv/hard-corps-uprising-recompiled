#!/usr/bin/env bash
# Builds ReXGlue's librexruntime.so from the pinned v0.10.0 source with this project's patches and installs it
# into an extracted linux-amd64 SDK folder. package-linux.sh runs this itself; see docs/rexglue-runtime-patch.md.
#
#   tools/build-patched-rexruntime.sh SDK_DIR [WORK_DIR]    build and install (network needed the first time)
#   tools/build-patched-rexruntime.sh --check SDK_DIR       only verify that SDK_DIR holds the patched runtime
#   tools/build-patched-rexruntime.sh --stress SDK_DIR BUILD_DIR   run the allocator stress test (see below)
#   tools/build-patched-rexruntime.sh --loader-test SDK_DIR BUILD_DIR   run the RenderDoc detection test (see below)
#
# WORK_DIR holds the fetched source and its build (default: out/rexglue-sdk-src in this repository).
# Needs git, clang-20, clang++-20, cmake 3.25+, ninja and the packages listed in docs/rexglue-runtime-patch.md.
# --stress and --loader-test run hcu_physical_heap_stress and hcu_renderdoc_detect_tests from an already configured
# project build in BUILD_DIR.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH_DIR="$ROOT/patches/rexglue-sdk"
# Applied in this order. The other patches in the folder change the D3D12 GPU plugin and Windows-only runtime code.
PATCHES=(0001-physical-release-order.patch 0010-renderdoc-detect-loaded-only.patch)
STAMP_NAME=rexruntime-patch.txt
LIB=lib/librexruntime.so

# shellcheck source=../patches/rexglue-sdk/baseline.env
. "$PATCH_DIR/baseline.env"

fail() { echo "ERROR: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
patch_names() { local IFS=,; echo "${PATCHES[*]}"; }
patch_set_sha() {  # every patch's name and hash, in order
  local name
  for name in "${PATCHES[@]}"; do echo "$name $(sha "$PATCH_DIR/$name")"; done | sha256sum | cut -d' ' -f1
}
for name in "${PATCHES[@]}"; do [ -f "$PATCH_DIR/$name" ] || fail "missing patch: patches/rexglue-sdk/$name"; done

check_stamp() {  # SDK_DIR
  local sdk="$1" stamp="$1/$STAMP_NAME" problems=0
  [ -f "$stamp" ] || { echo "  $STAMP_NAME is missing" >&2; return 1; }
  [ "$(sed -n 's/^sdk_commit=//p' "$stamp")" = "$SDK_COMMIT" ] ||
    { echo "  runtime was not built from ReXGlue $SDK_TAG ($SDK_COMMIT)" >&2; problems=1; }
  [ "$(sed -n 's/^patches=//p' "$stamp")" = "$(patch_names)" ] &&
    [ "$(sed -n 's/^patch_set_sha256=//p' "$stamp")" = "$(patch_set_sha)" ] ||
    { echo "  runtime was built with other patches than patches/rexglue-sdk (rebuild it)" >&2; problems=1; }
  [ -f "$sdk/$LIB" ] || { echo "  missing: $LIB" >&2; return 1; }
  local actual
  actual="$(sha "$sdk/$LIB")"
  grep -qx "$actual  $LIB" "$stamp" || { echo "  $LIB does not match the patched build" >&2; problems=1; }
  [ "$actual" != "$OFFICIAL_LINUX_RUNTIME_SHA256" ] || { echo "  $LIB is the stock ReXGlue release file" >&2; problems=1; }
  return "$problems"
}

if [ "${1:-}" = --check ]; then
  [ -n "${2:-}" ] || fail "usage: $0 --check SDK_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime"
  exit 0
fi

if [ "${1:-}" = --loader-test ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --loader-test SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime"
  cmake --build "$3" --target hcu_renderdoc_detect_tests
  (cd "$3" && ./hcu_renderdoc_detect_tests)
  exit 0
fi

if [ "${1:-}" = --stress ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --stress SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime"
  cmake --build "$3" --target hcu_physical_heap_stress
  (cd "$3" && ./hcu_physical_heap_stress | grep -E '^(view|PASS|FAIL)')
  exit 0
fi

SDK="${1:-}"
[ -n "$SDK" ] && [ -d "$SDK/lib/cmake/rexglue" ] ||
  fail "usage: $0 SDK_DIR [WORK_DIR] (SDK_DIR is the extracted linux-amd64 SDK)"
SRC="${2:-$ROOT/out/rexglue-sdk-src}"

for tool in git clang-20 clang++-20 cmake ninja sha256sum; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done

if [ ! -d "$SRC/.git" ]; then
  echo "== Fetching ReXGlue $SDK_TAG from $SDK_REPO (network) into $SRC"
  mkdir -p "$(dirname "$SRC")"
  git clone --branch "$SDK_TAG" --depth 1 --recurse-submodules --shallow-submodules --jobs 8 "$SDK_REPO" "$SRC"
fi
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
[ "$COMMIT" = "$SDK_COMMIT" ] || fail "$SRC is at $COMMIT; expected $SDK_TAG $SDK_COMMIT (delete the folder to fetch it again)"

echo "== Patch"
git -C "$SRC" checkout -- src include tests
for name in "${PATCHES[@]}"; do
  git -C "$SRC" apply --check "$PATCH_DIR/$name" || fail "$name no longer applies to ReXGlue $SDK_TAG"
  git -C "$SRC" apply "$PATCH_DIR/$name"
done

echo "== Build librexruntime.so (Release)"
# -ffile-prefix-map keeps the build folder out of the runtime's log strings.
FLAGS="-march=x86-64-v2 -ffile-prefix-map=$SRC=."
(
  cd "$SRC"
  cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS" >/dev/null
  cmake --build out/build/linux-amd64 --config Release --target rexruntime --parallel
)
BUILT="$SRC/out/linux-amd64/Release/librexruntime.so"
[ -f "$BUILT" ] || fail "ReXGlue build output is missing $BUILT"
[ "$(sha "$BUILT")" != "$OFFICIAL_LINUX_RUNTIME_SHA256" ] || fail "librexruntime.so came out identical to the stock release; the patches were not built in"
! grep -qaF "$SRC" "$BUILT" || fail "librexruntime.so embeds the build folder"

cp "$BUILT" "$SDK/$LIB"
{
  echo "sdk_commit=$SDK_COMMIT"
  echo "patches=$(patch_names)"
  echo "patch_set_sha256=$(patch_set_sha)"
  echo "$(sha "$SDK/$LIB")  $LIB"
} > "$SDK/$STAMP_NAME"
check_stamp "$SDK" || fail "the installed runtime failed its own check"
echo "Patched ReXGlue runtime installed: $SDK/$LIB"
cat "$SDK/$STAMP_NAME"
