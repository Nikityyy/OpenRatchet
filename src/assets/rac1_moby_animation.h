#pragma once

#include "assets/rac1_level.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1MobyAnimationStatus : std::uint8_t {
    Ok,
    InvalidIndexTable,
    InvalidGameplayHeader,
    InvalidInstanceBlock,
    MissingReferencedClass,
    InvalidClassHeader,
    InvalidSequenceTable,
    InvalidSequenceLayout,
    InvalidRigLayout,
    InvalidPacketTable,
    InvalidVertexTable,
    InvalidMatrixTransfer,
    InvalidSkinningProgram,
    EmptyScene,
};

enum class Rac1MobySequenceFailure : std::uint8_t {
    None,
    HeaderOutOfRange,
    ZeroFrameCount,
    HeaderByte11OutOfRange,
    FrameTableOutOfRange,
    FramePointerZero,
    FramePointerOutOfRange,
    FramePointerBeforeTableEnd,
    FramePointerMisaligned,
    FramePointersNotIncreasing,
};

enum class Rac1MobyRigFailure : std::uint8_t {
    None,
    SkeletonRangeMismatch,
    CommonTransformRangeMismatch,
};

constexpr std::size_t kRac1MobyFrameProbeMaxBytes = 0x1000u;

struct Rac1MobyFrameProbe {
    std::uint8_t sequenceIndex = 0u;
    std::uint8_t frameIndex = 0u;
    std::uint32_t offset = 0u;
    std::uint32_t stride = 0u;
    std::uint32_t payloadFnv1a = 0u;
    // Exact bytes for one representative frame of each observed non-final
    // frame stride. The Step 5 gate caps diagnostics at 4 KiB per frame to
    // prevent malformed retail data from causing unbounded allocations.
    std::vector<std::uint8_t> payload;
};

enum class Rac1MobySequenceStorage : std::uint8_t {
    ClassRelative,
    // Ratchet/oClass 0 is exceptional: LevelCoreHeader +0x78 points into the
    // core-index blob, whose 134 entries are absolute core sequence pointers.
    // Frame pointers inside those external sequences are sequence-relative.
    RatchetExternal,
};

struct Rac1MobySequenceLayout {
    std::uint8_t sequenceIndex = 0u;
    // Raw sequence pointer in the storage domain named below: class-relative
    // for ordinary mobys and core-absolute for Ratchet's external bank.
    std::uint32_t offset = 0u;
    Rac1MobySequenceStorage storage = Rac1MobySequenceStorage::ClassRelative;
    // Absolute sequence location plus the base used by the raw frame-pointer
    // entries. Zero preserves the legacy class-relative defaults for layouts
    // produced before Step 12.
    std::uint32_t sequenceAbsoluteOffset = 0u;
    std::uint32_t frameBaseOffset = 0u;
    std::uint32_t dataEndOffset = 0u;
    std::array<float, 4> headerVec4{};
    std::uint8_t frameCount = 0u;
    std::uint8_t headerByte11 = 0xffu;
    std::uint8_t controlByte = 0u;
    std::uint8_t reservedByte = 0u;
    std::uint32_t reservedWord = 0u;
    float headerScalar = 0.0f;
    std::vector<std::uint32_t> frameOffsets;
    std::uint32_t firstFrameOffset = 0u;
    std::uint32_t lastFrameOffset = 0u;
    std::uint32_t minFrameStride = 0u;
    std::uint32_t maxFrameStride = 0u;
    std::vector<std::uint32_t> uniqueFrameStrides;
    std::vector<Rac1MobyFrameProbe> frameProbes;
};

enum class Rac1MobyMatrixTransferFailure : std::uint8_t {
    None,
    TableOutOfRange,
    DestinationMisaligned,
    DestinationOverflow,
};


enum class Rac1MobyBlendLayout : std::uint8_t {
    NotApplicable,
    TwoWayThenThreeWay,
};

enum class Rac1MobySkinningFailure : std::uint8_t {
    None,
    VertexDataOutOfRange,
    MatrixAddressMisaligned,
    MatrixAddressOverflow,
};

enum class Rac1MobyPackedL3Encoding : std::uint8_t {
    NotApplicable,
    // R&C1 stores L3 in bits 9..15 of the first packed u16. Bit 8 is the
    // ninth ID bit, so the seven L3 bits physically occupy bits 1..7 of the
    // second byte and the address's low zero bit is implicit. Reconstruct the
    // byte-address value as packedL3 << 1 (equivalently byte1 & 0xfe).
    ShiftLeft1QwordAddress,
};

enum class Rac1MobySkinningVertexKind : std::uint8_t {
    None,
    TwoWay,
    ThreeWay,
    Main,
};

struct Rac1MobySkinningMatrixTransfer {
    std::uint8_t jointIndex = 0u;
    std::uint8_t vu0Address = 0u;
};

struct Rac1MobySkinningVertexProgram {
    Rac1MobySkinningVertexKind kind = Rac1MobySkinningVertexKind::None;
    std::uint16_t id = 0u;
    std::uint8_t directJointIndex = 0u;
    std::uint8_t l1 = 0u;
    std::uint8_t l2 = 0u;
    std::uint8_t l3 = 0u;
    std::uint8_t w1 = 0u;
    std::uint8_t w2 = 0u;
    std::uint8_t w3 = 0u;
    std::uint8_t directStore = 0u;
    std::uint8_t blendStore = 0u;
    std::array<std::int16_t, 3> position{};
};

struct Rac1MobySkinningPacketProgram {
    std::vector<Rac1MobySkinningMatrixTransfer> matrixTransfers;
    std::vector<Rac1MobySkinningVertexProgram> vertices;
};


constexpr std::size_t kRac1MobyProbePrefixBytes = 64u;

enum class Rac1MobyRigProbeKind : std::uint8_t {
    Skeleton,
    CommonTransforms,
    Joints,
};

struct Rac1MobySequenceProbe {
    std::uint8_t sequenceIndex = 0u;
    std::uint32_t offset = 0u;
    std::uint32_t nextBoundaryOffset = 0u;
    std::size_t aliasCount = 0u;
    std::size_t prefixSize = 0u;
    std::uint32_t prefixFnv1a = 0u;
    std::array<std::uint8_t, kRac1MobyProbePrefixBytes> prefix{};
};

struct Rac1MobyRigProbe {
    Rac1MobyRigProbeKind kind = Rac1MobyRigProbeKind::Skeleton;
    std::uint32_t offset = 0u;
    std::uint32_t nextBoundaryOffset = 0u;
    std::size_t prefixSize = 0u;
    std::uint32_t prefixFnv1a = 0u;
    std::array<std::uint8_t, kRac1MobyProbePrefixBytes> prefix{};
};

struct Rac1MobyAnimationClass {
    std::int32_t oClass = 0;
    std::size_t instanceCount = 0u;
    std::uint32_t classOffset = 0u;
    std::uint32_t classEndOffset = 0u;

    std::uint8_t jointCount = 0u;
    std::uint8_t sequenceCount = 0u;
    std::uint8_t highLodPacketCount = 0u;
    std::uint8_t lowLodPacketCount = 0u;

    std::uint32_t skeletonOffset = 0u;
    std::uint32_t commonTransformOffset = 0u;
    std::uint32_t jointsOffset = 0u;
    // Non-zero only for the verified oClass-0 external sequence table in the
    // core-index blob. Ordinary moby classes keep their sequence pointers in
    // the class-local table immediately after the 0x48-byte header.
    std::uint32_t externalSequenceTableOffset = 0u;
    // One entry per animation sequence slot. Ordinary classes store class-
    // relative offsets and may contain intentional zero holes. A class copy
    // returned by inspectRac1RatchetAnimationBank stores the verified external
    // core-absolute pointers instead and sets externalSequenceTableOffset.
    std::vector<std::uint32_t> sequenceOffsets;
    std::size_t presentSequenceCount = 0u;
    std::size_t nullSequenceCount = 0u;
    std::size_t uniqueSequencePayloadCount = 0u;
    std::size_t aliasedSequenceSlotCount = 0u;
    std::vector<Rac1MobySequenceProbe> sequenceProbes;
    std::vector<Rac1MobyRigProbe> rigProbes;

    // Phase 10 Step 4: verified sequence header/frame-pointer tables. Field
    // names remain deliberately conservative where semantics are not yet
    // proven: byte +0x10 is frameCount, +0x11 is the observed 0xff-or-frame-index byte,
    // and the class-relative frame pointer table begins at +0x1c. The remaining
    // header controls are preserved raw for the pose decoder in the next step.
    std::vector<Rac1MobySequenceLayout> sequenceLayouts;
    std::size_t totalFrameCount = 0u;
    std::size_t nonFfHeaderByte11Count = 0u;
    std::size_t oversizedFrameProbeCount = 0u;

    // The authentic rig layout proves these exact extents for every skeletal
    // class: jointCount 0x40-byte class +0x14 records followed by jointCount
    // 0x10-byte common-transform records. The +0x14 records are preserved raw
    // as 16 float lanes here. Retail sub_0020E0E0 later consumes only xyz from
    // each of their four qwords when post-composing the render pose; the four w
    // lanes are auxiliary and are deliberately not interpreted as matrix data.
    std::vector<std::array<float, 16>> skeletonMatrices;
    std::vector<std::array<std::uint32_t, 4>> commonTransformWords;

    std::size_t matrixTransferCount = 0u;
    std::size_t twoWayBlendVertexCount = 0u;
    std::size_t threeWayBlendVertexCount = 0u;
    std::size_t mainVertexCount = 0u;
    std::uint8_t maxScratchpadMatrixIndex = 0u;
    std::uint8_t maxVu0Destination = 0u;

    // Phase 10 Step 2: decoded 16-byte R&C1 skinning-program vertices.
    // Wrench's recovered header + pseudocode define the real order as two-way,
    // then three-way, then main/no-blend. The prose bitfield table swaps the
    // names of the first two rows, so the alternate interpretation is retained
    // only as a diagnostic score while the documented executable semantics are
    // validated directly against authentic VU0 matrix-address invariants.
    std::size_t skinningVertexCount = 0u;
    std::size_t documentedLayoutInvalidAddresses = 0u;
    std::size_t swappedLayoutInvalidAddresses = 0u;
    std::uint8_t maxFirstBlendLow7 = 0u;
    std::uint8_t maxSecondBlendLow7 = 0u;
    std::uint8_t maxMainScratchpadMatrixIndex = 0u;
    std::uint8_t maxPackedVu0Address = 0u;
    // Three-way L3 is a seven-bit packed VU0 address. The field is stored in
    // bits 9..15 beside the 9-bit vertex ID, which leaves the address's low
    // zero bit implicit. maxPackedL3Raw is the seven-bit stored value.
    std::uint8_t maxPackedL3Raw = 0u;

    // Phase 10 Step 8: executable CPU representation of the authentic high-LOD
    // skinning program. VU0 matrix memory persists across packet entries.
    std::vector<Rac1MobySkinningPacketProgram> skinningPackets;

    [[nodiscard]] bool hasMesh() const noexcept { return highLodPacketCount != 0u; }
    [[nodiscard]] bool hasSkeleton() const noexcept { return jointCount != 0u; }
    [[nodiscard]] bool hasSequences() const noexcept { return presentSequenceCount != 0u; }
};

struct Rac1MobyAnimationMetadata {
    std::vector<Rac1MobyAnimationClass> classes;

    std::size_t instanceCount = 0u;
    std::size_t referencedClassCount = 0u;
    std::size_t renderableClassCount = 0u;
    std::size_t skeletalClassCount = 0u;
    std::size_t sequencedClassCount = 0u;
    std::size_t skeletalInstanceCount = 0u;
    std::size_t sequencedInstanceCount = 0u;
    std::size_t sequenceSlotCount = 0u;
    std::size_t presentSequenceCount = 0u;
    std::size_t nullSequenceCount = 0u;
    std::size_t uniqueSequencePayloadCount = 0u;
    std::size_t aliasedSequenceSlotCount = 0u;
    std::size_t decodedSequenceCount = 0u;
    std::size_t totalFrameCount = 0u;
    std::size_t nonFfHeaderByte11Count = 0u;
    std::size_t skeletonMatrixCount = 0u;
    std::size_t commonTransformRecordCount = 0u;
    std::uint32_t minFrameStride = 0u;
    std::uint32_t maxFrameStride = 0u;
    std::size_t frameProbeCount = 0u;
    std::size_t uniqueFrameStrideCount = 0u;
    std::size_t oversizedFrameProbeCount = 0u;
    std::size_t maxProbedFrameBytes = 0u;

    std::size_t packetCount = 0u;
    std::size_t matrixTransferCount = 0u;
    std::size_t twoWayBlendVertexCount = 0u;
    std::size_t threeWayBlendVertexCount = 0u;
    std::size_t mainVertexCount = 0u;
    std::size_t skinningVertexCount = 0u;
    std::size_t documentedLayoutInvalidAddresses = 0u;
    std::size_t swappedLayoutInvalidAddresses = 0u;
    Rac1MobyBlendLayout blendLayout = Rac1MobyBlendLayout::NotApplicable;
    std::uint8_t maxPackedL3Raw = 0u;
    std::uint8_t maxDecodedPackedL3Address = 0u;
    Rac1MobyPackedL3Encoding packedL3Encoding = Rac1MobyPackedL3Encoding::NotApplicable;
};

enum class Rac1MobyPoseStatus : std::uint8_t {
    Ok,
    SequenceNotFound,
    FrameIndexOutOfRange,
    FrameOutOfRange,
    UnsupportedSparseFrame,
    InvalidDenseLayout,
    InvalidSparseLayout,
    InvalidSparseJointIndex,
    InvalidCommonTransform,
    InvalidParentPointer,
    InvalidInterpolationAlpha,
    NonFinitePose,
};

struct Rac1MobyPoseFrame {
    std::int32_t oClass = 0;
    std::uint8_t sequenceIndex = 0u;
    std::uint8_t frameIndex = 0u;
    std::uint8_t nextFrameIndex = 0u;
    std::uint8_t jointCount = 0u;
    std::uint16_t payloadQwordCount = 0u;
    std::uint16_t stream1Offset = 0u;
    std::uint16_t stream1Count = 0u;
    std::uint16_t stream2Offset = 0u;
    std::uint16_t stream2Count = 0u;
    std::size_t stream1ActiveCount = 0u;
    std::size_t stream2OverrideCount = 0u;
    std::size_t rootJointCount = 0u;
    std::size_t parentLinkCount = 0u;
    float maxQuaternionNormError = 0.0f;
    float maxAbsLocalTranslation = 0.0f;
    float interpolationAlpha = 0.0f;
    bool interpolated = false;
    // Column-major affine matrices matching the 0x40-byte matrices emitted by
    // retail FUN_002109b8 into scratchpad memory. Retail sub_00210850 exports
    // selected joints verbatim as these four qwords; Step 7 separately proves
    // that the skinning program's direct SPR sources share this joint-index domain.
    std::vector<std::array<float, 16>> jointMatrices;
};

struct Rac1MobyPoseResult {
    Rac1MobyPoseStatus status = Rac1MobyPoseStatus::FrameOutOfRange;
    Rac1MobyPoseFrame pose{};
    std::int32_t failureJointIndex = -1;
    std::uint32_t failureParentPointer = 0u;
    std::int32_t failureSparseEntryIndex = -1;
    std::int32_t failureSparseJointIndex = -1;

    [[nodiscard]] bool ok() const noexcept { return status == Rac1MobyPoseStatus::Ok; }
};

enum class Rac1MobyPosePaletteStatus : std::uint8_t {
    Ok,
    NoSkeleton,
    MatrixTransferJointOutOfRange,
    TwoWayJointOutOfRange,
    MainJointOutOfRange,
};

enum class Rac1MobyPosePaletteSourceKind : std::uint8_t {
    None,
    MatrixTransfer,
    TwoWay,
    Main,
};

struct Rac1MobyPosePaletteContract {
    std::int32_t oClass = 0;
    std::uint8_t jointCount = 0u;
    std::size_t matrixTransferReferences = 0u;
    std::size_t twoWayDirectReferences = 0u;
    std::size_t mainDirectReferences = 0u;
    std::size_t directSourceReferences = 0u;
    std::uint8_t maxReferencedJointIndex = 0u;
};

struct Rac1MobyPosePaletteResult {
    Rac1MobyPosePaletteStatus status = Rac1MobyPosePaletteStatus::NoSkeleton;
    Rac1MobyPosePaletteContract contract{};
    Rac1MobyPosePaletteSourceKind failureSourceKind = Rac1MobyPosePaletteSourceKind::None;
    std::int32_t failureJointIndex = -1;

    [[nodiscard]] bool ok() const noexcept { return status == Rac1MobyPosePaletteStatus::Ok; }
};

enum class Rac1MobySkinExecutionStatus : std::uint8_t {
    Ok,
    NoSkeleton,
    PoseJointCountMismatch,
    SkeletonMatrixCountMismatch,
    ProgramPacketCountMismatch,
    JointIndexOutOfRange,
    Vu0ReadBeforeWrite,
    Vu0AddressInvalid,
    NonFiniteMatrix,
    NonFiniteVertex,
};

struct Rac1MobySkinnedVertex {
    Rac1MobySkinningVertexKind kind = Rac1MobySkinningVertexKind::None;
    std::uint16_t id = 0u;
    std::array<float, 3> position{};
};

struct Rac1MobySkinExecution {
    std::int32_t oClass = 0;
    std::size_t packetCount = 0u;
    std::size_t matrixTransferCount = 0u;
    std::size_t twoWayVertexCount = 0u;
    std::size_t threeWayVertexCount = 0u;
    std::size_t mainVertexCount = 0u;
    std::size_t skeletonPostComposeCount = 0u;
    std::size_t vu0MatrixWrites = 0u;
    std::size_t vu0CrossPacketReads = 0u;
    float maxWeightSumError = 0.0f;
    float maxAbsPosition = 0.0f;
    std::vector<Rac1MobySkinnedVertex> vertices;
};

struct Rac1MobySkinExecutionResult {
    Rac1MobySkinExecutionStatus status = Rac1MobySkinExecutionStatus::NoSkeleton;
    Rac1MobySkinExecution execution{};
    std::int32_t failurePacketIndex = -1;
    std::int32_t failureVertexIndex = -1;
    Rac1MobySkinningVertexKind failureVertexKind = Rac1MobySkinningVertexKind::None;
    std::int32_t failureJointIndex = -1;
    std::int32_t failureVu0Address = -1;

    [[nodiscard]] bool ok() const noexcept { return status == Rac1MobySkinExecutionStatus::Ok; }
};

struct Rac1MobyAnimationMetadataResult {
    Rac1MobyAnimationStatus status = Rac1MobyAnimationStatus::InvalidIndexTable;
    Rac1MobyAnimationMetadata metadata{};

    // Failure context is populated for class-local validation failures. This is
    // deliberately part of the gate: authentic retail data must tell us exactly
    // which class/sequence disproved an assumption rather than only returning a
    // generic decoder error.
    bool failureOClassValid = false;
    std::int32_t failureOClass = 0;
    std::int32_t failureSequenceIndex = -1;
    std::int32_t failureSequenceRelative = 0;
    std::uint32_t failureSequenceTableOffset = 0u;

    std::int32_t failurePacketIndex = -1;
    std::int32_t failureMatrixTransferIndex = -1;
    std::int32_t failureMatrixScratchpadIndex = -1;
    std::int32_t failureMatrixVu0Destination = -1;
    Rac1MobySequenceFailure failureSequenceReason = Rac1MobySequenceFailure::None;
    std::int32_t failureFrameIndex = -1;
    std::uint32_t failureFrameOffset = 0u;

    Rac1MobyRigFailure failureRigReason = Rac1MobyRigFailure::None;
    std::uint32_t failureRigExpectedOffset = 0u;
    std::uint32_t failureRigActualOffset = 0u;

    Rac1MobyMatrixTransferFailure failureMatrixReason = Rac1MobyMatrixTransferFailure::None;

    std::int32_t failureSkinningPacketIndex = -1;
    std::int32_t failureSkinningVertexIndex = -1;
    std::int32_t failureSkinningAddress = -1;
    Rac1MobySkinningVertexKind failureSkinningVertexKind = Rac1MobySkinningVertexKind::None;
    Rac1MobySkinningFailure failureSkinningReason = Rac1MobySkinningFailure::None;

    [[nodiscard]] bool ok() const noexcept { return status == Rac1MobyAnimationStatus::Ok; }
};

// Phase 10 metadata gate. Before applying any native joint pose to authentic
// geometry, prove the R&C1 class animation boundary on the real level data:
// referenced oClasses, joint/sequence counts, sequence-pointer table, packet
// vertex categories and packet matrix transfers. The transfer source byte is an
// SPR joint/matrix source. Rigid classes with jointCount == 0 may still use the
// renderer's default SPR slot 0, so the metadata parser cannot globally bound it
// by jointCount; Step 7 applies the joint-domain bound specifically to skeletal
// mesh classes.
//
// Sequence pointers are class-relative and begin immediately after the 0x48-byte
// class header. Sequence tables are sparse: zero entries are intentional holes
// in the sequence-ID space and non-zero entries must resolve inside the class/core.
// Packet skinning metadata follows the R&C1 32-bit vertex-table
// header used by the retail EE path. This function intentionally does not pose
// vertices yet; the verified metadata is the input to the next Phase 10 step.
Rac1MobyAnimationMetadataResult inspectRac1MobyAnimationMetadata(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange mobyClasses);


// Phase 10 Step 12: Ratchet/oClass 0 does not use the class-local sequence
// pointer array. LevelCoreHeader +0x78 points into the separate core-index blob
// and that table contains one absolute core pointer per oClass-0 sequence slot.
// The pointed-to sequence headers use the same frame codec already validated for
// ordinary mobys, except their frame pointers are relative to the sequence base.
enum class Rac1RatchetAnimationBankStatus : std::uint8_t {
    Ok,
    WrongClass,
    MissingSkeleton,
    LocalSequenceTableNotEmpty,
    InvalidSequenceTable,
    SequencePointerZero,
    SequencePointerOutOfRange,
    SequencePointerMisaligned,
    SequencePointersNotIncreasing,
    InvalidSequenceLayout,
};

struct Rac1RatchetAnimationBank {
    std::uint32_t sequenceTableOffset = 0u;
    std::size_t sequenceCount = 0u;
    std::size_t totalFrameCount = 0u;
    std::size_t nonFfHeaderByte11Count = 0u;
    std::uint32_t minFrameStride = 0u;
    std::uint32_t maxFrameStride = 0u;
    std::size_t uniqueFrameStrideCount = 0u;
    std::size_t frameProbeCount = 0u;
    std::size_t oversizedFrameProbeCount = 0u;
    std::size_t maxProbedFrameBytes = 0u;
    Rac1MobyAnimationClass animationClass{};
};

struct Rac1RatchetAnimationBankResult {
    Rac1RatchetAnimationBankStatus status = Rac1RatchetAnimationBankStatus::InvalidSequenceTable;
    Rac1RatchetAnimationBank bank{};
    std::int32_t failureSequenceIndex = -1;
    std::uint32_t failureSequencePointer = 0u;
    Rac1MobySequenceFailure failureSequenceReason = Rac1MobySequenceFailure::None;
    std::int32_t failureFrameIndex = -1;
    std::uint32_t failureFrameOffset = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1RatchetAnimationBankStatus::Ok;
    }
};

Rac1RatchetAnimationBankResult inspectRac1RatchetAnimationBank(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::uint32_t sequenceTableOffset,
    const Rac1MobyAnimationClass& ratchetClass);

// Phase 10 Step 11A: decode one authentic animation frame into the same
// per-joint scratchpad pose matrices produced by retail FUN_002109b8. The base
// quaternion block is jointCount * 8 bytes. Sparse stream 2 is proven by the
// retail executable to contain 8-byte local-translation overrides: three signed
// s16 xyz values plus a joint index in byte 6. Those overrides are applied
// exactly. Sparse stream 1 is classified exactly by the retail low-32-bit sign
// test; entries that take the still-unresolved compressed-quaternion branch
// return UnsupportedSparseFrame rather than being approximated.
Rac1MobyPoseResult decodeRac1MobyPoseFrame(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex);

// Reproduce retail two-frame pose interpolation. Quaternion endpoints use the
// proven shortest-hemisphere normalized linear interpolation. Local translations
// independently use each frame's sparse-stream-2 override (or the class common
// translation when absent), then linearly interpolate exactly as FUN_002109b8.
// Active sparse-stream-1 quaternion entries remain an explicit unsupported gate.
Rac1MobyPoseResult decodeRac1MobyPoseInterpolated(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextFrameIndex,
    float alpha);

// Compatibility names retained for callers from the earlier dense-only steps.
Rac1MobyPoseResult decodeRac1MobyDensePoseFrame(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex);
Rac1MobyPoseResult decodeRac1MobyDensePoseInterpolated(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextFrameIndex,
    float alpha);

// Phase 10 Step 7: prove the direct pose-palette source domain without inventing
// a bind composition that the retail executable does not demonstrate. Retail
// sub_00210850 copies a selected FUN_002109b8 pose matrix verbatim from
// 0x70000000 + joint*0x40. The renderer's matrix-transfer and direct two-way/main
// JI fields likewise address SPR records by joint index. For skeletal mesh
// classes, every such direct source must therefore be < jointCount. Three-way
// records consume already-staged VU0 matrices and have no direct JI source.
Rac1MobyPosePaletteResult inspectRac1MobyPosePaletteContract(
    const Rac1MobyAnimationClass& cls);

// Phase 10 Step 8/9: execute the authentic R&C1 CPU/VU0 skinning program against
// one decoded pose. Before any JI transfer, reproduce retail sub_0020E0E0
// 0x20eadc..0x20ed94: DMA the class +0x14 joint table and post-multiply each
// pose matrix by its corresponding 0x40-byte render correction record. Only xyz
// lanes of each record qword participate; the four w lanes are auxiliary/ignored
// by the retail VU sequence. Matrix memory then begins unknown and persists
// across packets. Reads from a VU0 matrix slot before that slot has been written
// are rejected. Blends use the retail byte weights directly (weight/255) without
// renormalization.
Rac1MobySkinExecutionResult executeRac1MobySkinningProgram(
    const Rac1MobyAnimationClass& cls,
    const Rac1MobyPoseFrame& pose);

const char* rac1RatchetAnimationBankStatusName(Rac1RatchetAnimationBankStatus status) noexcept;
const char* rac1MobySequenceStorageName(Rac1MobySequenceStorage storage) noexcept;
const char* rac1MobyPoseStatusName(Rac1MobyPoseStatus status) noexcept;
const char* rac1MobySkinExecutionStatusName(Rac1MobySkinExecutionStatus status) noexcept;
const char* rac1MobyPosePaletteStatusName(Rac1MobyPosePaletteStatus status) noexcept;
const char* rac1MobyPosePaletteSourceKindName(Rac1MobyPosePaletteSourceKind kind) noexcept;
const char* rac1MobySequenceFailureName(Rac1MobySequenceFailure failure) noexcept;
const char* rac1MobyRigFailureName(Rac1MobyRigFailure failure) noexcept;
const char* rac1MobyAnimationStatusName(Rac1MobyAnimationStatus status) noexcept;
const char* rac1MobyMatrixTransferFailureName(Rac1MobyMatrixTransferFailure failure) noexcept;
const char* rac1MobyBlendLayoutName(Rac1MobyBlendLayout layout) noexcept;
const char* rac1MobySkinningFailureName(Rac1MobySkinningFailure failure) noexcept;
const char* rac1MobySkinningVertexKindName(Rac1MobySkinningVertexKind kind) noexcept;
const char* rac1MobyPackedL3EncodingName(Rac1MobyPackedL3Encoding encoding) noexcept;
const char* rac1MobyRigProbeKindName(Rac1MobyRigProbeKind kind) noexcept;

} // namespace ratchet::assets
