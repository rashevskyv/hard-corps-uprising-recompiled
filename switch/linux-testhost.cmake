# Linux test host (docs/LINUX-TEST-HOST.md), included from CMakeLists.txt with -DHCU_LINUX_TESTHOST=ON.
# The plain ReXApp (the Switch entry) on Linux, with the trace backend and the deko3d backend over the
# OpenGL stand-in for libdeko3d (src/gpu/dk_gl): the backend's register, texture and resolve logic runs
# headless on the cloud box. The SDK is the Switch SDK (patches/rexglue-sdk + switch/sdk-edits*.py) built
# for linux-amd64: -DREXSDK_DIR=/home/user/rexglue-sdk.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "HCU_LINUX_TESTHOST needs Linux")
endif()
# The SDK's byte-swap helpers use SSSE3 / SSE4.1 intrinsics.
add_compile_options(-mssse3 -msse4.1)
find_path(HCU_EGL_INCLUDE EGL/egl.h REQUIRED)
set(REXGPU_XENOS_EXTRA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/trace_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/deko3d_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/dk_gl/dk_gl_shim.cpp")
# The REX_HOOK wrapper times the waiting exports for the CPU-work meter here too (src/frame_meter.cpp).
add_compile_definitions(HCU_LINUX_TESTHOST)
include(generated/rexglue.cmake)
add_executable(hcu switch/hcu_nx_main.cpp src/crt_native.cpp src/prolog_native.cpp src/frame_meter.cpp src/settings_menu.cpp)
rexglue_setup_target(hcu GPU_PLUGINS xenos)
# The GPU plugin (dlopen) reads the CPU-work meter from the executable (rex_nx_meter_*, rex_nx_cp_wait_ticks).
set_target_properties(hcu PROPERTIES ENABLE_EXPORTS ON)
target_include_directories(hcu PRIVATE "${REXSDK_DIR}/thirdparty/imgui" src)
