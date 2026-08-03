add_library(openratchet_hal STATIC 
    ee_memory.cpp
    mmio.cpp
    elf_loader.cpp
    float_mode.cpp
)
target_include_directories(openratchet_hal PUBLIC ${CMAKE_SOURCE_DIR}/include)
