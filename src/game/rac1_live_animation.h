#pragma once

#include "assets/rac1_moby_animation.h"
#include "game/rac1_live_state.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::game {

// Retail Ratchet uses two sequence-storage domains at runtime. Phase 10 proved
// that the immutable oClass-0 bank is external: the original class-local +0x48
// slots are zero while LevelCoreHeader +0x78 names the 134-entry external bank.
// sub_00204790 proves that Retail may then append a new class-local sequence at
// the old sequenceCount slot and increment class+0x0c. sub_0020C880 resolves
// class-local sequence/frame endpoints and the sequenceA==0xff transition cache,
// while FUN_0020EDE8 is the authoritative pose consumer of moby+0x68/+0x6c.
struct Rac1LiveAnimationLayout final {
    static constexpr std::uint32_t kClassSequenceCountOffset = 0x0cu;
    static constexpr std::uint32_t kClassSequenceTableOffset = 0x48u;
    static constexpr std::uint32_t kSequenceFrameCountOffset = 0x10u;
    static constexpr std::uint32_t kSequenceFrameTableOffset = 0x1cu;

    static constexpr std::uint8_t kTransitionSequence = 0xffu;
    static constexpr std::uint32_t kTransitionCacheBase = 0x001aabc0u;
    static constexpr std::uint32_t kTransitionCacheSlotBytes = 0x800u;
    static constexpr std::size_t kTransitionCacheSlotCount = 16u;
};

enum class Rac1LiveAnimationEndpointKind : std::uint8_t {
    // Runtime-appended class-local sequence with an independently resolvable
    // class+0x48 -> sequence+0x1c frame-table provenance.
    SequenceFrame,
    // Immutable Ratchet sequence whose class-local pointer is intentionally zero
    // because its proved storage domain is the external Phase-10 bank.
    ExternalSequence,
    TransitionCache,
    // A proved class-local/cache identity whose observed Retail packet was
    // repointed by another producer before FUN_0020EDE8 consumed it.
    DirectGuestPacket,
};

enum class Rac1LiveRatchetAnimationStatus : std::uint8_t {
    Ok,
    PoolNotReady,
    PoolInvalid,
    NoRatchetCandidate,
    MultipleRatchetCandidates,
    RatchetCandidateAccountingMismatch,
    InvalidInterpolation,
    ClassPointerOutOfRange,
    SequenceCountZero,
    SequenceAOutOfRange,
    SequenceBOutOfRange,
    SequencePointerAOutOfRange,
    SequencePointerBOutOfRange,
    FrameAOutOfRange,
    FrameBOutOfRange,
    // Legal Retail construction state: sequence/frame identity is already
    // published, but neither FUN_0020EDE8 endpoint packet has been materialized
    // into moby+0x68/+0x6c yet. This is deliberately not status=ok.
    EndpointsNotMaterialized,
    FramePointerAOutOfRange,
    FramePointerBOutOfRange,
    FramePacketAInvalid,
    FramePacketBInvalid,
    TransitionSlotOutOfRange,
    TransitionPacketOutOfRange,
};

struct Rac1LiveAnimationEndpoint {
    Rac1LiveAnimationEndpointKind kind = Rac1LiveAnimationEndpointKind::SequenceFrame;
    std::uint8_t sequenceIndex = 0u;
    std::uint8_t frameIndex = 0u;
    // Known directly only for runtime-local sequences. External sequence frame
    // bounds are independently checked against the immutable native Ratchet bank
    // when a materialized live pose is decoded.
    std::uint8_t frameCount = 0u;
    std::uint32_t sequencePointer = 0u;
    std::uint32_t observedFramePointer = 0u;
    std::uint32_t expectedFramePointer = 0u;
    std::uint32_t packetBytes = 0u;
};

struct Rac1LiveRatchetAnimationSelection {
    std::uint32_t mobyGuestAddress = 0u;
    std::uint32_t classPointer = 0u;
    std::uint8_t sequenceCount = 0u;
    // Number of leading zero class-local +0x48 slots. Phase-10 external-bank
    // validation provides the independent native oracle for this prefix.
    std::uint8_t externalSequenceCount = 0u;
    std::uint8_t runtimeLocalSequenceCount = 0u;
    float interpolation = 0.0f;
    Rac1LiveAnimationEndpoint endpointA{};
    Rac1LiveAnimationEndpoint endpointB{};
};

struct Rac1LiveRatchetAnimationResult {
    Rac1LiveRatchetAnimationStatus status = Rac1LiveRatchetAnimationStatus::PoolNotReady;
    std::size_t ratchetCandidates = 0u;
    Rac1LiveRatchetAnimationSelection selection{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveRatchetAnimationStatus::Ok;
    }
};

enum class Rac1LiveRatchetPoseStatus : std::uint8_t {
    Ok,
    LiveSelectionInvalid,
    NativeClassMismatch,
    NativeSequenceCountMismatch,
    NativeExternalFrameOutOfRange,
    GuestPacketOutOfRange,
    PoseDecodeFailed,
};

struct Rac1LiveRatchetPoseResult {
    Rac1LiveRatchetPoseStatus status = Rac1LiveRatchetPoseStatus::LiveSelectionInvalid;
    assets::Rac1MobyPoseResult pose{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveRatchetPoseStatus::Ok;
    }
};

// Select exactly one live traversed oClass-0 record. The class-local sequence
// table is interpreted structurally as a leading external-bank prefix followed
// by any Retail-appended local suffix. Sequence/frame identity is validated in
// its proved storage domain. The observed moby+0x68/+0x6c packet pointers remain
// authoritative because FUN_0020EDE8 consumes them directly. Two simultaneous
// zero endpoint pointers are preserved as EndpointsNotMaterialized rather than
// fabricated from sequence metadata. No field at moby+0x70 participates.
[[nodiscard]] Rac1LiveRatchetAnimationResult inspectRac1LiveRatchetAnimation(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveMobyPoolSnapshot& poolSnapshot);

// Consume an already-proved, materialized live selection with the Phase-10
// native Ratchet pose codec. The immutable native bank independently validates
// the external zero-pointer prefix and external frame IDs; the pose itself is
// decoded from the two packets Retail actually placed at +0x68/+0x6c. This
// mirrors FUN_0020EDE8 and prevents sequence-bank substitution. `core` remains in
// the interface for the existing Phase-10 bridge boundary but is not used for a
// materialized packet-to-packet live pose. No demo clock participates.
[[nodiscard]] Rac1LiveRatchetPoseResult decodeRac1LiveRatchetPose(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveRatchetAnimationResult& liveAnimation,
    std::span<const std::uint8_t> core,
    const assets::Rac1MobyAnimationClass& nativeRatchetClass);

[[nodiscard]] const char* rac1LiveRatchetAnimationStatusName(
    Rac1LiveRatchetAnimationStatus status) noexcept;
[[nodiscard]] const char* rac1LiveRatchetPoseStatusName(
    Rac1LiveRatchetPoseStatus status) noexcept;
[[nodiscard]] const char* rac1LiveAnimationEndpointKindName(
    Rac1LiveAnimationEndpointKind kind) noexcept;

} // namespace ratchet::game
