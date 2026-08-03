#include "openratchet/syscalls.h"
#include <iostream>

namespace OpenRatchet {
namespace Kernel {

static void SysGsPutIMR(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] GsPutIMR called" << std::endl;
}

static void SysSetGsCrt(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SetGsCrt called" << std::endl;
}

static void SysGsSetDefDispEnv(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] GsSetDefDispEnv called" << std::endl;
}

static void SysGsSetDefDrawEnv(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] GsSetDefDrawEnv called" << std::endl;
}

void InitGSSyscalls() {
    RegisterSyscall(0x71, SysGsPutIMR, "GsPutIMR");
    RegisterSyscall(0x02, SysSetGsCrt, "SetGsCrt");
    
    // Unsure of exact syscall IDs for these, some are GS functions not kernel syscalls,
    // but we stub them to prevent crashes if they are invoked via syscalls.
    RegisterSyscall(0x75, SysGsSetDefDispEnv, "GsSetDefDispEnv");
    RegisterSyscall(0x76, SysGsSetDefDrawEnv, "GsSetDefDrawEnv");
}

} // namespace Kernel
} // namespace OpenRatchet
