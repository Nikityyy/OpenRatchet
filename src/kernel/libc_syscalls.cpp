#include "openratchet/syscalls.h"
#include "openratchet/kernel_state.h"
#include <iostream>
#include <cstring>
#include <array>

namespace OpenRatchet {
namespace Kernel {

constexpr uint32_t kGuestSyscallTableGuestBase = 0x80011F80u;
constexpr uint32_t kGuestSyscallTablePhysicalBase = kGuestSyscallTableGuestBase & 0x1FFFFFFFu;
constexpr uint32_t kGuestSyscallProbeBase = 0x000002F0u;
static std::array<uint32_t, 256> g_guest_syscall_handlers{};

static uint32_t NormalizeKernelAlias(uint32_t address) {
    return address >= 0x80000000u && address < 0xC0000000u
               ? address & 0x1FFFFFFFu
               : address;
}

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

static void SysFindAddress(MIPS_EE_Context* ctx, EE_Memory* mem) {
    uint32_t start = (static_cast<uint32_t>(ctx->r[4]) + 3u) & ~3u;
    const uint32_t end = static_cast<uint32_t>(ctx->r[5]) & ~3u;
    const uint32_t target = static_cast<uint32_t>(ctx->r[6]);
    const uint32_t normalized_target = NormalizeKernelAlias(target);

    ctx->r[2] = 0;
    if (start >= end) return;

    // FindAddress only operates on mapped EE RAM. Validate the complete range
    // up front so malformed guest arguments cannot create a host-side scan.
    const uint32_t normalized_start = NormalizeKernelAlias(start);
    const uint32_t normalized_end = NormalizeKernelAlias(end);
    if (normalized_start >= normalized_end ||
        !mem->IsValidRange(normalized_start, normalized_end - normalized_start)) return;

    for (uint32_t address = start; address < end; address += sizeof(uint32_t)) {
        const uint32_t value = mem->Read<uint32_t>(address);
        if (value == target || NormalizeKernelAlias(value) == normalized_target) {
            ctx->r[2] = address;
            return;
        }
    }
}

void ResetGuestSyscallTable(EE_Memory& memory) {
    g_guest_syscall_handlers.fill(0);
    for (uint32_t index = 0; index < g_guest_syscall_handlers.size(); ++index)
        memory.Write<uint32_t>(kGuestSyscallTablePhysicalBase + index * sizeof(uint32_t), 0);

    // R&C's startup locates the syscall table by scanning the BIOS probe area.
    memory.Write<uint32_t>(kGuestSyscallProbeBase, kGuestSyscallTableGuestBase >> 16);
    memory.Write<uint32_t>(kGuestSyscallProbeBase + 8, kGuestSyscallTableGuestBase & 0xFFFFu);
}

static void SysSetSyscall(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t index = static_cast<uint32_t>(ctx->r[4]);
    const uint32_t handler = static_cast<uint32_t>(ctx->r[5]);
    if (index >= g_guest_syscall_handlers.size()) {
        ctx->r[2] = static_cast<uint64_t>(-1);
        return;
    }
    g_guest_syscall_handlers[index] = handler;
    mem->Write<uint32_t>(kGuestSyscallTablePhysicalBase + index * sizeof(uint32_t), handler);
    ctx->r[2] = 0;
}

static void SysGetEntryAddress(MIPS_EE_Context* ctx, EE_Memory*) {
    const uint32_t index = static_cast<uint32_t>(ctx->r[4]);
    ctx->r[2] = index < g_guest_syscall_handlers.size() ? g_guest_syscall_handlers[index] : 0;
}

void InitLibcSyscalls() {
    RegisterSyscall(0x64, SysFlushCache,      "FlushCache");
    RegisterSyscall(0x83, SysFindAddress,     "FindAddress");
    RegisterSyscall(0x74, SysSetSyscall,      "SetSyscall");
    RegisterSyscall(0x5B, SysGetEntryAddress, "GetEntryAddress");

    // scePrintf — PS2 BIOS syscall 0x3F
    RegisterSyscall(0x3F, SysPrintf, "scePrintf");

    // RotateThreadReadyQueue (0x29) — called during startup; no-op is safe
    auto SysRotate = [](MIPS_EE_Context* c, EE_Memory*) { c->r[2] = 0; };
    RegisterSyscall(0x29, SysRotate, "RotateThreadReadyQueue");
}

} // namespace Kernel
} // namespace OpenRatchet
