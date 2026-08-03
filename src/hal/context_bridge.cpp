#include "openratchet/context_bridge.h"

void copyContextToR5900(const MIPS_EE_Context& src, R5900Context& dst) {
    for (int i = 0; i < 32; i++) {
#if defined(_MSC_VER)
        dst.r[i].m128i_u64[0] = src.r[i];
        dst.r[i].m128i_u64[1] = 0;
#else
        ((uint64_t*)&dst.r[i])[0] = src.r[i];
        ((uint64_t*)&dst.r[i])[1] = 0;
#endif
        dst.f[i] = src.f[i];
    }
    dst.pc = src.pc;
    dst.hi = src.hi;
    dst.lo = src.lo;
    dst.hi1 = src.hi1;
    dst.lo1 = src.lo1;
    dst.sa = src.sa;
    dst.insn_count = src.insn_count;
    std::memcpy(dst.vu0_vf, src.vu0_vf, sizeof(dst.vu0_vf));
    std::memcpy(dst.vi, src.vi, sizeof(dst.vi));
    dst.vu0_q = src.vu0_q; dst.vu0_p = src.vu0_p; dst.vu0_i = src.vu0_i;
    dst.vu0_r = src.vu0_r; dst.vu0_acc = src.vu0_acc;
    dst.vu0_status = src.vu0_status; dst.vu0_mac_flags = src.vu0_mac_flags;
    dst.vu0_clip_flags = src.vu0_clip_flags; dst.vu0_clip_flags2 = src.vu0_clip_flags2;
    dst.vu0_cmsar0 = src.vu0_cmsar0; dst.vu0_cmsar1 = src.vu0_cmsar1;
    dst.vu0_cmsar2 = src.vu0_cmsar2; dst.vu0_cmsar3 = src.vu0_cmsar3;
    dst.vu0_vpu_stat = src.vu0_vpu_stat; dst.vu0_vpu_stat2 = src.vu0_vpu_stat2;
    dst.vu0_vpu_stat3 = src.vu0_vpu_stat3; dst.vu0_vpu_stat4 = src.vu0_vpu_stat4;
    dst.vu0_tpc = src.vu0_tpc; dst.vu0_tpc2 = src.vu0_tpc2;
    dst.vu0_fbrst = src.vu0_fbrst; dst.vu0_fbrst2 = src.vu0_fbrst2;
    dst.vu0_fbrst3 = src.vu0_fbrst3; dst.vu0_fbrst4 = src.vu0_fbrst4;
    dst.vu0_itop = src.vu0_itop; dst.vu0_top = src.vu0_top; dst.vu0_info = src.vu0_info;
    dst.vu0_xitop = src.vu0_xitop; dst.vu0_pc = src.vu0_pc;
    std::memcpy(dst.vu0_cf, src.vu0_cf, sizeof(dst.vu0_cf));
    dst.cop0_index = src.cop0_index; dst.cop0_random = src.cop0_random;
    dst.cop0_entrylo0 = src.cop0_entrylo0; dst.cop0_entrylo1 = src.cop0_entrylo1;
    dst.cop0_context = src.cop0_context; dst.cop0_pagemask = src.cop0_pagemask;
    dst.cop0_wired = src.cop0_wired; dst.cop0_badvaddr = src.cop0_badvaddr;
    dst.cop0_count = src.cop0_count; dst.cop0_entryhi = src.cop0_entryhi;
    dst.cop0_compare = src.cop0_compare; dst.cop0_status = src.cop0_status;
    dst.cop0_cause = src.cop0_cause; dst.cop0_epc = src.cop0_epc; dst.cop0_prid = src.cop0_prid;
    dst.cop0_config = src.cop0_config; dst.cop0_badpaddr = src.cop0_badpaddr;
    dst.cop0_debug = src.cop0_debug; dst.cop0_perf = src.cop0_perf;
    dst.cop0_taglo = src.cop0_taglo; dst.cop0_taghi = src.cop0_taghi;
    dst.cop0_errorepc = src.cop0_errorepc; dst.llbit = src.llbit; dst.lladdr = src.lladdr;
    dst.in_delay_slot = src.in_delay_slot; dst.branch_pc = src.branch_pc;
    std::memcpy(dst.cop2_ccr, src.cop2_ccr, sizeof(dst.cop2_ccr));
    dst.f_acc = src.f_acc; dst.fcr31 = src.fcr31;
}

void copyContextFromR5900(const R5900Context& src, MIPS_EE_Context& dst) {
    for (int i = 0; i < 32; i++) {
#if defined(_MSC_VER)
        dst.r[i] = src.r[i].m128i_u64[0];
#else
        dst.r[i] = ((uint64_t*)&src.r[i])[0];
#endif
        dst.f[i] = src.f[i];
    }
    dst.pc = src.pc;
    dst.hi = src.hi;
    dst.lo = src.lo;
    dst.hi1 = src.hi1;
    dst.lo1 = src.lo1;
    dst.sa = src.sa;
    dst.insn_count = src.insn_count;
    std::memcpy(dst.vu0_vf, src.vu0_vf, sizeof(dst.vu0_vf));
    std::memcpy(dst.vi, src.vi, sizeof(dst.vi));
    dst.vu0_q = src.vu0_q; dst.vu0_p = src.vu0_p; dst.vu0_i = src.vu0_i;
    dst.vu0_r = src.vu0_r; dst.vu0_acc = src.vu0_acc;
    dst.vu0_status = src.vu0_status; dst.vu0_mac_flags = src.vu0_mac_flags;
    dst.vu0_clip_flags = src.vu0_clip_flags; dst.vu0_clip_flags2 = src.vu0_clip_flags2;
    dst.vu0_cmsar0 = src.vu0_cmsar0; dst.vu0_cmsar1 = src.vu0_cmsar1;
    dst.vu0_cmsar2 = src.vu0_cmsar2; dst.vu0_cmsar3 = src.vu0_cmsar3;
    dst.vu0_vpu_stat = src.vu0_vpu_stat; dst.vu0_vpu_stat2 = src.vu0_vpu_stat2;
    dst.vu0_vpu_stat3 = src.vu0_vpu_stat3; dst.vu0_vpu_stat4 = src.vu0_vpu_stat4;
    dst.vu0_tpc = src.vu0_tpc; dst.vu0_tpc2 = src.vu0_tpc2;
    dst.vu0_fbrst = src.vu0_fbrst; dst.vu0_fbrst2 = src.vu0_fbrst2;
    dst.vu0_fbrst3 = src.vu0_fbrst3; dst.vu0_fbrst4 = src.vu0_fbrst4;
    dst.vu0_itop = src.vu0_itop; dst.vu0_top = src.vu0_top; dst.vu0_info = src.vu0_info;
    dst.vu0_xitop = src.vu0_xitop; dst.vu0_pc = src.vu0_pc;
    std::memcpy(dst.vu0_cf, src.vu0_cf, sizeof(dst.vu0_cf));
    dst.cop0_index = src.cop0_index; dst.cop0_random = src.cop0_random;
    dst.cop0_entrylo0 = src.cop0_entrylo0; dst.cop0_entrylo1 = src.cop0_entrylo1;
    dst.cop0_context = src.cop0_context; dst.cop0_pagemask = src.cop0_pagemask;
    dst.cop0_wired = src.cop0_wired; dst.cop0_badvaddr = src.cop0_badvaddr;
    dst.cop0_count = src.cop0_count; dst.cop0_entryhi = src.cop0_entryhi;
    dst.cop0_compare = src.cop0_compare; dst.cop0_status = src.cop0_status;
    dst.cop0_cause = src.cop0_cause; dst.cop0_epc = src.cop0_epc; dst.cop0_prid = src.cop0_prid;
    dst.cop0_config = src.cop0_config; dst.cop0_badpaddr = src.cop0_badpaddr;
    dst.cop0_debug = src.cop0_debug; dst.cop0_perf = src.cop0_perf;
    dst.cop0_taglo = src.cop0_taglo; dst.cop0_taghi = src.cop0_taghi;
    dst.cop0_errorepc = src.cop0_errorepc; dst.llbit = src.llbit; dst.lladdr = src.lladdr;
    dst.in_delay_slot = src.in_delay_slot; dst.branch_pc = src.branch_pc;
    std::memcpy(dst.cop2_ccr, src.cop2_ccr, sizeof(dst.cop2_ccr));
    dst.f_acc = src.f_acc; dst.fcr31 = src.fcr31;
}
