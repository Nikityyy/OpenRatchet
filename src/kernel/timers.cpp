#include "openratchet/syscalls.h"
#include <iostream>

namespace OpenRatchet {
namespace Kernel {

static void SysSetVSyncCallback(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SetVSyncCallback called" << std::endl;
}

static void SysSetTimer(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SetTimer called" << std::endl;
}

void InitTimerSyscalls() {
    RegisterSyscall(0x73, SysSetVSyncCallback, "SetVSyncCallback");
    RegisterSyscall(0x74, SysSetTimer, "SetTimer"); // Approximate ID
}

} // namespace Kernel
} // namespace OpenRatchet
