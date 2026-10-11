# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(hard_corps_uprising VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 and tools/package-linux.sh unpack the pinned SDK.
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")
else()
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/linux-amd64")
endif()

# Release binaries must not carry the build machine's folders (__FILE__ in log calls).
add_compile_options("$<$<CONFIG:Release>:-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>")

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

set(HCU_SOURCES
    src/main.cpp
    src/cheats.cpp
    src/game_files.cpp
    src/launcher.cpp
    src/legacy_settings.cpp
    src/platform.cpp
    src/settings.cpp
    src/stfs.cpp
    src/window_host.cpp
)
if(WIN32)
    add_executable(hard_corps_uprising WIN32 ${HCU_SOURCES})
else()
    add_executable(hard_corps_uprising ${HCU_SOURCES})
endif()

rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)
target_link_libraries(hard_corps_uprising PRIVATE trg::launcher)
if(WIN32)
    target_link_libraries(hard_corps_uprising PRIVATE shcore)
else()
    # The SDK stages the runtime beside the program only on Windows and macOS; the RPATH is $ORIGIN.
    add_custom_command(TARGET hard_corps_uprising POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rex::runtime>"
            "$<TARGET_FILE_DIR:hard_corps_uprising>"
        VERBATIM
    )
endif()
target_compile_definitions(hard_corps_uprising PRIVATE
    HCU_PORT_VERSION="${PROJECT_VERSION}"
    HCU_TRG_VERSION="${HCU_TRG_VERSION}")

# The runtime reads its cvars from <exe dir>/hard_corps_uprising.toml.
add_custom_command(TARGET hard_corps_uprising POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/hard_corps_uprising.toml"
        "$<TARGET_FILE_DIR:hard_corps_uprising>"
    VERBATIM
)

# Installer tests, not part of the default build: cmake --build <dir> --target hcu_installer_tests
add_executable(hcu_installer_tests EXCLUDE_FROM_ALL
    tests/installer_tests.cpp
    src/game_files.cpp
    src/stfs.cpp
)
target_include_directories(hcu_installer_tests PRIVATE src)

# Cheat guard tests, not part of the default build: cmake --build <dir> --target hcu_cheats_tests
add_executable(hcu_cheats_tests EXCLUDE_FROM_ALL tests/cheats_tests.cpp)
target_include_directories(hcu_cheats_tests PRIVATE src)
