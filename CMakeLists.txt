cmake_minimum_required(VERSION 3.22)
project(OpenRatchet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT OPENRATCHET_SKIP_DEPS)
    find_package(Vulkan REQUIRED)
    find_package(SDL2 REQUIRED)
endif()

include_directories(include/)

add_subdirectory(src/hal)
add_subdirectory(src/renderer)
add_subdirectory(src/kernel)
add_subdirectory(src/iop)

add_executable(openratchet src/main.cpp)
target_link_libraries(openratchet PRIVATE openratchet_hal openratchet_renderer openratchet_kernel openratchet_iop)
