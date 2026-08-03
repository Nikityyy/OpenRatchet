add_library(openratchet_hal STATIC 
    ee_memory.cpp
    mmio.cpp
    elf_loader.cpp
    float_mode.cpp
    native_hal.cpp
    ps2_runtime_stubs.cpp
    context_bridge.cpp
)
target_include_directories(openratchet_hal PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(openratchet_hal PUBLIC ${CMAKE_SOURCE_DIR}/third_party/PS2Recomp/ps2xRuntime/include)
target_include_directories(openratchet_hal PRIVATE ${CMAKE_SOURCE_DIR}/third_party/PS2Recomp/ps2xIOP/include)
target_include_directories(openratchet_hal PRIVATE ${CMAKE_SOURCE_DIR}/third_party/PS2Recomp/ps2xRuntime/src/lib)
if(OPENRATCHET_SKIP_DEPS)
    target_include_directories(openratchet_hal PRIVATE ${OPENRATCHET_SDL2_INCLUDE_DIR})
endif()
if(TARGET SDL2::SDL2)
    target_link_libraries(openratchet_hal PUBLIC SDL2::SDL2)
endif()
if(TARGET SDL2::SDL2main)
    target_link_libraries(openratchet_hal PUBLIC SDL2::SDL2main)
endif()
