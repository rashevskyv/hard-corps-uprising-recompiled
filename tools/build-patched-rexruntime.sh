#!/usr/bin/env bash
# Builds ReXGlue's librexruntime.so and librexgpu-xenos.so (the Vulkan GPU plugin) from the pinned v0.10.0 source with
# this project's patches and installs them into an extracted linux-amd64 SDK folder. package-linux.sh runs this itself;
# see docs/rexglue-runtime-patch.md.
#
#   tools/build-patched-rexruntime.sh SDK_DIR [WORK_DIR]    build and install (network needed the first time)
#   tools/build-patched-rexruntime.sh --check SDK_DIR       only verify that SDK_DIR holds the patched libraries
#   tools/build-patched-rexruntime.sh --list                print the patches applied here, in order, and the fingerprint
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
PATCH_PLATFORM=linux
STAMP_NAME=rexruntime-patch.txt
LIBS=(lib/librexruntime.so lib/librexgpu-xenos.so)
# How the patched files are built; part of the fingerprint, so changing them invalidates the installed files.
BUILD_FLAGS=-march=x86-64-v2
LINK_RPATH='$ORIGIN:$ORIGIN/../lib'
BUILD_TARGETS=(rexruntime rexgpu-xenos)

# shellcheck source=../patches/rexglue-sdk/baseline.env
. "$PATCH_DIR/baseline.env"

fail() { echo "ERROR: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
text_sha() { sha256sum | cut -d' ' -f1; }
join_by() { local IFS="$1"; shift; echo "$*"; }

# The patches in the order they are applied: NNNN-name.patch applies everywhere, NNNN-name.windows.patch only on
# Windows and NNNN-name.linux.patch only here. Sorted by name; the numbers are unique, so the order is unambiguous.
PATCHES=()
collect_patches() {
  local file name number
  local -A numbers=()
  local -a names=()
  shopt -s nullglob
  for file in "$PATCH_DIR"/*.patch; do
    name="$(basename "$file")"
    [[ "$name" =~ ^([0-9]{4})-[a-z0-9]+(-[a-z0-9]+)*(\.(windows|linux))?\.patch$ ]] ||
      fail "patches/rexglue-sdk/$name is not named NNNN-name[.windows|.linux].patch"
    number="${BASH_REMATCH[1]}"
    [ -z "${numbers[$number]:-}" ] ||
      fail "patches/rexglue-sdk/$name and ${numbers[$number]} share the number $number"
    numbers[$number]="$name"
    case "$name" in
      *.windows.patch) ;;
      *) names+=("$name") ;;
    esac
  done
  shopt -u nullglob
  [ "${#names[@]}" -gt 0 ] || fail "no patches in patches/rexglue-sdk"
  mapfile -t PATCHES < <(printf '%s\n' "${names[@]}" | LC_ALL=C sort)
}
collect_patches

patch_names() { join_by , "${PATCHES[@]}"; }
patch_set_sha() {  # every patch's name and hash, in order
  local name
  for name in "${PATCHES[@]}"; do echo "$name $(sha "$PATCH_DIR/$name")"; done | text_sha
}
inputs_sha() {  # everything the installed files are built from
  {
    echo "sdk_version=$SDK_VERSION"
    echo "sdk_tag=$SDK_TAG"
    echo "sdk_commit=$SDK_COMMIT"
    echo "build_flags=$BUILD_FLAGS"
    echo "link_rpath=$LINK_RPATH"
    echo "build_targets=$(join_by , "${BUILD_TARGETS[@]}")"
    local name
    for name in "${PATCHES[@]}"; do echo "patch $name $(sha "$PATCH_DIR/$name")"; done
  } | text_sha
}
official_sha() {  # lib
  case "$1" in
    lib/librexruntime.so) echo "$OFFICIAL_LINUX_RUNTIME_SHA256" ;;
    lib/librexgpu-xenos.so) echo "$OFFICIAL_LINUX_GPU_SHA256" ;;
    *) fail "no stock hash for $1" ;;
  esac
}

inputs_change() {  # stamp: which inputs differ from the ones the stamp was written for
  local old current added removed name
  old="$(sed -n 's/^patches=//p' "$1")"
  current="$(patch_names)"
  added=""; removed=""
  for name in ${current//,/ }; do case ",$old," in *",$name,"*) ;; *) added="$added $name" ;; esac; done
  for name in ${old//,/ }; do case ",$current," in *",$name,"*) ;; *) removed="$removed $name" ;; esac; done
  if [ -n "$added" ]; then echo "patches added:$added"
  elif [ -n "$removed" ]; then echo "patches removed or renamed:$removed"
  elif [ "$(sed -n 's/^patch_sha256=//p' "$1")" != "$(patch_set_sha)" ]; then echo "a patch changed"
  else echo "ReXGlue baseline or build settings changed"; fi
}

check_stamp() {  # SDK_DIR
  local sdk="$1" stamp="$1/$STAMP_NAME" problems=0 lib actual
  [ -f "$stamp" ] || { echo "  $STAMP_NAME is missing" >&2; return 1; }
  [ "$(sed -n 's/^sdk_commit=//p' "$stamp")" = "$SDK_COMMIT" ] ||
    { echo "  libraries were not built from ReXGlue $SDK_TAG ($SDK_COMMIT)" >&2; problems=1; }
  [ "$(sed -n 's/^inputs_sha256=//p' "$stamp")" = "$(inputs_sha)" ] ||
    { echo "  libraries were built from other inputs than the current ones ($(inputs_change "$stamp"))" >&2; problems=1; }
  for lib in "${LIBS[@]}"; do
    grep -q "^[0-9a-f]\{64\}  $lib\$" "$stamp" || { echo "  $lib is not in $STAMP_NAME" >&2; problems=1; continue; }
    [ -f "$sdk/$lib" ] || { echo "  missing: $lib" >&2; problems=1; continue; }
    actual="$(sha "$sdk/$lib")"
    grep -qx "$actual  $lib" "$stamp" || { echo "  $lib does not match the patched build" >&2; problems=1; }
    [ "$actual" != "$(official_sha "$lib")" ] || { echo "  $lib is the stock ReXGlue release file" >&2; problems=1; }
  done
  return "$problems"
}

if [ "${1:-}" = --list ]; then
  printf '%s\n' "${PATCHES[@]}"
  echo "fingerprint $(inputs_sha)"
  exit 0
fi

if [ "${1:-}" = --check ]; then
  [ -n "${2:-}" ] || fail "usage: $0 --check SDK_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime and GPU plugin"
  echo "ReXGlue patches: current (${#PATCHES[@]} patches, fingerprint $(inputs_sha | cut -c1-12))"
  exit 0
fi

if [ "${1:-}" = --loader-test ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --loader-test SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime and GPU plugin"
  cmake --build "$3" --target hcu_renderdoc_detect_tests
  (cd "$3" && ./hcu_renderdoc_detect_tests)
  exit 0
fi

if [ "${1:-}" = --stress ]; then
  [ -n "${2:-}" ] && [ -n "${3:-}" ] || fail "usage: $0 --stress SDK_DIR BUILD_DIR"
  check_stamp "$2" || fail "$2 does not hold the patched ReXGlue runtime and GPU plugin"
  cmake --build "$3" --target hcu_physical_heap_stress
  (cd "$3" && ./hcu_physical_heap_stress | grep -E '^(view|PASS|FAIL)')
  exit 0
fi

SDK="${1:-}"
[ -n "$SDK" ] && [ -d "$SDK/lib/cmake/rexglue" ] ||
  fail "usage: $0 SDK_DIR [WORK_DIR] (SDK_DIR is the extracted linux-amd64 SDK)"
SRC="${2:-$ROOT/out/rexglue-sdk-src}"

for tool in git clang-20 clang++-20 cmake ninja sha256sum readelf; do
  command -v "$tool" >/dev/null || fail "$tool not found"
done

if [ ! -d "$SRC/.git" ]; then
  echo "== Fetching ReXGlue $SDK_TAG from $SDK_REPO (network) into $SRC"
  mkdir -p "$(dirname "$SRC")"
  git clone --branch "$SDK_TAG" --depth 1 --recurse-submodules --shallow-submodules --jobs 8 "$SDK_REPO" "$SRC"
fi
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
[ "$COMMIT" = "$SDK_COMMIT" ] || fail "$SRC is at $COMMIT; expected $SDK_TAG $SDK_COMMIT (delete the folder to fetch it again)"

echo "== Patch ($(patch_names))"
git -C "$SRC" checkout -- src include tests
git -C "$SRC" clean -fdq -- src include tests
for name in "${PATCHES[@]}"; do
  git -C "$SRC" apply --check "$PATCH_DIR/$name" || fail "$name no longer applies to ReXGlue $SDK_TAG"
  git -C "$SRC" apply "$PATCH_DIR/$name"
done

echo "== Build ${BUILD_TARGETS[*]} (Release)"
# -ffile-prefix-map keeps the build folder out of the libraries' log strings; the RUNPATH is the stock libraries'
# instead of the build folder and the empty entries CMake would add.
FLAGS="$BUILD_FLAGS -ffile-prefix-map=$SRC=."
(
  cd "$SRC"
  cmake --preset linux-amd64 -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
    "-DCMAKE_C_FLAGS=$FLAGS" "-DCMAKE_CXX_FLAGS=$FLAGS" -DCMAKE_SKIP_RPATH=ON \
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-rpath,'$LINK_RPATH'" >/dev/null
  cmake --build out/build/linux-amd64 --config Release --target "${BUILD_TARGETS[@]}" --parallel
)
for lib in "${LIBS[@]}"; do
  BUILT="$SRC/out/linux-amd64/Release/$(basename "$lib")"
  [ -f "$BUILT" ] || fail "ReXGlue build output is missing $BUILT"
  [ "$(sha "$BUILT")" != "$(official_sha "$lib")" ] || fail "$(basename "$lib") came out identical to the stock release; the patches were not built in"
  ! grep -qaF "$SRC" "$BUILT" || fail "$(basename "$lib") embeds the build folder"
  readelf -d "$BUILT" | grep -qF "[$LINK_RPATH]" || fail "$(basename "$lib") does not have the RUNPATH $LINK_RPATH"
done

rm -f "$SDK/$STAMP_NAME"
for lib in "${LIBS[@]}"; do cp "$SRC/out/linux-amd64/Release/$(basename "$lib")" "$SDK/$lib"; done
{
  echo "sdk_version=$SDK_VERSION"
  echo "sdk_commit=$SDK_COMMIT"
  echo "inputs_sha256=$(inputs_sha)"
  echo "patch_sha256=$(patch_set_sha)"
  echo "patches=$(patch_names)"
  for lib in "${LIBS[@]}"; do echo "$(sha "$SDK/$lib")  $lib"; done
} > "$SDK/$STAMP_NAME"
check_stamp "$SDK" || fail "the installed libraries failed their own check"
echo "Patched ReXGlue runtime and GPU plugin installed:"
cat "$SDK/$STAMP_NAME"
