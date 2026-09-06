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
        record.oClass = static_cast<std::int16_t>(readLe16(
            guestRdram, address + Rac1LiveMobyLayout::kOClassOffset));
        record.storedPoolIndex = readLe32(
            guestRdram, address + Rac1LiveMobyLayout::kPoolIndexOffset);
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

} // namespace ratchet::game
