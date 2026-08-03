#include "openratchet/syscalls.h"
#include <iostream>
#include <array>
#include <mutex>
#include <algorithm>

namespace OpenRatchet {
namespace Kernel {

struct Thread { uint32_t entry = 0, stack = 0, priority = 0, state = 0; };
static std::mutex g_mutex;
static std::array<Thread, 256> g_threads{};
static uint32_t g_next_thread = 1;
static uint32_t g_current_thread = 1;

static void SysCreateThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    // $a0 contains thread_param structure
    std::lock_guard lock(g_mutex);
    const uint32_t id = g_next_thread++;
    if (id >= g_threads.size()) { ctx->r[2] = static_cast<uint64_t>(-1); return; }
    const uint32_t param = static_cast<uint32_t>(ctx->r[4]);
    g_threads[id] = {mem->Read<uint32_t>(param), mem->Read<uint32_t>(param + 4),
                     mem->Read<uint32_t>(param + 16), 0};
    ctx->r[2] = id;
}

static void SysDeleteThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t id = static_cast<uint32_t>(ctx->r[4]);
    std::lock_guard lock(g_mutex);
    if (id == 0 || id >= g_threads.size() || g_threads[id].state == 0) { ctx->r[2] = static_cast<uint64_t>(-1); return; }
    g_threads[id] = {};
    ctx->r[2] = 0;
}

static void SysStartThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t id = static_cast<uint32_t>(ctx->r[4]);
    std::lock_guard lock(g_mutex);
    if (id == 0 || id >= g_threads.size() || g_threads[id].entry == 0) { ctx->r[2] = static_cast<uint64_t>(-1); return; }
    g_threads[id].state = 1; ctx->r[2] = 0;
}

static void SysExitThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::lock_guard lock(g_mutex); g_threads[g_current_thread].state = 3; ctx->r[2] = 0;
}

static void SysExitDeleteThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::lock_guard lock(g_mutex); g_threads[g_current_thread] = {}; ctx->r[2] = 0;
}

static void SysTerminateThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t id = static_cast<uint32_t>(ctx->r[4]); std::lock_guard lock(g_mutex);
    if (id == 0 || id >= g_threads.size() || g_threads[id].state == 0) { ctx->r[2] = static_cast<uint64_t>(-1); return; }
    g_threads[id].state = 3; ctx->r[2] = 0;
}

static void SysSleepThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    std::lock_guard lock(g_mutex); g_threads[g_current_thread].state = 2; ctx->r[2] = 0;
}

static void SysWakeupThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    const uint32_t id = static_cast<uint32_t>(ctx->r[4]); std::lock_guard lock(g_mutex);
    if (id == 0 || id >= g_threads.size() || g_threads[id].state == 0) { ctx->r[2] = static_cast<uint64_t>(-1); return; }
    g_threads[id].state = 1; ctx->r[2] = 0;
}

static void SysIWakeupThread(MIPS_EE_Context* ctx, EE_Memory* mem) {
    SysWakeupThread(ctx, mem);
}

static void SysGetThreadId(MIPS_EE_Context* ctx, EE_Memory* mem) {
    ctx->r[2] = g_current_thread;
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
