#include "assets/rac1_moby_animation.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <initializer_list>
#include <vector>

namespace {

void writeU16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
    b[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    b[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
}
void writeI16(std::vector<std::uint8_t>& b, std::size_t o, std::int16_t v) {
    writeU16(b, o, static_cast<std::uint16_t>(v));
}
void writeU32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
    b[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    b[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
    b[o + 2u] = static_cast<std::uint8_t>((v >> 16u) & 0xffu);
    b[o + 3u] = static_cast<std::uint8_t>((v >> 24u) & 0xffu);
}
void writeI32(std::vector<std::uint8_t>& b, std::size_t o, std::int32_t v) {
    writeU32(b, o, static_cast<std::uint32_t>(v));
}
void writeF32(std::vector<std::uint8_t>& b, std::size_t o, float v) {
    writeU32(b, o, std::bit_cast<std::uint32_t>(v));
}

void fillPattern(std::vector<std::uint8_t>& b,
                 std::size_t offset,
                 std::uint8_t seed,
                 std::size_t count = ratchet::assets::kRac1MobyProbePrefixBytes) {
    for (std::size_t i = 0u; i < count; ++i) {
        b[offset + i] = static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(i));
    }
}

bool near(float a, float b, float epsilon = 2.0e-4f) {
    return std::abs(a - b) <= epsilon;
}

std::array<float, 16> translationMatrix(float x, float y, float z) {
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            x,    y,    z,    1.0f};
}

std::array<float, 16> retailSkeletonRecord(
    const std::array<float, 16>& affine,
    float w0 = 12345.0f,
    float w1 = -23456.0f,
    float w2 = 34567.0f,
    float w3 = -45678.0f) {
    auto raw = affine;
    // Retail sub_0020E0E0 only consumes xyz from each 0x10-byte qword. Keep
    // deliberately absurd values in all four w lanes so the test catches any
    // regression to the earlier false "ordinary 4x4 matrix" interpretation.
    raw[3] = w0;
    raw[7] = w1;
    raw[11] = w2;
    raw[15] = w3;
    return raw;
}

std::uint32_t f32bits(float v) {
    return std::bit_cast<std::uint32_t>(v);
}

std::uint32_t fnv1a(const std::uint8_t* data, std::size_t count) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0u; i < count; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

void writePackedSkinVertex(std::vector<std::uint8_t>& b,
                           std::size_t o,
                           std::uint8_t low7,
                           std::uint8_t b2,
                           std::uint8_t b3,
                           std::uint8_t b6 = 0u,
                           std::uint8_t b7 = 0u) {
    // ID occupies bits 0..8. Keep it zero and place the following seven-bit
    // JI/L3 field in bits 9..15.
    b[o + 1u] = static_cast<std::uint8_t>(low7 << 1u);
    b[o + 2u] = b2;
    b[o + 3u] = b3;
    b[o + 6u] = b6;
    b[o + 7u] = b7;
}

std::vector<std::uint8_t> makeIndex() {
    std::vector<std::uint8_t> index(0x60u, 0xffu);
    writeU32(index, 0x00u, 0x100u);
    writeI32(index, 0x04u, 42);
    writeU32(index, 0x20u, 0u); // explicit no-class-data logic-only moby
    writeI32(index, 0x24u, 100);
    writeU32(index, 0x40u, 0x500u);
    writeI32(index, 0x44u, 77);
    return index;
}

std::vector<std::uint8_t> makeGameplay(std::initializer_list<std::int32_t> classes) {
    std::vector<std::uint8_t> gameplay(0x400u, 0u);
    constexpr std::size_t block = 0x100u;
    writeI32(gameplay, 0x44u, static_cast<std::int32_t>(block));
    writeI32(gameplay, block, static_cast<std::int32_t>(classes.size()));
    std::size_t i = 0u;
    for (const std::int32_t oClass : classes) {
        writeI32(gameplay, block + 0x10u + i * 0x78u + 0x18u, oClass);
        ++i;
    }
    return gameplay;
}

std::vector<std::uint8_t> makeCore() {
    std::vector<std::uint8_t> core(0x900u, 0u);
    constexpr std::size_t cls = 0x100u;

    // Animated class: 1 high-LOD packet, 4 joints, 2 animation sequences.
    writeI32(core, cls + 0x00u, 0x60);
    core[cls + 0x04u] = 1u;
    core[cls + 0x08u] = 4u;
    core[cls + 0x0cu] = 2u;
    writeI32(core, cls + 0x14u, 0x200);
    writeI32(core, cls + 0x18u, 0x300);
    writeI32(core, cls + 0x1cu, 0x340);
    writeI32(core, cls + 0x48u, 0x3a0);
    writeI32(core, cls + 0x4cu, 0x000); // sparse sequence-ID hole

    // Packet header points at a R&C1 32-bit vertex table.
    constexpr std::size_t packet = cls + 0x60u;
    writeI32(core, packet + 0x08u, 0x100);
    core[packet + 0x0cu] = 0x0fu; // vertex-data size in qwords
    core[packet + 0x0fu] = 3u;

    constexpr std::size_t vertexTable = cls + 0x100u;
    writeI32(core, vertexTable + 0x00u, 2); // matrix transfers
    writeI32(core, vertexTable + 0x04u, 3); // two-way
    writeI32(core, vertexTable + 0x08u, 2); // three-way
    writeI32(core, vertexTable + 0x0cu, 5); // main/no-blend
    writeI32(core, vertexTable + 0x10u, 0);
    writeI32(core, vertexTable + 0x14u, 3); // packet transfer vertex count
    writeI32(core, vertexTable + 0x18u, 0x30);
    writeI32(core, vertexTable + 0x1cu, 0xf0);
    core[vertexTable + 0x20u] = 1u; // SPR matrix 1 -> VU0 qword address 0x10
    core[vertexTable + 0x21u] = 0x10u;
    // The source byte is a runtime SPR matrix index, not bounded by the class's
    // jointCount. Use 7 here with jointCount=4 to lock in the real semantics.
    core[vertexTable + 0x22u] = 7u;
    core[vertexTable + 0x23u] = 0x24u;

    // Main packed vertex program begins at vertexTable + 0x30. The documented
    // executable semantics are: first the two-way records (JI/L1/L2/W1/W2/ST/SB),
    // then the three-way records (L3/L1/L2/W1/W2/W3/SB), then main records
    // (JI/L1/ST). Use values that make the accidentally swapped interpretation
    // observably wrong without affecting the real one.
    constexpr std::size_t packed = vertexTable + 0x30u;
    for (std::size_t i = 0u; i < 3u; ++i) {
        writePackedSkinVertex(core, packed + i * 0x10u,
                              static_cast<std::uint8_t>(1u + i),
                              0x10u, 0x14u, 0x18u, 0x1cu);
    }
    // Three-way L3 has seven packed bits in bits 9..15. The semantic address
    // is reconstructed as raw << 1, so raw 0x7e is the final legal matrix
    // slot 0xfc.
    writePackedSkinVertex(core, packed + 3u * 0x10u, 0x7eu, 0x10u, 0x14u, 0x55u, 0x20u);
    writePackedSkinVertex(core, packed + 4u * 0x10u, 0x0cu, 0x10u, 0x14u, 0x77u, 0x20u);
    for (std::size_t i = 0u; i < 5u; ++i) {
        writePackedSkinVertex(core, packed + (5u + i) * 0x10u,
                              static_cast<std::uint8_t>(5u + i), 0x10u, 0x14u);
    }

    // Distinct authentic-byte stand-ins for the Step 3 bounded probes. These
    // blocks do not overlap the mesh packet/vertex data above.
    fillPattern(core, cls + 0x200u, 0x10u); // skeleton matrix prefix
    fillPattern(core, cls + 0x300u, 0x50u); // common transforms
    fillPattern(core, cls + 0x340u, 0x90u); // compact joints program

    // Step 4 sequence layout: 0x1c-byte header followed by frameCount class-
    // relative u32 frame pointers. Keep the less-understood controls raw while
    // locking in the fields proven by the authentic Step 3 dump.
    constexpr std::size_t sequence = cls + 0x3a0u;
    writeF32(core, sequence + 0x00u, 1.0f);
    writeF32(core, sequence + 0x04u, 2.0f);
    writeF32(core, sequence + 0x08u, 3.0f);
    writeF32(core, sequence + 0x0cu, 4.0f);
    core[sequence + 0x10u] = 2u;    // frame count
    core[sequence + 0x11u] = 1u;    // non-0xff authentic-shaped value
    core[sequence + 0x12u] = 3u;    // preserved raw control
    core[sequence + 0x13u] = 0u;
    writeU32(core, sequence + 0x14u, 0u);
    writeF32(core, sequence + 0x18u, 0.5f);
    writeU32(core, sequence + 0x1cu, 0x3d0u);
    writeU32(core, sequence + 0x20u, 0x3f0u);
    fillPattern(core, cls + 0x3d0u, 0xd0u, 0x20u);
    fillPattern(core, cls + 0x3f0u, 0xf0u, 0x10u);

    // Second real class is rigid/no animations, but has a valid mesh packet.
    constexpr std::size_t rigid = 0x500u;
    writeI32(core, rigid + 0x00u, 0x48);
    core[rigid + 0x04u] = 1u;
    constexpr std::size_t rigidPacket = rigid + 0x48u;
    writeI32(core, rigidPacket + 0x08u, 0x80);
    core[rigidPacket + 0x0cu] = 0x0bu;
    constexpr std::size_t rigidTable = rigid + 0x80u;
    // Rigid classes can still upload SPR matrix 0 even with no skeleton joints.
    // This mirrors the authentic oClass=500 case that exposed the bad jointCount
    // assumption in the metadata gate.
    writeI32(core, rigidTable + 0x00u, 1);
    writeI32(core, rigidTable + 0x04u, 0);
    writeI32(core, rigidTable + 0x08u, 0);
    writeI32(core, rigidTable + 0x0cu, 7);
    writeI32(core, rigidTable + 0x10u, 0);
    writeI32(core, rigidTable + 0x14u, 0);
    writeI32(core, rigidTable + 0x18u, 0x30);
    writeI32(core, rigidTable + 0x1cu, 0xb0);
    core[rigidTable + 0x20u] = 0u; // authentic rigid-class SPR matrix slot
    core[rigidTable + 0x21u] = 0x00u;
    constexpr std::size_t rigidPacked = rigidTable + 0x30u;
    for (std::size_t i = 0u; i < 7u; ++i) {
        writePackedSkinVertex(core, rigidPacked + i * 0x10u,
                              static_cast<std::uint8_t>(i), 0x00u, 0x00u);
    }
    return core;
}

} // namespace

int main() {
    using namespace ratchet::assets;

    const auto result = inspectRac1MobyAnimationMetadata(
        makeCore(), makeIndex(), makeGameplay({42, 42, 100, 77}), {0x00u, 3u});
    if (!result.ok()) {
        std::cerr << "animation metadata fixture failed: "
                  << rac1MobyAnimationStatusName(result.status) << '\n';
        return 1;
    }

    const auto& m = result.metadata;
    if (m.instanceCount != 4u || m.referencedClassCount != 3u ||
        m.renderableClassCount != 2u || m.skeletalClassCount != 1u ||
        m.sequencedClassCount != 1u || m.skeletalInstanceCount != 2u ||
        m.sequencedInstanceCount != 2u || m.sequenceSlotCount != 2u ||
        m.presentSequenceCount != 1u || m.nullSequenceCount != 1u ||
        m.uniqueSequencePayloadCount != 1u || m.aliasedSequenceSlotCount != 0u ||
        m.decodedSequenceCount != 1u || m.totalFrameCount != 2u ||
        m.nonFfHeaderByte11Count != 1u || m.skeletonMatrixCount != 4u ||
        m.commonTransformRecordCount != 4u || m.minFrameStride != 0x20u ||
        m.maxFrameStride != 0x20u || m.frameProbeCount != 1u ||
        m.uniqueFrameStrideCount != 1u || m.oversizedFrameProbeCount != 0u ||
        m.maxProbedFrameBytes != 0x20u || m.packetCount != 2u ||
        m.matrixTransferCount != 3u || m.twoWayBlendVertexCount != 3u ||
        m.threeWayBlendVertexCount != 2u || m.mainVertexCount != 12u ||
        m.skinningVertexCount != 17u || m.documentedLayoutInvalidAddresses != 0u ||
        m.swappedLayoutInvalidAddresses != 5u ||
        m.maxPackedL3Raw != 0x7eu || m.maxDecodedPackedL3Address != 0xfcu ||
        m.packedL3Encoding != Rac1MobyPackedL3Encoding::ShiftLeft1QwordAddress ||
        m.blendLayout != Rac1MobyBlendLayout::TwoWayThenThreeWay) {
        std::cerr << "animation metadata aggregate mismatch\n";
        return 1;
    }

    const Rac1MobyAnimationClass* animated = nullptr;
    const Rac1MobyAnimationClass* logicOnly = nullptr;
    const Rac1MobyAnimationClass* rigid = nullptr;
    for (const auto& cls : m.classes) {
        if (cls.oClass == 42) animated = &cls;
        if (cls.oClass == 100) logicOnly = &cls;
        if (cls.oClass == 77) rigid = &cls;
    }
    if (animated == nullptr || animated->instanceCount != 2u ||
        animated->jointCount != 4u || animated->sequenceCount != 2u ||
        animated->sequenceOffsets.size() != 2u ||
        animated->sequenceOffsets[0] != 0x3a0u || animated->sequenceOffsets[1] != 0u ||
        animated->presentSequenceCount != 1u || animated->nullSequenceCount != 1u ||
        animated->uniqueSequencePayloadCount != 1u || animated->aliasedSequenceSlotCount != 0u ||
        animated->sequenceProbes.size() != 1u || animated->rigProbes.size() != 3u ||
        animated->sequenceLayouts.size() != 1u || animated->totalFrameCount != 2u ||
        animated->nonFfHeaderByte11Count != 1u || animated->oversizedFrameProbeCount != 0u ||
        animated->skeletonMatrices.size() != 4u ||
        animated->commonTransformWords.size() != 4u ||
        animated->classOffset != 0x100u || animated->classEndOffset != 0x500u ||
        animated->matrixTransferCount != 2u || animated->maxScratchpadMatrixIndex != 7u ||
        animated->maxVu0Destination != 0x24u ||
        animated->twoWayBlendVertexCount != 3u || animated->threeWayBlendVertexCount != 2u ||
        animated->mainVertexCount != 5u || animated->skinningVertexCount != 10u ||
        animated->documentedLayoutInvalidAddresses != 0u ||
        animated->swappedLayoutInvalidAddresses != 5u ||
        animated->maxPackedL3Raw != 0x7eu ||
        animated->maxFirstBlendLow7 != 3u || animated->maxSecondBlendLow7 != 0x7eu ||
        animated->maxMainScratchpadMatrixIndex != 9u ||
        animated->maxPackedVu0Address != 0xfcu ||
        animated->skinningPackets.size() != 1u ||
        animated->skinningPackets[0].matrixTransfers.size() != 2u ||
        animated->skinningPackets[0].vertices.size() != 10u ||
        animated->skinningPackets[0].matrixTransfers[0].jointIndex != 1u ||
        animated->skinningPackets[0].matrixTransfers[0].vu0Address != 0x10u ||
        animated->skinningPackets[0].matrixTransfers[1].jointIndex != 7u ||
        animated->skinningPackets[0].matrixTransfers[1].vu0Address != 0x24u ||
        animated->skinningPackets[0].vertices[0].kind != Rac1MobySkinningVertexKind::TwoWay ||
        animated->skinningPackets[0].vertices[0].directJointIndex != 1u ||
        animated->skinningPackets[0].vertices[0].l1 != 0x10u ||
        animated->skinningPackets[0].vertices[0].l2 != 0x14u ||
        animated->skinningPackets[0].vertices[0].directStore != 0x18u ||
        animated->skinningPackets[0].vertices[0].blendStore != 0x1cu ||
        animated->skinningPackets[0].vertices[3].kind != Rac1MobySkinningVertexKind::ThreeWay ||
        animated->skinningPackets[0].vertices[3].l3 != 0xfcu ||
        animated->skinningPackets[0].vertices[5].kind != Rac1MobySkinningVertexKind::Main ||
        animated->skinningPackets[0].vertices[5].directJointIndex != 5u ||
        animated->skinningPackets[0].vertices[5].l1 != 0x10u ||
        animated->skinningPackets[0].vertices[5].directStore != 0x14u ||
        animated->skeletonOffset != 0x200u ||
        animated->commonTransformOffset != 0x300u || animated->jointsOffset != 0x340u) {
        std::cerr << "animated class metadata mismatch\n";
        return 1;
    }
    const auto& sequenceProbe = animated->sequenceProbes[0];
    if (sequenceProbe.sequenceIndex != 0u || sequenceProbe.offset != 0x3a0u ||
        sequenceProbe.nextBoundaryOffset != 0x400u || sequenceProbe.aliasCount != 1u ||
        sequenceProbe.prefixSize != kRac1MobyProbePrefixBytes ||
        sequenceProbe.prefix[0x10u] != 2u || sequenceProbe.prefix[0x11u] != 1u ||
        sequenceProbe.prefixFnv1a !=
            fnv1a(sequenceProbe.prefix.data(), sequenceProbe.prefixSize)) {
        std::cerr << "sequence prefix probe mismatch\n";
        return 1;
    }

    const auto& sequenceLayout = animated->sequenceLayouts[0];
    if (sequenceLayout.sequenceIndex != 0u || sequenceLayout.offset != 0x3a0u ||
        sequenceLayout.frameCount != 2u || sequenceLayout.headerByte11 != 1u ||
        sequenceLayout.controlByte != 3u || sequenceLayout.reservedByte != 0u ||
        sequenceLayout.reservedWord != 0u || sequenceLayout.headerScalar != 0.5f ||
        sequenceLayout.headerVec4 != std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f} ||
        sequenceLayout.frameOffsets != std::vector<std::uint32_t>{0x3d0u, 0x3f0u} ||
        sequenceLayout.firstFrameOffset != 0x3d0u || sequenceLayout.lastFrameOffset != 0x3f0u ||
        sequenceLayout.minFrameStride != 0x20u || sequenceLayout.maxFrameStride != 0x20u ||
        sequenceLayout.uniqueFrameStrides != std::vector<std::uint32_t>{0x20u} ||
        sequenceLayout.frameProbes.size() != 1u) {
        std::cerr << "sequence layout decode mismatch\n";
        return 1;
    }

    const auto& frameProbe = sequenceLayout.frameProbes[0];
    if (frameProbe.sequenceIndex != 0u || frameProbe.frameIndex != 0u ||
        frameProbe.offset != 0x3d0u || frameProbe.stride != 0x20u ||
        frameProbe.payload.size() != 0x20u || frameProbe.payload.front() != 0xd0u ||
        frameProbe.payload.back() != 0xefu ||
        frameProbe.payloadFnv1a != fnv1a(frameProbe.payload.data(), frameProbe.payload.size())) {
        std::cerr << "frame payload probe mismatch\n";
        return 1;
    }

    const std::array<Rac1MobyRigProbeKind, 3> expectedRigKinds = {
        Rac1MobyRigProbeKind::Skeleton,
        Rac1MobyRigProbeKind::CommonTransforms,
        Rac1MobyRigProbeKind::Joints,
    };
    const std::array<std::uint32_t, 3> expectedRigOffsets = {0x200u, 0x300u, 0x340u};
    const std::array<std::uint32_t, 3> expectedRigBoundaries = {0x300u, 0x340u, 0x3a0u};
    const std::array<std::uint8_t, 3> expectedRigSeeds = {0x10u, 0x50u, 0x90u};
    for (std::size_t i = 0u; i < animated->rigProbes.size(); ++i) {
        const auto& probe = animated->rigProbes[i];
        if (probe.kind != expectedRigKinds[i] || probe.offset != expectedRigOffsets[i] ||
            probe.nextBoundaryOffset != expectedRigBoundaries[i] ||
            probe.prefixSize != kRac1MobyProbePrefixBytes ||
            probe.prefix[0] != expectedRigSeeds[i] ||
            probe.prefixFnv1a != fnv1a(probe.prefix.data(), probe.prefixSize)) {
            std::cerr << "rig prefix probe mismatch\n";
            return 1;
        }
    }

    if (logicOnly == nullptr || logicOnly->hasMesh() || logicOnly->hasSkeleton() ||
        logicOnly->hasSequences()) {
        std::cerr << "logic-only class should contain no animation payload\n";
        return 1;
    }
    if (rigid == nullptr || !rigid->hasMesh() || rigid->hasSkeleton() ||
        rigid->matrixTransferCount != 1u || rigid->maxScratchpadMatrixIndex != 0u ||
        rigid->maxVu0Destination != 0x00u || rigid->skinningVertexCount != 7u ||
        rigid->documentedLayoutInvalidAddresses != 0u ||
        rigid->swappedLayoutInvalidAddresses != 0u ||
        rigid->maxMainScratchpadMatrixIndex != 6u || rigid->maxPackedVu0Address != 0u) {
        std::cerr << "rigid SPR matrix transfer metadata mismatch\n";
        return 1;
    }

    // Sequence slots may alias one payload. Preserve both sequence IDs while
    // reporting one unique payload and one extra aliased slot.
    auto aliasedSequenceCore = makeCore();
    aliasedSequenceCore[0x100u + 0x0cu] = 3u;
    writeI32(aliasedSequenceCore, 0x100u + 0x48u, 0x3a0);
    writeI32(aliasedSequenceCore, 0x100u + 0x4cu, 0x3a0);
    writeI32(aliasedSequenceCore, 0x100u + 0x50u, 0x000);
    const auto aliasedSequence = inspectRac1MobyAnimationMetadata(
        aliasedSequenceCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (!aliasedSequence.ok() || aliasedSequence.metadata.presentSequenceCount != 2u ||
        aliasedSequence.metadata.nullSequenceCount != 1u ||
        aliasedSequence.metadata.uniqueSequencePayloadCount != 1u ||
        aliasedSequence.metadata.aliasedSequenceSlotCount != 1u ||
        aliasedSequence.metadata.classes.size() != 1u ||
        aliasedSequence.metadata.classes[0].sequenceProbes.size() != 2u ||
        aliasedSequence.metadata.classes[0].sequenceProbes[0].aliasCount != 2u ||
        aliasedSequence.metadata.classes[0].sequenceProbes[1].aliasCount != 2u ||
        aliasedSequence.metadata.classes[0].sequenceProbes[0].prefixFnv1a !=
            aliasedSequence.metadata.classes[0].sequenceProbes[1].prefixFnv1a) {
        std::cerr << "aliased sequence payload accounting mismatch\n";
        return 1;
    }

    // Step 4 sequence failures carry exact sequence/frame context.
    auto zeroFramesCore = makeCore();
    zeroFramesCore[0x100u + 0x3a0u + 0x10u] = 0u;
    const auto zeroFrames = inspectRac1MobyAnimationMetadata(
        zeroFramesCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (zeroFrames.status != Rac1MobyAnimationStatus::InvalidSequenceLayout ||
        zeroFrames.failureSequenceReason != Rac1MobySequenceFailure::ZeroFrameCount ||
        zeroFrames.failureOClass != 42 || zeroFrames.failureSequenceIndex != 0) {
        std::cerr << "zero-frame sequence was not rejected precisely\n";
        return 1;
    }

    auto badLoopCore = makeCore();
    badLoopCore[0x100u + 0x3a0u + 0x11u] = 2u;
    const auto badLoop = inspectRac1MobyAnimationMetadata(
        badLoopCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badLoop.status != Rac1MobyAnimationStatus::InvalidSequenceLayout ||
        badLoop.failureSequenceReason != Rac1MobySequenceFailure::HeaderByte11OutOfRange) {
        std::cerr << "invalid sequence +0x11 value was not rejected\n";
        return 1;
    }

    auto badFramePointerCore = makeCore();
    writeU32(badFramePointerCore, 0x100u + 0x3a0u + 0x1cu, 0x3c1u);
    const auto badFramePointer = inspectRac1MobyAnimationMetadata(
        badFramePointerCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badFramePointer.status != Rac1MobyAnimationStatus::InvalidSequenceLayout ||
        badFramePointer.failureSequenceReason != Rac1MobySequenceFailure::FramePointerBeforeTableEnd ||
        badFramePointer.failureFrameIndex != 0 || badFramePointer.failureFrameOffset != 0x3c1u) {
        std::cerr << "frame pointer before table end was not rejected precisely\n";
        return 1;
    }

    auto nonIncreasingFramesCore = makeCore();
    writeU32(nonIncreasingFramesCore, 0x100u + 0x3a0u + 0x20u, 0x3d0u);
    const auto nonIncreasingFrames = inspectRac1MobyAnimationMetadata(
        nonIncreasingFramesCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (nonIncreasingFrames.status != Rac1MobyAnimationStatus::InvalidSequenceLayout ||
        nonIncreasingFrames.failureSequenceReason != Rac1MobySequenceFailure::FramePointersNotIncreasing ||
        nonIncreasingFrames.failureFrameIndex != 1) {
        std::cerr << "non-increasing frame table was not rejected\n";
        return 1;
    }

    auto badRigCore = makeCore();
    writeI32(badRigCore, 0x100u + 0x18u, 0x2f0);
    const auto badRig = inspectRac1MobyAnimationMetadata(
        badRigCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badRig.status != Rac1MobyAnimationStatus::InvalidRigLayout ||
        badRig.failureRigReason != Rac1MobyRigFailure::SkeletonRangeMismatch ||
        badRig.failureRigExpectedOffset != 0x300u || badRig.failureRigActualOffset != 0x2f0u) {
        std::cerr << "skeleton/common extent mismatch was not rejected precisely\n";
        return 1;
    }

    // L3 is not selected statistically. The first packed u16 contains a 9-bit
    // ID plus a 7-bit L3 field; physically, those L3 bits occupy bits 1..7 of
    // byte 1, so the address's low zero bit is implicit and reconstruction is
    // exactly raw << 1. Even when raw << 2 would also happen to point at an
    // aligned in-range matrix, the format semantics remain raw << 1.
    auto structurallyAmbiguousL3Core = makeCore();
    constexpr std::size_t animatedPacked = 0x100u + 0x100u + 0x30u;
    structurallyAmbiguousL3Core[animatedPacked + 3u * 0x10u + 1u] =
        static_cast<std::uint8_t>(0x0cu << 1u);
    structurallyAmbiguousL3Core[animatedPacked + 4u * 0x10u + 1u] =
        static_cast<std::uint8_t>(0x10u << 1u);
    const auto structurallyAmbiguousL3 = inspectRac1MobyAnimationMetadata(
        structurallyAmbiguousL3Core, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (!structurallyAmbiguousL3.ok() ||
        structurallyAmbiguousL3.metadata.packedL3Encoding !=
            Rac1MobyPackedL3Encoding::ShiftLeft1QwordAddress ||
        structurallyAmbiguousL3.metadata.maxPackedL3Raw != 0x10u ||
        structurallyAmbiguousL3.metadata.maxDecodedPackedL3Address != 0x20u) {
        std::cerr << "exact packed L3 bit reconstruction was not preserved\n";
        return 1;
    }

    // An odd seven-bit L3 value reconstructs to an address that is 2 mod 4 and
    // therefore cannot be a 4-qword VU0 matrix start. Reject it with exact
    // three-way vertex context.
    auto misalignedL3Core = makeCore();
    misalignedL3Core[animatedPacked + 3u * 0x10u + 1u] =
        static_cast<std::uint8_t>(0x0du << 1u);
    const auto misalignedL3 = inspectRac1MobyAnimationMetadata(
        misalignedL3Core, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (misalignedL3.status != Rac1MobyAnimationStatus::InvalidSkinningProgram ||
        misalignedL3.failureSkinningReason != Rac1MobySkinningFailure::MatrixAddressMisaligned ||
        !misalignedL3.failureOClassValid || misalignedL3.failureOClass != 42 ||
        misalignedL3.failureSkinningPacketIndex != 0 ||
        misalignedL3.failureSkinningVertexIndex != 3 ||
        misalignedL3.failureSkinningVertexKind != Rac1MobySkinningVertexKind::ThreeWay ||
        misalignedL3.failureSkinningAddress != 0x1a) {
        std::cerr << "misaligned decoded packed L3 was not rejected precisely\n";
        return 1;
    }

    // 0x7f reconstructs to 0xfe, which cannot fit a four-qword matrix in the
    // 0x00..0xff VU0 data memory. Overflow takes precedence over alignment.
    auto overflowingL3Core = makeCore();
    overflowingL3Core[animatedPacked + 3u * 0x10u + 1u] =
        static_cast<std::uint8_t>(0x7fu << 1u);
    const auto overflowingL3 = inspectRac1MobyAnimationMetadata(
        overflowingL3Core, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (overflowingL3.status != Rac1MobyAnimationStatus::InvalidSkinningProgram ||
        overflowingL3.failureSkinningReason != Rac1MobySkinningFailure::MatrixAddressOverflow ||
        overflowingL3.failureSkinningVertexIndex != 3 ||
        overflowingL3.failureSkinningVertexKind != Rac1MobySkinningVertexKind::ThreeWay ||
        overflowingL3.failureSkinningAddress != 0xfe) {
        std::cerr << "overflowing decoded packed L3 was not rejected precisely\n";
        return 1;
    }

    // Matrix-transfer sources are SPR staging indices, not skeleton-joint indices.
    // The animated fixture deliberately uses SPR index 7 with jointCount=4, and
    // the rigid fixture uses SPR index 0 with jointCount=0. Both must remain legal.

    // A 4-qword matrix must fit in the 256-qword VU0 data memory. 0xfd would
    // run through 0x100 and is rejected before alignment is considered.
    auto badDestinationCore = makeCore();
    badDestinationCore[0x100u + 0x100u + 0x21u] = 0xfdu;
    const auto badDestination = inspectRac1MobyAnimationMetadata(
        badDestinationCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badDestination.status != Rac1MobyAnimationStatus::InvalidMatrixTransfer ||
        badDestination.failureMatrixReason != Rac1MobyMatrixTransferFailure::DestinationOverflow ||
        badDestination.failurePacketIndex != 0 || badDestination.failureMatrixTransferIndex != 0 ||
        badDestination.failureMatrixScratchpadIndex != 1 ||
        badDestination.failureMatrixVu0Destination != 0xfd) {
        std::cerr << "overflowing VU0 matrix destination was not rejected precisely\n";
        return 1;
    }

    // Wrench's recovered EE/VU0 path addresses matrices as VU0mem[address / 4].
    // Preserve the four-qword matrix-slot alignment until authentic data proves
    // otherwise; Fix 2 had loosened this before the detailed failure context
    // showed that alignment was not the real failing assumption.
    auto misalignedDestinationCore = makeCore();
    misalignedDestinationCore[0x100u + 0x100u + 0x21u] = 0x25u;
    const auto misalignedDestination = inspectRac1MobyAnimationMetadata(
        misalignedDestinationCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (misalignedDestination.status != Rac1MobyAnimationStatus::InvalidMatrixTransfer ||
        misalignedDestination.failureMatrixReason !=
            Rac1MobyMatrixTransferFailure::DestinationMisaligned ||
        misalignedDestination.failurePacketIndex != 0 ||
        misalignedDestination.failureMatrixTransferIndex != 0 ||
        misalignedDestination.failureMatrixScratchpadIndex != 1 ||
        misalignedDestination.failureMatrixVu0Destination != 0x25) {
        std::cerr << "misaligned VU0 matrix destination was not rejected precisely\n";
        return 1;
    }

    // The packed skinning program itself is now a hard authenticity gate. A
    // main/no-blend L1 address that is not a four-qword VU0 matrix slot must
    // fail with the exact packet/vertex/type/address context.
    auto badSkinningCore = makeCore();
    constexpr std::size_t animatedMainVertex = 0x100u + 0x100u + 0x30u + 5u * 0x10u;
    badSkinningCore[animatedMainVertex + 0x02u] = 0x11u;
    const auto badSkinning = inspectRac1MobyAnimationMetadata(
        badSkinningCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badSkinning.status != Rac1MobyAnimationStatus::InvalidSkinningProgram ||
        !badSkinning.failureOClassValid || badSkinning.failureOClass != 42 ||
        badSkinning.failureSkinningPacketIndex != 0 ||
        badSkinning.failureSkinningVertexIndex != 5 ||
        badSkinning.failureSkinningVertexKind != Rac1MobySkinningVertexKind::Main ||
        badSkinning.failureSkinningAddress != 0x11 ||
        badSkinning.failureSkinningReason != Rac1MobySkinningFailure::MatrixAddressMisaligned) {
        std::cerr << "misaligned packed skinning matrix address was not rejected precisely\n";
        return 1;
    }

    // Likewise, a vertex table whose declared skinning payload end does not
    // contain all packed 16-byte vertices must never bleed into neighboring
    // class data.
    auto shortSkinningCore = makeCore();
    writeI32(shortSkinningCore, 0x100u + 0x100u + 0x1cu, 0x90);
    const auto shortSkinning = inspectRac1MobyAnimationMetadata(
        shortSkinningCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (shortSkinning.status != Rac1MobyAnimationStatus::InvalidSkinningProgram ||
        shortSkinning.failureSkinningReason != Rac1MobySkinningFailure::VertexDataOutOfRange ||
        shortSkinning.failureSkinningPacketIndex != 0) {
        std::cerr << "truncated packed skinning program was not rejected\n";
        return 1;
    }

    // Sequence pointers are class-relative and must resolve inside the class/core
    // asset. Do not let a bogus pointer become a future native animation read.
    auto badSequenceCore = makeCore();
    writeI32(badSequenceCore, 0x100u + 0x4cu, 0x7fffffff);
    const auto badSequence = inspectRac1MobyAnimationMetadata(
        badSequenceCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (badSequence.status != Rac1MobyAnimationStatus::InvalidSequenceTable ||
        badSequence.failureOClass != 42 || badSequence.failureSequenceIndex != 1 ||
        badSequence.failureSequenceRelative != 0x7fffffff ||
        badSequence.failureSequenceTableOffset != 0x4cu) {
        std::cerr << "invalid sequence pointer was not rejected with exact context\n";
        return 1;
    }

    // Negative pointers are malformed; zero alone is the valid sparse sentinel.
    auto negativeSequenceCore = makeCore();
    writeI32(negativeSequenceCore, 0x100u + 0x48u, -4);
    const auto negativeSequence = inspectRac1MobyAnimationMetadata(
        negativeSequenceCore, makeIndex(), makeGameplay({42}), {0x00u, 3u});
    if (negativeSequence.status != Rac1MobyAnimationStatus::InvalidSequenceTable ||
        negativeSequence.failureSequenceIndex != 0 ||
        negativeSequence.failureSequenceRelative != -4) {
        std::cerr << "negative sequence pointer was not rejected\n";
        return 1;
    }


    // Step 6: reproduce the retail dense pose builder before touching vertices.
    // The quaternion basis below intentionally catches a transpose/sign error:
    // retail 0x211308..0x21136c treats the generated vectors as matrix columns.
    std::vector<std::uint8_t> poseCore(0x300u, 0u);
    Rac1MobyAnimationClass poseClass{};
    poseClass.oClass = 4242;
    poseClass.classOffset = 0x100u;
    poseClass.classEndOffset = 0x300u;
    poseClass.jointCount = 3u;
    Rac1MobySequenceLayout poseLayout{};
    poseLayout.sequenceIndex = 7u;
    poseLayout.frameCount = 2u;
    poseLayout.frameOffsets = {0x80u, 0x140u};
    poseClass.sequenceLayouts.push_back(poseLayout);
    poseClass.commonTransformWords = {
        std::array<std::uint32_t, 4>{f32bits(1.0f), f32bits(2.0f), f32bits(3.0f), 0u},
        std::array<std::uint32_t, 4>{f32bits(1.0f), f32bits(0.0f), f32bits(0.0f), 0x70000000u},
        std::array<std::uint32_t, 4>{f32bits(0.0f), f32bits(2.0f), f32bits(0.0f), 0x70000040u},
    };
    constexpr std::size_t poseFrame = 0x180u;
    writeU16(poseCore, poseFrame + 0x06u, 2u);     // 0x20 payload bytes.
    writeU16(poseCore, poseFrame + 0x08u, 0x18u); // dense quaternion bytes end.
    writeU16(poseCore, poseFrame + 0x0au, 0u);
    writeU16(poseCore, poseFrame + 0x0cu, 0x18u);
    writeU16(poseCore, poseFrame + 0x0eu, 0u);
    // Root identity.
    writeI16(poseCore, poseFrame + 0x10u + 0u, 0);
    writeI16(poseCore, poseFrame + 0x10u + 2u, 0);
    writeI16(poseCore, poseFrame + 0x10u + 4u, 0);
    writeI16(poseCore, poseFrame + 0x10u + 6u, 32767);
    // Child: +90 degree Z quaternion. The retail matrix convention maps local
    // +Y to +X, which distinguishes it from the transposed implementation.
    writeI16(poseCore, poseFrame + 0x18u + 0u, 0);
    writeI16(poseCore, poseFrame + 0x18u + 2u, 0);
    writeI16(poseCore, poseFrame + 0x18u + 4u, 23170);
    writeI16(poseCore, poseFrame + 0x18u + 6u, 23170);
    // Grandchild identity.
    writeI16(poseCore, poseFrame + 0x20u + 0u, 0);
    writeI16(poseCore, poseFrame + 0x20u + 2u, 0);
    writeI16(poseCore, poseFrame + 0x20u + 4u, 0);
    writeI16(poseCore, poseFrame + 0x20u + 6u, 32767);

    // Second dense frame for Step 10 interpolation. Use the opposite-sign
    // identity quaternion on the root/grandchild to prove hemisphere handling,
    // and rotate the child from +90 to +180 degrees around Z.
    constexpr std::size_t poseFrame1 = 0x240u;
    writeU16(poseCore, poseFrame1 + 0x06u, 2u);
    writeU16(poseCore, poseFrame1 + 0x08u, 0x18u);
    writeU16(poseCore, poseFrame1 + 0x0au, 0u);
    writeU16(poseCore, poseFrame1 + 0x0cu, 0x18u);
    writeU16(poseCore, poseFrame1 + 0x0eu, 0u);
    writeI16(poseCore, poseFrame1 + 0x10u + 0u, 0);
    writeI16(poseCore, poseFrame1 + 0x10u + 2u, 0);
    writeI16(poseCore, poseFrame1 + 0x10u + 4u, 0);
    writeI16(poseCore, poseFrame1 + 0x10u + 6u, -32767);
    writeI16(poseCore, poseFrame1 + 0x18u + 0u, 0);
    writeI16(poseCore, poseFrame1 + 0x18u + 2u, 0);
    writeI16(poseCore, poseFrame1 + 0x18u + 4u, 32767);
    writeI16(poseCore, poseFrame1 + 0x18u + 6u, 0);
    writeI16(poseCore, poseFrame1 + 0x20u + 0u, 0);
    writeI16(poseCore, poseFrame1 + 0x20u + 2u, 0);
    writeI16(poseCore, poseFrame1 + 0x20u + 4u, 0);
    writeI16(poseCore, poseFrame1 + 0x20u + 6u, -32767);

    const auto densePose = decodeRac1MobyDensePoseFrame(poseCore, poseClass, 7u, 0u);
    if (!densePose.ok() || densePose.pose.jointMatrices.size() != 3u ||
        densePose.pose.rootJointCount != 1u || densePose.pose.parentLinkCount != 2u ||
        densePose.pose.payloadQwordCount != 2u || densePose.pose.stream1Offset != 0x18u ||
        densePose.pose.stream1Count != 0u || densePose.pose.stream2Offset != 0x18u ||
        densePose.pose.stream2Count != 0u) {
        std::cerr << "dense retail-pose fixture failed: "
                  << rac1MobyPoseStatusName(densePose.status) << '\n';
        return 1;
    }
    const auto& rootPose = densePose.pose.jointMatrices[0];
    const auto& childPose = densePose.pose.jointMatrices[1];
    const auto& grandchildPose = densePose.pose.jointMatrices[2];
    if (!near(rootPose[12], 1.0f) || !near(rootPose[13], 2.0f) || !near(rootPose[14], 3.0f) ||
        !near(childPose[0], 0.0f) || !near(childPose[1], -1.0f) ||
        !near(childPose[4], 1.0f) || !near(childPose[5], 0.0f) ||
        !near(childPose[12], 2.0f) || !near(childPose[13], 2.0f) || !near(childPose[14], 3.0f) ||
        !near(grandchildPose[12], 4.0f) || !near(grandchildPose[13], 2.0f) ||
        !near(grandchildPose[14], 3.0f)) {
        std::cerr << "retail quaternion/parent pose convention mismatch\n";
        return 1;
    }

    // Step 10: FUN_002109b8 interpolates quaternion components between the
    // two frame pointers, chooses the equivalent quaternion hemisphere, then
    // normalizes before hierarchy evaluation. At alpha=0.5 the child is the
    // midpoint between +90 and +180 degrees around Z; opposite-sign identity
    // endpoints on the root must not collapse to a zero quaternion.
    const auto interpolatedPose = decodeRac1MobyDensePoseInterpolated(
        poseCore, poseClass, 7u, 0u, 1u, 0.5f);
    if (!interpolatedPose.ok() || !interpolatedPose.pose.interpolated ||
        interpolatedPose.pose.frameIndex != 0u ||
        interpolatedPose.pose.nextFrameIndex != 1u ||
        !near(interpolatedPose.pose.interpolationAlpha, 0.5f) ||
        interpolatedPose.pose.jointMatrices.size() != 3u) {
        std::cerr << "retail dense interpolation fixture failed: "
                  << rac1MobyPoseStatusName(interpolatedPose.status) << '\n';
        return 1;
    }
    constexpr float kInvSqrt2 = 0.70710678f;
    const auto& interpolatedRoot = interpolatedPose.pose.jointMatrices[0];
    const auto& interpolatedChild = interpolatedPose.pose.jointMatrices[1];
    if (!near(interpolatedRoot[0], 1.0f) || !near(interpolatedRoot[5], 1.0f) ||
        !near(interpolatedRoot[10], 1.0f) ||
        !near(interpolatedChild[0], -kInvSqrt2, 4.0e-4f) ||
        !near(interpolatedChild[1], -kInvSqrt2, 4.0e-4f) ||
        !near(interpolatedChild[4], kInvSqrt2, 4.0e-4f) ||
        !near(interpolatedChild[5], -kInvSqrt2, 4.0e-4f)) {
        std::cerr << "retail shortest-hemisphere nlerp convention mismatch\n";
        return 1;
    }

    const auto interpolatedStart = decodeRac1MobyDensePoseInterpolated(
        poseCore, poseClass, 7u, 0u, 1u, 0.0f);
    const auto densePose1 = decodeRac1MobyDensePoseFrame(poseCore, poseClass, 7u, 1u);
    const auto interpolatedEnd = decodeRac1MobyDensePoseInterpolated(
        poseCore, poseClass, 7u, 0u, 1u, 1.0f);
    if (!interpolatedStart.ok() || !densePose1.ok() || !interpolatedEnd.ok()) {
        std::cerr << "retail interpolation endpoint decode failed\n";
        return 1;
    }
    for (std::size_t joint = 0u; joint < 3u; ++joint) {
        for (std::size_t word = 0u; word < 16u; ++word) {
            if (!near(interpolatedStart.pose.jointMatrices[joint][word],
                      densePose.pose.jointMatrices[joint][word], 4.0e-4f) ||
                !near(interpolatedEnd.pose.jointMatrices[joint][word],
                      densePose1.pose.jointMatrices[joint][word], 4.0e-4f)) {
                std::cerr << "retail interpolation endpoints do not match dense poses\n";
                return 1;
            }
        }
    }

    const auto invalidAlpha = decodeRac1MobyDensePoseInterpolated(
        poseCore, poseClass, 7u, 0u, 1u, 1.01f);
    if (invalidAlpha.status != Rac1MobyPoseStatus::InvalidInterpolationAlpha) {
        std::cerr << "out-of-range interpolation alpha was not rejected\n";
        return 1;
    }

    // Step 7: retail selected-joint export copies FUN_002109b8 pose matrices
    // verbatim, and the skinning program addresses direct SPR sources by the
    // same joint-index domain. Prove every direct source category is bounded by
    // jointCount without inspecting or inventing semantics for skeletonMatrices.
    Rac1MobyAnimationClass paletteClass{};
    paletteClass.oClass = 7777;
    paletteClass.jointCount = 3u;
    paletteClass.highLodPacketCount = 1u;
    paletteClass.matrixTransferCount = 2u;
    paletteClass.maxScratchpadMatrixIndex = 2u;
    paletteClass.twoWayBlendVertexCount = 4u;
    paletteClass.maxFirstBlendLow7 = 1u;
    paletteClass.mainVertexCount = 5u;
    paletteClass.maxMainScratchpadMatrixIndex = 2u;
    paletteClass.skeletonMatrices = {
        std::array<float, 16>{1.0e20f, -2.0e20f, 3.0e20f, -4.0e20f,
                             5.0e20f, -6.0e20f, 7.0e20f, -8.0e20f,
                             9.0e20f, -1.0e21f, 1.1e21f, -1.2e21f,
                             1.3e21f, -1.4e21f, 1.5e21f, -1.6e21f},
    };
    const auto palette = inspectRac1MobyPosePaletteContract(paletteClass);
    if (!palette.ok() || palette.contract.jointCount != 3u ||
        palette.contract.matrixTransferReferences != 2u ||
        palette.contract.twoWayDirectReferences != 4u ||
        palette.contract.mainDirectReferences != 5u ||
        palette.contract.directSourceReferences != 11u ||
        palette.contract.maxReferencedJointIndex != 2u) {
        std::cerr << "pose palette contract fixture failed: "
                  << rac1MobyPosePaletteStatusName(palette.status) << '\n';
        return 1;
    }

    auto badTransferPalette = paletteClass;
    badTransferPalette.maxScratchpadMatrixIndex = 3u;
    const auto badTransfer = inspectRac1MobyPosePaletteContract(badTransferPalette);
    if (badTransfer.status != Rac1MobyPosePaletteStatus::MatrixTransferJointOutOfRange ||
        badTransfer.failureSourceKind != Rac1MobyPosePaletteSourceKind::MatrixTransfer ||
        badTransfer.failureJointIndex != 3) {
        std::cerr << "matrix-transfer pose joint overflow was not rejected precisely\n";
        return 1;
    }

    auto badTwoWayPalette = paletteClass;
    badTwoWayPalette.maxFirstBlendLow7 = 3u;
    const auto badTwoWay = inspectRac1MobyPosePaletteContract(badTwoWayPalette);
    if (badTwoWay.status != Rac1MobyPosePaletteStatus::TwoWayJointOutOfRange ||
        badTwoWay.failureSourceKind != Rac1MobyPosePaletteSourceKind::TwoWay ||
        badTwoWay.failureJointIndex != 3) {
        std::cerr << "two-way pose joint overflow was not rejected precisely\n";
        return 1;
    }

    auto badMainPalette = paletteClass;
    badMainPalette.maxMainScratchpadMatrixIndex = 3u;
    const auto badMain = inspectRac1MobyPosePaletteContract(badMainPalette);
    if (badMain.status != Rac1MobyPosePaletteStatus::MainJointOutOfRange ||
        badMain.failureSourceKind != Rac1MobyPosePaletteSourceKind::Main ||
        badMain.failureJointIndex != 3) {
        std::cerr << "main pose joint overflow was not rejected precisely\n";
        return 1;
    }

    auto noSkeletonPalette = paletteClass;
    noSkeletonPalette.jointCount = 0u;
    const auto noSkeleton = inspectRac1MobyPosePaletteContract(noSkeletonPalette);
    if (noSkeleton.status != Rac1MobyPosePaletteStatus::NoSkeleton) {
        std::cerr << "zero-joint palette class was not classified explicitly\n";
        return 1;
    }

    // Step 8: execute the recovered VU0 matrix program literally. Packet 0
    // builds two blended matrices, then packet 1 consumes one of them. This
    // proves that VU0 matrix state persists between packets and that main
    // vertices may select an already-blended matrix rather than their ST write.
    Rac1MobyAnimationClass skinClass{};
    skinClass.oClass = 8888;
    skinClass.jointCount = 3u;
    skinClass.highLodPacketCount = 2u;
    skinClass.skinningVertexCount = 4u;
    skinClass.skeletonMatrices = {
        retailSkeletonRecord(translationMatrix(0.0f, 0.0f, 0.0f)),
        retailSkeletonRecord(translationMatrix(0.0f, 0.0f, 0.0f)),
        retailSkeletonRecord(translationMatrix(0.0f, 0.0f, 0.0f)),
    };
    skinClass.skinningPackets.resize(2u);
    auto& skinPacket0 = skinClass.skinningPackets[0];
    skinPacket0.matrixTransfers = {{0u, 0x10u}, {1u, 0x14u}};

    Rac1MobySkinningVertexProgram skinTwo{};
    skinTwo.kind = Rac1MobySkinningVertexKind::TwoWay;
    skinTwo.id = 10u;
    skinTwo.directJointIndex = 2u;
    skinTwo.l1 = 0x10u;
    skinTwo.l2 = 0x18u;
    skinTwo.w1 = 128u;
    skinTwo.w2 = 127u;
    skinTwo.directStore = 0x18u;
    skinTwo.blendStore = 0x1cu;
    skinTwo.position = {1, 2, 3};
    skinPacket0.vertices.push_back(skinTwo);

    Rac1MobySkinningVertexProgram skinThree{};
    skinThree.kind = Rac1MobySkinningVertexKind::ThreeWay;
    skinThree.id = 11u;
    skinThree.l1 = 0x10u;
    skinThree.l2 = 0x14u;
    skinThree.l3 = 0x1cu;
    skinThree.w1 = 85u;
    skinThree.w2 = 85u;
    skinThree.w3 = 85u;
    skinThree.blendStore = 0x20u;
    skinThree.position = {0, 0, 0};
    skinPacket0.vertices.push_back(skinThree);

    Rac1MobySkinningVertexProgram skinMain0{};
    skinMain0.kind = Rac1MobySkinningVertexKind::Main;
    skinMain0.id = 12u;
    skinMain0.directJointIndex = 0u;
    skinMain0.l1 = 0x20u;
    skinMain0.directStore = 0x24u;
    skinMain0.position = {1, 0, 0};
    skinPacket0.vertices.push_back(skinMain0);

    Rac1MobySkinningVertexProgram skinMain1{};
    skinMain1.kind = Rac1MobySkinningVertexKind::Main;
    skinMain1.id = 13u;
    skinMain1.directJointIndex = 1u;
    skinMain1.l1 = 0x20u; // packet-0 result: deliberate cross-packet dependency.
    skinMain1.directStore = 0x28u;
    skinMain1.position = {0, 1, 0};
    skinClass.skinningPackets[1].vertices.push_back(skinMain1);

    Rac1MobyPoseFrame skinPose{};
    skinPose.oClass = skinClass.oClass;
    skinPose.jointCount = skinClass.jointCount;
    skinPose.jointMatrices = {
        translationMatrix(10.0f, 0.0f, 0.0f),
        translationMatrix(0.0f, 30.0f, 0.0f),
        translationMatrix(0.0f, 0.0f, 20.0f),
    };

    const auto skinExecution = executeRac1MobySkinningProgram(skinClass, skinPose);
    if (!skinExecution.ok() || skinExecution.execution.packetCount != 2u ||
        skinExecution.execution.matrixTransferCount != 2u ||
        skinExecution.execution.twoWayVertexCount != 1u ||
        skinExecution.execution.threeWayVertexCount != 1u ||
        skinExecution.execution.mainVertexCount != 2u ||
        skinExecution.execution.skeletonPostComposeCount != 3u ||
        skinExecution.execution.vu0MatrixWrites != 7u ||
        skinExecution.execution.vu0CrossPacketReads != 1u ||
        !near(skinExecution.execution.maxWeightSumError, 0.0f) ||
        skinExecution.execution.vertices.size() != 4u) {
        std::cerr << "native skinning execution fixture failed: "
                  << rac1MobySkinExecutionStatusName(skinExecution.status) << '\n';
        return 1;
    }

    const float twoTx = 10.0f * (128.0f / 255.0f);
    const float twoTz = 20.0f * (127.0f / 255.0f);
    const auto& skinnedTwo = skinExecution.execution.vertices[0].position;
    if (!near(skinnedTwo[0], 1.0f + twoTx) || !near(skinnedTwo[1], 2.0f) ||
        !near(skinnedTwo[2], 3.0f + twoTz)) {
        std::cerr << "two-way matrix execution mismatch\n";
        return 1;
    }

    const float threeTx = (10.0f + twoTx) / 3.0f;
    const float threeTy = 10.0f;
    const float threeTz = twoTz / 3.0f;
    const auto& skinnedThree = skinExecution.execution.vertices[1].position;
    const auto& skinnedMain0 = skinExecution.execution.vertices[2].position;
    const auto& skinnedMain1 = skinExecution.execution.vertices[3].position;
    if (!near(skinnedThree[0], threeTx) || !near(skinnedThree[1], threeTy) ||
        !near(skinnedThree[2], threeTz) ||
        !near(skinnedMain0[0], 1.0f + threeTx) ||
        !near(skinnedMain0[1], threeTy) || !near(skinnedMain0[2], threeTz) ||
        !near(skinnedMain1[0], threeTx) ||
        !near(skinnedMain1[1], 1.0f + threeTy) || !near(skinnedMain1[2], threeTz)) {
        std::cerr << "three-way/main persistent VU0 execution mismatch\n";
        return 1;
    }

    // Step 9 correction: the actual render animation path does one more matrix
    // operation after FUN_002109b8. FUN_00211808 stores class +0x14 at work
    // +0x1c and sub_0020E0E0 0x20ed28..0x20ed94 computes pose * that record.
    // Use a rotated pose and translated correction so reversed multiplication
    // would land at (12,0,0) instead of the required (10,2,0).
    Rac1MobyAnimationClass postComposeClass{};
    postComposeClass.oClass = 9991;
    postComposeClass.jointCount = 1u;
    postComposeClass.highLodPacketCount = 1u;
    postComposeClass.skinningVertexCount = 1u;
    postComposeClass.skeletonMatrices = {
        retailSkeletonRecord(translationMatrix(2.0f, 0.0f, 0.0f)),
    };
    postComposeClass.skinningPackets.resize(1u);
    Rac1MobySkinningVertexProgram postComposeVertex{};
    postComposeVertex.kind = Rac1MobySkinningVertexKind::Main;
    postComposeVertex.id = 77u;
    postComposeVertex.directJointIndex = 0u;
    postComposeVertex.l1 = 0x10u;
    postComposeVertex.directStore = 0x10u;
    postComposeVertex.position = {0, 0, 0};
    postComposeClass.skinningPackets[0].vertices.push_back(postComposeVertex);

    Rac1MobyPoseFrame postComposePose{};
    postComposePose.oClass = postComposeClass.oClass;
    postComposePose.jointCount = 1u;
    postComposePose.jointMatrices = {{
         0.0f, 1.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 0.0f, 0.0f,
         0.0f, 0.0f, 1.0f, 0.0f,
        10.0f, 0.0f, 0.0f, 1.0f,
    }};
    const auto postComposeExecution =
        executeRac1MobySkinningProgram(postComposeClass, postComposePose);
    if (!postComposeExecution.ok() ||
        postComposeExecution.execution.skeletonPostComposeCount != 1u ||
        postComposeExecution.execution.vertices.size() != 1u ||
        !near(postComposeExecution.execution.vertices[0].position[0], 10.0f) ||
        !near(postComposeExecution.execution.vertices[0].position[1], 2.0f) ||
        !near(postComposeExecution.execution.vertices[0].position[2], 0.0f)) {
        std::cerr << "retail pose-times-skeleton post-compose mismatch\n";
        return 1;
    }

    auto missingSkeletonMatrixClass = postComposeClass;
    missingSkeletonMatrixClass.skeletonMatrices.clear();
    const auto missingSkeletonMatrix =
        executeRac1MobySkinningProgram(missingSkeletonMatrixClass, postComposePose);
    if (missingSkeletonMatrix.status !=
        Rac1MobySkinExecutionStatus::SkeletonMatrixCountMismatch) {
        std::cerr << "missing render correction table was not rejected\n";
        return 1;
    }

    auto readBeforeWriteClass = skinClass;
    readBeforeWriteClass.highLodPacketCount = 1u;
    readBeforeWriteClass.skinningPackets.resize(1u);
    readBeforeWriteClass.skinningPackets[0].matrixTransfers.clear();
    readBeforeWriteClass.skinningPackets[0].vertices.clear();
    auto badReadVertex = skinMain0;
    badReadVertex.l1 = 0x2cu;
    badReadVertex.directStore = 0x30u;
    readBeforeWriteClass.skinningPackets[0].vertices.push_back(badReadVertex);
    const auto readBeforeWrite = executeRac1MobySkinningProgram(readBeforeWriteClass, skinPose);
    if (readBeforeWrite.status != Rac1MobySkinExecutionStatus::Vu0ReadBeforeWrite ||
        readBeforeWrite.failurePacketIndex != 0 || readBeforeWrite.failureVertexIndex != 0 ||
        readBeforeWrite.failureVertexKind != Rac1MobySkinningVertexKind::Main ||
        readBeforeWrite.failureVu0Address != 0x2c) {
        std::cerr << "VU0 read-before-write was not rejected precisely\n";
        return 1;
    }

    auto shortSkinPose = skinPose;
    shortSkinPose.jointCount = 2u;
    shortSkinPose.jointMatrices.resize(2u);
    const auto shortPoseExecution = executeRac1MobySkinningProgram(skinClass, shortSkinPose);
    if (shortPoseExecution.status != Rac1MobySkinExecutionStatus::PoseJointCountMismatch) {
        std::cerr << "skinning pose-joint mismatch was not rejected\n";
        return 1;
    }

    // Step 11A: retail sparse stream 2 overrides local translation with three
    // signed s16 values and byte 6 selects the joint. Stream 1 entries whose
    // signed low 32-bit word is nonnegative are skipped by FUN_002109b8, so
    // they must not make an otherwise-decodable sparse frame fail.
    auto sparsePoseCore = poseCore;
    writeU16(sparsePoseCore, poseFrame + 0x06u, 3u);      // 0x30 payload bytes
    writeU16(sparsePoseCore, poseFrame + 0x08u, 0x18u);  // 3 * 8-byte quats
    writeU16(sparsePoseCore, poseFrame + 0x0au, 1u);     // one inert stream-1 entry
    writeU16(sparsePoseCore, poseFrame + 0x0cu, 0x20u);
    writeU16(sparsePoseCore, poseFrame + 0x0eu, 2u);     // two translations
    const std::size_t sparseStream1 = poseFrame + 0x10u + 0x18u;
    sparsePoseCore[sparseStream1 + 0u] = 0x34u;
    sparsePoseCore[sparseStream1 + 1u] = 0x12u;
    sparsePoseCore[sparseStream1 + 2u] = 0x00u;
    sparsePoseCore[sparseStream1 + 3u] = 0x00u; // low s32 >= 0 => retail skip
    sparsePoseCore[sparseStream1 + 6u] = 1u;
    const std::size_t sparseStream2 = poseFrame + 0x10u + 0x20u;
    writeI16(sparsePoseCore, sparseStream2 + 0u, 10);
    writeI16(sparsePoseCore, sparseStream2 + 2u, 0);
    writeI16(sparsePoseCore, sparseStream2 + 4u, 0);
    sparsePoseCore[sparseStream2 + 6u] = 1u;
    writeI16(sparsePoseCore, sparseStream2 + 8u + 0u, 0);
    writeI16(sparsePoseCore, sparseStream2 + 8u + 2u, 4);
    writeI16(sparsePoseCore, sparseStream2 + 8u + 4u, 0);
    sparsePoseCore[sparseStream2 + 8u + 6u] = 2u;

    const auto sparsePose = decodeRac1MobyPoseFrame(sparsePoseCore, poseClass, 7u, 0u);
    if (!sparsePose.ok() || sparsePose.pose.stream1Count != 1u ||
        sparsePose.pose.stream1ActiveCount != 0u ||
        sparsePose.pose.stream2Count != 2u || sparsePose.pose.stream2OverrideCount != 2u ||
        sparsePose.pose.jointMatrices.size() != 3u ||
        !near(sparsePose.pose.jointMatrices[1][12], 11.0f) ||
        !near(sparsePose.pose.jointMatrices[1][13], 2.0f) ||
        !near(sparsePose.pose.jointMatrices[2][12], 15.0f) ||
        !near(sparsePose.pose.jointMatrices[2][13], 2.0f)) {
        std::cerr << "retail sparse translation override fixture failed: "
                  << rac1MobyPoseStatusName(sparsePose.status) << '\n';
        return 1;
    }

    // The translation override participates before hierarchy evaluation and is
    // interpolated against the next frame's common translation when that frame
    // has no stream-2 entry for the same joint.
    const auto sparseInterpolated = decodeRac1MobyPoseInterpolated(
        sparsePoseCore, poseClass, 7u, 0u, 1u, 0.5f);
    if (!sparseInterpolated.ok() ||
        !near(sparseInterpolated.pose.jointMatrices[1][12], 6.5f, 4.0e-4f) ||
        !near(sparseInterpolated.pose.jointMatrices[1][13], 2.0f, 4.0e-4f)) {
        std::cerr << "retail sparse translation interpolation mismatch\n";
        return 1;
    }

    // The unresolved stream-1 branch is guarded by the exact retail bgez
    // predicate. Flip only the low-s32 sign bit: the same entry must become an
    // explicit unsupported compressed-quaternion frame, never approximated.
    auto activeSparseCore = sparsePoseCore;
    activeSparseCore[sparseStream1 + 3u] = 0x80u;
    const auto activeSparse = decodeRac1MobyPoseFrame(activeSparseCore, poseClass, 7u, 0u);
    if (activeSparse.status != Rac1MobyPoseStatus::UnsupportedSparseFrame ||
        activeSparse.pose.stream1Count != 1u || activeSparse.pose.stream1ActiveCount != 1u) {
        std::cerr << "active sparse quaternion stream was not gated precisely\n";
        return 1;
    }

    auto badSparseJointCore = sparsePoseCore;
    badSparseJointCore[sparseStream2 + 6u] = 3u;
    const auto badSparseJoint = decodeRac1MobyPoseFrame(
        badSparseJointCore, poseClass, 7u, 0u);
    if (badSparseJoint.status != Rac1MobyPoseStatus::InvalidSparseJointIndex ||
        badSparseJoint.failureSparseEntryIndex != 0 ||
        badSparseJoint.failureSparseJointIndex != 3) {
        std::cerr << "out-of-range sparse translation joint was not rejected\n";
        return 1;
    }

    auto badSparseLayoutCore = sparsePoseCore;
    writeU16(badSparseLayoutCore, poseFrame + 0x0cu, 0x28u);
    const auto badSparseLayout = decodeRac1MobyPoseFrame(
        badSparseLayoutCore, poseClass, 7u, 0u);
    if (badSparseLayout.status != Rac1MobyPoseStatus::InvalidSparseLayout) {
        std::cerr << "non-retail sparse stream layout was not rejected\n";
        return 1;
    }

    auto badParentClass = poseClass;
    badParentClass.commonTransformWords[2][3] = 0x70000080u; // points to itself
    const auto badParentPose = decodeRac1MobyDensePoseFrame(poseCore, badParentClass, 7u, 0u);
    if (badParentPose.status != Rac1MobyPoseStatus::InvalidParentPointer ||
        badParentPose.failureJointIndex != 2 || badParentPose.failureParentPointer != 0x70000080u) {
        std::cerr << "forward/self parent pointer was not rejected precisely\n";
        return 1;
    }

    // Step 12: Ratchet/oClass 0 uses a core-index-resident external sequence
    // pointer table. The table entries are absolute core pointers, while frame
    // pointers inside each sequence are relative to that sequence. Keep the
    // synthetic classEnd intentionally below the external frames so this test
    // fails if the pose decoder silently falls back to class-relative bounds.
    std::vector<std::uint8_t> ratchetExternalCore(0x400u, 0u);
    std::vector<std::uint8_t> ratchetExternalIndex(0x40u, 0u);
    writeU32(ratchetExternalIndex, 0x10u, 0x100u);
    writeU32(ratchetExternalIndex, 0x14u, 0x200u);
    auto writeExternalDenseFrame = [&](std::size_t frame,
                                       std::int16_t z,
                                       std::int16_t w) {
        writeU16(ratchetExternalCore, frame + 0x06u, 1u);
        writeU16(ratchetExternalCore, frame + 0x08u, 8u);
        writeU16(ratchetExternalCore, frame + 0x0au, 0u);
        writeU16(ratchetExternalCore, frame + 0x0cu, 8u);
        writeU16(ratchetExternalCore, frame + 0x0eu, 0u);
        writeI16(ratchetExternalCore, frame + 0x10u + 0u, 0);
        writeI16(ratchetExternalCore, frame + 0x10u + 2u, 0);
        writeI16(ratchetExternalCore, frame + 0x10u + 4u, z);
        writeI16(ratchetExternalCore, frame + 0x10u + 6u, w);
    };

    // Sequence 0 deliberately has two frames so Step 12B's exact runtime path
    // is covered: an external core-absolute sequence pointer whose internal
    // frame pointers remain sequence-relative, followed by pose-space nlerp.
    ratchetExternalCore[0x100u + 0x10u] = 2u;
    ratchetExternalCore[0x100u + 0x11u] = 0xffu;
    writeF32(ratchetExternalCore, 0x100u + 0x18u, 0.125f);
    writeU32(ratchetExternalCore, 0x100u + 0x1cu, 0x30u);
    writeU32(ratchetExternalCore, 0x100u + 0x20u, 0x60u);
    writeExternalDenseFrame(0x130u, 0, 32767);
    writeExternalDenseFrame(0x160u, 23170, 23170);

    ratchetExternalCore[0x200u + 0x10u] = 1u;
    ratchetExternalCore[0x200u + 0x11u] = 0xffu;
    writeF32(ratchetExternalCore, 0x200u + 0x18u, 0.125f);
    writeU32(ratchetExternalCore, 0x200u + 0x1cu, 0x20u);
    writeExternalDenseFrame(0x220u, 0, 32767);

    Rac1MobyAnimationClass ratchetExternalClass{};
    ratchetExternalClass.oClass = 0;
    ratchetExternalClass.classOffset = 0x20u;
    ratchetExternalClass.classEndOffset = 0x80u;
    ratchetExternalClass.jointCount = 1u;
    ratchetExternalClass.sequenceCount = 2u;
    ratchetExternalClass.sequenceOffsets = {0u, 0u};
    ratchetExternalClass.nullSequenceCount = 2u;
    ratchetExternalClass.commonTransformWords = {{
        {f32bits(3.0f), f32bits(4.0f), f32bits(5.0f), 0u},
    }};

    const auto externalBank = inspectRac1RatchetAnimationBank(
        ratchetExternalCore, ratchetExternalIndex, 0x10u, ratchetExternalClass);
    if (!externalBank.ok() || externalBank.bank.sequenceTableOffset != 0x10u ||
        externalBank.bank.sequenceCount != 2u || externalBank.bank.totalFrameCount != 3u ||
        externalBank.bank.nonFfHeaderByte11Count != 0u ||
        externalBank.bank.animationClass.externalSequenceTableOffset != 0x10u ||
        externalBank.bank.animationClass.presentSequenceCount != 2u ||
        externalBank.bank.animationClass.nullSequenceCount != 0u ||
        externalBank.bank.animationClass.sequenceLayouts.size() != 2u ||
        externalBank.bank.animationClass.sequenceOffsets !=
            std::vector<std::uint32_t>{0x100u, 0x200u}) {
        std::cerr << "Ratchet external sequence bank fixture failed: "
                  << rac1RatchetAnimationBankStatusName(externalBank.status) << '\n';
        return 1;
    }
    const auto& externalLayout = externalBank.bank.animationClass.sequenceLayouts[0];
    if (externalLayout.storage != Rac1MobySequenceStorage::RatchetExternal ||
        externalLayout.sequenceAbsoluteOffset != 0x100u ||
        externalLayout.frameBaseOffset != 0x100u ||
        externalLayout.dataEndOffset != 0x200u ||
        externalLayout.frameOffsets != std::vector<std::uint32_t>{0x30u, 0x60u}) {
        std::cerr << "Ratchet external sequence addressing mismatch\n";
        return 1;
    }
    const auto externalPose = decodeRac1MobyPoseFrame(
        ratchetExternalCore, externalBank.bank.animationClass, 0u, 0u);
    if (!externalPose.ok() || externalPose.pose.jointMatrices.size() != 1u ||
        !near(externalPose.pose.jointMatrices[0][12], 3.0f) ||
        !near(externalPose.pose.jointMatrices[0][13], 4.0f) ||
        !near(externalPose.pose.jointMatrices[0][14], 5.0f)) {
        std::cerr << "Ratchet external frame did not reach the native pose evaluator: "
                  << rac1MobyPoseStatusName(externalPose.status) << '\n';
        return 1;
    }

    const auto externalInterpolated = decodeRac1MobyPoseInterpolated(
        ratchetExternalCore, externalBank.bank.animationClass, 0u, 0u, 1u, 0.5f);
    constexpr float kExternalInvSqrt2 = 0.70710678f;
    if (!externalInterpolated.ok() || !externalInterpolated.pose.interpolated ||
        externalInterpolated.pose.jointMatrices.size() != 1u ||
        !near(externalInterpolated.pose.jointMatrices[0][0], kExternalInvSqrt2, 4.0e-4f) ||
        !near(externalInterpolated.pose.jointMatrices[0][1], -kExternalInvSqrt2, 4.0e-4f) ||
        !near(externalInterpolated.pose.jointMatrices[0][4], kExternalInvSqrt2, 4.0e-4f) ||
        !near(externalInterpolated.pose.jointMatrices[0][5], kExternalInvSqrt2, 4.0e-4f) ||
        !near(externalInterpolated.pose.jointMatrices[0][12], 3.0f) ||
        !near(externalInterpolated.pose.jointMatrices[0][13], 4.0f) ||
        !near(externalInterpolated.pose.jointMatrices[0][14], 5.0f)) {
        std::cerr << "Ratchet external sequence interpolation did not preserve the retail addressing/nlerp contract: "
                  << rac1MobyPoseStatusName(externalInterpolated.status) << '\n';
        return 1;
    }

    // FUN_0020d580's same-sequence forward path wraps frame B to zero once
    // frame B reaches frameCount, while retaining the fractional interpolation
    // state. Exercise that exact last->first pair through the external-bank
    // pose decoder so a future viewer cannot reintroduce a hard reset.
    const auto externalWrappedInterpolated = decodeRac1MobyPoseInterpolated(
        ratchetExternalCore, externalBank.bank.animationClass, 0u, 1u, 0u, 0.5f);
    if (!externalWrappedInterpolated.ok() ||
        !externalWrappedInterpolated.pose.interpolated ||
        externalWrappedInterpolated.pose.frameIndex != 1u ||
        externalWrappedInterpolated.pose.nextFrameIndex != 0u ||
        !near(externalWrappedInterpolated.pose.interpolationAlpha, 0.5f) ||
        externalWrappedInterpolated.pose.jointMatrices.size() !=
            externalInterpolated.pose.jointMatrices.size()) {
        std::cerr << "Ratchet retail last-to-first interpolation fixture failed: "
                  << rac1MobyPoseStatusName(externalWrappedInterpolated.status) << '\n';
        return 1;
    }
    for (std::size_t joint = 0u; joint < externalInterpolated.pose.jointMatrices.size(); ++joint) {
        for (std::size_t word = 0u; word < 16u; ++word) {
            if (!near(externalWrappedInterpolated.pose.jointMatrices[joint][word],
                      externalInterpolated.pose.jointMatrices[joint][word],
                      4.0e-4f)) {
                std::cerr << "Ratchet retail last-to-first midpoint differs from the reversed retail nlerp fixture\n";
                return 1;
            }
        }
    }

    auto mixedRatchetClass = ratchetExternalClass;
    mixedRatchetClass.sequenceOffsets[0] = 0x40u;
    const auto mixedBank = inspectRac1RatchetAnimationBank(
        ratchetExternalCore, ratchetExternalIndex, 0x10u, mixedRatchetClass);
    if (mixedBank.status != Rac1RatchetAnimationBankStatus::LocalSequenceTableNotEmpty) {
        std::cerr << "mixed Ratchet local/external sequence tables were not rejected\n";
        return 1;
    }

    const auto missing = inspectRac1MobyAnimationMetadata(
        makeCore(), makeIndex(), makeGameplay({999}), {0x00u, 3u});
    if (missing.status != Rac1MobyAnimationStatus::MissingReferencedClass) {
        std::cerr << "missing referenced class was not rejected\n";
        return 1;
    }

    std::cout << "rac1_moby_animation_tests: PASS\n";
    return 0;
}
