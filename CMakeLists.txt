cmake_minimum_required(VERSION 3.22)
project(OpenRatchet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/MP)
endif()

if(NOT OPENRATCHET_SKIP_DEPS)
    find_package(Vulkan)
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2/cmake")
    find_package(SDL2 REQUIRED)
endif()

include_directories(include/)

add_subdirectory(src/hal)
add_subdirectory(src/renderer)
add_subdirectory(src/kernel)
add_subdirectory(src/iop)

file(GLOB RECOMPILED_SOURCES "src/recompiled/*.cpp")
if(RECOMPILED_SOURCES)
    add_library(openratchet_recompiled STATIC ${RECOMPILED_SOURCES})
    target_link_libraries(openratchet_recompiled PRIVATE openratchet_hal)
    target_include_directories(openratchet_recompiled PUBLIC data/analysis/output third_party/PS2Recomp/ps2xRuntime/include third_party/PS2Recomp/ps2xIOP/include third_party/PS2Recomp/ps2xRuntime/src/lib/Kernel)
else()
    # Dummy target if sources aren't generated yet
    file(WRITE "${CMAKE_BINARY_DIR}/dummy_recompiled.cpp" "void dummy_recompiled(){}\n")
    add_library(openratchet_recompiled STATIC "${CMAKE_BINARY_DIR}/dummy_recompiled.cpp")
endif()

add_executable(openratchet src/main.cpp)
target_link_libraries(openratchet PRIVATE openratchet_hal openratchet_renderer openratchet_kernel openratchet_iop openratchet_recompiled)
