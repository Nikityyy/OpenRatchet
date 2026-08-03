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
}
