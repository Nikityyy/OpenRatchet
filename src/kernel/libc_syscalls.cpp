#include "openratchet/syscalls.h"
#include "openratchet/kernel_state.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <algorithm>
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
    // printf / scePrintf — $a0 = format string, $a1-$a3 + stack = args
    const uint32_t fmt_ptr = static_cast<uint32_t>(ctx->r[4]); // $a0
    if (!fmt_ptr) return;
    const char* host_fmt = reinterpret_cast<const char*>(mem->GetRamPointer(fmt_ptr));
    if (!host_fmt) return;

    // Build a host-side formatted string from the format and up to 3 register args.
    // We handle %s (guest string pointer) and %d/%i/%u/%x/%X/%f by feeding snprintf.
    // This is best-effort: complex formats or stack args are skipped.
    char out[512];
    uint64_t args[3] = { ctx->r[5], ctx->r[6], ctx->r[7] }; // $a1, $a2, $a3
    const char* p = host_fmt;
    char* o = out;
    const char* o_end = out + sizeof(out) - 1;
    int arg_idx = 0;

    while (*p && o < o_end) {
        if (*p != '%') { *o++ = *p++; continue; }
        ++p; // skip '%'
        if (*p == '%') { *o++ = '%'; ++p; continue; }
        // Collect width/precision flags
        char spec[16] = {'%', 0}; int si = 1;
        while (*p && std::strchr("-+ #0123456789.*", *p) && si < 14) spec[si++] = *p++;
        const char fmt_ch = *p ? *p++ : 0;
        spec[si++] = fmt_ch; spec[si] = 0;
        if (arg_idx >= 3) break;
        const uint64_t arg = args[arg_idx++];
        if (fmt_ch == 's') {
            const char* s = reinterpret_cast<const char*>(mem->GetRamPointer(static_cast<uint32_t>(arg)));
            if (s) {
                int written = std::snprintf(o, static_cast<size_t>(o_end - o), spec, s);
                if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
            }
        } else if (fmt_ch == 'd' || fmt_ch == 'i') {
            int written = std::snprintf(o, static_cast<size_t>(o_end - o), spec, static_cast<int32_t>(arg));
            if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
        } else if (fmt_ch == 'u') {
            int written = std::snprintf(o, static_cast<size_t>(o_end - o), spec, static_cast<uint32_t>(arg));
            if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
        } else if (fmt_ch == 'x' || fmt_ch == 'X') {
            int written = std::snprintf(o, static_cast<size_t>(o_end - o), spec, static_cast<uint32_t>(arg));
            if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
        } else if (fmt_ch == 'f') {
            float fv; std::memcpy(&fv, &arg, sizeof(float));
            int written = std::snprintf(o, static_cast<size_t>(o_end - o), spec, fv);
            if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
        } else {
            // Unknown specifier — just copy the spec literally
            int written = std::snprintf(o, static_cast<size_t>(o_end - o), "%s", spec);
            if (written > 0) o += std::min<int>(written, static_cast<int>(o_end - o));
            --arg_idx; // didn't consume an arg
        }
    }
    *o = '\0';
    std::cout << "[EE_PRINTF] " << out;
    if (o > out && *(o-1) != '\n') std::cout << '\n';
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

static void SysGetOsdConfigParam(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t param_ptr = static_cast<uint32_t>(ctx->r[4]);
    if (param_ptr) {
        // Pack ConfigParam bitfield: screenType=16:9(2), videoOutput=component(1), japLanguage=english(1), version=1, language=english(1)
        uint32_t val = 0;
        val |= 0 & 1;             // spdifMode
        val |= (2 & 3) << 1;      // screenType = 16:9
        val |= (1 & 1) << 3;      // videoOutput = component
        val |= (1 & 1) << 4;      // japLanguage = english
        val |= (0 & 0xFF) << 5;   // ps1drvConfig
        val |= (1 & 7) << 13;     // version = 1
        val |= (1 & 0x1F) << 16;  // language = english
        val |= (0 & 0x7FF) << 21; // timezoneOffset
        mem->Write<uint32_t>(param_ptr, val);
    }
    ctx->r[2] = 0;
}

static void SysSetOsdConfigParam(MIPS_EE_Context* ctx, EE_Memory*) {
    ctx->r[2] = 0;
}

static void SysCopy(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t dest = static_cast<uint32_t>(ctx->r[4]);
    const uint32_t src = static_cast<uint32_t>(ctx->r[5]);
    const uint32_t size = static_cast<uint32_t>(ctx->r[6]);
    if (size > 0) {
        void* host_dest = mem->GetRamPointer(dest);
        const void* host_src = mem->GetRamPointer(src);
        if (host_dest && host_src) {
            std::memmove(host_dest, host_src, size);
        }
    }
    ctx->r[2] = dest;
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

    // EnableCache (0x61) / Syscall 97 — enables Emotion Engine instruction/data caches; safe no-op on host
    auto SysEnableCache = [](MIPS_EE_Context* c, EE_Memory*) { c->r[2] = 0; };
    RegisterSyscall(0x61, SysEnableCache, "EnableCache");

    RegisterSyscall(0x4A, SysSetOsdConfigParam, "SetOsdConfigParam");
    RegisterSyscall(0x4B, SysGetOsdConfigParam, "GetOsdConfigParam");
    RegisterSyscall(0x5A, SysCopy,              "Copy");
}

} // namespace Kernel
} // namespace OpenRatchet
