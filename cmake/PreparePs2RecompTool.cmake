cmake_minimum_required(VERSION 3.21)

foreach(required_var
        OPENRATCHET_PS2RECOMP_UPSTREAM
        OPENRATCHET_PS2RECOMP_PATCH
        OPENRATCHET_PS2RECOMP_REVISION
        OPENRATCHET_PS2RECOMP_OUTPUT)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "Missing required -D${required_var}=... argument.")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/PreparePs2Recomp.cmake")
openratchet_prepare_ps2recomp(
    "${OPENRATCHET_PS2RECOMP_UPSTREAM}"
    "${OPENRATCHET_PS2RECOMP_PATCH}"
    "${OPENRATCHET_PS2RECOMP_REVISION}"
    "${OPENRATCHET_PS2RECOMP_OUTPUT}")
