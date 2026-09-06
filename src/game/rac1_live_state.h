#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::game {

// Retail R&C1 NTSC-U live-Moby contract recovered from the generated EE code.
// These are game-data semantics, not PS2 hardware semantics.
struct Rac1LiveMobyLayout final {
    // sub_001E9B10 stores the 0x4000-byte Moby arena base here and stores
    // base+0x3f00 at kPoolLastSlotPointerAddress.
    static constexpr std::uint32_t kPoolBasePointerAddress = 0x0015ff18u;
    static constexpr std::uint32_t kPoolLastSlotPointerAddress = 0x0015ff20u;
    static constexpr std::uint32_t kPoolBytes = 0x4000u;
    static constexpr std::uint32_t kStride = 0x100u;
    static constexpr std::size_t kCapacity = kPoolBytes / kStride;

    // FUN_0020d868 walks +0x100 records and reads this signed byte. -1 ends
    // the retail walk; other negative values are skipped; >=0 records enter
    // the live-Moby spatial/update path.
    static constexpr std::uint32_t kTraversalStateOffset = 0x20u;

    // FUN_0020c5f0 writes the resolved class pointer and original oClass here.
    static constexpr std::uint32_t kClassPointerOffset = 0x24u;
    static constexpr std::uint32_t kOClassOffset = 0xa6u;
    static constexpr std::uint32_t kPoolIndexOffset = 0xacu;

    // FUN_00212ed8 establishes these exact animation fields. FUN_0020d580
    // advances them in-place, including the retail final->first frame wrap.
    static constexpr std::uint32_t kFrameAOffset = 0x50u;
    static constexpr std::uint32_t kFrameBOffset = 0x51u;
    static constexpr std::uint32_t kSequenceAOffset = 0x52u;
    static constexpr std::uint32_t kSequenceBOffset = 0x53u;
    static constexpr std::uint32_t kInterpolationOffset = 0x54u;
    static constexpr std::uint32_t kFramePointerAOffset = 0x68u;
    static constexpr std::uint32_t kFramePointerBOffset = 0x6cu;
    static constexpr std::uint32_t kAnimationFlagsOffset = 0x70u;
};

enum class Rac1LiveMobyPoolStatus : std::uint8_t {
    Ok,
    GuestMemoryTooSmall,
    PoolNotInitialized,
    PoolRangeOutOfRange,
    PoolLastSlotMismatch,
    MissingTraversalTerminator,
};

struct Rac1LiveMobyAnimationState {
    std::uint8_t frameA = 0u;
    std::uint8_t frameB = 0u;
    std::uint8_t sequenceA = 0u;
    std::uint8_t sequenceB = 0u;
    float interpolation = 0.0f;
    std::uint32_t framePointerA = 0u;
    std::uint32_t framePointerB = 0u;
    std::uint8_t flags = 0u;
};

struct Rac1LiveMobyRecord {
    std::size_t slotIndex = 0u;
    std::uint32_t guestAddress = 0u;
    std::int8_t traversalState = -1;
    bool participatesInRetailTraversal = false;

    std::uint32_t classPointer = 0u;
    std::int16_t oClass = 0;
    std::uint32_t storedPoolIndex = 0u;
    Rac1LiveMobyAnimationState animation;
};

struct Rac1LiveMobyPoolSnapshot {
    Rac1LiveMobyPoolStatus status = Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
    std::uint32_t poolBase = 0u;
    std::uint32_t poolLastSlot = 0u;
    std::size_t terminatorSlot = 0u;
    std::size_t slotsBeforeTerminator = 0u;
    std::size_t traversedMobyCount = 0u;
    std::size_t skippedNegativeStateCount = 0u;
    std::size_t ratchetCandidateCount = 0u;
    std::vector<Rac1LiveMobyRecord> records;
};

// Decode only the fields whose layout/use is proved by retail generated code.
// Camera ownership and world-transform interpretation intentionally remain out
// of this contract until their independent retail consumers are established.
[[nodiscard]] Rac1LiveMobyPoolSnapshot inspectRac1LiveMobyPool(
    std::span<const std::uint8_t> guestRdram);

[[nodiscard]] const char* rac1LiveMobyPoolStatusName(Rac1LiveMobyPoolStatus status);

} // namespace ratchet::game
