#include "openratchet/syscalls.h"
#include <iostream>

namespace OpenRatchet {
namespace Kernel {

static void SysCreateThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    // $a0 contains thread_param structure
    std::cout << "[SYSCALL] CreateThread called" << std::endl;
    // Return dummy thread ID
    ctx->r[2] = 1; // $v0
}

static void SysDeleteThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] DeleteThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysStartThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] StartThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysExitThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] ExitThread called" << std::endl;
}

static void SysExitDeleteThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] ExitDeleteThread called" << std::endl;
}

static void SysTerminateThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] TerminateThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysSleepThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SleepThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysWakeupThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] WakeupThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysIWakeupThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] iWakeupThread called" << std::endl;
    ctx->r[2] = 1;
}

static void SysGetThreadId(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] GetThreadId called" << std::endl;
    ctx->r[2] = 1; // Return dummy current thread ID
}

void InitThreadSyscalls() {
    RegisterSyscall(0x20, SysCreateThread, "CreateThread");
    RegisterSyscall(0x21, SysDeleteThread, "DeleteThread");
    RegisterSyscall(0x22, SysStartThread, "StartThread");
    RegisterSyscall(0x23, SysExitThread, "ExitThread");
    RegisterSyscall(0x24, SysExitDeleteThread, "ExitDeleteThread");
    RegisterSyscall(0x25, SysTerminateThread, "TerminateThread");
    RegisterSyscall(0x2B, SysSleepThread, "SleepThread");
    RegisterSyscall(0x2C, SysWakeupThread, "WakeupThread");
    RegisterSyscall(0x2D, SysIWakeupThread, "iWakeupThread");
    RegisterSyscall(0x2F, SysGetThreadId, "GetThreadId");
    
    // Add stubs for ChangeThreadPriority (60) and iChangeThreadPriority (61)
    auto SysChangeThreadPriority = [](MIPS_EE_Context* ctx, EE_Memory* mem) { ctx->r[2] = 0; };
    RegisterSyscall(60, SysChangeThreadPriority, "ChangeThreadPriority");
    RegisterSyscall(61, SysChangeThreadPriority, "iChangeThreadPriority");
}

} // namespace Kernel
} // namespace OpenRatchet
