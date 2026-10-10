# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(hard_corps_uprising LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where tools/codegen.ps1 unpacks the pinned SDK.
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools/rexglue/win-amd64")

include(generated/rexglue.cmake)

add_executable(hard_corps_uprising WIN32 src/main.cpp src/debug_saulo_input.cpp)

rexglue_setup_target(hard_corps_uprising GPU_PLUGINS xenos)

# The runtime reads its cvars from <exe dir>/hard_corps_uprising.toml.
add_custom_command(TARGET hard_corps_uprising POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/hard_corps_uprising.toml"
        "$<TARGET_FILE_DIR:hard_corps_uprising>"
    VERBATIM
)
