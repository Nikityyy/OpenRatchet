#include "game/rac1_live_animation.h"

#include <cmath>
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

enum class EndpointSide : std::uint8_t { A, B };

Rac1LiveRatchetAnimationStatus sequenceOutOfRangeStatus(EndpointSide side) {
    return side == EndpointSide::A
        ? Rac1LiveRatchetAnimationStatus::SequenceAOutOfRange
        : Rac1LiveRatchetAnimationStatus::SequenceBOutOfRange;
}

Rac1LiveRatchetAnimationStatus sequencePointerOutOfRangeStatus(EndpointSide side) {
    return side == EndpointSide::A
        ? Rac1LiveRatchetAnimationStatus::SequencePointerAOutOfRange
        : Rac1LiveRatchetAnimationStatus::SequencePointerBOutOfRange;
}

Rac1LiveRatchetAnimationStatus frameOutOfRangeStatus(EndpointSide side) {
    return side == EndpointSide::A
        ? Rac1LiveRatchetAnimationStatus::FrameAOutOfRange
        : Rac1LiveRatchetAnimationStatus::FrameBOutOfRange;
}

Rac1LiveRatchetAnimationStatus framePointerOutOfRangeStatus(EndpointSide side) {
    return side == EndpointSide::A
        ? Rac1LiveRatchetAnimationStatus::FramePointerAOutOfRange
        : Rac1LiveRatchetAnimationStatus::FramePointerBOutOfRange;
}

Rac1LiveRatchetAnimationStatus framePacketInvalidStatus(EndpointSide side) {
    return side == EndpointSide::A
        ? Rac1LiveRatchetAnimationStatus::FramePacketAInvalid
        : Rac1LiveRatchetAnimationStatus::FramePacketBInvalid;
}

bool validateObservedPacket(
    std::span<const std::uint8_t> guestRdram,
    std::uint32_t observedFramePointer,
    EndpointSide side,
    Rac1LiveAnimationEndpoint& endpoint,
    Rac1LiveRatchetAnimationStatus& status,
    std::size_t maxPacketBytes = std::numeric_limits<std::size_t>::max()) {
    if (observedFramePointer == 0u ||
        !contains(guestRdram, observedFramePointer, 0x10u)) {
        status = framePointerOutOfRangeStatus(side);
        return false;
    }

    const std::uint16_t payloadQwords = readLe16(
        guestRdram, observedFramePointer + 0x06u);
    const std::size_t packetBytes =
        0x10u + static_cast<std::size_t>(payloadQwords) * 0x10u;
    if (packetBytes > maxPacketBytes ||
        !contains(guestRdram, observedFramePointer, packetBytes)) {
        status = framePacketInvalidStatus(side);
        return false;
    }

    endpoint.packetBytes = static_cast<std::uint32_t>(packetBytes);
    return true;
}

std::uint8_t externalSequencePrefixCount(
    std::span<const std::uint8_t> guestRdram,
    std::uint32_t classPointer,
    std::uint8_t sequenceCount) {
    using L = Rac1LiveAnimationLayout;

    std::uint8_t prefix = 0u;
    for (; prefix < sequenceCount; ++prefix) {
        const std::uint32_t slot =
            classPointer + L::kClassSequenceTableOffset +
            static_cast<std::uint32_t>(prefix) * sizeof(std::uint32_t);
        if (readLe32(guestRdram, slot) != 0u) break;
    }
    return prefix;
}

bool resolveSequenceIdentity(
    std::span<const std::uint8_t> guestRdram,
    std::uint32_t classPointer,
    std::uint8_t sequenceCount,
    std::uint8_t externalSequenceCount,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint32_t observedFramePointer,
    EndpointSide side,
    Rac1LiveAnimationEndpoint& endpoint,
    Rac1LiveRatchetAnimationStatus& status) {
    using L = Rac1LiveAnimationLayout;

    endpoint.sequenceIndex = sequenceIndex;
    endpoint.frameIndex = frameIndex;
    endpoint.observedFramePointer = observedFramePointer;

    if (sequenceIndex >= sequenceCount) {
        status = sequenceOutOfRangeStatus(side);
        return false;
    }

    const std::uint32_t sequenceSlot =
        classPointer + L::kClassSequenceTableOffset +
        static_cast<std::uint32_t>(sequenceIndex) * sizeof(std::uint32_t);
    if (!contains(guestRdram, sequenceSlot, sizeof(std::uint32_t))) {
        status = sequencePointerOutOfRangeStatus(side);
        return false;
    }

    endpoint.sequencePointer = readLe32(guestRdram, sequenceSlot);
    if (sequenceIndex < externalSequenceCount) {
        // Phase 10 proved that Ratchet's immutable bank is represented by zero
        // class-local slots. The live selector records only identity here; frame
        // bounds receive an independent oracle from the native external bank at
        // pose decode time.
        endpoint.kind = Rac1LiveAnimationEndpointKind::ExternalSequence;
        return true;
    }

    endpoint.kind = Rac1LiveAnimationEndpointKind::SequenceFrame;
    if (endpoint.sequencePointer == 0u ||
        !contains(guestRdram,
                  endpoint.sequencePointer,
                  L::kSequenceFrameTableOffset + sizeof(std::uint32_t))) {
        status = sequencePointerOutOfRangeStatus(side);
        return false;
    }

    endpoint.frameCount = guestRdram[
        endpoint.sequencePointer + L::kSequenceFrameCountOffset];
    if (frameIndex >= endpoint.frameCount) {
        status = frameOutOfRangeStatus(side);
        return false;
    }

    const std::uint32_t frameSlot =
        endpoint.sequencePointer + L::kSequenceFrameTableOffset +
        static_cast<std::uint32_t>(frameIndex) * sizeof(std::uint32_t);
    if (!contains(guestRdram, frameSlot, sizeof(std::uint32_t))) {
        status = framePointerOutOfRangeStatus(side);
        return false;
    }

    endpoint.expectedFramePointer = readLe32(guestRdram, frameSlot);
    if (endpoint.expectedFramePointer == 0u ||
        !contains(guestRdram, endpoint.expectedFramePointer, 0x10u)) {
        status = framePointerOutOfRangeStatus(side);
        return false;
    }

    return true;
}

bool resolveTransitionIdentity(
    std::span<const std::uint8_t> guestRdram,
    std::uint8_t transitionSlot,
    std::uint32_t observedFramePointer,
    Rac1LiveAnimationEndpoint& endpoint,
    Rac1LiveRatchetAnimationStatus& status) {
    using L = Rac1LiveAnimationLayout;

    endpoint.kind = Rac1LiveAnimationEndpointKind::TransitionCache;
    endpoint.sequenceIndex = L::kTransitionSequence;
    endpoint.frameIndex = transitionSlot;
    endpoint.observedFramePointer = observedFramePointer;

    if (transitionSlot >= L::kTransitionCacheSlotCount) {
        status = Rac1LiveRatchetAnimationStatus::TransitionSlotOutOfRange;
        return false;
    }

    endpoint.expectedFramePointer =
        L::kTransitionCacheBase +
        static_cast<std::uint32_t>(transitionSlot) * L::kTransitionCacheSlotBytes;
    if (!contains(guestRdram,
                  endpoint.expectedFramePointer,
                  L::kTransitionCacheSlotBytes)) {
        status = Rac1LiveRatchetAnimationStatus::TransitionPacketOutOfRange;
        return false;
    }
    return true;
}

bool classifyObservedPacket(
    std::span<const std::uint8_t> guestRdram,
    EndpointSide side,
    Rac1LiveAnimationEndpoint& endpoint,
    Rac1LiveRatchetAnimationStatus& status) {
    using L = Rac1LiveAnimationLayout;

    const bool exactTransitionCache =
        endpoint.kind == Rac1LiveAnimationEndpointKind::TransitionCache &&
        endpoint.observedFramePointer == endpoint.expectedFramePointer;
    const std::size_t maxPacketBytes = exactTransitionCache
        ? L::kTransitionCacheSlotBytes
        : std::numeric_limits<std::size_t>::max();

    if (!validateObservedPacket(
            guestRdram,
            endpoint.observedFramePointer,
            side,
            endpoint,
            status,
            maxPacketBytes)) {
        if (exactTransitionCache &&
            status == Rac1LiveRatchetAnimationStatus::FramePacketAInvalid) {
            status = Rac1LiveRatchetAnimationStatus::TransitionPacketOutOfRange;
        }
        return false;
    }

    // sub_0020C880 proves the expected pointer for runtime-local/cache producer
    // paths, while FUN_00224B70/FUN_00224D28/FUN_00224E18 prove legal repointing.
    // ExternalSequence intentionally has no class-local expected pointer.
    if ((endpoint.kind == Rac1LiveAnimationEndpointKind::SequenceFrame ||
         endpoint.kind == Rac1LiveAnimationEndpointKind::TransitionCache) &&
        endpoint.observedFramePointer != endpoint.expectedFramePointer) {
        endpoint.kind = Rac1LiveAnimationEndpointKind::DirectGuestPacket;
    }
    return true;
}

std::span<const std::uint8_t> observedPacketSpan(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveAnimationEndpoint& endpoint) {
    if (endpoint.packetBytes == 0u ||
        !contains(guestRdram, endpoint.observedFramePointer, endpoint.packetBytes)) {
        return {};
    }
    return guestRdram.subspan(
        static_cast<std::size_t>(endpoint.observedFramePointer),
        static_cast<std::size_t>(endpoint.packetBytes));
}

bool validateExternalEndpointAgainstNativeBank(
    const Rac1LiveAnimationEndpoint& endpoint,
    const assets::Rac1MobyAnimationClass& nativeRatchetClass) {
    if (endpoint.kind != Rac1LiveAnimationEndpointKind::ExternalSequence) {
        return true;
    }
    const std::size_t sequenceIndex = endpoint.sequenceIndex;
    return sequenceIndex < nativeRatchetClass.sequenceLayouts.size() &&
           endpoint.frameIndex < nativeRatchetClass.sequenceLayouts[sequenceIndex].frameCount;
}

} // namespace

Rac1LiveRatchetAnimationResult inspectRac1LiveRatchetAnimation(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveMobyPoolSnapshot& poolSnapshot) {
    using L = Rac1LiveAnimationLayout;

    Rac1LiveRatchetAnimationResult result{};
    if (poolSnapshot.status == Rac1LiveMobyPoolStatus::PoolNotInitialized) {
        result.status = Rac1LiveRatchetAnimationStatus::PoolNotReady;
        return result;
    }
    if (poolSnapshot.status != Rac1LiveMobyPoolStatus::Ok) {
        result.status = Rac1LiveRatchetAnimationStatus::PoolInvalid;
        return result;
    }

    const Rac1LiveMobyRecord* ratchet = nullptr;
    for (const auto& record : poolSnapshot.records) {
        if (!record.participatesInRetailTraversal || record.oClass != 0) {
            continue;
        }
        ++result.ratchetCandidates;
        if (ratchet == nullptr) ratchet = &record;
    }

    if (result.ratchetCandidates == 0u || ratchet == nullptr) {
        result.status = Rac1LiveRatchetAnimationStatus::NoRatchetCandidate;
        return result;
    }
    if (result.ratchetCandidates != 1u) {
        result.status = Rac1LiveRatchetAnimationStatus::MultipleRatchetCandidates;
        return result;
    }
    if (result.ratchetCandidates != poolSnapshot.ratchetCandidateCount) {
        result.status = Rac1LiveRatchetAnimationStatus::RatchetCandidateAccountingMismatch;
        return result;
    }

    auto& selection = result.selection;
    selection.mobyGuestAddress = ratchet->guestAddress;
    selection.classPointer = ratchet->classPointer;
    selection.interpolation = ratchet->animation.interpolation;

    if (!std::isfinite(selection.interpolation) ||
        selection.interpolation < 0.0f || selection.interpolation > 1.0f) {
        result.status = Rac1LiveRatchetAnimationStatus::InvalidInterpolation;
        return result;
    }

    if (!contains(guestRdram,
                  selection.classPointer,
                  L::kClassSequenceTableOffset)) {
        result.status = Rac1LiveRatchetAnimationStatus::ClassPointerOutOfRange;
        return result;
    }

    selection.sequenceCount = guestRdram[
        selection.classPointer + L::kClassSequenceCountOffset];
    if (selection.sequenceCount == 0u) {
        result.status = Rac1LiveRatchetAnimationStatus::SequenceCountZero;
        return result;
    }

    const std::size_t sequenceTableBytes =
        static_cast<std::size_t>(selection.sequenceCount) * sizeof(std::uint32_t);
    if (!contains(guestRdram,
                  selection.classPointer + L::kClassSequenceTableOffset,
                  sequenceTableBytes)) {
        result.status = Rac1LiveRatchetAnimationStatus::ClassPointerOutOfRange;
        return result;
    }

    selection.externalSequenceCount = externalSequencePrefixCount(
        guestRdram, selection.classPointer, selection.sequenceCount);
    selection.runtimeLocalSequenceCount = static_cast<std::uint8_t>(
        selection.sequenceCount - selection.externalSequenceCount);

    if (ratchet->animation.sequenceA == L::kTransitionSequence) {
        if (!resolveTransitionIdentity(
                guestRdram,
                ratchet->animation.frameA,
                ratchet->animation.framePointerA,
                selection.endpointA,
                result.status)) {
            return result;
        }
    } else if (!resolveSequenceIdentity(
                   guestRdram,
                   selection.classPointer,
                   selection.sequenceCount,
                   selection.externalSequenceCount,
                   ratchet->animation.sequenceA,
                   ratchet->animation.frameA,
                   ratchet->animation.framePointerA,
                   EndpointSide::A,
                   selection.endpointA,
                   result.status)) {
        return result;
    }

    if (!resolveSequenceIdentity(
            guestRdram,
            selection.classPointer,
            selection.sequenceCount,
            selection.externalSequenceCount,
            ratchet->animation.sequenceB,
            ratchet->animation.frameB,
            ratchet->animation.framePointerB,
            EndpointSide::B,
            selection.endpointB,
            result.status)) {
        return result;
    }

    // FUN_0020C5F0 can skip sub_0020C880 when Ratchet's first class-local
    // sequence slot is zero, and sub_00204790 appends a local sequence/updates
    // sequence IDs without materializing +0x68/+0x6c. Preserve the observed
    // two-zero construction state explicitly; never fabricate packets from IDs.
    if (selection.endpointA.observedFramePointer == 0u &&
        selection.endpointB.observedFramePointer == 0u) {
        result.status = Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized;
        return result;
    }

    if (!classifyObservedPacket(
            guestRdram, EndpointSide::A, selection.endpointA, result.status)) {
        return result;
    }
    if (!classifyObservedPacket(
            guestRdram, EndpointSide::B, selection.endpointB, result.status)) {
        return result;
    }

    result.status = Rac1LiveRatchetAnimationStatus::Ok;
    return result;
}

Rac1LiveRatchetPoseResult decodeRac1LiveRatchetPose(
    std::span<const std::uint8_t> guestRdram,
    const Rac1LiveRatchetAnimationResult& liveAnimation,
    std::span<const std::uint8_t> core,
    const assets::Rac1MobyAnimationClass& nativeRatchetClass) {
    Rac1LiveRatchetPoseResult result{};
    if (!liveAnimation.ok()) {
        result.status = Rac1LiveRatchetPoseStatus::LiveSelectionInvalid;
        return result;
    }
    if (nativeRatchetClass.oClass != 0) {
        result.status = Rac1LiveRatchetPoseStatus::NativeClassMismatch;
        return result;
    }

    const auto& selection = liveAnimation.selection;
    if (nativeRatchetClass.sequenceCount != selection.externalSequenceCount ||
        nativeRatchetClass.sequenceLayouts.size() != nativeRatchetClass.sequenceCount) {
        result.status = Rac1LiveRatchetPoseStatus::NativeSequenceCountMismatch;
        return result;
    }
    if (!validateExternalEndpointAgainstNativeBank(
            selection.endpointA, nativeRatchetClass) ||
        !validateExternalEndpointAgainstNativeBank(
            selection.endpointB, nativeRatchetClass)) {
        result.status = Rac1LiveRatchetPoseStatus::NativeExternalFrameOutOfRange;
        return result;
    }

    // FUN_0020EDE8 consumes +0x68/+0x6c directly. Once both are materialized,
    // those packets are the strongest available oracle regardless of whether
    // their identity originated in the immutable external bank, a runtime-local
    // appended sequence, the transition cache, or a later direct repoint.
    const auto packetA = observedPacketSpan(guestRdram, selection.endpointA);
    const auto packetB = observedPacketSpan(guestRdram, selection.endpointB);
    if (packetA.empty() || packetB.empty()) {
        result.status = Rac1LiveRatchetPoseStatus::GuestPacketOutOfRange;
        return result;
    }

    (void)core;
    result.pose = assets::decodeRac1MobyPoseInterpolatedFromPackets(
        packetA,
        packetB,
        nativeRatchetClass,
        selection.endpointA.sequenceIndex,
        selection.endpointA.frameIndex,
        selection.endpointB.sequenceIndex,
        selection.endpointB.frameIndex,
        selection.interpolation);

    result.status = result.pose.ok()
        ? Rac1LiveRatchetPoseStatus::Ok
        : Rac1LiveRatchetPoseStatus::PoseDecodeFailed;
    return result;
}

const char* rac1LiveRatchetAnimationStatusName(
    Rac1LiveRatchetAnimationStatus status) noexcept {
    switch (status) {
    case Rac1LiveRatchetAnimationStatus::Ok: return "ok";
    case Rac1LiveRatchetAnimationStatus::PoolNotReady: return "pool-not-ready";
    case Rac1LiveRatchetAnimationStatus::PoolInvalid: return "pool-invalid";
    case Rac1LiveRatchetAnimationStatus::NoRatchetCandidate: return "no-ratchet-candidate";
    case Rac1LiveRatchetAnimationStatus::MultipleRatchetCandidates:
        return "multiple-ratchet-candidates";
    case Rac1LiveRatchetAnimationStatus::RatchetCandidateAccountingMismatch:
        return "ratchet-candidate-accounting-mismatch";
    case Rac1LiveRatchetAnimationStatus::InvalidInterpolation: return "invalid-interpolation";
    case Rac1LiveRatchetAnimationStatus::ClassPointerOutOfRange:
        return "class-pointer-out-of-range";
    case Rac1LiveRatchetAnimationStatus::SequenceCountZero: return "sequence-count-zero";
    case Rac1LiveRatchetAnimationStatus::SequenceAOutOfRange: return "sequence-a-out-of-range";
    case Rac1LiveRatchetAnimationStatus::SequenceBOutOfRange: return "sequence-b-out-of-range";
    case Rac1LiveRatchetAnimationStatus::SequencePointerAOutOfRange:
        return "sequence-pointer-a-out-of-range";
    case Rac1LiveRatchetAnimationStatus::SequencePointerBOutOfRange:
        return "sequence-pointer-b-out-of-range";
    case Rac1LiveRatchetAnimationStatus::FrameAOutOfRange: return "frame-a-out-of-range";
    case Rac1LiveRatchetAnimationStatus::FrameBOutOfRange: return "frame-b-out-of-range";
    case Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized:
        return "endpoints-not-materialized";
    case Rac1LiveRatchetAnimationStatus::FramePointerAOutOfRange:
        return "frame-pointer-a-out-of-range";
    case Rac1LiveRatchetAnimationStatus::FramePointerBOutOfRange:
        return "frame-pointer-b-out-of-range";
    case Rac1LiveRatchetAnimationStatus::FramePacketAInvalid:
        return "frame-packet-a-invalid";
    case Rac1LiveRatchetAnimationStatus::FramePacketBInvalid:
        return "frame-packet-b-invalid";
    case Rac1LiveRatchetAnimationStatus::TransitionSlotOutOfRange:
        return "transition-slot-out-of-range";
    case Rac1LiveRatchetAnimationStatus::TransitionPacketOutOfRange:
        return "transition-packet-out-of-range";
    }
    return "unknown";
}

const char* rac1LiveRatchetPoseStatusName(
    Rac1LiveRatchetPoseStatus status) noexcept {
    switch (status) {
    case Rac1LiveRatchetPoseStatus::Ok: return "ok";
    case Rac1LiveRatchetPoseStatus::LiveSelectionInvalid: return "live-selection-invalid";
    case Rac1LiveRatchetPoseStatus::NativeClassMismatch: return "native-class-mismatch";
    case Rac1LiveRatchetPoseStatus::NativeSequenceCountMismatch:
        return "native-sequence-count-mismatch";
    case Rac1LiveRatchetPoseStatus::NativeExternalFrameOutOfRange:
        return "native-external-frame-out-of-range";
    case Rac1LiveRatchetPoseStatus::GuestPacketOutOfRange:
        return "guest-packet-out-of-range";
    case Rac1LiveRatchetPoseStatus::PoseDecodeFailed: return "pose-decode-failed";
    }
    return "unknown";
}

const char* rac1LiveAnimationEndpointKindName(
    Rac1LiveAnimationEndpointKind kind) noexcept {
    switch (kind) {
    case Rac1LiveAnimationEndpointKind::SequenceFrame: return "sequence-frame";
    case Rac1LiveAnimationEndpointKind::ExternalSequence: return "external-sequence";
    case Rac1LiveAnimationEndpointKind::TransitionCache: return "transition-cache";
    case Rac1LiveAnimationEndpointKind::DirectGuestPacket: return "direct-guest-packet";
    }
    return "unknown";
}

} // namespace ratchet::game
