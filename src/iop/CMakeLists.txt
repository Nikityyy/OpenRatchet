add_library(openratchet_iop STATIC 
    sif.cpp 
    cdvd.cpp 
    pad.cpp 
    spu2.cpp 
    mc.cpp
)

target_include_directories(openratchet_iop PUBLIC ${CMAKE_SOURCE_DIR}/include)
