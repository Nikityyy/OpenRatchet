#pragma once
#include <cstdint>

// 128-bit register for PS2 MMI and Vector Unit operations
struct uint128_t {
    uint64_t lo;
    uint64_t hi;
};

// Emotion Engine guest CPU register context
struct MIPS_EE_Context {
    uint64_t  r[32];      // General Purpose Registers ($r0 = 0 always)
    uint128_t mmi[32];    // 128-bit Multimedia Registers
    float     f[32];      // FPU Registers (COP1)
    uint32_t  pc;         // Program Counter
    uint32_t  hi, lo;     // HI/LO multiply/divide registers
    uint64_t  hi1, lo1;   // Pipeline 1 HI/LO (for MMI)
    uint32_t  sa;         // Shift Amount register
};
