cmake_minimum_required(VERSION 3.21)

project(OpenRatchet LANGUAGES CXX)

include(CTest)

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
    src/runtime/openratchet_runtime.cpp
    src/runtime/native_replacements.cpp
    src/assets/wad_decompressor.cpp
    src/assets/rac1_level.cpp
    src/assets/rac1_collision.cpp
    src/platform/native_vfs.cpp
    src/game/native_services.cpp
    src/game/native_io.cpp
    src/game/native_assets.cpp
    src/game/native_replacements.cpp
    src/guest_overrides.cpp
    src/sif_rpc_transport.cpp
    src/sif_startup_responses.cpp
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


add_executable(native_level_viewer
    tools/native_level_viewer.cpp
    src/assets/rac1_level.cpp
    src/assets/rac1_collision.cpp
    src/assets/wad_decompressor.cpp
    src/platform/native_vfs.cpp)
target_include_directories(native_level_viewer PRIVATE src)
target_link_libraries(native_level_viewer PRIVATE raylib)

if(BUILD_TESTING)
    add_executable(native_replacements_tests
        tests/native_replacements_tests.cpp)
    target_include_directories(native_replacements_tests PRIVATE src)
    add_test(NAME native_replacements COMMAND native_replacements_tests)

    add_executable(native_vfs_tests
        tests/native_vfs_tests.cpp
        src/platform/native_vfs.cpp)
    target_include_directories(native_vfs_tests PRIVATE src)
    add_test(NAME native_vfs COMMAND native_vfs_tests)

    add_executable(rac1_level_tests
        tests/rac1_level_tests.cpp
        src/assets/rac1_level.cpp
        src/assets/wad_decompressor.cpp)
    target_include_directories(rac1_level_tests PRIVATE src)
    add_test(NAME rac1_level COMMAND rac1_level_tests)

    add_executable(rac1_collision_tests
        tests/rac1_collision_tests.cpp
        src/assets/rac1_collision.cpp)
    target_include_directories(rac1_collision_tests PRIVATE src)
    add_test(NAME rac1_collision COMMAND rac1_collision_tests)

    add_executable(native_level_inspector
        tools/native_level_inspector.cpp
        src/assets/rac1_level.cpp
        src/assets/wad_decompressor.cpp
        src/platform/native_vfs.cpp)
    target_include_directories(native_level_inspector PRIVATE src)

    add_executable(wad_decompressor_tests
        tests/wad_decompressor_tests.cpp
        src/assets/wad_decompressor.cpp)
    target_include_directories(wad_decompressor_tests PRIVATE src)

    set(WAD_DECOMPRESSOR_TEST_ARGS)
    set(RATCHET_BOOT_WAD
        "${CMAKE_CURRENT_SOURCE_DIR}/build/extracted/wads2/wad2_0.wad")
    set(RATCHET_WADS2_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/build/extracted/wads2")
    if(EXISTS "${RATCHET_BOOT_WAD}")
        list(APPEND WAD_DECOMPRESSOR_TEST_ARGS --boot "${RATCHET_BOOT_WAD}")
    endif()
    if(IS_DIRECTORY "${RATCHET_WADS2_DIR}")
        list(APPEND WAD_DECOMPRESSOR_TEST_ARGS --corpus "${RATCHET_WADS2_DIR}")
    endif()
    add_test(NAME wad_decompressor
        COMMAND wad_decompressor_tests ${WAD_DECOMPRESSOR_TEST_ARGS})

    add_executable(sif_startup_responses_tests
        tests/sif_startup_responses_tests.cpp
        src/sif_startup_responses.cpp)
    target_include_directories(sif_startup_responses_tests PRIVATE src)
    add_test(NAME sif_startup_responses COMMAND sif_startup_responses_tests)

    add_executable(sif_rpc_transport_tests
        tests/sif_rpc_transport_tests.cpp
        src/sif_rpc_transport.cpp)
    target_include_directories(sif_rpc_transport_tests PRIVATE src)
    add_test(NAME sif_rpc_transport COMMAND sif_rpc_transport_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME pcsx2_sif_capture
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/pcsx2_sif_capture_tests.py")
        set_tests_properties(pcsx2_sif_capture PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
