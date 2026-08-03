#include "openratchet/syscalls.h"
#include <iostream>

namespace OpenRatchet {
namespace Kernel {

static void SysEnableDmac(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] EnableDmac called" << std::endl;
}

static void SysDisableDmac(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] DisableDmac called" << std::endl;
}

static void SysSetDma(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SetDma called" << std::endl;
}

void InitDMASyscalls() {
    RegisterSyscall(0x1B, SysEnableDmac, "EnableDmac");
    RegisterSyscall(0x1C, SysDisableDmac, "DisableDmac");
    RegisterSyscall(0x11, SysSetDma, "SetDma"); // IDs might differ, usually handled by DMA registers though.
}

} // namespace Kernel
} // namespace OpenRatchet
