#include "ps2_runtime.h"
#include <iostream>
#include "openratchet/ee_memory.h"
#include "openratchet/syscalls.h"
#include "openratchet/context_bridge.h"
#include "ps2x/iop/iop_subsystem.h"
#include "../../../third_party/PS2Recomp/ps2xRuntime/src/lib/ps2_iop_host.h"

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
    return false; // Don't preempt
}

// These might also be needed if not inlined properly in all places
void PS2Runtime::reportMissingFunction(uint8_t *rdram,
                               R5900Context *ctx,
                               uint32_t targetPc,
                               uint32_t sourcePc,
                               GuestBranchKind kind,
                               const char *debugName) {
    std::cerr << "PS2Runtime::reportMissingFunction stub called: " << (debugName ? debugName : "unknown") << "\n";
}

bool PS2Runtime::dispatchGuestBranch(uint8_t *rdram,
                         R5900Context *ctx,
                         uint32_t targetPc,
                         uint32_t sourcePc,
                         uint32_t fallthroughPc,
                         GuestBranchKind kind,
                         const char *debugName) {
    // Return false so that the recompiled function yields back to the main loop's dispatcher
    return false;
}

void PS2Runtime::HandleIntegerOverflow(R5900Context *ctx) {
    std::cerr << "PS2Runtime::HandleIntegerOverflow stub called\n";
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx) {
    MIPS_EE_Context ee_ctx;
    copyContextFromR5900(*ctx, ee_ctx);
    OpenRatchet::Kernel::DispatchSyscall(&ee_ctx, &g_ee_memory);
    copyContextToR5900(ee_ctx, *ctx);
}

void PS2Runtime::handleSyscall(uint8_t *rdram, R5900Context *ctx, uint32_t encodedSyscallId) {
    MIPS_EE_Context ee_ctx;
    copyContextFromR5900(*ctx, ee_ctx);
    OpenRatchet::Kernel::DispatchSyscall(&ee_ctx, &g_ee_memory);
    copyContextToR5900(ee_ctx, *ctx);
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

