#include "openratchet/syscalls.h"
#include <iostream>
#include <iomanip>

namespace OpenRatchet {
namespace Kernel {

struct SyscallEntry {
    SyscallHandler handler;
    const char* name;
};

static std::array<SyscallEntry, 256> g_syscallTable;

static void UnimplementedSyscall(MIPS_EE_Context* ctx, EE_Memory* mem) {
    uint32_t syscall_num = ctx->r[3]; // $v1
    std::cerr << "UNIMPLEMENTED SYSCALL: " << syscall_num << " at PC 0x" 
              << std::hex << std::setw(8) << std::setfill('0') << ctx->pc << std::dec << std::endl;
}

void RegisterSyscall(uint32_t id, SyscallHandler handler, const char* name) {
    if (id < 256) {
        g_syscallTable[id].handler = handler;
        g_syscallTable[id].name = name;
    }
}

void DispatchSyscall(MIPS_EE_Context* ctx, EE_Memory* mem) {
    uint32_t syscall_num = ctx->r[3]; // $v1 holds the syscall number in PS2
    
    // In PS2, some syscalls are negative when treating $v1 as signed, 
    // but the actual syscall number is just the lower bits. 
    // Usually it's limited to 0-127 or 0-255.
    uint32_t index = syscall_num & 0xFF;

    if (g_syscallTable[index].handler) {
        // std::cout << "[SYSCALL] " << g_syscallTable[index].name << " (" << index << ")" << std::endl;
        g_syscallTable[index].handler(ctx, mem);
    } else {
        UnimplementedSyscall(ctx, mem);
    }
}

void InitSyscalls() {
    for (int i = 0; i < 256; ++i) {
        g_syscallTable[i].handler = nullptr;
        g_syscallTable[i].name = "Unknown";
    }

    InitLibcSyscalls();
    InitThreadSyscalls();
    InitSyncSyscalls();
    InitDMASyscalls();
    InitTimerSyscalls();
    InitGSSyscalls();
}

} // namespace Kernel
} // namespace OpenRatchet
