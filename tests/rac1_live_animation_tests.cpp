#include "assets/rac1_moby_animation.h"
#include "game/rac1_live_animation.h"
#include "game/rac1_live_state.h"

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using LiveLayout = ratchet::game::Rac1LiveMobyLayout;
using AnimLayout = ratchet::game::Rac1LiveAnimationLayout;

static_assert(AnimLayout::kTransitionCacheBase == 0x001aabc0u,
              "Retail sub_0020C880/FUN_0020EDE8 transition cache base changed");

void writeLe16(std::vector<std::uint8_t>& bytes,
               std::uint32_t address,
               std::uint16_t value) {
    bytes.at(address + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(address + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeLe32(std::vector<std::uint8_t>& bytes,
               std::uint32_t address,
               std::uint32_t value) {
    bytes.at(address + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(address + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(address + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(address + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void writeFloat(std::vector<std::uint8_t>& bytes,
                std::uint32_t address,
                float value) {
    writeLe32(bytes, address, std::bit_cast<std::uint32_t>(value));
}

void writeI16(std::vector<std::uint8_t>& bytes,
              std::size_t address,
              std::int16_t value) {
    writeLe16(bytes, static_cast<std::uint32_t>(address),
              static_cast<std::uint16_t>(value));
}

void writeDenseOneJointFrame(std::vector<std::uint8_t>& bytes,
                             std::size_t frame,
                             std::int16_t z,
                             std::int16_t w) {
    writeLe16(bytes, static_cast<std::uint32_t>(frame + 0x06u), 1u);
    writeLe16(bytes, static_cast<std::uint32_t>(frame + 0x08u), 8u);
    writeLe16(bytes, static_cast<std::uint32_t>(frame + 0x0au), 0u);
    writeLe16(bytes, static_cast<std::uint32_t>(frame + 0x0cu), 8u);
    writeLe16(bytes, static_cast<std::uint32_t>(frame + 0x0eu), 0u);
    writeI16(bytes, frame + 0x10u + 0u, 0);
    writeI16(bytes, frame + 0x10u + 2u, 0);
    writeI16(bytes, frame + 0x10u + 4u, z);
    writeI16(bytes, frame + 0x10u + 6u, w);
}

bool near(float a, float b, float epsilon = 5.0e-4f) {
    return std::abs(a - b) <= epsilon;
}

// Mirrors the proved Ratchet runtime shape instead of an ordinary all-local
// Moby class: two immutable external sequence IDs have zero class-local +0x48
// pointers, then one runtime-appended local sequence occupies the old count slot.
struct LiveFixture {
    static constexpr std::uint32_t kPoolBase = 0x00180020u;
    static constexpr std::uint32_t kClass = 0x00100000u;
    static constexpr std::uint32_t kRuntimeSequence2 = 0x00110000u;
    static constexpr std::uint32_t kRuntimeFrame20 = 0x00120000u;
    static constexpr std::uint32_t kRuntimeFrame21 = 0x00120040u;
    static constexpr std::uint32_t kExternalFrame0 = 0x00121000u;
    static constexpr std::uint32_t kExternalFrame1 = 0x00121040u;
    static constexpr std::uint32_t kDirectFrameA = 0x00130000u;
    static constexpr std::uint32_t kDirectFrameB = 0x00130100u;

    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x00200000u, 0u);

    std::uint32_t slot(std::size_t index) const {
        return kPoolBase + static_cast<std::uint32_t>(index * LiveLayout::kStride);
    }

    LiveFixture() {
        writeLe32(ram, LiveLayout::kPoolBasePointerAddress, kPoolBase);
        writeLe32(
            ram,
            LiveLayout::kPoolLastSlotPointerAddress,
            kPoolBase + static_cast<std::uint32_t>(
                (LiveLayout::kCapacity - 1u) * LiveLayout::kStride));

        ram.at(slot(0) + LiveLayout::kTraversalStateOffset) = 0u;
        writeLe32(ram, slot(0) + LiveLayout::kClassPointerOffset, kClass);
        writeLe16(ram, slot(0) + LiveLayout::kOClassOffset, 0u);
        writeLe32(ram, slot(0) + LiveLayout::kPoolIndexOffset, 0u);
        ram.at(slot(1) + LiveLayout::kTraversalStateOffset) = 0xffu;

        ram.at(kClass + AnimLayout::kClassSequenceCountOffset) = 3u;
        // Sequence IDs 0 and 1 are the immutable external-bank prefix.
        writeLe32(ram, kClass + AnimLayout::kClassSequenceTableOffset + 0u, 0u);
        writeLe32(ram, kClass + AnimLayout::kClassSequenceTableOffset + 4u, 0u);
        // Retail appends a class-local sequence at the old count slot.
        writeLe32(ram, kClass + AnimLayout::kClassSequenceTableOffset + 8u,
                  kRuntimeSequence2);

        ram.at(kRuntimeSequence2 + AnimLayout::kSequenceFrameCountOffset) = 2u;
        writeLe32(ram,
                  kRuntimeSequence2 + AnimLayout::kSequenceFrameTableOffset + 0u,
                  kRuntimeFrame20);
        writeLe32(ram,
                  kRuntimeSequence2 + AnimLayout::kSequenceFrameTableOffset + 4u,
                  kRuntimeFrame21);

        setNormalAnimation();
    }

    void setNormalAnimation() {
        ram.at(slot(0) + LiveLayout::kSequenceAOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameAOffset) = 1u;
        ram.at(slot(0) + LiveLayout::kSequenceBOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameBOffset) = 0u;
        writeFloat(ram, slot(0) + LiveLayout::kInterpolationOffset, 0.25f);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerAOffset, kRuntimeFrame21);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerBOffset, kRuntimeFrame20);
        ram.at(slot(0) + LiveLayout::kAnimationFlagsOffset) = 0xa5u;
    }

    void setExternalCrossSequenceAnimation(float alpha = 0.5f) {
        ram.at(slot(0) + LiveLayout::kSequenceAOffset) = 0u;
        ram.at(slot(0) + LiveLayout::kFrameAOffset) = 0u;
        ram.at(slot(0) + LiveLayout::kSequenceBOffset) = 1u;
        ram.at(slot(0) + LiveLayout::kFrameBOffset) = 0u;
        writeFloat(ram, slot(0) + LiveLayout::kInterpolationOffset, alpha);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerAOffset, kExternalFrame0);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerBOffset, kExternalFrame1);
    }

    void setRuntimeToExternalAnimation(bool materialized, float alpha = 0.5f) {
        ram.at(slot(0) + LiveLayout::kSequenceAOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameAOffset) = 0u;
        ram.at(slot(0) + LiveLayout::kSequenceBOffset) = 0u;
        ram.at(slot(0) + LiveLayout::kFrameBOffset) = 0u;
        writeFloat(ram, slot(0) + LiveLayout::kInterpolationOffset, alpha);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerAOffset,
                  materialized ? kRuntimeFrame20 : 0u);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerBOffset,
                  materialized ? kExternalFrame0 : 0u);
    }

    void setDirectPacketAnimation(float alpha = 0.5f) {
        ram.at(slot(0) + LiveLayout::kSequenceAOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameAOffset) = 0u;
        ram.at(slot(0) + LiveLayout::kSequenceBOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameBOffset) = 1u;
        writeFloat(ram, slot(0) + LiveLayout::kInterpolationOffset, alpha);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerAOffset, kDirectFrameA);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerBOffset, kDirectFrameB);
    }

    void setTransitionAnimation(std::uint8_t cacheSlot, float alpha = 0.5f) {
        const std::uint32_t cachePointer =
            AnimLayout::kTransitionCacheBase +
            static_cast<std::uint32_t>(cacheSlot) * AnimLayout::kTransitionCacheSlotBytes;
        ram.at(slot(0) + LiveLayout::kSequenceAOffset) = AnimLayout::kTransitionSequence;
        ram.at(slot(0) + LiveLayout::kFrameAOffset) = cacheSlot;
        ram.at(slot(0) + LiveLayout::kSequenceBOffset) = 2u;
        ram.at(slot(0) + LiveLayout::kFrameBOffset) = 0u;
        writeFloat(ram, slot(0) + LiveLayout::kInterpolationOffset, alpha);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerAOffset, cachePointer);
        writeLe32(ram, slot(0) + LiveLayout::kFramePointerBOffset, kRuntimeFrame20);
    }

    ratchet::game::Rac1LiveMobyPoolSnapshot snapshot() const {
        return ratchet::game::inspectRac1LiveMobyPool(ram);
    }

    ratchet::game::Rac1LiveRatchetAnimationResult inspect() const {
        return ratchet::game::inspectRac1LiveRatchetAnimation(ram, snapshot());
    }
};

ratchet::assets::Rac1MobyAnimationClass makeNativeRatchetClass() {
    ratchet::assets::Rac1MobyAnimationClass cls{};
    cls.oClass = 0;
    cls.jointCount = 1u;
    cls.sequenceCount = 2u;
    cls.commonTransformWords = {{
        {std::bit_cast<std::uint32_t>(0.0f),
         std::bit_cast<std::uint32_t>(0.0f),
         std::bit_cast<std::uint32_t>(0.0f),
         0u},
    }};

    ratchet::assets::Rac1MobySequenceLayout a{};
    a.sequenceIndex = 0u;
    a.storage = ratchet::assets::Rac1MobySequenceStorage::RatchetExternal;
    a.frameCount = 1u;
    cls.sequenceLayouts.push_back(a);

    ratchet::assets::Rac1MobySequenceLayout b{};
    b.sequenceIndex = 1u;
    b.storage = ratchet::assets::Rac1MobySequenceStorage::RatchetExternal;
    b.frameCount = 1u;
    cls.sequenceLayouts.push_back(b);
    return cls;
}

void writeMaterializedPosePackets(LiveFixture& fixture) {
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kRuntimeFrame20, 23170, 23170);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kRuntimeFrame21, 0, 32767);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kExternalFrame0, 0, 32767);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kExternalFrame1, 32767, 0);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kDirectFrameA, 23170, 23170);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kDirectFrameB, 32767, 0);
}

void testRuntimeLocalTableBackedSelection() {
    LiveFixture fixture;
    const auto result = fixture.inspect();
    assert(result.ok());
    assert(result.ratchetCandidates == 1u);
    assert(result.selection.mobyGuestAddress == fixture.slot(0));
    assert(result.selection.classPointer == LiveFixture::kClass);
    assert(result.selection.sequenceCount == 3u);
    assert(result.selection.externalSequenceCount == 2u);
    assert(result.selection.runtimeLocalSequenceCount == 1u);
    assert(near(result.selection.interpolation, 0.25f, 1.0e-7f));
    assert(result.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::SequenceFrame);
    assert(result.selection.endpointA.sequenceIndex == 2u);
    assert(result.selection.endpointA.frameIndex == 1u);
    assert(result.selection.endpointA.frameCount == 2u);
    assert(result.selection.endpointA.sequencePointer == LiveFixture::kRuntimeSequence2);
    assert(result.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame21);
    assert(result.selection.endpointA.observedFramePointer == LiveFixture::kRuntimeFrame21);
    assert(result.selection.endpointB.sequenceIndex == 2u);
    assert(result.selection.endpointB.frameIndex == 0u);
    assert(result.selection.endpointB.expectedFramePointer == LiveFixture::kRuntimeFrame20);

    // +0x70 remains raw Phase-11 state. Changing it must not alter selection.
    fixture.ram.at(fixture.slot(0) + LiveLayout::kAnimationFlagsOffset) = 0x5au;
    const auto flagsIgnored = fixture.inspect();
    assert(flagsIgnored.ok());
    assert(flagsIgnored.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame21);
    assert(flagsIgnored.selection.endpointB.expectedFramePointer == LiveFixture::kRuntimeFrame20);
}

void testExternalPrefixAndLegalUnmaterializedState() {
    LiveFixture fixture;
    fixture.setRuntimeToExternalAnimation(false, 0.0f);
    const auto result = fixture.inspect();
    assert(result.status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized);
    assert(result.selection.sequenceCount == 3u);
    assert(result.selection.externalSequenceCount == 2u);
    assert(result.selection.runtimeLocalSequenceCount == 1u);

    assert(result.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::SequenceFrame);
    assert(result.selection.endpointA.sequenceIndex == 2u);
    assert(result.selection.endpointA.sequencePointer == LiveFixture::kRuntimeSequence2);
    assert(result.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame20);
    assert(result.selection.endpointA.observedFramePointer == 0u);

    assert(result.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence);
    assert(result.selection.endpointB.sequenceIndex == 0u);
    assert(result.selection.endpointB.sequencePointer == 0u);
    assert(result.selection.endpointB.expectedFramePointer == 0u);
    assert(result.selection.endpointB.observedFramePointer == 0u);

    // One zero pointer is not silently treated as the two-endpoint construction
    // state. Only the observed simultaneous zero pair has the proved meaning.
    fixture.setRuntimeToExternalAnimation(true);
    writeLe32(fixture.ram, fixture.slot(0) + LiveLayout::kFramePointerAOffset, 0u);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::FramePointerAOutOfRange);

    fixture.setRuntimeToExternalAnimation(true);
    writeLe32(fixture.ram, fixture.slot(0) + LiveLayout::kFramePointerBOffset, 0u);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::FramePointerBOutOfRange);
}

void testExactWindows135SequenceConstructionState() {
    LiveFixture fixture;
    // Mirror the September Windows evidence literally: Phase-10 external prefix
    // 0..133, one runtime-appended sequence 134, A=134/B=0, both packets zero.
    fixture.ram.at(LiveFixture::kClass + AnimLayout::kClassSequenceCountOffset) = 135u;
    writeLe32(fixture.ram,
              LiveFixture::kClass + AnimLayout::kClassSequenceTableOffset + 8u,
              0u);
    writeLe32(fixture.ram,
              LiveFixture::kClass + AnimLayout::kClassSequenceTableOffset + 134u * 4u,
              LiveFixture::kRuntimeSequence2);
    fixture.ram.at(fixture.slot(0) + LiveLayout::kSequenceAOffset) = 134u;
    fixture.ram.at(fixture.slot(0) + LiveLayout::kFrameAOffset) = 0u;
    fixture.ram.at(fixture.slot(0) + LiveLayout::kSequenceBOffset) = 0u;
    fixture.ram.at(fixture.slot(0) + LiveLayout::kFrameBOffset) = 0u;
    writeFloat(fixture.ram, fixture.slot(0) + LiveLayout::kInterpolationOffset, 0.0f);
    writeLe32(fixture.ram, fixture.slot(0) + LiveLayout::kFramePointerAOffset, 0u);
    writeLe32(fixture.ram, fixture.slot(0) + LiveLayout::kFramePointerBOffset, 0u);

    const auto result = fixture.inspect();
    assert(result.status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized);
    assert(result.selection.sequenceCount == 135u);
    assert(result.selection.externalSequenceCount == 134u);
    assert(result.selection.runtimeLocalSequenceCount == 1u);
    assert(result.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::SequenceFrame);
    assert(result.selection.endpointA.sequenceIndex == 134u);
    assert(result.selection.endpointA.sequencePointer == LiveFixture::kRuntimeSequence2);
    assert(result.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame20);
    assert(result.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence);
    assert(result.selection.endpointB.sequenceIndex == 0u);
}

void testDirectRetailPacketProvenanceAndStrictFailures() {
    LiveFixture fixture;

    // A local table mismatch is provenance, not itself failure. Retail consumes
    // the observed packet directly, but arbitrary/unbounded memory still fails.
    writeLe32(fixture.ram,
              fixture.slot(0) + LiveLayout::kFramePointerAOffset,
              LiveFixture::kRuntimeFrame20);
    const auto directA = fixture.inspect();
    assert(directA.ok());
    assert(directA.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::DirectGuestPacket);
    assert(directA.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame21);
    assert(directA.selection.endpointA.observedFramePointer == LiveFixture::kRuntimeFrame20);

    fixture.setNormalAnimation();
    writeLe32(fixture.ram,
              fixture.slot(0) + LiveLayout::kFramePointerBOffset,
              LiveFixture::kRuntimeFrame21);
    const auto directB = fixture.inspect();
    assert(directB.ok());
    assert(directB.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::DirectGuestPacket);
    assert(directB.selection.endpointB.expectedFramePointer == LiveFixture::kRuntimeFrame20);
    assert(directB.selection.endpointB.observedFramePointer == LiveFixture::kRuntimeFrame21);

    fixture.setNormalAnimation();
    writeLe32(fixture.ram,
              fixture.slot(0) + LiveLayout::kFramePointerAOffset,
              static_cast<std::uint32_t>(fixture.ram.size() - 8u));
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::FramePointerAOutOfRange);

    fixture.setNormalAnimation();
    constexpr std::uint32_t truncatedPacket = 0x001ffff0u;
    writeLe16(fixture.ram, truncatedPacket + 0x06u, 1u);
    writeLe32(fixture.ram,
              fixture.slot(0) + LiveLayout::kFramePointerAOffset,
              truncatedPacket);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::FramePacketAInvalid);

    fixture.setNormalAnimation();
    writeLe16(fixture.ram, truncatedPacket + 0x06u, 1u);
    writeLe32(fixture.ram,
              fixture.slot(0) + LiveLayout::kFramePointerBOffset,
              truncatedPacket);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::FramePacketBInvalid);

    fixture.setNormalAnimation();
    writeFloat(fixture.ram,
               fixture.slot(0) + LiveLayout::kInterpolationOffset,
               1.25f);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::InvalidInterpolation);

    fixture.setNormalAnimation();
    fixture.ram.at(fixture.slot(1) + LiveLayout::kTraversalStateOffset) = 0u;
    writeLe32(fixture.ram, fixture.slot(1) + LiveLayout::kClassPointerOffset,
              LiveFixture::kClass);
    writeLe16(fixture.ram, fixture.slot(1) + LiveLayout::kOClassOffset, 0u);
    fixture.ram.at(fixture.slot(2) + LiveLayout::kTraversalStateOffset) = 0xffu;
    const auto ambiguous = fixture.inspect();
    assert(ambiguous.status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::MultipleRatchetCandidates);
    assert(ambiguous.ratchetCandidates == 2u);

    fixture = LiveFixture{};
    auto snapshot = fixture.snapshot();
    snapshot.ratchetCandidateCount = 0u;
    const auto accounting = ratchet::game::inspectRac1LiveRatchetAnimation(
        fixture.ram, snapshot);
    assert(accounting.status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::RatchetCandidateAccountingMismatch);
}

void testTransitionCacheSelection() {
    LiveFixture fixture;
    fixture.setTransitionAnimation(3u, 0.625f);
    const auto result = fixture.inspect();
    assert(result.ok());
    assert(result.selection.externalSequenceCount == 2u);
    assert(result.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::TransitionCache);
    assert(result.selection.endpointA.sequenceIndex == AnimLayout::kTransitionSequence);
    assert(result.selection.endpointA.frameIndex == 3u);
    assert(result.selection.endpointA.expectedFramePointer ==
           AnimLayout::kTransitionCacheBase + 3u * AnimLayout::kTransitionCacheSlotBytes);
    assert(result.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::SequenceFrame);
    assert(result.selection.endpointB.sequenceIndex == 2u);

    fixture.setTransitionAnimation(16u);
    assert(fixture.inspect().status ==
           ratchet::game::Rac1LiveRatchetAnimationStatus::TransitionSlotOutOfRange);
}

void testExternalPacketsDriveNativeCrossSequencePose() {
    LiveFixture fixture;
    fixture.setExternalCrossSequenceAnimation(0.5f);
    writeMaterializedPosePackets(fixture);
    const auto live = fixture.inspect();
    assert(live.ok());
    assert(live.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence);
    assert(live.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence);

    std::vector<std::uint8_t> unusedCore;
    const auto cls = makeNativeRatchetClass();
    const auto driven = ratchet::game::decodeRac1LiveRatchetPose(
        fixture.ram, live, unusedCore, cls);
    assert(driven.ok());
    assert(driven.pose.ok());
    assert(driven.pose.pose.sequenceIndex == 0u);
    assert(driven.pose.pose.nextSequenceIndex == 1u);
    assert(driven.pose.pose.interpolated);
    assert(near(driven.pose.pose.interpolationAlpha, 0.5f));

    // identity -> 180 Z at alpha .5 = 90 Z.
    const auto& m = driven.pose.pose.jointMatrices[0];
    assert(near(m[0], 0.0f));
    assert(near(m[1], -1.0f));
    assert(near(m[4], 1.0f));
    assert(near(m[5], 0.0f));
}

void testRuntimeLocalToExternalPacketsDriveNativePose() {
    LiveFixture fixture;
    fixture.setRuntimeToExternalAnimation(true, 0.5f);
    writeMaterializedPosePackets(fixture);
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kExternalFrame0, 32767, 0);
    const auto live = fixture.inspect();
    assert(live.ok());
    assert(live.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::SequenceFrame);
    assert(live.selection.endpointA.sequenceIndex == 2u);
    assert(live.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence);
    assert(live.selection.endpointB.sequenceIndex == 0u);

    std::vector<std::uint8_t> unusedCore;
    const auto cls = makeNativeRatchetClass();
    const auto driven = ratchet::game::decodeRac1LiveRatchetPose(
        fixture.ram, live, unusedCore, cls);
    assert(driven.ok());
    assert(driven.pose.pose.sequenceIndex == 2u);
    assert(driven.pose.pose.nextSequenceIndex == 0u);

    constexpr float kInvSqrt2 = 0.70710678f; // midpoint 90 -> 180 = 135 deg.
    const auto& m = driven.pose.pose.jointMatrices[0];
    assert(near(m[0], -kInvSqrt2));
    assert(near(m[1], -kInvSqrt2));
    assert(near(m[4], kInvSqrt2));
    assert(near(m[5], -kInvSqrt2));
}

void testLiveTransitionPacketDrivesNativePose() {
    LiveFixture fixture;
    constexpr std::uint8_t cacheSlot = 4u;
    fixture.setTransitionAnimation(cacheSlot, 0.5f);
    writeMaterializedPosePackets(fixture);
    const std::uint32_t packet =
        AnimLayout::kTransitionCacheBase +
        static_cast<std::uint32_t>(cacheSlot) * AnimLayout::kTransitionCacheSlotBytes;
    writeDenseOneJointFrame(fixture.ram, packet, 23170, 23170); // 90 deg Z
    writeDenseOneJointFrame(fixture.ram, LiveFixture::kRuntimeFrame20, 32767, 0); // 180

    const auto live = fixture.inspect();
    assert(live.ok());
    assert(live.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::TransitionCache);

    std::vector<std::uint8_t> unusedCore;
    const auto cls = makeNativeRatchetClass();
    const auto driven = ratchet::game::decodeRac1LiveRatchetPose(
        fixture.ram, live, unusedCore, cls);
    assert(driven.ok());
    assert(driven.pose.pose.sequenceIndex == AnimLayout::kTransitionSequence);
    assert(driven.pose.pose.frameIndex == cacheSlot);
    assert(driven.pose.pose.nextSequenceIndex == 2u);

    constexpr float kInvSqrt2 = 0.70710678f;
    const auto& m = driven.pose.pose.jointMatrices[0];
    assert(near(m[0], -kInvSqrt2));
    assert(near(m[1], -kInvSqrt2));
    assert(near(m[4], kInvSqrt2));
    assert(near(m[5], -kInvSqrt2));
}

void testDirectRetailPacketsDriveNativePose() {
    LiveFixture fixture;
    fixture.setDirectPacketAnimation(0.5f);
    writeMaterializedPosePackets(fixture);

    const auto live = fixture.inspect();
    assert(live.ok());
    assert(live.selection.endpointA.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::DirectGuestPacket);
    assert(live.selection.endpointB.kind ==
           ratchet::game::Rac1LiveAnimationEndpointKind::DirectGuestPacket);
    assert(live.selection.endpointA.expectedFramePointer == LiveFixture::kRuntimeFrame20);
    assert(live.selection.endpointB.expectedFramePointer == LiveFixture::kRuntimeFrame21);
    assert(live.selection.endpointA.packetBytes == 0x20u);
    assert(live.selection.endpointB.packetBytes == 0x20u);

    std::vector<std::uint8_t> unusedCore;
    const auto cls = makeNativeRatchetClass();
    const auto driven = ratchet::game::decodeRac1LiveRatchetPose(
        fixture.ram, live, unusedCore, cls);
    assert(driven.ok());

    constexpr float kInvSqrt2 = 0.70710678f;
    const auto& m = driven.pose.pose.jointMatrices[0];
    assert(near(m[0], -kInvSqrt2));
    assert(near(m[1], -kInvSqrt2));
    assert(near(m[4], kInvSqrt2));
    assert(near(m[5], -kInvSqrt2));
}

void testNativePoseBridgeRejectsMismatchedExternalContract() {
    LiveFixture fixture;
    fixture.setExternalCrossSequenceAnimation(0.5f);
    writeMaterializedPosePackets(fixture);
    const auto live = fixture.inspect();
    assert(live.ok());

    std::vector<std::uint8_t> unusedCore;
    auto wrongClass = makeNativeRatchetClass();
    wrongClass.oClass = 1;
    assert(ratchet::game::decodeRac1LiveRatchetPose(
               fixture.ram, live, unusedCore, wrongClass).status ==
           ratchet::game::Rac1LiveRatchetPoseStatus::NativeClassMismatch);

    auto wrongSequenceCount = makeNativeRatchetClass();
    wrongSequenceCount.sequenceCount = 3u;
    assert(ratchet::game::decodeRac1LiveRatchetPose(
               fixture.ram, live, unusedCore, wrongSequenceCount).status ==
           ratchet::game::Rac1LiveRatchetPoseStatus::NativeSequenceCountMismatch);

    // External frame bounds come from the independently decoded immutable bank,
    // not from a zero class-local sequence pointer.
    fixture.setExternalCrossSequenceAnimation(0.5f);
    fixture.ram.at(fixture.slot(0) + LiveLayout::kFrameAOffset) = 1u;
    writeMaterializedPosePackets(fixture);
    const auto badExternalFrame = fixture.inspect();
    assert(badExternalFrame.ok());
    const auto cls = makeNativeRatchetClass();
    assert(ratchet::game::decodeRac1LiveRatchetPose(
               fixture.ram, badExternalFrame, unusedCore, cls).status ==
           ratchet::game::Rac1LiveRatchetPoseStatus::NativeExternalFrameOutOfRange);
}

} // namespace

int main() {
    testRuntimeLocalTableBackedSelection();
    testExternalPrefixAndLegalUnmaterializedState();
    testExactWindows135SequenceConstructionState();
    testDirectRetailPacketProvenanceAndStrictFailures();
    testTransitionCacheSelection();
    testExternalPacketsDriveNativeCrossSequencePose();
    testRuntimeLocalToExternalPacketsDriveNativePose();
    testLiveTransitionPacketDrivesNativePose();
    testDirectRetailPacketsDriveNativePose();
    testNativePoseBridgeRejectsMismatchedExternalContract();

    assert(std::string_view(ratchet::game::rac1LiveRatchetAnimationStatusName(
               ratchet::game::Rac1LiveRatchetAnimationStatus::Ok)) == "ok");
    assert(std::string_view(ratchet::game::rac1LiveRatchetAnimationStatusName(
               ratchet::game::Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized)) ==
           "endpoints-not-materialized");
    assert(std::string_view(ratchet::game::rac1LiveAnimationEndpointKindName(
               ratchet::game::Rac1LiveAnimationEndpointKind::ExternalSequence)) ==
           "external-sequence");
    assert(std::string_view(ratchet::game::rac1LiveAnimationEndpointKindName(
               ratchet::game::Rac1LiveAnimationEndpointKind::TransitionCache)) ==
           "transition-cache");
    assert(std::string_view(ratchet::game::rac1LiveAnimationEndpointKindName(
               ratchet::game::Rac1LiveAnimationEndpointKind::DirectGuestPacket)) ==
           "direct-guest-packet");
    assert(std::string_view(ratchet::game::rac1LiveRatchetPoseStatusName(
               ratchet::game::Rac1LiveRatchetPoseStatus::NativeExternalFrameOutOfRange)) ==
           "native-external-frame-out-of-range");
    assert(std::string_view(ratchet::game::rac1LiveRatchetPoseStatusName(
               ratchet::game::Rac1LiveRatchetPoseStatus::Ok)) == "ok");
    std::cout << "rac1_live_animation_tests: PASS\n";
    return 0;
}
