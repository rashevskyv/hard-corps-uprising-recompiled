# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(hard_corps_uprising LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")

include(generated/rexglue.cmake)

# TRG Launcher: the third_party/trg-launcher submodule, or a local checkout
# named by HCU_TRG_LAUNCHER_DIR while the submodule is not checked out.
set(HCU_TRG_LAUNCHER_DIR "$ENV{HCU_TRG_LAUNCHER_DIR}" CACHE PATH
    "Local TRG Launcher checkout, used when third_party/trg-launcher is empty")
set(_trg_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/trg-launcher")
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    set(_trg_dir "${HCU_TRG_LAUNCHER_DIR}")
endif()
if(NOT EXISTS "${_trg_dir}/CMakeLists.txt")
    message(FATAL_ERROR "TRG Launcher not found: run `git submodule update --init third_party/trg-launcher`")
endif()

# The launcher draws inside the runtime's own Dear ImGui frame, so it must use
# the ImGui compiled into rexruntime (one context), never a downloaded copy.
add_library(hcu_imgui INTERFACE)
target_link_libraries(hcu_imgui INTERFACE rex::runtime)
set(TRG_LAUNCHER_IMGUI_TARGET hcu_imgui CACHE STRING "" FORCE)
set(TRG_LAUNCHER_STANDALONE OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TRG_LAUNCHER_FETCH_DEPS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_trg_dir}" trg-launcher)
# Written for C++17: in C++20 and later u8string() is a different type.
set_target_properties(trg_launcher PROPERTIES CXX_STANDARD 17)
# TRG defines this only for cl.exe; clang++ needs it too for the MSVC CRT.
target_compile_definitions(trg_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
get_directory_property(HCU_TRG_VERSION DIRECTORY "${_trg_dir}" DEFINITION PROJECT_VERSION)

add_executable(hard_corps_uprising WIN32
    src/main.cpp
    src/launcher.cpp
    src/legacy_settings.cpp
    src/platform.cpp
    src/settings.cpp
    src/window_host.cpp
)

rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)
target_link_libraries(hard_corps_uprising PRIVATE trg::launcher shcore)
target_compile_definitions(hard_corps_uprising PRIVATE HCU_TRG_VERSION="${HCU_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/hard_corps_uprising.toml.
add_custom_command(TARGET hard_corps_uprising POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/hard_corps_uprising.toml"
        "$<TARGET_FILE_DIR:hard_corps_uprising>"
    VERBATIM
)
