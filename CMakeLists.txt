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

option(OPENRATCHET_GENERATED_UNITY
    "Batch PS2Recomp-generated Retail sources into bounded unity translation units" ON)
set(OPENRATCHET_GENERATED_UNITY_BATCH_SIZE 16 CACHE STRING
    "Maximum generated Retail functions per unity translation unit")
set(OPENRATCHET_GENERATED_UNITY_MAX_BYTES 2097152 CACHE STRING
    "Maximum generated Retail source bytes per unity translation unit")
foreach(_unity_limit IN ITEMS
        OPENRATCHET_GENERATED_UNITY_BATCH_SIZE
        OPENRATCHET_GENERATED_UNITY_MAX_BYTES)
    if(NOT ${_unity_limit} MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${_unity_limit} must be a positive integer")
    endif()
endforeach()

set(PS2RECOMP_SOURCE_DIR "" CACHE PATH
    "Path to a PS2Recomp checkout (ran-j/PS2Recomp)")
set(RATCHET_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated" CACHE PATH
    "Directory containing boot ps2xRecomp C++ output")
set(RATCHET_BUILD_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/generated" CACHE PATH
    "Build-local generated code, including Retail level overlay AOT modules")
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

# Retail executable overlays reuse guest addresses across distinct code
# generations.  The initial Veldin path is ELF -> wad_158 -> level_00.
# Generated secondary AOT modules live under build/generated; generated/ remains
# the authoritative boot-ELF output.  A build-local dense table provides storage
# for the largest proved generation without modifying generated/register_functions.cpp.
set(RATCHET_BUILD_REGISTER "${RATCHET_BUILD_GENERATED_DIR}/register_functions.cpp")
set(RATCHET_BOOT_REGISTER "${RATCHET_GENERATED_DIR}/register_functions.cpp")
set(RATCHET_WAD158_OVERLAY_DIR "${RATCHET_BUILD_GENERATED_DIR}/overlays/wad_158")
set(RATCHET_LEVEL0_OVERLAY_DIR "${RATCHET_BUILD_GENERATED_DIR}/overlays/level_00")
set(RATCHET_OVERLAY_GENERATED_SOURCES)

if(EXISTS "${RATCHET_BUILD_REGISTER}")
    list(REMOVE_ITEM RATCHET_GENERATED_SOURCES "${RATCHET_BOOT_REGISTER}")
    list(APPEND RATCHET_GENERATED_SOURCES "${RATCHET_BUILD_REGISTER}")
endif()

if(EXISTS "${RATCHET_WAD158_OVERLAY_DIR}/openratchet_overlay_table.h")
    file(GLOB_RECURSE RATCHET_WAD158_GENERATED_SOURCES CONFIGURE_DEPENDS
        "${RATCHET_WAD158_OVERLAY_DIR}/*.cpp")
    if(NOT RATCHET_WAD158_GENERATED_SOURCES)
        message(FATAL_ERROR
            "wad_158 overlay AOT header exists but no generated C++ sources were found")
    endif()
    list(APPEND RATCHET_OVERLAY_GENERATED_SOURCES ${RATCHET_WAD158_GENERATED_SOURCES})
    set(OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE ON)
else()
    set(OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE OFF)
endif()

if(EXISTS "${RATCHET_LEVEL0_OVERLAY_DIR}/openratchet_overlay_table.h")
    file(GLOB_RECURSE RATCHET_LEVEL0_GENERATED_SOURCES CONFIGURE_DEPENDS
        "${RATCHET_LEVEL0_OVERLAY_DIR}/*.cpp")
    if(NOT RATCHET_LEVEL0_GENERATED_SOURCES)
        message(FATAL_ERROR
            "Level-0 overlay AOT header exists but no generated C++ sources were found")
    endif()
    list(APPEND RATCHET_OVERLAY_GENERATED_SOURCES ${RATCHET_LEVEL0_GENERATED_SOURCES})
    set(OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE ON)
else()
    set(OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE OFF)
endif()

if((OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE AND NOT OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE) OR
   (OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE AND NOT OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE))
    message(FATAL_ERROR
        "Phase-12 Retail generation chain is incomplete: wad_158 and Level-0 AOT modules must exist together")
endif()
if((OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE OR OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE) AND
   NOT EXISTS "${RATCHET_BUILD_REGISTER}")
    message(FATAL_ERROR
        "Overlay AOT modules require the build-local expanded dense register_functions.cpp")
endif()

list(APPEND RATCHET_GENERATED_SOURCES ${RATCHET_OVERLAY_GENERATED_SOURCES})

# PS2Recomp deliberately emits one C++ file per Retail function. Compiling
# thousands of those files independently is dominated by compiler/PCH/object-file
# setup rather than useful work, especially after adding WAD158 + Level-0 AOT.
#
# Do not use CMake's built-in UNITY_BUILD here. Visual Studio still retains every
# original generated source as a ClCompile item, so equal basenames from distinct
# Retail generations (for example boot/FUN_x.cpp and wad_158/FUN_x.cpp) trigger
# MSB8027 and may target the same intermediate object path even though the files
# are unity-included. Instead, generate explicit wrapper translation units and add
# only those wrappers to the target. The original generated files remain ordinary
# #include dependencies, so source-content changes still invalidate the owning
# wrapper without exposing basename collisions to MSBuild.
set(RATCHET_GENERATED_COMPILE_SOURCES ${RATCHET_GENERATED_SOURCES})
if(OPENRATCHET_GENERATED_UNITY)
    set(_openratchet_unity_dir "${CMAKE_BINARY_DIR}/openratchet-generated-unity")
    file(MAKE_DIRECTORY "${_openratchet_unity_dir}")

    set(_openratchet_unity_wrappers)
    set(_openratchet_unity_group_sources)
    set(_openratchet_unity_group_bytes 0)
    set(_openratchet_unity_group_index 0)
    set(_openratchet_unity_function_count 0)
    set(_openratchet_unity_tu_count 0)
    set(_openratchet_single_tu_count 0)

    function(_openratchet_write_if_different path content)
        set(_write_required TRUE)
        if(EXISTS "${path}")
            file(READ "${path}" _existing_content)
            if(_existing_content STREQUAL "${content}")
                set(_write_required FALSE)
            endif()
        endif()
        if(_write_required)
            file(WRITE "${path}" "${content}")
        endif()
    endfunction()

    function(_openratchet_emit_generated_unity_group)
        if(NOT _openratchet_unity_group_sources)
            return()
        endif()

        set(_wrapper "${_openratchet_unity_dir}/openratchet_retail_${_openratchet_unity_group_index}.cpp")
        set(_wrapper_content "// Generated by CMake; do not edit.\n")
        foreach(_source IN LISTS _openratchet_unity_group_sources)
            file(TO_CMAKE_PATH "${_source}" _include_path)
            string(REPLACE "\\" "\\\\" _include_path "${_include_path}")
            string(REPLACE "\"" "\\\"" _include_path "${_include_path}")
            string(APPEND _wrapper_content "#include \"${_include_path}\"\n")
        endforeach()
        _openratchet_write_if_different("${_wrapper}" "${_wrapper_content}")
        set(_openratchet_unity_wrappers
            ${_openratchet_unity_wrappers} "${_wrapper}" PARENT_SCOPE)
        math(EXPR _next_tu_count "${_openratchet_unity_tu_count} + 1")
        set(_openratchet_unity_tu_count ${_next_tu_count} PARENT_SCOPE)
    endfunction()

    foreach(_generated_source IN LISTS RATCHET_GENERATED_SOURCES)
        get_filename_component(_generated_name "${_generated_source}" NAME)
        if(_generated_name STREQUAL "register_functions.cpp" OR
           _generated_name STREQUAL "openratchet_overlay_table.cpp")
            # Registry/table files deliberately stay one source per translation
            # unit because they own file-scope state. Wrap them one-to-one solely
            # to give MSBuild a unique object basename across Retail generations.
            string(SHA256 _single_hash "${_generated_source}")
            string(SUBSTRING "${_single_hash}" 0 16 _single_hash)
            set(_single_wrapper
                "${_openratchet_unity_dir}/openratchet_retail_single_${_single_hash}.cpp")
            file(TO_CMAKE_PATH "${_generated_source}" _single_include_path)
            string(REPLACE "\\" "\\\\" _single_include_path "${_single_include_path}")
            string(REPLACE "\"" "\\\"" _single_include_path "${_single_include_path}")
            set(_single_wrapper_content
                "// Generated by CMake; do not edit.\n#include \"${_single_include_path}\"\n")
            _openratchet_write_if_different("${_single_wrapper}" "${_single_wrapper_content}")
            list(APPEND _openratchet_unity_wrappers "${_single_wrapper}")
            math(EXPR _openratchet_single_tu_count "${_openratchet_single_tu_count} + 1")
            continue()
        endif()

        file(SIZE "${_generated_source}" _generated_source_bytes)
        list(LENGTH _openratchet_unity_group_sources _group_fill)
        math(EXPR _next_unity_group_bytes
            "${_openratchet_unity_group_bytes} + ${_generated_source_bytes}")
        if(_group_fill GREATER 0 AND
           (_group_fill EQUAL OPENRATCHET_GENERATED_UNITY_BATCH_SIZE OR
            _next_unity_group_bytes GREATER OPENRATCHET_GENERATED_UNITY_MAX_BYTES))
            _openratchet_emit_generated_unity_group()
            math(EXPR _openratchet_unity_group_index "${_openratchet_unity_group_index} + 1")
            set(_openratchet_unity_group_sources)
            set(_openratchet_unity_group_bytes 0)
        endif()

        list(APPEND _openratchet_unity_group_sources "${_generated_source}")
        math(EXPR _openratchet_unity_group_bytes
            "${_openratchet_unity_group_bytes} + ${_generated_source_bytes}")
        math(EXPR _openratchet_unity_function_count "${_openratchet_unity_function_count} + 1")
    endforeach()
    _openratchet_emit_generated_unity_group()

    set(RATCHET_GENERATED_COMPILE_SOURCES ${_openratchet_unity_wrappers})
    math(EXPR _openratchet_generated_compile_tu_count
        "${_openratchet_unity_tu_count} + ${_openratchet_single_tu_count}")
    message(STATUS
        "OpenRatchet generated aggregation: ${_openratchet_unity_function_count} function sources -> "
        "${_openratchet_unity_tu_count} bounded function TUs + ${_openratchet_single_tu_count} "
        "single-source registry/table TUs = ${_openratchet_generated_compile_tu_count} compile TUs "
        "(max-functions=${OPENRATCHET_GENERATED_UNITY_BATCH_SIZE}, "
        "max-source-bytes=${OPENRATCHET_GENERATED_UNITY_MAX_BYTES})")
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
    src/game/rac1_native_stash.cpp
    src/game/rac1_native_input.cpp
    src/game/rac1_frontend_autopilot.cpp
    src/game/rac1_overlay_coherence.cpp
    src/game/rac1_overlay_aot_dispatch.cpp
    src/game/rac1_overlay_aot_bridge.cpp
    src/game/native_audio_bootstrap.cpp
    src/game/native_replacements.cpp
    src/game/rac1_live_state.cpp
    src/game/rac1_live_animation.cpp
    src/game/rac1_live_transform.cpp
    src/game/rac1_live_camera.cpp
    src/game/rac1_live_sky.cpp
    src/render/native_mesh_renderer.cpp
    src/render/native_skinned_moby_renderer.cpp
    src/render/rac1_render_parity.cpp
    src/render/rac1_render_bridge.cpp
    src/render/rac1_runtime_renderer.cpp
    src/render/rac1_presentation_ownership.cpp
    src/guest_overrides.cpp
    src/sif_rpc_transport.cpp
    src/sif_startup_responses.cpp
    src/Memory.cpp
    ${RATCHET_GENERATED_COMPILE_SOURCES})

target_include_directories(openratchet PRIVATE
    src
    "${RATCHET_GENERATED_DIR}"
    "${RATCHET_BUILD_GENERATED_DIR}"
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
    RATCHET_BOOT_ELF="${RATCHET_BOOT_ELF}"
    OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE=$<BOOL:${OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE}>
    OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE=$<BOOL:${OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE}>)


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
    src/platform/native_vfs.cpp
    src/render/native_mesh_renderer.cpp
    src/render/native_skinned_moby_renderer.cpp
    src/render/rac1_render_parity.cpp)
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

    add_executable(rac1_live_camera_tests
        tests/rac1_live_camera_tests.cpp
        src/game/rac1_live_camera.cpp)
    target_include_directories(rac1_live_camera_tests PRIVATE src)
    add_test(NAME rac1_live_camera COMMAND rac1_live_camera_tests)

    add_executable(rac1_render_bridge_tests
        tests/rac1_render_bridge_tests.cpp
        src/render/rac1_render_bridge.cpp
        src/game/rac1_live_camera.cpp
        src/game/rac1_live_sky.cpp)
    target_include_directories(rac1_render_bridge_tests PRIVATE src)
    add_test(NAME rac1_render_bridge COMMAND rac1_render_bridge_tests)

    add_executable(rac1_render_parity_tests
        tests/rac1_render_parity_tests.cpp
        src/render/rac1_render_parity.cpp)
    target_include_directories(rac1_render_parity_tests PRIVATE src)
    add_test(NAME rac1_render_parity COMMAND rac1_render_parity_tests)

    add_executable(rac1_presentation_ownership_tests
        tests/rac1_presentation_ownership_tests.cpp
        src/render/rac1_presentation_ownership.cpp)
    target_include_directories(rac1_presentation_ownership_tests PRIVATE src)
    add_test(NAME rac1_presentation_ownership COMMAND rac1_presentation_ownership_tests)

    add_executable(ps2recomp_control_flow_tests
        tests/ps2recomp_control_flow_tests.cpp
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRecomp/src/lib/control_flow_analyzer.cpp"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRecomp/src/lib/recompiler_reporter.cpp")
    target_include_directories(ps2recomp_control_flow_tests PRIVATE
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRecomp/include")
    add_test(NAME ps2recomp_control_flow COMMAND ps2recomp_control_flow_tests)

    add_executable(ps2recomp_spr_dma_tests
        tests/ps2recomp_spr_dma_tests.cpp
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(ps2recomp_spr_dma_tests PRIVATE ps2_runtime)
    add_test(NAME ps2recomp_spr_dma COMMAND ps2recomp_spr_dma_tests)

    add_executable(ps2recomp_deci2_tests
        tests/ps2recomp_deci2_tests.cpp
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(ps2recomp_deci2_tests PRIVATE ps2_runtime)
    add_test(NAME ps2recomp_deci2 COMMAND ps2recomp_deci2_tests)

    add_executable(native_platform_bootstrap_tests
        tests/native_platform_bootstrap_tests.cpp
        src/game/native_platform_bootstrap.cpp)
    target_include_directories(native_platform_bootstrap_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME native_platform_bootstrap COMMAND native_platform_bootstrap_tests)

    add_executable(rac1_native_stash_tests
        tests/rac1_native_stash_tests.cpp
        src/game/rac1_native_stash.cpp)
    target_include_directories(rac1_native_stash_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME rac1_native_stash COMMAND rac1_native_stash_tests)

    add_executable(rac1_native_input_tests
        tests/rac1_native_input_tests.cpp
        src/game/rac1_native_input.cpp)
    target_include_directories(rac1_native_input_tests PRIVATE
        src
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/src/lib/Kernel"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xIOP/include")
    add_test(NAME rac1_native_input COMMAND rac1_native_input_tests)

    add_executable(rac1_frontend_autopilot_tests
        tests/rac1_frontend_autopilot_tests.cpp
        src/game/rac1_frontend_autopilot.cpp)
    target_include_directories(rac1_frontend_autopilot_tests PRIVATE src)
    add_test(NAME rac1_frontend_autopilot COMMAND rac1_frontend_autopilot_tests)

    add_executable(rac1_overlay_coherence_tests
        tests/rac1_overlay_coherence_tests.cpp
        src/game/rac1_overlay_coherence.cpp
        src/assets/rac1_level.cpp
        src/assets/wad_decompressor.cpp)
    target_include_directories(rac1_overlay_coherence_tests PRIVATE src)
    add_test(NAME rac1_overlay_coherence COMMAND rac1_overlay_coherence_tests)

    add_executable(rac1_overlay_aot_dispatch_tests
        tests/rac1_overlay_aot_dispatch_tests.cpp
        src/game/rac1_overlay_aot_dispatch.cpp)
    target_include_directories(rac1_overlay_aot_dispatch_tests PRIVATE
        src
        "${RATCHET_GENERATED_DIR}"
        "${OPENRATCHET_PS2RECOMP_BUILD_SOURCE_DIR}/ps2xRuntime/include")
    add_test(NAME rac1_overlay_aot_dispatch COMMAND rac1_overlay_aot_dispatch_tests)

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

    add_executable(rac1_live_sky_tests
        tests/rac1_live_sky_tests.cpp
        src/game/rac1_live_sky.cpp)
    target_include_directories(rac1_live_sky_tests PRIVATE src)
    add_test(NAME rac1_live_sky COMMAND rac1_live_sky_tests)

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

        add_test(NAME pcsx2_level0_oracle
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/pcsx2_level0_oracle_tests.py")
        set_tests_properties(pcsx2_level0_oracle PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(NAME pcsx2_boot_chain_trace
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/pcsx2_boot_chain_trace_tests.py")
        set_tests_properties(pcsx2_boot_chain_trace PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        add_test(NAME rac1_overlay_aot_codegen
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/rac1_overlay_aot_tests.py")
        set_tests_properties(rac1_overlay_aot_codegen PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
