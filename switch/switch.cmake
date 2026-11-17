# Nintendo Switch build (devkitA64 + libnx + mesa-switch NVK), included from CMakeLists.txt.
# Host layer from cvhd-recomp (switch/host); the SDK is edited by switch/sdk-edits.py on top of
# patches/rexglue-sdk (common + .linux patches). switch/build.sh configures and builds hcu.nro.

# The SDK's byte-swap helpers are header-only; the Horizon replacements for its POSIX sources:
set(HCU_SWITCH_HOST "${CMAKE_CURRENT_SOURCE_DIR}/switch/host")
set(REXGLUE_SWITCH_INCLUDE_DIR "${HCU_SWITCH_HOST}/include")
set(REXGLUE_SWITCH_CORE_SOURCES
    ${HCU_SWITCH_HOST}/memory_nx.cpp
    ${HCU_SWITCH_HOST}/mapped_memory_nx.cpp
    ${HCU_SWITCH_HOST}/fiber_nx.cpp
    ${HCU_SWITCH_HOST}/exception_handler_nx.cpp
    ${HCU_SWITCH_HOST}/seh_nx.cpp)
set(REXGLUE_SWITCH_UI_SOURCES
    ${HCU_SWITCH_HOST}/ui/window_nx.cpp
    ${HCU_SWITCH_HOST}/ui/windowed_app_context_nx.cpp)
set(REXGLUE_SWITCH_MAIN_SOURCE ${HCU_SWITCH_HOST}/ui/windowed_app_main_nx.cpp)
set(REXGLUE_SWITCH_INPUT_SOURCES ${HCU_SWITCH_HOST}/input/nx_input_driver.cpp)
# newlib hides POSIX declarations (fileno, fdopen, tm_gmtoff) under strict -std=c++23.
add_compile_definitions(_GNU_SOURCE)
# Horizon has no time zones (newlib has no tm_gmtoff): spdlog logs in UTC.
add_compile_definitions(SPDLOG_NO_TZ_OFFSET)
# vulkan.hpp has no dlopen for this platform; the driver is linked statically.
add_compile_definitions(VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0)
# newlib/libnx gaps, in every translation unit.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
                    "$<$<COMPILE_LANGUAGE:C,CXX>:${REXGLUE_SWITCH_INCLUDE_DIR}/switch_compat.h>")
include_directories(${REXGLUE_SWITCH_INCLUDE_DIR})
# -mearly-ra=none (renut_nx_ultimate_base app/CMakeLists.txt, from peggle2-nx): devkitA64 GCC 16.1's AArch64 early
# register allocation pass (on at -O2 and above) dropped table lookups of the NEON vperm helper and overwrote a constant's
# register in Peggle 2's console build: large textures were garbage on the console only. Here, before the SDK is added,
# so the SDK, FFmpeg and the LTO link get it too.
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-mearly-ra=none>")
add_link_options(-mearly-ra=none)
# generated/rexglue.cmake runs the rexglue CLI as a build step: the cross build takes the host-built one.
set(REXGLUE_HOST_CLI "${REXSDK_DIR}/out/linux-amd64/Release/rexglue" CACHE FILEPATH "Host rexglue CLI")
add_executable(rex::rexglue IMPORTED GLOBAL)
set_target_properties(rex::rexglue PROPERTIES IMPORTED_LOCATION "${REXGLUE_HOST_CLI}")
set(REXGLUE_HOST_TARGET hcu)
# GPU backends compiled into the SDK's GPU plugin (sdk-edits.py adds the hook), shared with cvhd-recomp:
# the trace backend (--gpu_backend=trace: every draw's state logged, shaders dumped, nothing drawn) and
# the deko3d renderer (--gpu_backend=deko3d, selected by the file sdmc:/switch/hcu/deko3d).
set(REXGPU_XENOS_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/trace_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/deko3d_backend.cpp")
# HCU_FPCR_ONE_MODE (CG-13, REX_FPCR_ONE_MODE, the FPSCRRegister edit of switch/sdk-edits-hcu.py): the host's flush mode
# stays on for all guest code, so a switch off writes nothing (no `msr fpcr` at the game code's switch-off sites).
# Risk: the scalar FPU then flushes denormals to zero as well, which the Xenon's does not; not bit-exact for scalar
# values below FLT_MIN / DBL_MIN. Host threads without InitHost (the audio client threads) run scalar code unflushed
# until their first switch on. Defined here, before the SDK and the game code are added, so both see one FPSCRRegister.
# OFF until an A/B on the console decides.
option(HCU_FPCR_ONE_MODE "CG-13: one FPU flush mode for all guest code (scalar denormals flushed, not bit-exact)" OFF)
if(HCU_FPCR_ONE_MODE)
    add_compile_definitions(REX_FPCR_ONE_MODE)
endif()

include(generated/rexglue.cmake)

add_executable(hcu
    switch/hcu_nx_main.cpp
    src/crt_native.cpp
    src/prolog_native.cpp
    src/frame_meter.cpp
    src/gpu_fence_wait.cpp
    src/settings_menu.cpp
    src/button_glyphs.cpp
    src/frame_recorder.cpp
)
rexglue_setup_target(hcu GPU_PLUGINS xenos)
# The SDK's UI headers include imgui.h (headers only: ImGui is compiled into rexruntime).
target_include_directories(hcu PRIVATE "${REXSDK_DIR}/thirdparty/imgui" src)

set(NVK_SDK "" CACHE PATH "mesa-switch NVK install (lib/libvulkan.a)")
target_sources(hcu PRIVATE
    ${HCU_SWITCH_HOST}/switch_nx.cpp
    ${HCU_SWITCH_HOST}/membench_nx.cpp
    ${HCU_SWITCH_HOST}/switch_compat.c
)
set(HCU_VERSION "0.0.unknown" CACHE STRING "NACP display version, from git (switch/build.sh)")
# hcu_version.h is written by switch/build.sh into the build dir on every build.
target_include_directories(hcu PRIVATE ${HCU_SWITCH_HOST}/include ${CMAKE_BINARY_DIR})
# lz4 (src/frame_recorder.cpp) from devkitPro portlibs.
target_include_directories(hcu PRIVATE "$ENV{DEVKITPRO}/portlibs/switch/include")
target_link_options(hcu PRIVATE
    -Wl,--wrap=__syscall_thread_create
    -Wl,-u,vk_icdGetInstanceProcAddr
)
# zstd, expat and zlib from devkitPro portlibs (the SDK links them on Linux through the system).
# HCU_DEKO3D_DEBUG: libdeko3dd checks the arguments of every call and, when the GPU channel faults, hands the fault
# (page fault address, bad method, timeout) to the backend's debug callback; the release libdeko3d drops the text.
option(HCU_DEKO3D_DEBUG "link the debug deko3d (argument checks, fault text in the log)" OFF)
target_link_libraries(hcu PRIVATE "${NVK_SDK}/lib/libvulkan.a" $<IF:$<BOOL:${HCU_DEKO3D_DEBUG}>,deko3dd,deko3d> zstd lz4 expat z nx m stdc++)
# HCU_SWITCH_LTO: GCC link-time optimisation (renut-nx docs/pgo-lto.md, renut_nx_ultimate_base docs/building.md). The
# game code, the app, the GPU plugin (the deko3d renderer), the SDK runtime with its OBJECT libraries and FFmpeg are
# compiled to GIMPLE and optimised as one program at the link; the toolchain's archiver is gcc-ar, which indexes LTO
# objects. The link repeats the SDK's -ffp-contract=off and -fno-strict-aliasing (the reference's link flags), so the
# game code, compiled without them, links with them too. XXH_FORCE_MEMORY_ACCESS=0: xxHash's default access is a
# strict-aliasing violation once inlined (garbage keys in nfsmw-nx's LTO build). Out of LTO: switch_nx.cpp defines
# --wrap's __wrap___syscall_thread_create (the link must see an ordinary definition), fiber_nx.cpp has top-level asm.
# The PC's ThinLTO (CMakeLists.txt, clang) is separate. OFF: the plain link (much faster for daily builds).
option(HCU_SWITCH_LTO "link-time optimisation of the game code, the app and the SDK (Switch)" ON)
if(HCU_SWITCH_LTO)
    foreach(_t hcu hcu_recomp rexruntime rexgpu-xenos rexaudio rexcore rexfilesystem rexinput rexui libavcodec libavutil)
        target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-flto;-fno-fat-lto-objects>")
        target_compile_definitions(${_t} PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
    endforeach()
    target_link_options(hcu PRIVATE -flto=8 -flto-partition=balanced -ffp-contract=off -fno-strict-aliasing)
    set_property(SOURCE ${HCU_SWITCH_HOST}/switch_nx.cpp APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
    set_property(SOURCE ${HCU_SWITCH_HOST}/fiber_nx.cpp TARGET_DIRECTORY rexcore APPEND PROPERTY COMPILE_OPTIONS -fno-lto)
endif()
# nx_create_nro ignores NAME/AUTHOR/VERSION: the NACP is generated here and handed to it.
nx_generate_nacp(hcu.nacp NAME "Hard Corps: Uprising" AUTHOR "hcu-switch" VERSION "${HCU_VERSION}")
# The hbmenu icon: the title image from the game's own package, kept local (gitignored, game material); without it
# the devkitPro default icon.
set(HCU_NRO_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/switch/icon-local.jpg")
if(EXISTS "${HCU_NRO_ICON}")
    nx_create_nro(hcu NACP hcu.nacp ICON "${HCU_NRO_ICON}")
else()
    nx_create_nro(hcu NACP hcu.nacp)
endif()
