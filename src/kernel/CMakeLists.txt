# CMakeLists.txt for kernel subsystem

add_library(openratchet_kernel STATIC
    syscall_table.cpp
    libc_syscalls.cpp
    threads.cpp
    sync.cpp
    dma.cpp
    timers.cpp
    gs_syscalls.cpp
)

target_include_directories(openratchet_kernel PUBLIC ${CMAKE_SOURCE_DIR}/include)
