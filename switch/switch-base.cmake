# Nintendo Switch build on the ultimate base's SDK (renut_nx_ultimate_base sdk, BSD-3-Clause; its app and tools
# GPL-3.0, packaging/licenses/THIRD-PARTY-LICENSES.txt; the HCU edits of
# switch/sdk-edits-base.py on top), included from CMakeLists.txt when the base's toolchain file
# (tools/switch/cmake/switch-devkitA64.cmake of the base) sets REXGLUE_PLATFORM_SWITCH. switch/build-base.sh configures
# and builds hcu.nro. The base owns the Horizon layer (main, memory, threads, crash hooks, NRO packaging); HCU adds its
# deko3d renderer to the GPU plugin, its pad driver, its start-up shim (switch/hcu_nx_base.cpp) and its hooks.
# The old SDK's build (switch/switch.cmake, NINTENDO_SWITCH) stays for the fallback branch renut-base.

# The base's compile-wide choices that are exact (app/CMakeLists.txt of the base), before the SDK is added:
#   -mearly-ra=none: devkitA64 GCC 16.1's AArch64 early register allocation miscompiled table lookups (Peggle 2);
#   -mno-outline-atomics: the atomics inline (the A57 has no LSE: the helper was a call and a test for nothing);
#   REX_CARD_VIEWS_ONLY: the generated tree's store cards only for the physical views, written only when set.
# The base's inexact ones (CG-13 one FPU mode, VMX estimates, fused madd, near-exact) stay off: the game is bit-exact
# by default; each one is a later A/B behind its own option.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-mearly-ra=none;-mno-outline-atomics>")
add_link_options(-mearly-ra=none)
add_compile_definitions(REX_CARD_VIEWS_ONLY HCU_BASE_SDK)
# The SDK's libraries in this build folder (the base's rule: each folder builds them with its own LTO / PGO options).
set(REXGLUE_OUTPUT_IN_BUILD ON CACHE BOOL "The SDK's libraries and tools in this build folder")
# HCU_NX_METER: the CPU-work meter of src/frame_meter.cpp (call counts of the exports, the time in the waiting ones and
# in VdSwap, the command processor's waits; switch/sdk-edits-base.py). A diagnostic: no guest-visible change. OFF by
# default (2026-11-19): every export call paid a count and the waiting ones two clock reads (Eden: rex_nx_meter_leave
# 2.4 % of the busy samples). Without it the overlay shows fps only (no "m c g", no 30 s report): measurement builds
# take -DHCU_NX_METER=ON (flavour "-meter", switch/build-base.sh).
option(HCU_NX_METER "The CPU-work meter's counters in the SDK (diagnostic)" OFF)
if(HCU_NX_METER)
    add_compile_definitions(HCU_NX_METER)
endif()
# No rexglue target: the cross build cannot run the host CLI, and a codegen re-run inside the build would drop the
# store cards (tools/store_cards.py of the base). switch/build-base.sh runs codegen and the cards before the build.
set(REXGLUE_HOST_TARGET hcu)
# The trace backend and the deko3d renderer, compiled into the SDK's GPU plugin (the hook of sdk-edits.py's "Extra GPU
# backends from the game tree", kept by sdk-edits-base.py).
set(REXGPU_XENOS_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/trace_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/deko3d_backend.cpp")

include(generated/rexglue.cmake)

set(HCU_SWITCH_HOST "${CMAKE_CURRENT_SOURCE_DIR}/switch/host")
add_executable(hcu
    switch/hcu_nx_main.cpp
    switch/hcu_nx_base.cpp
    switch/hcu_pgo.cpp
    src/crt_native.cpp
    src/prolog_native.cpp
    src/frame_meter.cpp
    src/gpu_fence_wait.cpp
    src/settings_menu.cpp
    src/button_glyphs.cpp
    src/frame_recorder.cpp
    ${HCU_SWITCH_HOST}/input/nx_input_driver.cpp
)
rexglue_setup_target(hcu GPU_PLUGINS xenos)
# The pad driver's header lives with HCU's old host layer, whose include folder also replaces system headers (dlfcn.h,
# endian.h, switch_compat.h): only the files that need the header get it, and only its own subfolder's path.
set_property(SOURCE switch/hcu_nx_main.cpp ${HCU_SWITCH_HOST}/input/nx_input_driver.cpp APPEND PROPERTY
             INCLUDE_DIRECTORIES "${HCU_SWITCH_HOST}/include")
# Item 15a of docs/bible-checklist.md (the base's rule: both flags on every target that compiles guest code).
foreach(_t hcu hcu_recomp)
    target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-ffp-contract=off;-fno-strict-aliasing>")
endforeach()
# Item 9 of docs/bible-checklist.md: guest loads and stores without volatile, loop barriers at the guest loops that need
# them (switch/build-base.sh runs the base's tools/pch_no_volatile.py and tools/loop_barriers.py on the tree). OFF
# (default): HCU_KEEP_VOLATILE, the code as before. ON: the base's "optimised" flavour (masseffect-nx: +15-20 % fps);
# a risk of a hang at a guest wait that the barriers miss, so an A/B NRO first.
option(HCU_NO_VOLATILE "Guest loads and stores without volatile (with the loop barriers)" OFF)
if(NOT HCU_NO_VOLATILE)
    target_compile_definitions(hcu_recomp PRIVATE HCU_KEEP_VOLATILE)
endif()
# Item 11 of docs/bible-checklist.md (the base's tools/guest_cas.py, run by switch/build-base.sh): every stwcx. / stdcx.
# goes through REX_GUEST_CAS. OFF (default): REX_GUEST_CAS_LEGACY, the old __sync builtin with its trailing `dmb ish`.
# ON: acquire/release exclusives without that barrier (the guest's own lwsync / sync stay). Not a runtime key: the SDK's
# byte g_rex_atomic_legacy_barrier also drives the SDK's own atomics, which have no barrier now.
option(HCU_GUEST_CAS "Guest compare-and-swap without the trailing dmb" OFF)
if(NOT HCU_GUEST_CAS)
    target_compile_definitions(hcu_recomp PRIVATE REX_GUEST_CAS_LEGACY)
endif()
# Item 11c: the base's tools/float_loads.py (run by switch/build-base.sh). ON: a single-precision guest load as
# `ldr s` + `rev32` (the same bits, still volatile). OFF: REX_FLOAT_LOADS_INTEGER, the integer load as before.
option(HCU_FLOAT_LOADS "Single-precision guest loads in the vector unit" ON)
if(NOT HCU_FLOAT_LOADS)
    target_compile_definitions(hcu_recomp PRIVATE REX_FLOAT_LOADS_INTEGER)
endif()
# Item 15 of docs/bible-checklist.md: link-time optimisation (the base's BANJO_SWITCH_LTO and BANJO_SWITCH_SDK_OPT,
# app/CMakeLists.txt of the base). The game code, HCU's code, the GPU plugin (deko3d) and the SDK runtime are compiled
# to GIMPLE and optimised as one program at the link, which repeats -ffp-contract=off and -fno-strict-aliasing (the
# LTRANS stage must not fuse what the compile stage kept apart): exact. Out of LTO: the files that define --wrap's
# __wrap_ functions (the link must see them as ordinary definitions) and the ones with top-level asm naming C symbols.
option(HCU_SWITCH_LTO "Link-time optimisation of the game code, HCU's code, the GPU plugin and the SDK runtime" ON)
set(HCU_SWITCH_LTO_JOBS "4" CACHE STRING "Parallel LTRANS jobs of the LTO link")
if(HCU_SWITCH_LTO)
    set(_hcu_lto_compile -flto -fno-fat-lto-objects)
    foreach(_t hcu_recomp hcu rexgpu-xenos rexruntime libavcodec libavutil rexaudio rexcore rexfilesystem rexinput rexui)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_hcu_lto_compile}>")
            target_compile_definitions(${_t} PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
        endif()
    endforeach()
    target_link_options(hcu PRIVATE -flto=${HCU_SWITCH_LTO_JOBS} -flto-partition=balanced -ffp-contract=off
                        -fno-strict-aliasing)
    set_property(SOURCE switch/hcu_nx_base.cpp APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE "${REXSDK_DIR}/src/core/threading_switch.cpp" "${REXSDK_DIR}/src/ui/switch_perf.cpp"
                        "${REXSDK_DIR}/src/core/exception_handler_switch.cpp" "${REXSDK_DIR}/src/core/fiber_switch.cpp"
                 TARGET_DIRECTORY rexcore rexui APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    message(STATUS "hcu: LTO (${HCU_SWITCH_LTO_JOBS} jobs)")
endif()
# hcu_version.h (switch/build-base.sh writes it into the build folder); src for settings_menu.h; lz4 from portlibs.
target_include_directories(hcu PRIVATE src ${CMAKE_BINARY_DIR} "${DEVKITPRO}/portlibs/switch/include")
foreach(_dep imgui xxhash)
    if(TARGET ${_dep})
        target_include_directories(hcu PRIVATE $<TARGET_PROPERTY:${_dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endforeach()
# The base's main takes HCU's console defaults and args.txt first (switch/hcu_nx_base.cpp, __wrap_main); a link map
# for the profiler's samples.
target_link_options(hcu PRIVATE -Wl,--wrap=main "LINKER:-Map=${CMAKE_BINARY_DIR}/hcu.map")
option(HCU_DEKO3D_DEBUG "link the debug deko3d (argument checks, fault text in the log)" OFF)
target_link_libraries(hcu PRIVATE $<IF:$<BOOL:${HCU_DEKO3D_DEBUG}>,deko3dd,deko3d> zstd lz4 expat z nx m stdc++)

# Profile-guided optimisation (the base's BANJO_PGO, app/CMakeLists.txt of the base; GPL-3.0):
#   HCU_PGO=generate: an instrumented NRO; it streams its branch counts to pgo/banjo_pgo_<N>.stream next to the NRO
#     (switch/hcu_pgo.cpp). Slower than a plain build: never a speed test.
#   HCU_PGO=use: the counts merged on the PC by the base's tools/pgo_merge.py into HCU_PGO_DIR (one .gcda per object,
#     named by its path in the build folder: -fprofile-prefix-path lets the two builds use different folders).
# The game code, HCU's own code, the GPU plugin (deko3d) and the SDK runtime; the SDK's options change code only, the
# guest-visible results stay exact (no -ffast-math; -ffp-contract=off stays).
set(HCU_PGO "" CACHE STRING "Profile-guided optimisation: empty, generate or use")
# Outside the source folder: switch/build-base.sh's rsync --delete would remove it.
set(HCU_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
if(HCU_PGO)
    get_filename_component(_hcu_pgo_dir "${HCU_PGO_DIR}" ABSOLUTE)
    if(HCU_PGO STREQUAL "generate")
        set(_hcu_pgo "-fprofile-generate=${_hcu_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                     -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(hcu PRIVATE HCU_PGO_GENERATE=1)
    elseif(HCU_PGO STREQUAL "use")
        file(GLOB _hcu_pgo_gcda "${_hcu_pgo_dir}/*.gcda")
        if(NOT _hcu_pgo_gcda)
            message(FATAL_ERROR "HCU_PGO=use: no profile in ${_hcu_pgo_dir}")
        endif()
        # -fprofile-correction: the counts were taken without atomic updates while several threads ran the same code.
        set(_hcu_pgo "-fprofile-use=${_hcu_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                     -fprofile-correction -fno-profile-values -fprofile-partial-training -Wno-missing-profile
                     -Wno-coverage-mismatch)
    else()
        message(FATAL_ERROR "HCU_PGO must be empty, generate or use (got ${HCU_PGO})")
    endif()
    foreach(_t hcu_recomp hcu rexgpu-xenos rexruntime)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_hcu_pgo}>")
        endif()
    endforeach()
    target_link_options(hcu PRIVATE ${_hcu_pgo})
    message(STATUS "hcu: PGO ${HCU_PGO} (${_hcu_pgo_dir})")
endif()

set(HCU_VERSION "0.0.unknown" CACHE STRING "NACP display version, from git (switch/build-base.sh)")
# The hbmenu icon: the title image from the game's own package, kept local (gitignored, game material).
set(HCU_NRO_ICON_ARG)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/switch/icon-local.jpg")
    set(HCU_NRO_ICON_ARG ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/switch/icon-local.jpg")
endif()
rexglue_switch_add_nro(hcu NAME "Hard Corps: Uprising" AUTHOR "hcu-switch" VERSION "${HCU_VERSION}" ${HCU_NRO_ICON_ARG})
