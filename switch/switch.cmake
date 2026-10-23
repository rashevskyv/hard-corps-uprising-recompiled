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

include(generated/rexglue.cmake)

add_executable(hcu
    switch/hcu_nx_main.cpp
    src/crt_native.cpp
    src/gpu_fence_wait.cpp
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
target_link_options(hcu PRIVATE
    -Wl,--wrap=__syscall_thread_create
    -Wl,-u,vk_icdGetInstanceProcAddr
)
# zstd, expat and zlib from devkitPro portlibs (the SDK links them on Linux through the system).
target_link_libraries(hcu PRIVATE "${NVK_SDK}/lib/libvulkan.a" deko3d zstd expat z nx m stdc++)
# nx_create_nro ignores NAME/AUTHOR/VERSION: the NACP is generated here and handed to it.
nx_generate_nacp(hcu.nacp NAME "Hard Corps: Uprising" AUTHOR "hcu-switch" VERSION "${HCU_VERSION}")
nx_create_nro(hcu NACP hcu.nacp)
