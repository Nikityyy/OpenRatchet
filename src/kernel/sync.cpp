#include "openratchet/syscalls.h"
#include <iostream>

namespace OpenRatchet {
namespace Kernel {

static void SysCreateSema(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] CreateSema called" << std::endl;
    ctx->r[2] = 1; // dummy semaphore id
}

static void SysDeleteSema(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] DeleteSema called" << std::endl;
    ctx->r[2] = 1;
}

static void SysSignalSema(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SignalSema called" << std::endl;
    ctx->r[2] = 1;
}

static void SysWaitSema(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] WaitSema called" << std::endl;
    ctx->r[2] = 1;
}

static void SysPollSema(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] PollSema called" << std::endl;
    ctx->r[2] = 1;
}

static void SysSetAlarm(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] SetAlarm called" << std::endl;
    ctx->r[2] = 1;
}

static void SysReleaseAlarm(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::cout << "[SYSCALL] ReleaseAlarm called" << std::endl;
    ctx->r[2] = 1;
}

void InitSyncSyscalls() {
    RegisterSyscall(0x40, SysCreateSema, "CreateSema");
    RegisterSyscall(0x41, SysDeleteSema, "DeleteSema");
    RegisterSyscall(0x42, SysSignalSema, "SignalSema");
    // 0x43 is iSignalSema
    RegisterSyscall(0x43, SysSignalSema, "iSignalSema");
    RegisterSyscall(0x44, SysWaitSema, "WaitSema");
    RegisterSyscall(0x45, SysPollSema, "PollSema");
    // 0x46 is iPollSema
    RegisterSyscall(0x46, SysPollSema, "iPollSema");
    
    // Alarms
    RegisterSyscall(0x32, SysSetAlarm, "SetAlarm"); // approximate ID, maybe different
    RegisterSyscall(0x33, SysReleaseAlarm, "ReleaseAlarm");
}

} // namespace Kernel
} // namespace OpenRatchet
