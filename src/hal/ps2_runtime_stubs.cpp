#include "ps2_runtime.h"
#include <iostream>
#include "openratchet/ee_memory.h"
#include "openratchet/syscalls.h"
#include "openratchet/context_bridge.h"
#include "openratchet/runtime_dispatch.h"
#include "ps2x/iop/iop_subsystem.h"
#include "ps2_iop_host.h"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>

namespace {
thread_local std::chrono::steady_clock::time_point g_guest_deadline{};
std::atomic<uint64_t> g_guest_dispatch_count{0};
}

namespace OpenRatchet::Runtime {
void RegisterGeneratedFunctions(PS2Runtime&) {}
void SetGuestDeadline(std::chrono::steady_clock::time_point deadline) { g_guest_deadline = deadline; }
void ClearGuestDeadline() { g_guest_deadline = {}; }
bool GuestDeadlineExpired() {
    return g_guest_deadline != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() >= g_guest_deadline;
}
uint64_t GetGuestDispatchCount() { return g_guest_dispatch_count.load(std::memory_order_relaxed); }
}

PS2Runtime::PS2Runtime() {}
PS2Runtime::~PS2Runtime() {}

PS2Memory::PS2Memory() {}
PS2Memory::~PS2Memory() {}

GS::GS() {}

VU1Interpreter::VU1Interpreter() {}

PS2AudioBackend::PS2AudioBackend() {}
struct PS2AudioBackend::Impl {};
PS2AudioBackend::~PS2AudioBackend() {}

namespace ps2x {
namespace iop {
    struct IopSubsystem::Impl {};
    IopSubsystem::~IopSubsystem() {}
}
}
PS2IopHostAdapter::~PS2IopHostAdapter() {}
bool PS2IopHostAdapter::readGuest(uint32_t address, void *destination, size_t size) const { return false; }
bool PS2IopHostAdapter::writeGuest(uint32_t address, const void *source, size_t size) { return false; }
bool PS2IopHostAdapter::zeroGuest(uint32_t address, size_t size) { return false; }
bool PS2IopHostAdapter::normalizeGuestAddress(uint32_t address, uint32_t &normalized) const { return false; }
uint32_t PS2IopHostAdapter::allocateIopHandle(ps2x::iop::IopHandleKind kind) { return 0; }
uint32_t PS2IopHostAdapter::allocateGuest(uint32_t size, uint32_t alignment) { return 0; }
void PS2IopHostAdapter::freeGuest(uint32_t address) {}
void PS2IopHostAdapter::audioCommand(uint32_t sid, uint32_t function, ps2x::iop::GuestBuffer send, ps2x::iop::GuestBuffer receive) {}
std::string PS2IopHostAdapter::hostPath(ps2x::iop::HostPathKind kind) const { return ""; }
std::string PS2IopHostAdapter::translateGuestPath(std::string_view path) const { return ""; }
uint64_t PS2IopHostAdapter::openHostFile(std::string_view path) { return 0; }
bool PS2IopHostAdapter::hostFileSize(uint64_t handle, uint64_t &size) const { return false; }
bool PS2IopHostAdapter::readHostFile(uint64_t handle, uint64_t offset, void *destination, size_t size, size_t &bytesRead) { return false; }
void PS2IopHostAdapter::closeHostFile(uint64_t handle) {}
int32_t PS2IopHostAdapter::memoryCard(const ps2x::iop::MemoryCardRequest &request) { return 0; }
bool PS2IopHostAdapter::hasGuestFunction(uint32_t address) const { return false; }
bool PS2IopHostAdapter::invokeGuestFunction(uint64_t callToken, uint32_t address, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *resultAddress) { return false; }
void PS2IopHostAdapter::log(ps2x::iop::LogLevel level, std::string_view message) {}

void PS2Runtime::SignalException(R5900Context* ctx, PS2Exception exception) {
    std::cerr << "PS2Runtime::SignalException called. Exception: " << (int)exception << "\n";
}

void PS2Runtime::executeVU0Microprogram(uint8_t* rdram, R5900Context* ctx, uint32_t address) {
    std::cerr << "PS2Runtime::executeVU0Microprogram stub called\n";
}

void PS2Runtime::vu0StartMicroProgram(uint8_t* rdram, R5900Context* ctx, uint32_t address) {
    std::cerr << "PS2Runtime::vu0StartMicroProgram stub called\n";
}

void PS2Runtime::handleTrap(uint8_t* rdram, R5900Context* ctx) {
    std::cerr << "PS2Runtime::handleTrap stub called\n";
}

void PS2Runtime::handleBreak(uint8_t* rdram, R5900Context* ctx) {
    std::cerr << "PS2Runtime::handleBreak stub called\n";
}

bool PS2Runtime::shouldPreemptGuestExecution() {
    return isStopRequested() || OpenRatchet::Runtime::GuestDeadlineExpired();
}

// These might also be needed if not inlined properly in all places
void PS2Runtime::reportMissingFunction(uint8_t *rdram,
                               R5900Context *ctx,
                               uint32_t targetPc,
                               uint32_t sourcePc,
                               GuestBranchKind kind,
                               const char *debugName) {
    const uint32_t ra = ctx ? getRegU32(ctx, 31) : 0;
    const uint32_t sp = ctx ? getRegU32(ctx, 29) : 0;
    const uint32_t gp = ctx ? getRegU32(ctx, 28) : 0;
    std::cerr << "MISSING-TARGET: kind=" << static_cast<uint32_t>(kind)
              << " op=" << (debugName ? debugName : "unknown")
              << " source=0x" << std::hex << sourcePc
              << " target=0x" << targetPc
              << " pc=0x" << (ctx ? ctx->pc : 0)
              << " ra=0x" << ra << " sp=0x" << sp << " gp=0x" << gp
              << std::dec << '\n';
    if (ctx) ctx->pc = targetPc;
    requestStop();
}

bool PS2Runtime::dispatchGuestBranch(uint8_t *rdram,
                         R5900Context *ctx,
                         uint32_t targetPc,
                         uint32_t sourcePc,
                         uint32_t fallthroughPc,
                         GuestBranchKind kind,
                         const char *debugName) {
    if (!ctx) return false;
    ctx->pc = targetPc;
    const bool is_call = kind == GuestBranchKind::DirectCall || kind == GuestBranchKind::IndirectCall;
    if (kind == GuestBranchKind::Return) return false;

    RecompiledFunction function = lookupFunction(targetPc);
    if (!function) {
        reportMissingFunction(rdram, ctx, targetPc, sourcePc, kind, debugName);
        return false;
    }

    ++g_guest_dispatch_count;
    function(rdram, ctx, this);
    if (isStopRequested() || ctx->pc == 0 || OpenRatchet::Runtime::GuestDeadlineExpired()) return false;
    if (!is_call) return false;
    if (ctx->pc == targetPc) ctx->pc = fallthroughPc;
    // A generated callee returns by placing its RA in PC.  The caller's
    // generated code expects true precisely when that return reached its
    // fallthrough label; returning false here incorrectly unwinds every JAL.
    return ctx->pc == fallthroughPc;
}

bool PS2Runtime::registerFunction(uint32_t, RecompiledFunction) { return false; }
bool PS2Runtime::replaceFunction(uint32_t, RecompiledFunction) { return false; }
PS2Runtime::RecompiledFunction PS2Runtime::lookupFunction(uint32_t address) {
    if (address < g_ps2RecompiledFunctionTableBase || address >= g_ps2RecompiledFunctionTableEnd ||
        (address & 3u) != 0) return nullptr;
    const uint32_t slot = (address - g_ps2RecompiledFunctionTableBase) / 4u;
    return slot < g_ps2RecompiledFunctionTableSlotCount ? g_ps2RecompiledFunctionTable[slot] : nullptr;
}
bool PS2Runtime::hasFunction(uint32_t address) const {
    if (address < g_ps2RecompiledFunctionTableBase || address >= g_ps2RecompiledFunctionTableEnd ||
        (address & 3u) != 0) return false;
    const uint32_t slot = (address - g_ps2RecompiledFunctionTableBase) / 4u;
    return slot < g_ps2RecompiledFunctionTableSlotCount && g_ps2RecompiledFunctionTable[slot] != nullptr;
}
void PS2Runtime::requestStop() { m_stopRequested.store(true, std::memory_order_release); }
bool PS2Runtime::isStopRequested() const { return m_stopRequested.load(std::memory_order_acquire); }

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx) {
    std::cerr << "PS2Runtime::HandleIntegerOverflow stub called\n";
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx) {
    const uint32_t syscall_id = getRegU32(ctx, 3);
    if (syscall_id == 0x3Cu) {
        const uint32_t gp = getRegU32(ctx, 4);
        const uint32_t stack = getRegU32(ctx, 5);
        const int32_t stack_size = static_cast<int32_t>(getRegU32(ctx, 6));
        if (gp != 0) ctx->r[28] = _mm_set_epi64x(0, gp);
        uint32_t sp = getRegU32(ctx, 29);
        if (stack == 0xFFFFFFFFu) sp = stack_size > 0 ? EE_MAIN_RAM_SIZE - static_cast<uint32_t>(stack_size) : EE_MAIN_RAM_SIZE;
        else if (stack != 0) sp = stack_size > 0 ? stack + static_cast<uint32_t>(stack_size) : stack;
        setReturnU32(ctx, sp & ~0xFu);
        return;
    }
    if (syscall_id == 0x3Du) {
        setReturnU32(ctx, (getRegU32(ctx, 4) + 0xFu) & ~0xFu);
        return;
    }
    MIPS_EE_Context ee_ctx;
    copyContextFromR5900(*ctx, ee_ctx);
    OpenRatchet::Kernel::DispatchSyscall(&ee_ctx, &g_ee_memory);
    copyContextToR5900(ee_ctx, *ctx);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx, uint32_t encodedSyscallId) {
    (void)encodedSyscallId;
    handleSyscall(rdram, ctx);
}

void PS2Runtime::clearLLBit(R5900Context *ctx) {
    std::cerr << "PS2Runtime::clearLLBit stub called\n";
}

void PS2Runtime::Store64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint64_t value) {
    g_ee_memory.Write<uint64_t>(vaddr, value);
}

void PS2Runtime::Store32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint32_t value) {
    g_ee_memory.Write<uint32_t>(vaddr, value);
}

void PS2Runtime::Store16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint16_t value) {
    g_ee_memory.Write<uint16_t>(vaddr, value);
}

void PS2Runtime::Store8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, uint8_t value) {
    g_ee_memory.Write<uint8_t>(vaddr, value);
}

void PS2Runtime::Store128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr, __m128i value) {
    g_ee_memory.Write<uint128_t>(vaddr, *reinterpret_cast<uint128_t*>(&value));
}

uint64_t PS2Runtime::Load64(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr) {
    return g_ee_memory.Read<uint64_t>(vaddr);
}

uint32_t PS2Runtime::Load32(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr) {
    return g_ee_memory.Read<uint32_t>(vaddr);
}

uint16_t PS2Runtime::Load16(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr) {
    return g_ee_memory.Read<uint16_t>(vaddr);
}

uint8_t PS2Runtime::Load8(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr) {
    return g_ee_memory.Read<uint8_t>(vaddr);
}

__m128i PS2Runtime::Load128(uint8_t *rdram, R5900Context *ctx, uint32_t vaddr) {
    uint128_t val = g_ee_memory.Read<uint128_t>(vaddr);
    return *reinterpret_cast<__m128i*>(&val);
}
