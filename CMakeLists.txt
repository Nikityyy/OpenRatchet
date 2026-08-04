cmake_minimum_required(VERSION 3.21)

project(OpenRatchet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OPENRATCHET_MSVC_MP "Compile independent MSVC sources in parallel" ON)
if(MSVC AND OPENRATCHET_MSVC_MP)
    add_compile_options(/MP)
endif()

set(PS2RECOMP_SOURCE_DIR "" CACHE PATH
    "Path to a PS2Recomp checkout (ran-j/PS2Recomp)")
set(RATCHET_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH
    "Directory containing ps2xRecomp C++ output")
set(RATCHET_BOOT_ELF "${CMAKE_CURRENT_SOURCE_DIR}/build/extracted/PS2_MAIN.ELF" CACHE FILEPATH
    "Extracted PS2 executable passed to the runtime")

if(NOT PS2RECOMP_SOURCE_DIR OR NOT EXISTS "${PS2RECOMP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "PS2RECOMP_SOURCE_DIR must point at a PS2Recomp checkout. "
        "Run tools/bootstrap.ps1 or pass -DPS2RECOMP_SOURCE_DIR=...")
endif()

if(NOT EXISTS "${RATCHET_GENERATED_DIR}")
    message(FATAL_ERROR
        "Missing generated output directory: ${RATCHET_GENERATED_DIR}. "
        "Run the extraction and recompilation pipeline first.")
endif()

set(PS2X_BUILD_RECOMP OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_ANALYZER OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PS2X_BUILD_STUDIO OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_FFMPEG OFF CACHE BOOL "" FORCE)
set(PS2X_ENABLE_RUNTIME_LOGS ON CACHE BOOL "" FORCE)
set(PS2X_ENABLE_AGRESSIVE_LOGS ON CACHE BOOL "" FORCE)
set(PS2X_DEFAULT_BOOT_ELF "${RATCHET_BOOT_ELF}" CACHE STRING "" FORCE)
add_subdirectory("${PS2RECOMP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)

file(GLOB_RECURSE RATCHET_GENERATED_SOURCES CONFIGURE_DEPENDS
    "${RATCHET_GENERATED_DIR}/*.cpp")
if(NOT RATCHET_GENERATED_SOURCES)
    message(FATAL_ERROR
        "No recompiled C++ files found under ${RATCHET_GENERATED_DIR}. "
        "Run tools/bootstrap.ps1 -Stage Recompile after exporting build/game.toml from Ghidra.")
endif()

add_executable(openratchet
    src/main.cpp
    src/guest_overrides.cpp
    src/Memory.cpp
    ${RATCHET_GENERATED_SOURCES})
target_include_directories(openratchet PRIVATE
    src
    "${RATCHET_GENERATED_DIR}"
    "${PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include"
    "${PS2RECOMP_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel")
target_precompile_headers(openratchet PRIVATE
    "${PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include/ps2_runtime_macros.h"
    "${RATCHET_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${RATCHET_GENERATED_DIR}/ps2_recompiled_stubs.h"
    "${PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include/ps2_syscalls.h"
    "${PS2RECOMP_SOURCE_DIR}/ps2xRuntime/include/ps2_stubs.h")
target_link_libraries(openratchet PRIVATE ps2_runtime)
target_compile_definitions(openratchet PRIVATE
    RATCHET_BOOT_ELF="${RATCHET_BOOT_ELF}")
