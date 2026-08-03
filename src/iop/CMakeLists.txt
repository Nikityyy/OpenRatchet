add_library(openratchet_iop STATIC 
    sif.cpp 
    cdvd.cpp 
    pad.cpp 
    spu2.cpp 
    mc.cpp
)

target_include_directories(openratchet_iop PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(openratchet_iop PRIVATE ${CMAKE_SOURCE_DIR}/third_party/SDL2/include)
if(TARGET SDL2::SDL2)
    target_link_libraries(openratchet_iop PUBLIC SDL2::SDL2)
endif()
