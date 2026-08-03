#pragma once
#include <cstdint>
#include "ee_context.h"

// Forward declaration of the actual memory manager class
class EE_Memory {
public:
    template<typename T>
    T Read(uint32_t addr) { return 0; }

    template<typename T>
    void Write(uint32_t addr, T val) {}
};

// Global memory instance for the recompiled code to access
extern EE_Memory g_ee_memory;

// PS2Recomp generated code expects these macros/functions to exist
template<typename T>
inline T MEM_READ(MIPS_EE_Context* ctx, uint32_t addr) {
    (void)ctx;
    return g_ee_memory.Read<T>(addr);
}

template<typename T>
inline void MEM_WRITE(MIPS_EE_Context* ctx, uint32_t addr, T val) {
    (void)ctx;
    g_ee_memory.Write<T>(addr, val);
}
