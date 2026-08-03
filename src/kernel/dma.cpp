#include "openratchet/syscalls.h"

namespace OpenRatchet::Kernel {
static bool g_enabled = false;
static void SysEnableDmac(MIPS_EE_Context* c, EE_Memory*) { g_enabled = true; c->r[2] = 0; }
static void SysDisableDmac(MIPS_EE_Context* c, EE_Memory*) { g_enabled = false; c->r[2] = 0; }
static void SysSetDma(MIPS_EE_Context* c, EE_Memory* m) {
    const uint32_t madr = static_cast<uint32_t>(c->r[5]); const uint32_t qwc = static_cast<uint32_t>(c->r[6]);
    if (!g_enabled || qwc > 0x100000 || !m->IsValidRange(madr, static_cast<size_t>(qwc) * 16)) { c->r[2] = static_cast<uint64_t>(-1); return; }
    c->r[2] = 0;
}
void InitDMASyscalls() { RegisterSyscall(0x1B,SysEnableDmac,"EnableDmac"); RegisterSyscall(0x1C,SysDisableDmac,"DisableDmac"); RegisterSyscall(0x11,SysSetDma,"SetDma"); }
}
