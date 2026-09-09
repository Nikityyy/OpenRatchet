#pragma once

#include "assets/rac1_level.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ratchet::game {

enum class Rac1StaticElfStatus : std::uint8_t {
    Ok,
    FileOpenFailed,
    FileTooSmall,
    InvalidHeader,
    InvalidProgramHeaders,
    InvalidLoadSegment,
    NoLoadSegments,
};

struct Rac1StaticElfLoadSegment {
    std::uint32_t guestAddress = 0u;
    std::uint32_t fileOffset = 0u;
    std::uint32_t fileSize = 0u;
};

struct Rac1StaticElfImage {
    Rac1StaticElfStatus status = Rac1StaticElfStatus::FileOpenFailed;
    std::vector<std::uint8_t> fileBytes;
    std::vector<Rac1StaticElfLoadSegment> loadSegments;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1StaticElfStatus::Ok;
    }
};

Rac1StaticElfImage parseRac1StaticElfImage(std::span<const std::uint8_t> fileBytes);
Rac1StaticElfImage loadRac1StaticElfImage(const std::filesystem::path& path);
const char* rac1StaticElfStatusName(Rac1StaticElfStatus status) noexcept;

enum class Rac1OverlayCoherenceStatus : std::uint8_t {
    OverlayUnavailable,
    StaticElfUnavailable,
    SegmentPayloadOutOfRange,
    GuestDestinationOutOfRange,
    OverlayNotMaterialized,
    NoRegisteredFallbackOverlap,
    NoComparableFallbackOverlap,
    NoStaticConflictObserved,
    StaleFallbackConflictProved,
};

struct Rac1OverlayCoherenceResult {
    Rac1OverlayCoherenceStatus status = Rac1OverlayCoherenceStatus::OverlayUnavailable;
    std::size_t segmentCount = 0u;
    std::size_t payloadBytes = 0u;
    std::size_t materializedSegments = 0u;
    std::size_t materializedBytes = 0u;
    std::size_t registeredFallbackEntries = 0u;
    std::size_t comparableFallbackEntries = 0u;
    std::size_t conflictingFallbackEntries = 0u;
    bool hasFirstConflictPc = false;
    std::uint32_t firstConflictPc = 0u;

    [[nodiscard]] bool conflictProved() const noexcept {
        return status == Rac1OverlayCoherenceStatus::StaleFallbackConflictProved;
    }
};

// Compares the Retail level-overlay payload against both live guest RDRAM and
// the original boot ELF. Static comparable/conflict counts are populated even
// before the overlay is live so the two independent facts remain visible:
// (1) whether Retail payload bytes are materialized in guest memory and (2)
// whether that payload represents a different code generation from registered
// boot-ELF fallback entries. StaleFallbackConflictProved is reserved for the
// stronger case where the complete payload is live and conflicts with those
// static entries. This is read-only; it never executes, patches or substitutes
// gameplay.
Rac1OverlayCoherenceResult inspectRac1OverlayCoherence(
    std::span<const std::uint8_t> guestRdram,
    std::span<const std::uint8_t> overlay,
    std::span<const assets::Rac1LevelOverlaySegment> segments,
    const Rac1StaticElfImage& staticElf,
    std::span<const std::uint32_t> registeredFallbackAddresses);

const char* rac1OverlayCoherenceStatusName(Rac1OverlayCoherenceStatus status) noexcept;

// Read-only progress snapshot for the Boot-generation path that eventually
// hands the first secondary generation to sub_0012D8F8. 0x15EE4C is a shared
// loader pointer, not unconditionally an overlay-record stream: sub_001EB798
// first publishes its resource workspace there, while FUN_002043B0 later
// replaces it with the first-generation stream and writes the source-proved
// 0x60 record offset at [pointer+0]. The state/branch fields below are fixed
// Retail addresses on the Boot caller/FUN_00231FF0 path; 0x15F618 is consumed
// later in that function, while the three named branch predicates are all from
// the pre-FUN_002043B0 setup arm. No value is patched or synthesized.
enum class Rac1BootOverlayProgressStatus : std::uint8_t {
    Ok,
    GuestMemoryTooSmall,
    SharedLoaderPointerUnavailable,
    SharedLoaderHeaderOutOfRange,
    FirstGenerationStreamSignatureAbsent,
    Wad158FirstRecordHeaderMismatch,
    FirstRecordOutOfRange,
    FirstPayloadOutOfRange,
};

struct Rac1BootOverlayProgress {
    Rac1BootOverlayProgressStatus status =
        Rac1BootOverlayProgressStatus::GuestMemoryTooSmall;
    std::uint32_t processedPressedEdges = 0u;
    bool processedRightEdge = false;
    std::uint32_t bootExitFlag = 0u;

    // Raw lifecycle word at 0x13CAE4. Its producer/consumer meaning is kept
    // deliberately neutral until a matching Retail control-flow edge is proved.
    std::uint32_t state13CAE4 = 0u;

    // Exact fixed-address state used on the Boot caller/FUN_00231FF0 path.
    std::uint32_t state15ED84 = 0u;
    std::uint32_t state15ED88 = 0u;
    std::uint32_t state15F600 = 0u;
    // FUN_001EB190 @ 0x1EB23C reads 0x15F604; its 0x1EB244 bne skips the
    // 0x219070 call unless this exact word equals 3.
    std::uint32_t state15F604 = 0u;
    std::uint32_t state15F618 = 0u;
    std::uint32_t state13D364 = 0u;
    std::uint32_t state13D36C = 0u;
    bool branch2321A4Bypass = false;
    bool branch2321B8Wait = false;
    bool branch2321C4EnterSetup = false;

    // sub_002192A8 @ 0x2195AC loads an owner pointer from 0x1D5BF4,
    // then @ 0x2195C0..0x219600 walks exactly 14 object slots starting
    // at owner+0x44. For each non-null object, 0x2195DC loads object[0]
    // and 0x2195E8 JALRs to that callback when non-zero. These fields
    // mirror that Retail-owned callback list without executing or patching it.
    static constexpr std::size_t kCallback2195SlotCount = 14u;
    std::uint32_t callback2195OwnerPointer = 0u;
    std::uint32_t callback2195TablePointer = 0u;
    bool callback2195TableReadable = false;
    std::uint32_t callback2195NonNullObjects = 0u;
    std::uint32_t callback2195ReadableObjects = 0u;
    std::uint32_t callback2195NonNullTargets = 0u;
    std::array<std::uint32_t, kCallback2195SlotCount> callback2195Targets{};
    bool callback21E7C8Present = false;
    std::uint32_t callback21E7C8Slot = 0xFFFFFFFFu;

    std::uint32_t sharedLoaderPointer = 0u;
    std::uint32_t sharedLoaderPrefixBytes = 0u;
    bool firstGenerationStreamSignatureObserved = false;
    bool wad158FirstRecordHeaderMatched = false;
    std::uint32_t firstRecordAddress = 0u;
    std::uint32_t firstDestination = 0u;
    std::uint32_t firstPayloadBytes = 0u;
    std::uint32_t firstField8 = 0u;
    std::uint32_t firstGenerationEntry = 0u;

    bool operator==(const Rac1BootOverlayProgress&) const = default;
};

Rac1BootOverlayProgress inspectRac1BootOverlayProgress(
    std::span<const std::uint8_t> guestRdram) noexcept;
const char* rac1BootOverlayProgressStatusName(
    Rac1BootOverlayProgressStatus status) noexcept;

} // namespace ratchet::game
