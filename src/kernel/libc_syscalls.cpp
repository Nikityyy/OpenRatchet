#include "openratchet/syscalls.h"
#include <iostream>
#include <cstring>

namespace OpenRatchet {
namespace Kernel {

static void SysFlushCache(MIPS_EE_Context* ctx, EE_Memory* mem) {
    // Syscall 0x64
    // No-op on host since we don't have an instruction cache to flush
}

static void SysPrintf(MIPS_EE_Context* ctx, EE_Memory* mem) {
    // printf / scePrintf
    // $a0 contains format string pointer, subsequent registers/stack contain args
    // Since implementing full MIPS variadic args to host printf is complex,
    // we just log that printf was called, or print the format string if possible.
    uint32_t fmt_ptr = ctx->r[4]; // $a0
    if (fmt_ptr) {
        char* host_fmt = reinterpret_cast<char*>(mem->GetRamPointer(fmt_ptr));
        if (host_fmt) {
            std::cout << "[EE_PRINTF] " << host_fmt << std::endl;
        }
    }
}

void InitLibcSyscalls() {
    RegisterSyscall(0x64, SysFlushCache, "FlushCache");
    
    // RegisterPrintf? ID might be 0x3F or similar depending on BIOS, 
    // will leave it un-registered until we hit the unimplemented syscall or know the ID.
    // Some BIOS versions use 0x3E or 0x3F for printf.
}

} // namespace Kernel
} // namespace OpenRatchet
