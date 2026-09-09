#include "game/rac1_live_state.h"

#include <bit>
#include <limits>

namespace ratchet::game {
namespace {

bool contains(std::span<const std::uint8_t> bytes,
              std::uint32_t address,
              std::size_t length) {
    const std::size_t offset = static_cast<std::size_t>(address);
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

std::uint16_t readLe16(std::span<const std::uint8_t> bytes, std::uint32_t address) {
    return static_cast<std::uint16_t>(bytes[address + 0u]) |
           (static_cast<std::uint16_t>(bytes[address + 1u]) << 8u);
}

std::uint32_t readLe32(std::span<const std::uint8_t> bytes, std::uint32_t address) {
    return static_cast<std::uint32_t>(bytes[address + 0u]) |
           (static_cast<std::uint32_t>(bytes[address + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[address + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[address + 3u]) << 24u);
}

float readLeFloat(std::span<const std::uint8_t> bytes, std::uint32_t address) {
    return std::bit_cast<float>(readLe32(bytes, address));
}

} // namespace

Rac1LiveMobyPoolSnapshot inspectRac1LiveMobyPool(
    std::span<const std::uint8_t> guestRdram) {
    Rac1LiveMobyPoolSnapshot out;

    constexpr auto baseGlobal = Rac1LiveMobyLayout::kPoolBasePointerAddress;
    constexpr auto lastGlobal = Rac1LiveMobyLayout::kPoolLastSlotPointerAddress;
    if (!contains(guestRdram, baseGlobal, sizeof(std::uint32_t)) ||
        !contains(guestRdram, lastGlobal, sizeof(std::uint32_t))) {
        out.status = Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
        return out;
    }

    out.poolBase = readLe32(guestRdram, baseGlobal);
    out.poolLastSlot = readLe32(guestRdram, lastGlobal);
    if (out.poolBase == 0u) {
        out.status = Rac1LiveMobyPoolStatus::PoolNotInitialized;
        return out;
    }

    if (out.poolBase > std::numeric_limits<std::uint32_t>::max() -
                           Rac1LiveMobyLayout::kPoolBytes ||
        !contains(guestRdram, out.poolBase, Rac1LiveMobyLayout::kPoolBytes)) {
        out.status = Rac1LiveMobyPoolStatus::PoolRangeOutOfRange;
        return out;
    }

    const std::uint32_t expectedLast =
        out.poolBase + static_cast<std::uint32_t>(
                           (Rac1LiveMobyLayout::kCapacity - 1u) *
                           Rac1LiveMobyLayout::kStride);
    if (out.poolLastSlot != expectedLast) {
        out.status = Rac1LiveMobyPoolStatus::PoolLastSlotMismatch;
        return out;
    }

    out.records.reserve(Rac1LiveMobyLayout::kCapacity);
    for (std::size_t slot = 0u; slot < Rac1LiveMobyLayout::kCapacity; ++slot) {
        const std::uint32_t address =
            out.poolBase + static_cast<std::uint32_t>(slot * Rac1LiveMobyLayout::kStride);
        const std::int8_t traversalState = static_cast<std::int8_t>(
            guestRdram[address + Rac1LiveMobyLayout::kTraversalStateOffset]);

        // FUN_0020d868 treats exactly -1 as the end marker. Do not read stale
        // bytes beyond it and do not silently substitute the full 64-slot cap.
        if (traversalState == -1) {
            out.terminatorSlot = slot;
            out.slotsBeforeTerminator = slot;
            out.status = Rac1LiveMobyPoolStatus::Ok;
            return out;
        }

        Rac1LiveMobyRecord record;
        record.slotIndex = slot;
        record.guestAddress = address;
        record.traversalState = traversalState;
        record.participatesInRetailTraversal = traversalState >= 0;
        record.classPointer = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kClassPointerOffset);
        record.updateCallback = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kUpdateCallbackOffset);
        record.oClass = static_cast<std::int16_t>(readLe16(
            guestRdram, address + Rac1LiveMobyLayout::kOClassOffset));
        record.storedPoolIndex = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kPoolIndexOffset);

        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            const std::uint32_t byteOffset = static_cast<std::uint32_t>(axis * sizeof(float));
            record.worldTransform.position[axis] = readLeFloat(
                guestRdram, address + Rac1LiveMobyLayout::kWorldPositionOffset + byteOffset);
            record.worldTransform.rotationInput[axis] = readLeFloat(
                guestRdram, address + Rac1LiveMobyLayout::kRotationInputOffset + byteOffset);
            record.worldTransform.basisX[axis] = readLeFloat(
                guestRdram, address + Rac1LiveMobyLayout::kRotationBasisXOffset + byteOffset);
            record.worldTransform.basisY[axis] = readLeFloat(
                guestRdram, address + Rac1LiveMobyLayout::kRotationBasisYOffset + byteOffset);
            record.worldTransform.basisZ[axis] = readLeFloat(
                guestRdram, address + Rac1LiveMobyLayout::kRotationBasisZOffset + byteOffset);
        }
        record.worldTransform.rawModelScale = readLeFloat(
            guestRdram, address + Rac1LiveMobyLayout::kRawModelScaleOffset);

        record.animation.frameA =
            guestRdram[address + Rac1LiveMobyLayout::kFrameAOffset];
        record.animation.frameB =
            guestRdram[address + Rac1LiveMobyLayout::kFrameBOffset];
        record.animation.sequenceA =
            guestRdram[address + Rac1LiveMobyLayout::kSequenceAOffset];
        record.animation.sequenceB =
            guestRdram[address + Rac1LiveMobyLayout::kSequenceBOffset];
        record.animation.interpolation = readLeFloat(
            guestRdram, address + Rac1LiveMobyLayout::kInterpolationOffset);
        record.animation.framePointerA = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kFramePointerAOffset);
        record.animation.framePointerB = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kFramePointerBOffset);
        record.animation.flags =
            guestRdram[address + Rac1LiveMobyLayout::kAnimationFlagsOffset];

        if (record.participatesInRetailTraversal) {
            ++out.traversedMobyCount;
            if (record.oClass == 0) {
                ++out.ratchetCandidateCount;
            }
        } else {
            ++out.skippedNegativeStateCount;
        }
        out.records.push_back(record);
    }

    out.slotsBeforeTerminator = Rac1LiveMobyLayout::kCapacity;
    out.status = Rac1LiveMobyPoolStatus::MissingTraversalTerminator;
    return out;
}

Rac1LiveRatchetControlStateSnapshot inspectRac1LiveRatchetControlState(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveMobyPoolSnapshot& livePool) {
    Rac1LiveRatchetControlStateSnapshot out;
    out.ratchetCandidates = livePool.ratchetCandidateCount;
    if (livePool.status != Rac1LiveMobyPoolStatus::Ok) {
        out.status = Rac1LiveRatchetControlStateStatus::PoolUnavailable;
        return out;
    }

    const Rac1LiveMobyRecord* ratchet = nullptr;
    for (const auto& record : livePool.records) {
        if (!record.participatesInRetailTraversal || record.oClass != 0) continue;
        if (ratchet != nullptr) {
            out.status = Rac1LiveRatchetControlStateStatus::RatchetNotUnique;
            return out;
        }
        ratchet = &record;
    }
    if (ratchet == nullptr || livePool.ratchetCandidateCount != 1u) {
        out.status = Rac1LiveRatchetControlStateStatus::RatchetNotUnique;
        return out;
    }

    out.ratchetMoby = ratchet->guestAddress;
    out.ratchetUpdateCallback = ratchet->updateCallback;

    constexpr std::uint32_t pvarOffset = Rac1LiveMobyLayout::kPVarPointerOffset;
    if (out.ratchetMoby > std::numeric_limits<std::uint32_t>::max() - pvarOffset ||
        !contains(guestRdram, out.ratchetMoby + pvarOffset, sizeof(std::uint32_t))) {
        out.status = Rac1LiveRatchetControlStateStatus::GuestMemoryTooSmall;
        return out;
    }

    out.ratchetPVar = readLe32(guestRdram, out.ratchetMoby + pvarOffset);
    if (out.ratchetPVar == 0u) {
        out.status = Rac1LiveRatchetControlStateStatus::PVarUnavailable;
        return out;
    }
    if (!contains(guestRdram,
                  out.ratchetPVar + Rac1RatchetLifecycleStateLayout::kPVarOwnerStateOffset,
                  sizeof(std::uint32_t))) {
        out.status = Rac1LiveRatchetControlStateStatus::PVarOutOfRange;
        return out;
    }

    out.state = readLe32(
        guestRdram,
        out.ratchetPVar + Rac1RatchetLifecycleStateLayout::kPVarOwnerStateOffset);
    if (out.state == 0u) {
        out.status = Rac1LiveRatchetControlStateStatus::StateUnavailable;
        return out;
    }

    constexpr std::uint32_t ratchetOffset =
        Rac1RatchetLifecycleStateLayout::kRatchetMobyOffset;
    constexpr std::uint32_t companionOffset =
        Rac1RatchetLifecycleStateLayout::kCompanionMobyOffset;
    static_assert(companionOffset == ratchetOffset + sizeof(std::uint32_t));
    if (out.state > std::numeric_limits<std::uint32_t>::max() - companionOffset ||
        !contains(guestRdram, out.state + ratchetOffset,
                  companionOffset - ratchetOffset + sizeof(std::uint32_t))) {
        out.status = Rac1LiveRatchetControlStateStatus::StateOutOfRange;
        return out;
    }

    out.stateRatchetMoby = readLe32(guestRdram, out.state + ratchetOffset);
    out.companionMoby = readLe32(guestRdram, out.state + companionOffset);

    // +0x48 is optional by construction: FUN_002240C8 stores the return value
    // of func_225490(0x259) even when allocation returned zero. Preserve it only
    // as read-only diagnostics and never use it to manufacture state identity.
    if (out.companionMoby != 0u) {
        for (const auto& record : livePool.records) {
            if (record.guestAddress != out.companionMoby) continue;
            out.companionOClass = record.oClass;
            out.companionLive = record.participatesInRetailTraversal;
            break;
        }
    }

    if (out.stateRatchetMoby != out.ratchetMoby) {
        out.status = Rac1LiveRatchetControlStateStatus::StateRatchetMismatch;
        return out;
    }

    out.status = Rac1LiveRatchetControlStateStatus::Ok;
    return out;
}

Rac1LiveMobyClassRegistrySnapshot inspectRac1LiveMobyClassRegistry(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveMobyPoolSnapshot& livePool) {
    Rac1LiveMobyClassRegistrySnapshot out;
    if (livePool.status != Rac1LiveMobyPoolStatus::Ok) {
        out.status = Rac1LiveMobyClassRegistryStatus::PoolUnavailable;
        return out;
    }

    constexpr auto mapAddress = Rac1LiveMobyClassRegistryLayout::kOClassToSlotAddress;
    constexpr auto mapBytes = Rac1LiveMobyClassRegistryLayout::kOClassToSlotBytes;
    constexpr auto pointerTable = Rac1LiveMobyClassRegistryLayout::kClassDataPointerTableAddress;
    constexpr std::size_t pointerTableReach =
        (static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max()) + 1u) *
        sizeof(std::uint32_t);
    if (!contains(guestRdram, mapAddress, mapBytes) ||
        !contains(guestRdram, pointerTable, pointerTableReach)) {
        out.status = Rac1LiveMobyClassRegistryStatus::GuestMemoryTooSmall;
        return out;
    }

    out.records = livePool.records.size();
    out.activeEntries.reserve(livePool.traversedMobyCount);
    for (const auto& record : livePool.records) {
        if (!record.participatesInRetailTraversal) {
            ++out.inactive;
            continue;
        }
        ++out.active;

        Rac1LiveMobyClassRegistryEntry entry;
        entry.mobyGuestAddress = record.guestAddress;
        entry.oClass = record.oClass;
        entry.mobyClassPointer = record.classPointer;
        if (record.oClass < 0 ||
            static_cast<std::uint32_t>(record.oClass) >= mapBytes) {
            entry.status = Rac1LiveMobyClassRegistryEntryStatus::OClassOutOfRange;
            out.activeEntries.push_back(entry);
            continue;
        }

        entry.registrySlot = guestRdram[
            mapAddress + static_cast<std::uint32_t>(record.oClass)];
        if (entry.registrySlot == Rac1LiveMobyClassRegistryLayout::kUnregisteredSlot) {
            entry.status = Rac1LiveMobyClassRegistryEntryStatus::UnregisteredOClass;
            out.activeEntries.push_back(entry);
            continue;
        }

        entry.registryClassPointer = readLe32(
            guestRdram,
            pointerTable + static_cast<std::uint32_t>(entry.registrySlot) *
                               sizeof(std::uint32_t));
        entry.status = entry.registryClassPointer == entry.mobyClassPointer
                           ? Rac1LiveMobyClassRegistryEntryStatus::Ok
                           : Rac1LiveMobyClassRegistryEntryStatus::ClassPointerMismatch;
        out.activeEntries.push_back(entry);
    }

    const bool accountingMatches =
        out.records == livePool.slotsBeforeTerminator &&
        out.active == livePool.traversedMobyCount &&
        out.inactive == livePool.skippedNegativeStateCount &&
        out.active + out.inactive == out.records &&
        out.activeEntries.size() == out.active;
    out.status = accountingMatches
                     ? Rac1LiveMobyClassRegistryStatus::Ok
                     : Rac1LiveMobyClassRegistryStatus::AccountingMismatch;
    return out;
}

const char* rac1LiveMobyPoolStatusName(Rac1LiveMobyPoolStatus status) {
    switch (status) {
    case Rac1LiveMobyPoolStatus::Ok:
        return "ok";
    case Rac1LiveMobyPoolStatus::GuestMemoryTooSmall:
        return "guest-memory-too-small";
    case Rac1LiveMobyPoolStatus::PoolNotInitialized:
        return "pool-not-initialized";
    case Rac1LiveMobyPoolStatus::PoolRangeOutOfRange:
        return "pool-range-out-of-range";
    case Rac1LiveMobyPoolStatus::PoolLastSlotMismatch:
        return "pool-last-slot-mismatch";
    case Rac1LiveMobyPoolStatus::MissingTraversalTerminator:
        return "missing-traversal-terminator";
    }
    return "unknown";
}

const char* rac1LiveRatchetControlStateStatusName(
    Rac1LiveRatchetControlStateStatus status) noexcept {
    switch (status) {
    case Rac1LiveRatchetControlStateStatus::Ok:
        return "ok";
    case Rac1LiveRatchetControlStateStatus::PoolUnavailable:
        return "pool-unavailable";
    case Rac1LiveRatchetControlStateStatus::RatchetNotUnique:
        return "ratchet-not-unique";
    case Rac1LiveRatchetControlStateStatus::GuestMemoryTooSmall:
        return "guest-memory-too-small";
    case Rac1LiveRatchetControlStateStatus::PVarUnavailable:
        return "pvar-unavailable";
    case Rac1LiveRatchetControlStateStatus::PVarOutOfRange:
        return "pvar-out-of-range";
    case Rac1LiveRatchetControlStateStatus::StateUnavailable:
        return "state-unavailable";
    case Rac1LiveRatchetControlStateStatus::StateOutOfRange:
        return "state-out-of-range";
    case Rac1LiveRatchetControlStateStatus::StateRatchetMismatch:
        return "state-ratchet-mismatch";
    }
    return "unknown";
}

const char* rac1LiveMobyClassRegistryStatusName(
    Rac1LiveMobyClassRegistryStatus status) noexcept {
    switch (status) {
    case Rac1LiveMobyClassRegistryStatus::Ok:
        return "ok";
    case Rac1LiveMobyClassRegistryStatus::PoolUnavailable:
        return "pool-unavailable";
    case Rac1LiveMobyClassRegistryStatus::GuestMemoryTooSmall:
        return "guest-memory-too-small";
    case Rac1LiveMobyClassRegistryStatus::AccountingMismatch:
        return "accounting-mismatch";
    }
    return "unknown";
}

const char* rac1LiveMobyClassRegistryEntryStatusName(
    Rac1LiveMobyClassRegistryEntryStatus status) noexcept {
    switch (status) {
    case Rac1LiveMobyClassRegistryEntryStatus::Ok:
        return "ok";
    case Rac1LiveMobyClassRegistryEntryStatus::OClassOutOfRange:
        return "oclass-out-of-range";
    case Rac1LiveMobyClassRegistryEntryStatus::UnregisteredOClass:
        return "unregistered-oclass";
    case Rac1LiveMobyClassRegistryEntryStatus::ClassPointerMismatch:
        return "class-pointer-mismatch";
    }
    return "unknown";
}

} // namespace ratchet::game
