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
        "PS2RECOMP_SOURCE_DIR must point at the pristine pinned PS2Recomp checkout. "
        "Run tools/bootstrap.ps1 or pass -DPS2RECOMP_SOURCE_DIR=...")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PreparePs2Recomp.cmake")
set(OPENRATCHET_PS2RECOMP_PATCH
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/ps2recomp-synchronous-dmac-interrupt-stack.patch")
set(OPENRATCHET_PS2RECOMP_REVISION
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/ps2recomp-base-revision.txt")
set(OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR
    "${CMAKE_BINARY_DIR}/_openratchet/PS2Recomp")
openratchet_prepare_ps2recomp(
    "${PS2RECOMP_SOURCE_DIR}"
    "${OPENRATCHET_PS2RECOMP_PATCH}"
    "${OPENRATCHET_PS2RECOMP_REVISION}"
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}")

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
add_subdirectory("${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)

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
    src/assets/rac1_texture.cpp
    src/assets/rac1_tfrag.cpp
    src/assets/rac1_static_scene.cpp
    src/assets/rac1_sky.cpp
    src/assets/rac1_moby.cpp
    src/assets/rac1_moby_animation.cpp
    src/platform/native_vfs.cpp
    src/game/native_services.cpp
    src/game/native_io.cpp
    src/game/native_assets.cpp
    src/game/native_platform_bootstrap.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_replacements.cpp
    src/game/rac1_live_state.cpp
    src/game/rac1_live_animation.cpp
    src/game/rac1_live_transform.cpp
    src/guest_overrides.cpp
    src/sif_rpc_transport.cpp
    src/sif_startup_responses.cpp
    src/Memory.cpp
    ${RATCHET_GENERATED_SOURCES})
target_include_directories(openratchet PRIVATE
    src
    "${RATCHET_GENERATED_DIR}"
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel")
target_precompile_headers(openratchet PRIVATE
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include/ps2_runtime_macros.h"
    "${RATCHET_GENERATED_DIR}/ps2_recompiled_functions.h"
    "${RATCHET_GENERATED_DIR}/ps2_recompiled_stubs.h"
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include/ps2_syscalls.h"
    "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include/ps2_stubs.h")
target_link_libraries(openratchet PRIVATE ps2_runtime)
target_compile_definitions(openratchet PRIVATE
    RATCHET_BOOT_ELF="${RATCHET_BOOT_ELF}")


add_executable(native_level_viewer
    tools/native_level_viewer.cpp
    src/assets/rac1_level.cpp
    src/assets/rac1_collision.cpp
    src/assets/rac1_texture.cpp
    src/assets/rac1_tfrag.cpp
    src/assets/rac1_static_scene.cpp
    src/assets/rac1_sky.cpp
    src/assets/rac1_moby.cpp
    src/assets/rac1_moby_animation.cpp
    src/assets/wad_decompressor.cpp
    src/platform/native_vfs.cpp)
target_include_directories(native_level_viewer PRIVATE src)
target_link_libraries(native_level_viewer PRIVATE raylib)

if(BUILD_TESTING)
    add_executable(native_replacements_tests
        tests/native_replacements_tests.cpp)
    target_include_directories(native_replacements_tests PRIVATE src)
    add_test(NAME native_replacements COMMAND native_replacements_tests)

    add_executable(rac1_live_state_tests
        tests/rac1_live_state_tests.cpp
        src/game/rac1_live_state.cpp)
    target_include_directories(rac1_live_state_tests PRIVATE src)
    add_test(NAME rac1_live_state COMMAND rac1_live_state_tests)

    add_executable(rac1_live_animation_tests
        tests/rac1_live_animation_tests.cpp
        src/game/rac1_live_state.cpp
        src/game/rac1_live_animation.cpp
        src/assets/rac1_moby_animation.cpp)
    target_include_directories(rac1_live_animation_tests PRIVATE src)
    add_test(NAME rac1_live_animation COMMAND rac1_live_animation_tests)

    add_executable(rac1_live_transform_tests
        tests/rac1_live_transform_tests.cpp
        src/game/rac1_live_state.cpp
        src/game/rac1_live_transform.cpp)
    target_include_directories(rac1_live_transform_tests PRIVATE src)
    add_test(NAME rac1_live_transform COMMAND rac1_live_transform_tests)

    add_executable(native_platform_bootstrap_tests
        tests/native_platform_bootstrap_tests.cpp
        src/game/native_platform_bootstrap.cpp)
    target_include_directories(native_platform_bootstrap_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME native_platform_bootstrap COMMAND native_platform_bootstrap_tests)

    add_executable(native_audio_bootstrap_tests
        tests/native_audio_bootstrap_tests.cpp
        src/game/native_audio_bootstrap.cpp)
    target_include_directories(native_audio_bootstrap_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME native_audio_bootstrap COMMAND native_audio_bootstrap_tests)

    add_executable(native_vfs_tests
        tests/native_vfs_tests.cpp
        src/platform/native_vfs.cpp)
    target_include_directories(native_vfs_tests PRIVATE src)
    add_test(NAME native_vfs COMMAND native_vfs_tests)

    add_executable(native_io_tests
        tests/native_io_tests.cpp
        src/game/native_io.cpp
        src/game/native_services.cpp
        src/platform/native_vfs.cpp)
    target_include_directories(native_io_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME native_io COMMAND native_io_tests)

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

    add_executable(rac1_texture_tests
        tests/rac1_texture_tests.cpp
        src/assets/rac1_texture.cpp)
    target_include_directories(rac1_texture_tests PRIVATE src)
    add_test(NAME rac1_texture COMMAND rac1_texture_tests)

    add_executable(rac1_tfrag_tests
        tests/rac1_tfrag_tests.cpp
        src/assets/rac1_tfrag.cpp)
    target_include_directories(rac1_tfrag_tests PRIVATE src)
    add_test(NAME rac1_tfrag COMMAND rac1_tfrag_tests)

    add_executable(rac1_static_scene_tests
        tests/rac1_static_scene_tests.cpp
        src/assets/rac1_static_scene.cpp)
    target_include_directories(rac1_static_scene_tests PRIVATE src)
    add_test(NAME rac1_static_scene COMMAND rac1_static_scene_tests)

    add_executable(rac1_sky_tests
        tests/rac1_sky_tests.cpp
        src/assets/rac1_sky.cpp)
    target_include_directories(rac1_sky_tests PRIVATE src)
    add_test(NAME rac1_sky COMMAND rac1_sky_tests)

    add_executable(rac1_moby_tests
        tests/rac1_moby_tests.cpp
        src/assets/rac1_moby.cpp)
    target_include_directories(rac1_moby_tests PRIVATE src)
    add_test(NAME rac1_moby COMMAND rac1_moby_tests)

    add_executable(rac1_moby_animation_tests
        tests/rac1_moby_animation_tests.cpp
        src/assets/rac1_moby_animation.cpp)
    target_include_directories(rac1_moby_animation_tests PRIVATE src)
    add_test(NAME rac1_moby_animation COMMAND rac1_moby_animation_tests)

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
