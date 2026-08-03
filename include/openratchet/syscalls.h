#pragma once
#include <cstdint>
#include <array>
#include "ee_context.h"
#include "ee_memory.h"

namespace OpenRatchet {
namespace Kernel {

typedef void (*SyscallHandler)(MIPS_EE_Context* ctx, EE_Memory* mem);

// Main dispatch function to be called on 'syscall' instruction
void DispatchSyscall(MIPS_EE_Context* ctx, EE_Memory* mem);

// Syscall initialization
void InitSyscalls();

// Handlers for specific modules
void InitLibcSyscalls();
void InitThreadSyscalls();
void InitSyncSyscalls();
void InitDMASyscalls();
void InitTimerSyscalls();
void InitGSSyscalls();

void RegisterSyscall(uint32_t id, SyscallHandler handler, const char* name);

} // namespace Kernel
} // namespace OpenRatchet
