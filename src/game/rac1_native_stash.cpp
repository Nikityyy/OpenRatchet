#include "game/rac1_native_stash.h"

#include "runtime/native_replacements.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

using Contract = Rac1NativeStashContract;

struct NativeStashSlot {
    std::uint32_t reservedUnits = 0u;
    std::uint32_t copiedUnits = 0u;
    std::uint32_t retailTag = 0u;
    std::vector<std::uint8_t> bytes;
};

struct NativeStashState {
    std::array<NativeStashSlot, Contract::kSlotCount> slots{};
    std::size_t nextSlot = 0u;
};

NativeStashState g_stash;
std::mutex g_stashMutex;
std::uint32_t g_storeDiagnostics = 0u;
std::uint32_t g_readDiagnostics = 0u;
bool g_initLogged = false;

void returnToGuestCaller(R5900Context* ctx) {
    ctx->pc = GPR_U32(ctx, 31);
}

void returnToGuestCaller(R5900Context* ctx, std::int32_t result) {
    SET_GPR_U32(ctx, 2, static_cast<std::uint32_t>(result));
    returnToGuestCaller(ctx);
}

bool checkedUnitBytes(std::uint32_t units, std::size_t& bytes) noexcept {
    constexpr std::uint32_t kMaxGuestUnits =
        Contract::kGuestRamBytes / static_cast<std::uint32_t>(Contract::kUnitBytes);
    if (units > kMaxGuestUnits) {
        return false;
    }
    bytes = static_cast<std::size_t>(units) * Contract::kUnitBytes;
    return true;
}

bool guestRangeValid(std::uint32_t address, std::size_t size) noexcept {
    return address <= Contract::kGuestRamBytes &&
           size <= static_cast<std::size_t>(Contract::kGuestRamBytes - address);
}

void resetNativeStash() {
    std::scoped_lock lock(g_stashMutex);
    g_stash = {};
    g_storeDiagnostics = 0u;
    g_readDiagnostics = 0u;
}

void nativeStashInitialize(std::uint8_t*, R5900Context* ctx, PS2Runtime*) {
    resetNativeStash();

    if (!g_initLogged) {
        g_initLogged = true;
        std::cerr << "[OpenRatchet:stash] component=init"
                  << " source=native-host slots=" << Contract::kSlotCount
                  << " unitBytes=" << Contract::kUnitBytes
                  << " remoteIopBuffer=removed"
                  << " sid11Bypass=1 status=ok\n";
    }

    // The only Retail caller (sub_001EB798) does not consume a return value.
    // Keep v0 untouched rather than inventing a native result for a void-like
    // initialization boundary.
    returnToGuestCaller(ctx);
}

void nativeStashStore(std::uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const std::uint32_t source = GPR_U32(ctx, 4);
    const std::uint32_t copyUnits = GPR_U32(ctx, 5);
    const std::uint32_t reservedUnits = GPR_U32(ctx, 6);
    const std::uint32_t retailTag = GPR_U32(ctx, 7);

    std::size_t copyBytes = 0u;
    std::size_t reservedBytes = 0u;
    if (!checkedUnitBytes(copyUnits, copyBytes) ||
        !checkedUnitBytes(reservedUnits, reservedBytes) ||
        copyUnits > reservedUnits ||
        reservedBytes > Contract::kGuestRamBytes ||
        !guestRangeValid(source, copyBytes)) {
        std::cerr << "[OpenRatchet:stash] component=store"
                  << " source=0x" << std::hex << source << std::dec
                  << " copyUnits=" << copyUnits
                  << " reservedUnits=" << reservedUnits
                  << " status=invalid-range\n";
        returnToGuestCaller(ctx, Contract::kStoreOutOfSpace);
        return;
    }

    std::scoped_lock lock(g_stashMutex);
    if (g_stash.nextSlot >= Contract::kSlotCount) {
        returnToGuestCaller(ctx, Contract::kStoreNoSlot);
        return;
    }

    const std::size_t slotIndex = g_stash.nextSlot;
    NativeStashSlot staged;
    staged.reservedUnits = reservedUnits;
    staged.copiedUnits = copyUnits;
    staged.retailTag = retailTag;
    staged.bytes.resize(copyBytes);
    if (copyBytes != 0u) {
        std::memcpy(staged.bytes.data(), rdram + source, copyBytes);
    }

    g_stash.slots[slotIndex] = std::move(staged);
    ++g_stash.nextSlot;

    ++g_storeDiagnostics;
    if (g_storeDiagnostics <= 12u) {
        std::cerr << "[OpenRatchet:stash] component=store"
                  << " slot=" << slotIndex
                  << " source=0x" << std::hex << source
                  << " tag=0x" << retailTag << std::dec
                  << " copyUnits=" << copyUnits
                  << " reservedUnits=" << reservedUnits
                  << " bytes=" << copyBytes
                  << " backend=native-host"
                  << " sifDmaBypass=1 status=ok\n";
    }

    returnToGuestCaller(ctx, static_cast<std::int32_t>(slotIndex));
}

void nativeStashRead(std::uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const std::uint32_t destination = GPR_U32(ctx, 4);
    const std::uint32_t slotIndex = GPR_U32(ctx, 5);
    const std::uint32_t offsetUnits = GPR_U32(ctx, 6);
    std::uint32_t readUnits = GPR_U32(ctx, 7);
    const std::uint32_t rpcMode = GPR_U32(ctx, 8);

    std::scoped_lock lock(g_stashMutex);
    if (slotIndex >= Contract::kSlotCount) {
        returnToGuestCaller(ctx, Contract::kStoreOutOfSpace);
        return;
    }

    const NativeStashSlot& slot = g_stash.slots[slotIndex];
    if (slot.reservedUnits == 0u) {
        returnToGuestCaller(ctx, Contract::kInvalidSlot);
        return;
    }

    if (readUnits == std::numeric_limits<std::uint32_t>::max()) {
        readUnits = slot.reservedUnits;
    }

    const std::uint64_t endUnits =
        static_cast<std::uint64_t>(offsetUnits) + static_cast<std::uint64_t>(readUnits);
    if (rpcMode != 0u || endUnits > slot.reservedUnits || endUnits > slot.copiedUnits) {
        returnToGuestCaller(ctx, Contract::kStoreOutOfSpace);
        return;
    }

    std::size_t offsetBytes = 0u;
    std::size_t readBytes = 0u;
    if (!checkedUnitBytes(offsetUnits, offsetBytes) ||
        !checkedUnitBytes(readUnits, readBytes) ||
        !guestRangeValid(destination, readBytes) ||
        offsetBytes > slot.bytes.size() ||
        readBytes > slot.bytes.size() - offsetBytes) {
        returnToGuestCaller(ctx, Contract::kStoreOutOfSpace);
        return;
    }

    if (readBytes != 0u) {
        std::memcpy(rdram + destination, slot.bytes.data() + offsetBytes, readBytes);
    }

    ++g_readDiagnostics;
    if (g_readDiagnostics <= 12u) {
        std::cerr << "[OpenRatchet:stash] component=read"
                  << " slot=" << slotIndex
                  << " destination=0x" << std::hex << destination << std::dec
                  << " offsetUnits=" << offsetUnits
                  << " readUnits=" << readUnits
                  << " bytes=" << readBytes
                  << " backend=native-host"
                  << " sid11Bypass=1 status=ok\n";
    }

    returnToGuestCaller(ctx, 0);
}

void nativeStashSize(std::uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const std::uint32_t slotIndex = GPR_U32(ctx, 4);
    std::scoped_lock lock(g_stashMutex);
    if (slotIndex >= Contract::kSlotCount) {
        returnToGuestCaller(ctx, Contract::kInvalidSlot);
        return;
    }

    returnToGuestCaller(
        ctx, static_cast<std::int32_t>(g_stash.slots[slotIndex].reservedUnits));
}

} // namespace

void declareRac1NativeStashReplacements(
    runtime::NativeReplacementRegistry& registry) {
    using runtime::NativeReplacementStage;

    registry.add(Contract::kInitializeFunction,
                 "rac1-stash-init",
                 NativeReplacementStage::Runtime,
                 &nativeStashInitialize);
    registry.add(Contract::kStoreFunction,
                 "rac1-stash-store",
                 NativeReplacementStage::Runtime,
                 &nativeStashStore);
    registry.add(Contract::kReadFunction,
                 "rac1-stash-read",
                 NativeReplacementStage::Runtime,
                 &nativeStashRead);
    registry.add(Contract::kSizeFunction,
                 "rac1-stash-size",
                 NativeReplacementStage::Runtime,
                 &nativeStashSize);
}

} // namespace ratchet::game
