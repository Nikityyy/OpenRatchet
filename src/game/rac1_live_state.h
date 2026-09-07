#pragma once

#include <array>
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

    // FUN_0020d868 loads the xyz vector at +0x10 and subtracts the current
    // world-space reference before its spatial-distance tests. FUN_0021e230
    // independently writes Moby spawn coordinates to +0x10/+0x14/+0x18.
    static constexpr std::uint32_t kWorldPositionOffset = 0x10u;

    // FUN_0020c5f0 initializes +0x2c from class+0x24. FUN_0020cca8 and
    // sub_0020CD48 consume it with the literal 1/1024 factor; FUN_0020def8
    // consumes the raw value directly before producing 1024-scaled world data.
    static constexpr std::uint32_t kRawModelScaleOffset = 0x2cu;

    // FUN_0020def8 feeds +0x40 into VU0 microprogram 0xD18 and stores the
    // resulting three rotation-basis vectors at +0xc0/+0xd0/+0xe0. The same
    // function then uses those vectors as the x/y/z basis columns. +0x40 is
    // retained as proved Retail rotation input/provenance; the native bridge
    // consumes the materialized Retail basis instead of guessing Euler order.
    static constexpr std::uint32_t kRotationInputOffset = 0x40u;
    static constexpr std::uint32_t kRotationBasisXOffset = 0xc0u;
    static constexpr std::uint32_t kRotationBasisYOffset = 0xd0u;
    static constexpr std::uint32_t kRotationBasisZOffset = 0xe0u;

    // FUN_0020c5f0 writes the class-data pointer selected from
    // 0x001B3200[slot] at +0x24 and preserves the original oClass at +0xA6.
    // It independently writes 0x001B3580[slot] at +0x74; that word is not the
    // class-data pointer and its higher-level semantic role remains uninterpreted.
    static constexpr std::uint32_t kClassPointerOffset = 0x24u;
    static constexpr std::uint32_t kRuntimeAuxPointerOffset = 0x74u;
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

// Retail's runtime Moby-class registry is independent from the current
// level-core class-index table. FUN_0020C5F0 performs two distinct slot-table
// lookups. The class-data identity path is exactly:
//
//   slot          = *(u8 *)(0x001B3AC0 + oClass)
//   classDataPtr  = *(u32 *)(0x001B3200 + slot * 4)
//   moby->class   = classDataPtr                         // moby+0x24
//
// The same constructor separately loads 0x001B3580[slot] and stores that word
// at moby+0x74. It must not be confused with the +0x24 class-data pointer.
// sub_001EA830 and FUN_00230F60 initialize the 0x800-byte oClass->slot table
// to 0xFF, while sub_00203640 publishes loaded class-data pointers into
// 0x001B3200. The +0x24/0x001B3200 equality is therefore the authoritative
// class-data identity oracle for live runtime-only and level-origin classes.
struct Rac1LiveMobyClassRegistryLayout final {
    static constexpr std::uint32_t kOClassToSlotAddress = 0x001b3ac0u;
    static constexpr std::uint32_t kOClassToSlotBytes = 0x800u;
    static constexpr std::uint8_t kUnregisteredSlot = 0xffu;
    static constexpr std::uint32_t kClassDataPointerTableAddress = 0x001b3200u;
    static constexpr std::uint32_t kRuntimeAuxPointerTableAddress = 0x001b3580u;
};

enum class Rac1LiveMobyPoolStatus : std::uint8_t {
    Ok,
    GuestMemoryTooSmall,
    PoolNotInitialized,
    PoolRangeOutOfRange,
    PoolLastSlotMismatch,
    MissingTraversalTerminator,
};

struct Rac1LiveMobyWorldTransformState {
    std::array<float, 3> position{};
    float rawModelScale = 0.0f;
    std::array<float, 3> rotationInput{};
    std::array<float, 3> basisX{};
    std::array<float, 3> basisY{};
    std::array<float, 3> basisZ{};
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
    Rac1LiveMobyWorldTransformState worldTransform;
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

enum class Rac1LiveMobyClassRegistryStatus : std::uint8_t {
    Ok,
    PoolUnavailable,
    GuestMemoryTooSmall,
    AccountingMismatch,
};

enum class Rac1LiveMobyClassRegistryEntryStatus : std::uint8_t {
    Ok,
    OClassOutOfRange,
    UnregisteredOClass,
    ClassPointerMismatch,
};

struct Rac1LiveMobyClassRegistryEntry {
    Rac1LiveMobyClassRegistryEntryStatus status =
        Rac1LiveMobyClassRegistryEntryStatus::OClassOutOfRange;
    std::uint32_t mobyGuestAddress = 0u;
    std::int16_t oClass = 0;
    std::uint8_t registrySlot = Rac1LiveMobyClassRegistryLayout::kUnregisteredSlot;
    std::uint32_t mobyClassPointer = 0u;
    std::uint32_t registryClassPointer = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveMobyClassRegistryEntryStatus::Ok;
    }
};

struct Rac1LiveMobyClassRegistrySnapshot {
    Rac1LiveMobyClassRegistryStatus status =
        Rac1LiveMobyClassRegistryStatus::PoolUnavailable;
    std::size_t records = 0u;
    std::size_t active = 0u;
    std::size_t inactive = 0u;
    std::vector<Rac1LiveMobyClassRegistryEntry> activeEntries;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveMobyClassRegistryStatus::Ok;
    }
};

// Decode only fields whose layout/use is proved by retail generated code.
// Step 11.4 adds the raw live world-transform inputs and Retail-cached rotation
// basis; semantic validation/conversion remains in rac1_live_transform.*. Camera
// ownership remains outside this contract until its independent Step-11.5 proof.
[[nodiscard]] Rac1LiveMobyPoolSnapshot inspectRac1LiveMobyPool(
    std::span<const std::uint8_t> guestRdram);

// Snapshot the same runtime-class lookup that FUN_0020C5F0 consumes. This is
// called under the same GuestExecutionScope as inspectRac1LiveMobyPool so the
// Moby's stored +0x24 class-data pointer and the 0x1B3200 registry table are coherent.
[[nodiscard]] Rac1LiveMobyClassRegistrySnapshot inspectRac1LiveMobyClassRegistry(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveMobyPoolSnapshot& livePool);

[[nodiscard]] const char* rac1LiveMobyPoolStatusName(Rac1LiveMobyPoolStatus status);
[[nodiscard]] const char* rac1LiveMobyClassRegistryStatusName(
    Rac1LiveMobyClassRegistryStatus status) noexcept;
[[nodiscard]] const char* rac1LiveMobyClassRegistryEntryStatusName(
    Rac1LiveMobyClassRegistryEntryStatus status) noexcept;

} // namespace ratchet::game
