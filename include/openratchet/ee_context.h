#pragma once
#include <cstdint>
#include <immintrin.h>

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

    // Runtime state retained when a generated function crosses into a host
    // service.  The compact fields above are the kernel ABI; these fields
    // prevent the syscall bridge from silently resetting R5900 state.
    uint64_t  insn_count = 0;
    __m128   vu0_vf[32]{};
    uint16_t vi[16]{};
    float vu0_q = 1.0f, vu0_p = 0.0f, vu0_i = 0.0f;
    __m128 vu0_r{}, vu0_acc{};
    uint16_t vu0_status = 0;
    uint32_t vu0_mac_flags = 0, vu0_clip_flags = 0, vu0_clip_flags2 = 0;
    uint32_t vu0_cmsar0 = 0, vu0_cmsar1 = 0, vu0_cmsar2 = 0, vu0_cmsar3 = 0;
    uint32_t vu0_vpu_stat = 0, vu0_vpu_stat2 = 0, vu0_vpu_stat3 = 0, vu0_vpu_stat4 = 0;
    uint32_t vu0_tpc = 0, vu0_tpc2 = 0, vu0_fbrst = 0, vu0_fbrst2 = 0;
    uint32_t vu0_fbrst3 = 0, vu0_fbrst4 = 0, vu0_itop = 0, vu0_top = 0;
    uint32_t vu0_info = 0, vu0_xitop = 0, vu0_pc = 0;
    float vu0_cf[4]{};
    uint32_t cop0_index = 0, cop0_random = 0, cop0_entrylo0 = 0, cop0_entrylo1 = 0;
    uint32_t cop0_context = 0, cop0_pagemask = 0, cop0_wired = 0, cop0_badvaddr = 0;
    uint32_t cop0_count = 0, cop0_entryhi = 0, cop0_compare = 0, cop0_status = 0;
    uint32_t cop0_cause = 0, cop0_epc = 0, cop0_prid = 0, cop0_config = 0;
    uint32_t cop0_badpaddr = 0, cop0_debug = 0, cop0_perf = 0, cop0_taglo = 0;
    uint32_t cop0_taghi = 0, cop0_errorepc = 0, llbit = 0, lladdr = 0;
    bool in_delay_slot = false;
    uint32_t branch_pc = 0;
    uint32_t cop2_ccr[32]{};
    float f_acc = 0.0f;
    uint32_t fcr31 = 0;
};
