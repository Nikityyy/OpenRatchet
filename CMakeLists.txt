cmake_minimum_required(VERSION 3.22)
project(OpenRatchet LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/MP)
endif()

if(NOT OPENRATCHET_SKIP_DEPS)
    find_package(Vulkan REQUIRED)
    set(SDL2_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2/cmake")
    find_package(SDL2 REQUIRED)
endif()

set(OPENRATCHET_SDL2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL2/include")

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

if(TARGET SDL2::SDL2)
    add_custom_command(TARGET openratchet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:openratchet>"
        COMMENT "Copying SDL2 runtime next to openratchet")
endif()

if(BUILD_TESTING)
    add_executable(openratchet_memory_tests tests/test_memory.cpp)
    target_link_libraries(openratchet_memory_tests PRIVATE openratchet_hal)
    target_include_directories(openratchet_memory_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME openratchet_memory_tests COMMAND openratchet_memory_tests --self-test)
    add_executable(openratchet_kernel_tests tests/test_kernel.cpp)
    target_link_libraries(openratchet_kernel_tests PRIVATE openratchet_kernel openratchet_hal)
    target_include_directories(openratchet_kernel_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME openratchet_kernel_tests COMMAND openratchet_kernel_tests)
    add_executable(openratchet_iop_tests tests/test_iop.cpp)
    target_link_libraries(openratchet_iop_tests PRIVATE openratchet_iop openratchet_hal)
    target_include_directories(openratchet_iop_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
    add_test(NAME openratchet_iop_tests COMMAND openratchet_iop_tests)
    set_tests_properties(openratchet_iop_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
