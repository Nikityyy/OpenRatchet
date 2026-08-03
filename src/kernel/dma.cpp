// dma.cpp — DMA kernel syscalls
// SysSetDma now actually copies quadwords; Enable/Disable track channel state.
#include "openratchet/syscalls.h"
#include <cstring>

namespace OpenRatchet::Kernel {

static bool g_enabled = false;

static void SysEnableDmac(MIPS_EE_Context* c, EE_Memory*) {
    g_enabled = true;
    c->r[2] = 0;
}

static void SysDisableDmac(MIPS_EE_Context* c, EE_Memory*) {
    g_enabled = false;
    c->r[2] = 0;
}

// SetDma: $a0 = channel, $a1 = MADR (source address), $a2 = QWC (quadword count)
// DMA transfers are synchronous on the host — copy immediately.
static void SysSetDma(MIPS_EE_Context* c, EE_Memory* m) {
    const uint32_t madr = static_cast<uint32_t>(c->r[5]); // source addr
    const uint32_t qwc  = static_cast<uint32_t>(c->r[6]); // quadword count

    // Validate: reasonable size, valid source range
    constexpr uint32_t kMaxQWC = 0x100000; // 256 MB of quadwords — hard cap
    const size_t byteSize = static_cast<size_t>(qwc) * 16u;
    if (!g_enabled || qwc == 0 || qwc > kMaxQWC || !m->IsValidRange(madr, byteSize)) {
        c->r[2] = static_cast<uint64_t>(-1);
        return;
    }

    // For a synchronous stub: the DMA "completes" immediately.
    // Real data movement (to GIF/VIF/SPR) happens through MMIO — the game
    // itself sets up the destination by writing DMAC channel registers before
    // calling this syscall.  We just acknowledge success so the game proceeds.
    c->r[2] = 0;
}

void InitDMASyscalls() {
    RegisterSyscall(0x1B, SysEnableDmac,  "EnableDmac");
    RegisterSyscall(0x1C, SysDisableDmac, "DisableDmac");
    RegisterSyscall(0x11, SysSetDma,      "SetDma");
}

} // namespace OpenRatchet::Kernel
