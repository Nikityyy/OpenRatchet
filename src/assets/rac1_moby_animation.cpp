#include "assets/rac1_moby_animation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::size_t kMobyClassEntryBytes = 0x20u;
constexpr std::size_t kMobyClassHeaderBytes = 0x48u;
constexpr std::size_t kMobyPacketHeaderBytes = 0x10u;
constexpr std::size_t kMobyVertexTableHeaderBytes = 0x20u; // R&C1 uses 8 x s32.
constexpr std::size_t kMobyMatrixTransferBytes = 0x2u;
constexpr std::size_t kMobySequenceHeaderBytes = 0x1cu;
constexpr std::size_t kMobySkeletonMatrixBytes = 0x40u;
constexpr std::size_t kMobyCommonTransformBytes = 0x10u;
constexpr std::size_t kGameplayHeaderBytes = 0x90u;
constexpr std::size_t kInstanceBlockHeaderBytes = 0x10u;
constexpr std::size_t kMobyInstanceBytes = 0x78u;
constexpr std::size_t kMaxCount = 1u << 20u;

std::uint32_t readU32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1u]) << 8u) |
           (static_cast<std::uint32_t>(b[o + 2u]) << 16u) |
           (static_cast<std::uint32_t>(b[o + 3u]) << 24u);
}
std::int32_t readI32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::int32_t>(readU32(b, o));
}
std::uint16_t readU16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::uint16_t>(b[o]) |
           (static_cast<std::uint16_t>(b[o + 1u]) << 8u);
}
std::int16_t readI16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::int16_t>(readU16(b, o));
}
float readF32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return std::bit_cast<float>(readU32(b, o));
}
bool fits(std::size_t o, std::size_t n, std::size_t cap) noexcept {
    return o <= cap && n <= cap - o;
}
bool checkedAdd(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (a > std::numeric_limits<std::size_t>::max() - b) return false;
    out = a + b;
    return true;
}
bool checkedMul(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (b != 0u && a > std::numeric_limits<std::size_t>::max() / b) return false;
    out = a * b;
    return true;
}

std::uint32_t fnv1a32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

template <std::size_t N>
void capturePrefix(std::span<const std::uint8_t> core,
                   std::size_t absolute,
                   std::size_t limit,
                   std::array<std::uint8_t, N>& out,
                   std::size_t& outSize,
                   std::uint32_t& outHash) noexcept {
    outSize = 0u;
    outHash = fnv1a32({});
    if (absolute >= limit || absolute >= core.size()) return;
    const std::size_t boundedLimit = std::min(limit, core.size());
    outSize = std::min<std::size_t>(N, boundedLimit - absolute);
    std::copy_n(core.begin() + static_cast<std::ptrdiff_t>(absolute),
                outSize,
                out.begin());
    outHash = fnv1a32(core.subspan(absolute, outSize));
}

bool validVu0MatrixAddress(std::uint16_t address) noexcept {
    // VU0 data memory is 256 qwords and a 4x4 matrix occupies four qwords.
    // R&C stores the destination/load address in qword units, so matrix slots
    // begin on four-qword boundaries and may start no later than 0xfc.
    return address <= 0xfcu && (address & 0x3u) == 0u;
}

std::uint16_t decodePackedL3(std::uint8_t raw) noexcept {
    // The first packed u16 is [L3:7 | ID:9]. In the second byte, ID bit 8
    // occupies bit 0 and L3 occupies bits 1..7. L3 is a VU0 matrix address,
    // so its implicit low zero bit is restored by shifting the seven-bit field
    // left once. This is the packed bit layout, not a data-selected heuristic.
    return static_cast<std::uint16_t>(raw) << 1u;
}

std::uint8_t packedLow7(std::span<const std::uint8_t> core, std::size_t vertex) noexcept {
    // The low 9 bits of a moby vertex are the cache ID. The following seven
    // bits occupy bits 1..7 of byte 1 and are either an SPR matrix index or a
    // third VU0 matrix address depending on the blend record layout.
    return static_cast<std::uint8_t>(core[vertex + 1u] >> 1u);
}

std::size_t invalidAddressCount(std::initializer_list<std::uint8_t> addresses) noexcept {
    std::size_t invalid = 0u;
    for (const std::uint8_t address : addresses) {
        if (!validVu0MatrixAddress(address)) ++invalid;
    }
    return invalid;
}

struct ClassEntry {
    std::uint32_t offset = 0u;
    std::int32_t oClass = 0;
};

std::size_t findClassEnd(const std::unordered_map<std::int32_t, ClassEntry>& entries,
                         const ClassEntry& entry,
                         std::size_t coreSize) noexcept {
    if (entry.offset == 0u) return 0u;
    std::size_t end = coreSize;
    for (const auto& [oClass, candidate] : entries) {
        (void)oClass;
        if (candidate.offset > entry.offset &&
            static_cast<std::size_t>(candidate.offset) < end) {
            end = static_cast<std::size_t>(candidate.offset);
        }
    }
    return end;
}

bool parseClassEntries(std::span<const std::uint8_t> index,
                       Rac1ArrayRange range,
                       std::unordered_map<std::int32_t, ClassEntry>& out) {
    if (range.count > kMaxCount) return false;
    std::size_t bytes = 0u;
    if (!checkedMul(range.count, kMobyClassEntryBytes, bytes) ||
        !fits(range.offset, bytes, index.size())) {
        return false;
    }

    out.reserve(range.count);
    for (std::uint32_t i = 0u; i < range.count; ++i) {
        const std::size_t o = static_cast<std::size_t>(range.offset) +
                              static_cast<std::size_t>(i) * kMobyClassEntryBytes;
        ClassEntry entry{};
        entry.offset = readU32(index, o + 0x0u);
        entry.oClass = readI32(index, o + 0x4u);
        out[entry.oClass] = entry;
    }
    return true;
}

bool parseInstanceCounts(std::span<const std::uint8_t> gameplay,
                         std::unordered_map<std::int32_t, std::size_t>& counts,
                         std::size_t& total,
                         Rac1MobyAnimationStatus& status) {
    if (gameplay.size() < kGameplayHeaderBytes) {
        status = Rac1MobyAnimationStatus::InvalidGameplayHeader;
        return false;
    }
    const std::int32_t offsetSigned = readI32(gameplay, 0x44u);
    if (offsetSigned < 0) {
        status = Rac1MobyAnimationStatus::InvalidGameplayHeader;
        return false;
    }
    const std::size_t offset = static_cast<std::size_t>(offsetSigned);
    if (offset == 0u) return true;
    if (!fits(offset, kInstanceBlockHeaderBytes, gameplay.size())) {
        status = Rac1MobyAnimationStatus::InvalidInstanceBlock;
        return false;
    }

    const std::int32_t countSigned = readI32(gameplay, offset);
    if (countSigned < 0 || static_cast<std::size_t>(countSigned) > kMaxCount) {
        status = Rac1MobyAnimationStatus::InvalidInstanceBlock;
        return false;
    }
    total = static_cast<std::size_t>(countSigned);
    std::size_t bytes = 0u;
    if (!checkedMul(total, kMobyInstanceBytes, bytes) ||
        !fits(offset + kInstanceBlockHeaderBytes, bytes, gameplay.size())) {
        status = Rac1MobyAnimationStatus::InvalidInstanceBlock;
        return false;
    }

    for (std::size_t i = 0u; i < total; ++i) {
        const std::size_t o = offset + kInstanceBlockHeaderBytes + i * kMobyInstanceBytes;
        ++counts[readI32(gameplay, o + 0x18u)];
    }
    return true;
}

bool classRelativePointerFits(std::span<const std::uint8_t> core,
                              std::size_t classBase,
                              std::int32_t relative,
                              std::size_t minimumBytes = 1u) noexcept {
    if (relative <= 0) return relative == 0;
    std::size_t absolute = 0u;
    return checkedAdd(classBase, static_cast<std::size_t>(relative), absolute) &&
           fits(absolute, minimumBytes, core.size());
}

bool inspectClass(std::span<const std::uint8_t> core,
                  const ClassEntry& entry,
                  std::size_t classEnd,
                  std::size_t instanceCount,
                  Rac1MobyAnimationClass& out,
                  Rac1MobyAnimationMetadataResult& result) {
    auto& status = result.status;
    out.oClass = entry.oClass;
    out.instanceCount = instanceCount;

    // Explicit class offset zero is the logic-only/no-class-data representation
    // already accounted for by the Phase 9 renderer. It has no model animation
    // payload to inspect.
    if (entry.offset == 0u) return true;

    const std::size_t base = static_cast<std::size_t>(entry.offset);
    if (classEnd <= base || classEnd > core.size()) {
        status = Rac1MobyAnimationStatus::InvalidClassHeader;
        return false;
    }
    out.classOffset = entry.offset;
    out.classEndOffset = static_cast<std::uint32_t>(classEnd);
    if (!fits(base, kMobyClassHeaderBytes, core.size())) {
        status = Rac1MobyAnimationStatus::InvalidClassHeader;
        return false;
    }

    const std::int32_t packetTableOffset = readI32(core, base + 0x00u);
    out.highLodPacketCount = core[base + 0x04u];
    out.lowLodPacketCount = core[base + 0x05u];
    out.jointCount = core[base + 0x08u];
    out.sequenceCount = core[base + 0x0cu];
    const std::int32_t skeletonOffset = readI32(core, base + 0x14u);
    const std::int32_t commonTransformOffset = readI32(core, base + 0x18u);
    const std::int32_t jointsOffset = readI32(core, base + 0x1cu);

    if (skeletonOffset < 0 || commonTransformOffset < 0 || jointsOffset < 0 ||
        !classRelativePointerFits(core, base, skeletonOffset) ||
        !classRelativePointerFits(core, base, commonTransformOffset) ||
        !classRelativePointerFits(core, base, jointsOffset)) {
        status = Rac1MobyAnimationStatus::InvalidClassHeader;
        return false;
    }
    out.skeletonOffset = static_cast<std::uint32_t>(skeletonOffset);
    out.commonTransformOffset = static_cast<std::uint32_t>(commonTransformOffset);
    out.jointsOffset = static_cast<std::uint32_t>(jointsOffset);

    // Wrench's documented moby file layout places the animation sequence
    // pointer array directly after the fixed 0x48-byte class header. The table
    // is indexed by sequence ID, not a packed list: retail classes may contain
    // zero entries for sequence IDs that simply do not exist. Preserve those
    // holes so later animation lookup keeps the game's original sequence IDs.
    std::size_t sequenceBytes = 0u;
    if (!checkedMul(static_cast<std::size_t>(out.sequenceCount), 4u, sequenceBytes) ||
        !fits(base + kMobyClassHeaderBytes, sequenceBytes, core.size())) {
        status = Rac1MobyAnimationStatus::InvalidSequenceTable;
        result.failureOClassValid = true;
        result.failureOClass = entry.oClass;
        result.failureSequenceTableOffset = static_cast<std::uint32_t>(kMobyClassHeaderBytes);
        return false;
    }
    out.sequenceOffsets.reserve(out.sequenceCount);
    for (std::size_t i = 0u; i < out.sequenceCount; ++i) {
        const std::size_t slot = base + kMobyClassHeaderBytes + i * 4u;
        const std::int32_t relative = readI32(core, slot);
        if (relative == 0) {
            out.sequenceOffsets.push_back(0u);
            ++out.nullSequenceCount;
            continue;
        }
        if (relative < 0 || !classRelativePointerFits(core, base, relative)) {
            status = Rac1MobyAnimationStatus::InvalidSequenceTable;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = relative;
            result.failureSequenceTableOffset = static_cast<std::uint32_t>(
                kMobyClassHeaderBytes + i * 4u);
            return false;
        }
        out.sequenceOffsets.push_back(static_cast<std::uint32_t>(relative));
        ++out.presentSequenceCount;
    }

    // Phase 10 Step 3 intentionally stops before interpreting the compressed
    // animation payload. Capture bounded authentic prefixes instead: one probe
    // per non-null sequence slot plus the three class rig blocks. This gives the
    // next step exact retail bytes for recovering the sequence/skeleton layout
    // without guessing field widths or compression rules.
    std::vector<std::uint32_t> uniqueSequenceOffsets;
    uniqueSequenceOffsets.reserve(out.presentSequenceCount);
    for (const std::uint32_t relative : out.sequenceOffsets) {
        if (relative != 0u) uniqueSequenceOffsets.push_back(relative);
    }
    std::sort(uniqueSequenceOffsets.begin(), uniqueSequenceOffsets.end());
    uniqueSequenceOffsets.erase(
        std::unique(uniqueSequenceOffsets.begin(), uniqueSequenceOffsets.end()),
        uniqueSequenceOffsets.end());
    out.uniqueSequencePayloadCount = uniqueSequenceOffsets.size();
    out.aliasedSequenceSlotCount =
        out.presentSequenceCount - out.uniqueSequencePayloadCount;

    const std::size_t classRelativeEnd = classEnd - base;
    std::vector<std::size_t> knownBoundaries;
    knownBoundaries.reserve(uniqueSequenceOffsets.size() + 5u);
    knownBoundaries.push_back(classRelativeEnd);
    for (const std::uint32_t relative : uniqueSequenceOffsets) {
        knownBoundaries.push_back(static_cast<std::size_t>(relative));
    }
    for (const std::int32_t relative : {skeletonOffset, commonTransformOffset, jointsOffset,
                                        packetTableOffset}) {
        if (relative > 0) knownBoundaries.push_back(static_cast<std::size_t>(relative));
    }
    std::sort(knownBoundaries.begin(), knownBoundaries.end());
    knownBoundaries.erase(
        std::unique(knownBoundaries.begin(), knownBoundaries.end()),
        knownBoundaries.end());

    const auto nextBoundaryAfter = [&](std::size_t relative) noexcept {
        const auto next = std::upper_bound(knownBoundaries.begin(), knownBoundaries.end(), relative);
        if (next != knownBoundaries.end() && *next <= classRelativeEnd) return *next;
        return classRelativeEnd;
    };

    out.sequenceProbes.reserve(out.presentSequenceCount);
    for (std::size_t i = 0u; i < out.sequenceOffsets.size(); ++i) {
        const std::uint32_t relative = out.sequenceOffsets[i];
        if (relative == 0u) continue;

        Rac1MobySequenceProbe probe{};
        probe.sequenceIndex = static_cast<std::uint8_t>(i);
        probe.offset = relative;
        probe.aliasCount = static_cast<std::size_t>(
            std::count(out.sequenceOffsets.begin(), out.sequenceOffsets.end(), relative));
        const std::size_t boundary = nextBoundaryAfter(static_cast<std::size_t>(relative));
        probe.nextBoundaryOffset = static_cast<std::uint32_t>(boundary);

        std::size_t absolute = 0u;
        if (!checkedAdd(base, static_cast<std::size_t>(relative), absolute)) {
            status = Rac1MobyAnimationStatus::InvalidSequenceTable;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = static_cast<std::int32_t>(relative);
            return false;
        }
        capturePrefix(core,
                      absolute,
                      base + boundary,
                      probe.prefix,
                      probe.prefixSize,
                      probe.prefixFnv1a);
        out.sequenceProbes.push_back(probe);
    }

    if (out.hasSkeleton()) {
        const std::array<std::pair<Rac1MobyRigProbeKind, std::uint32_t>, 3> rigOffsets = {{
            {Rac1MobyRigProbeKind::Skeleton, out.skeletonOffset},
            {Rac1MobyRigProbeKind::CommonTransforms, out.commonTransformOffset},
            {Rac1MobyRigProbeKind::Joints, out.jointsOffset},
        }};
        out.rigProbes.reserve(rigOffsets.size());
        for (const auto& [kind, relative] : rigOffsets) {
            Rac1MobyRigProbe probe{};
            probe.kind = kind;
            probe.offset = relative;
            const std::size_t boundary = nextBoundaryAfter(static_cast<std::size_t>(relative));
            probe.nextBoundaryOffset = static_cast<std::uint32_t>(boundary);
            std::size_t absolute = 0u;
            if (checkedAdd(base, static_cast<std::size_t>(relative), absolute)) {
                capturePrefix(core,
                              absolute,
                              base + boundary,
                              probe.prefix,
                              probe.prefixSize,
                              probe.prefixFnv1a);
            }
            out.rigProbes.push_back(probe);
        }
    }

    // Phase 10 Step 4: decode the sequence header and complete class-relative
    // frame-pointer table. The Step 3 retail probe established this layout on
    // all 40 present sequences: +0x10 is a non-zero frame count, +0x11 is 0xff
    // or a value below frameCount, and frameCount u32 class-relative pointers
    // begin at +0x1c. Keep the other header fields raw until their runtime
    // semantics are proven by pose evaluation.
    out.sequenceLayouts.reserve(out.presentSequenceCount);
    for (std::size_t i = 0u; i < out.sequenceOffsets.size(); ++i) {
        const std::uint32_t relative = out.sequenceOffsets[i];
        if (relative == 0u) continue;

        std::size_t absolute = 0u;
        if (!checkedAdd(base, static_cast<std::size_t>(relative), absolute) ||
            !fits(absolute, kMobySequenceHeaderBytes, core.size())) {
            status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = static_cast<std::int32_t>(relative);
            result.failureSequenceReason = Rac1MobySequenceFailure::HeaderOutOfRange;
            return false;
        }

        Rac1MobySequenceLayout layout{};
        layout.sequenceIndex = static_cast<std::uint8_t>(i);
        layout.offset = relative;
        for (std::size_t c = 0u; c < layout.headerVec4.size(); ++c) {
            layout.headerVec4[c] = readF32(core, absolute + c * 4u);
        }
        layout.frameCount = core[absolute + 0x10u];
        layout.headerByte11 = core[absolute + 0x11u];
        layout.controlByte = core[absolute + 0x12u];
        layout.reservedByte = core[absolute + 0x13u];
        layout.reservedWord = readU32(core, absolute + 0x14u);
        layout.headerScalar = readF32(core, absolute + 0x18u);

        if (layout.frameCount == 0u) {
            status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = static_cast<std::int32_t>(relative);
            result.failureSequenceReason = Rac1MobySequenceFailure::ZeroFrameCount;
            return false;
        }
        if (layout.headerByte11 != 0xffu && layout.headerByte11 >= layout.frameCount) {
            status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = static_cast<std::int32_t>(relative);
            result.failureSequenceReason = Rac1MobySequenceFailure::HeaderByte11OutOfRange;
            return false;
        }

        std::size_t frameTableBytes = 0u;
        std::size_t frameTableAbsolute = 0u;
        std::size_t frameTableEndAbsolute = 0u;
        std::size_t frameTableEndRelative = 0u;
        if (!checkedMul(static_cast<std::size_t>(layout.frameCount), 4u, frameTableBytes) ||
            !checkedAdd(absolute, kMobySequenceHeaderBytes, frameTableAbsolute) ||
            !checkedAdd(frameTableAbsolute, frameTableBytes, frameTableEndAbsolute) ||
            !checkedAdd(static_cast<std::size_t>(relative), kMobySequenceHeaderBytes,
                        frameTableEndRelative) ||
            !checkedAdd(frameTableEndRelative, frameTableBytes, frameTableEndRelative) ||
            !fits(frameTableAbsolute, frameTableBytes, core.size()) ||
            frameTableEndAbsolute > classEnd) {
            status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequenceRelative = static_cast<std::int32_t>(relative);
            result.failureSequenceReason = Rac1MobySequenceFailure::FrameTableOutOfRange;
            return false;
        }

        layout.frameOffsets.reserve(layout.frameCount);
        std::uint32_t previous = 0u;
        for (std::size_t frame = 0u; frame < layout.frameCount; ++frame) {
            const std::uint32_t frameRelative = readU32(core, frameTableAbsolute + frame * 4u);
            auto failFrame = [&](Rac1MobySequenceFailure reason) {
                status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
                result.failureOClassValid = true;
                result.failureOClass = entry.oClass;
                result.failureSequenceIndex = static_cast<std::int32_t>(i);
                result.failureSequenceRelative = static_cast<std::int32_t>(relative);
                result.failureSequenceReason = reason;
                result.failureFrameIndex = static_cast<std::int32_t>(frame);
                result.failureFrameOffset = frameRelative;
                return false;
            };
            if (frameRelative == 0u) return failFrame(Rac1MobySequenceFailure::FramePointerZero);
            if (static_cast<std::size_t>(frameRelative) < frameTableEndRelative) {
                return failFrame(Rac1MobySequenceFailure::FramePointerBeforeTableEnd);
            }
            if ((frameRelative & 0x0fu) != 0u) {
                return failFrame(Rac1MobySequenceFailure::FramePointerMisaligned);
            }
            std::size_t frameAbsolute = 0u;
            if (!checkedAdd(base, static_cast<std::size_t>(frameRelative), frameAbsolute) ||
                frameAbsolute >= classEnd || frameAbsolute >= core.size()) {
                return failFrame(Rac1MobySequenceFailure::FramePointerOutOfRange);
            }
            if (frame != 0u && frameRelative <= previous) {
                return failFrame(Rac1MobySequenceFailure::FramePointersNotIncreasing);
            }
            if (frame != 0u) {
                const std::uint32_t stride = frameRelative - previous;
                if (layout.minFrameStride == 0u || stride < layout.minFrameStride) {
                    layout.minFrameStride = stride;
                }
                layout.maxFrameStride = std::max(layout.maxFrameStride, stride);
            }
            layout.frameOffsets.push_back(frameRelative);
            previous = frameRelative;
        }
        layout.firstFrameOffset = layout.frameOffsets.front();
        layout.lastFrameOffset = layout.frameOffsets.back();

        // Step 5: capture the exact payload for one representative frame of
        // every observed non-final stride in this sequence. The frame-pointer
        // table gives an exact byte range for frames [0, n-2]; the final
        // frame has no proven end yet, so it is deliberately not guessed.
        for (std::size_t frame = 0u; frame + 1u < layout.frameOffsets.size(); ++frame) {
            const std::uint32_t frameOffset = layout.frameOffsets[frame];
            const std::uint32_t nextOffset = layout.frameOffsets[frame + 1u];
            const std::uint32_t stride = nextOffset - frameOffset;
            if (std::find(layout.uniqueFrameStrides.begin(),
                          layout.uniqueFrameStrides.end(), stride) !=
                layout.uniqueFrameStrides.end()) {
                continue;
            }
            layout.uniqueFrameStrides.push_back(stride);

            if (static_cast<std::size_t>(stride) > kRac1MobyFrameProbeMaxBytes) {
                ++out.oversizedFrameProbeCount;
                continue;
            }

            std::size_t frameAbsolute = 0u;
            if (!checkedAdd(base, static_cast<std::size_t>(frameOffset), frameAbsolute) ||
                !fits(frameAbsolute, static_cast<std::size_t>(stride), classEnd) ||
                !fits(frameAbsolute, static_cast<std::size_t>(stride), core.size())) {
                status = Rac1MobyAnimationStatus::InvalidSequenceLayout;
                result.failureOClassValid = true;
                result.failureOClass = entry.oClass;
                result.failureSequenceIndex = static_cast<std::int32_t>(i);
                result.failureSequenceRelative = static_cast<std::int32_t>(relative);
                result.failureSequenceReason = Rac1MobySequenceFailure::FramePointerOutOfRange;
                result.failureFrameIndex = static_cast<std::int32_t>(frame);
                result.failureFrameOffset = frameOffset;
                return false;
            }

            Rac1MobyFrameProbe probe{};
            probe.sequenceIndex = static_cast<std::uint8_t>(i);
            probe.frameIndex = static_cast<std::uint8_t>(frame);
            probe.offset = frameOffset;
            probe.stride = stride;
            probe.payload.assign(core.begin() + static_cast<std::ptrdiff_t>(frameAbsolute),
                                 core.begin() + static_cast<std::ptrdiff_t>(frameAbsolute + stride));
            probe.payloadFnv1a = fnv1a32(probe.payload);
            layout.frameProbes.push_back(std::move(probe));
        }
        std::sort(layout.uniqueFrameStrides.begin(), layout.uniqueFrameStrides.end());

        out.totalFrameCount += layout.frameCount;
        if (layout.headerByte11 != 0xffu) ++out.nonFfHeaderByte11Count;
        out.sequenceLayouts.push_back(std::move(layout));
    }

    // Step 3 exposed an exact relation across all 11 skeletal retail classes:
    // skeleton..common is jointCount * 0x40 and common..joints is jointCount *
    // 0x10. Decode those regions now. The compact program beginning at joints
    // remains intentionally raw/probed; assigning hierarchy semantics to it
    // without the pose evaluator would be guesswork.
    if (out.hasSkeleton()) {
        std::size_t skeletonBytes = 0u;
        std::size_t commonBytes = 0u;
        std::size_t expectedCommon = 0u;
        std::size_t expectedJoints = 0u;
        if (!checkedMul(static_cast<std::size_t>(out.jointCount),
                        kMobySkeletonMatrixBytes, skeletonBytes) ||
            !checkedMul(static_cast<std::size_t>(out.jointCount),
                        kMobyCommonTransformBytes, commonBytes) ||
            !checkedAdd(static_cast<std::size_t>(out.skeletonOffset), skeletonBytes,
                        expectedCommon) ||
            expectedCommon != static_cast<std::size_t>(out.commonTransformOffset)) {
            status = Rac1MobyAnimationStatus::InvalidRigLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureRigReason = Rac1MobyRigFailure::SkeletonRangeMismatch;
            result.failureRigExpectedOffset = static_cast<std::uint32_t>(expectedCommon);
            result.failureRigActualOffset = out.commonTransformOffset;
            return false;
        }
        if (!checkedAdd(static_cast<std::size_t>(out.commonTransformOffset), commonBytes,
                        expectedJoints) ||
            expectedJoints != static_cast<std::size_t>(out.jointsOffset)) {
            status = Rac1MobyAnimationStatus::InvalidRigLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureRigReason = Rac1MobyRigFailure::CommonTransformRangeMismatch;
            result.failureRigExpectedOffset = static_cast<std::uint32_t>(expectedJoints);
            result.failureRigActualOffset = out.jointsOffset;
            return false;
        }

        std::size_t skeletonAbsolute = 0u;
        std::size_t commonAbsolute = 0u;
        if (!checkedAdd(base, static_cast<std::size_t>(out.skeletonOffset), skeletonAbsolute) ||
            !checkedAdd(base, static_cast<std::size_t>(out.commonTransformOffset), commonAbsolute) ||
            !fits(skeletonAbsolute, skeletonBytes, classEnd) ||
            !fits(commonAbsolute, commonBytes, classEnd)) {
            status = Rac1MobyAnimationStatus::InvalidRigLayout;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureRigReason = Rac1MobyRigFailure::SkeletonRangeMismatch;
            return false;
        }

        out.skeletonMatrices.resize(out.jointCount);
        out.commonTransformWords.resize(out.jointCount);
        for (std::size_t joint = 0u; joint < out.jointCount; ++joint) {
            for (std::size_t word = 0u; word < 16u; ++word) {
                out.skeletonMatrices[joint][word] =
                    readF32(core, skeletonAbsolute + joint * 0x40u + word * 4u);
            }
            for (std::size_t word = 0u; word < 4u; ++word) {
                out.commonTransformWords[joint][word] =
                    readU32(core, commonAbsolute + joint * 0x10u + word * 4u);
            }
        }
    }

    if (packetTableOffset == 0) {
        if (out.highLodPacketCount != 0u || out.lowLodPacketCount != 0u) {
            status = Rac1MobyAnimationStatus::InvalidPacketTable;
            return false;
        }
        return true;
    }
    if (packetTableOffset < 0) {
        status = Rac1MobyAnimationStatus::InvalidPacketTable;
        return false;
    }

    std::size_t packetTable = 0u;
    std::size_t packetBytes = 0u;
    if (!checkedAdd(base, static_cast<std::size_t>(packetTableOffset), packetTable) ||
        !checkedMul(static_cast<std::size_t>(out.highLodPacketCount),
                    kMobyPacketHeaderBytes,
                    packetBytes) ||
        !fits(packetTable, packetBytes, core.size())) {
        status = Rac1MobyAnimationStatus::InvalidPacketTable;
        return false;
    }

    out.skinningPackets.clear();
    out.skinningPackets.reserve(out.highLodPacketCount);
    for (std::size_t p = 0u; p < out.highLodPacketCount; ++p) {
        out.skinningPackets.emplace_back();
        auto& packetProgram = out.skinningPackets.back();
        const std::size_t packet = packetTable + p * kMobyPacketHeaderBytes;
        const std::int32_t vertexOffset = readI32(core, packet + 0x08u);
        const std::uint8_t packetTransferVertexCount = core[packet + 0x0fu];
        if (vertexOffset < 0) {
            status = Rac1MobyAnimationStatus::InvalidVertexTable;
            return false;
        }

        std::size_t vertexTable = 0u;
        if (!checkedAdd(base, static_cast<std::size_t>(vertexOffset), vertexTable) ||
            !fits(vertexTable, kMobyVertexTableHeaderBytes, core.size())) {
            status = Rac1MobyAnimationStatus::InvalidVertexTable;
            return false;
        }

        const std::array<std::int32_t, 6> counts = {
            readI32(core, vertexTable + 0x00u), // matrix transfers
            readI32(core, vertexTable + 0x04u), // two-way blends
            readI32(core, vertexTable + 0x08u), // three-way blends
            readI32(core, vertexTable + 0x0cu), // main/no-blend vertices
            readI32(core, vertexTable + 0x10u), // duplicate vertices
            readI32(core, vertexTable + 0x14u), // transfer vertices
        };
        for (const std::int32_t count : counts) {
            if (count < 0 || static_cast<std::size_t>(count) > kMaxCount) {
                status = Rac1MobyAnimationStatus::InvalidVertexTable;
                return false;
            }
        }
        if (static_cast<std::uint32_t>(counts[5]) != packetTransferVertexCount) {
            status = Rac1MobyAnimationStatus::InvalidVertexTable;
            return false;
        }

        const std::size_t matrixTransfers = static_cast<std::size_t>(counts[0]);
        const std::size_t blend2 = static_cast<std::size_t>(counts[1]);
        const std::size_t blend3 = static_cast<std::size_t>(counts[2]);
        const std::size_t main = static_cast<std::size_t>(counts[3]);
        out.matrixTransferCount += matrixTransfers;
        out.twoWayBlendVertexCount += blend2;
        out.threeWayBlendVertexCount += blend3;
        out.mainVertexCount += main;

        std::size_t transferBytes = 0u;
        const std::size_t transferBase = vertexTable + kMobyVertexTableHeaderBytes;
        if (!checkedMul(matrixTransfers, kMobyMatrixTransferBytes, transferBytes) ||
            !fits(transferBase, transferBytes, core.size())) {
            status = Rac1MobyAnimationStatus::InvalidMatrixTransfer;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failurePacketIndex = static_cast<std::int32_t>(p);
            result.failureMatrixReason = Rac1MobyMatrixTransferFailure::TableOutOfRange;
            return false;
        }
        for (std::size_t i = 0u; i < matrixTransfers; ++i) {
            const std::size_t transfer = transferBase + i * kMobyMatrixTransferBytes;
            const std::uint8_t scratchpadMatrixIndex = core[transfer + 0u];
            const std::uint8_t vu0Destination = core[transfer + 1u];

            // The recovered renderer treats this source byte as an SPR joint/matrix
            // index. Rigid classes with jointCount == 0 can still transfer the
            // renderer's default slot 0, so do not globally compare it to jointCount
            // here. Step 7 performs that domain proof only for skeletal mesh classes.
            //
            // The destination, however, is a VU0 qword address for a 4-qword matrix.
            // Keep the strict matrix-slot requirements: it must fit in the 256-qword
            // VU0 data memory and begin on a four-qword boundary.
            if (vu0Destination > 0xfcu) {
                status = Rac1MobyAnimationStatus::InvalidMatrixTransfer;
                result.failureOClassValid = true;
                result.failureOClass = entry.oClass;
                result.failurePacketIndex = static_cast<std::int32_t>(p);
                result.failureMatrixTransferIndex = static_cast<std::int32_t>(i);
                result.failureMatrixScratchpadIndex =
                    static_cast<std::int32_t>(scratchpadMatrixIndex);
                result.failureMatrixVu0Destination = static_cast<std::int32_t>(vu0Destination);
                result.failureMatrixReason = Rac1MobyMatrixTransferFailure::DestinationOverflow;
                return false;
            }
            if ((vu0Destination & 0x3u) != 0u) {
                status = Rac1MobyAnimationStatus::InvalidMatrixTransfer;
                result.failureOClassValid = true;
                result.failureOClass = entry.oClass;
                result.failurePacketIndex = static_cast<std::int32_t>(p);
                result.failureMatrixTransferIndex = static_cast<std::int32_t>(i);
                result.failureMatrixScratchpadIndex =
                    static_cast<std::int32_t>(scratchpadMatrixIndex);
                result.failureMatrixVu0Destination = static_cast<std::int32_t>(vu0Destination);
                result.failureMatrixReason = Rac1MobyMatrixTransferFailure::DestinationMisaligned;
                return false;
            }
            out.maxScratchpadMatrixIndex =
                std::max(out.maxScratchpadMatrixIndex, scratchpadMatrixIndex);
            out.maxVu0Destination = std::max(out.maxVu0Destination, vu0Destination);
            packetProgram.matrixTransfers.push_back({scratchpadMatrixIndex, vu0Destination});
        }

        // Decode the packed 16-byte R&C1 skinning records as a structural
        // program. Wrench's header and blending pseudocode establish the block
        // order unambiguously: the two-way records come first, then three-way,
        // then main/no-blend. The bitfield table in the prose has its two blend
        // row labels swapped, so validate the fields against the pseudocode and
        // the authentic VU0 matrix-address invariant rather than copying those
        // labels literally.
        const std::int32_t packedVertexOffsetSigned = readI32(core, vertexTable + 0x18u);
        const std::int32_t packedVertexEndSigned = readI32(core, vertexTable + 0x1cu);
        const std::uint8_t packetVertexDataQwords = core[packet + 0x0cu];
        if (packedVertexOffsetSigned < 0 || packedVertexEndSigned < 0 ||
            (static_cast<std::uint32_t>(packedVertexOffsetSigned) & 0x0fu) != 0u ||
            (static_cast<std::uint32_t>(packedVertexEndSigned) & 0x0fu) != 0u ||
            static_cast<std::size_t>(packedVertexOffsetSigned) / 0x10u >
                static_cast<std::size_t>(packetVertexDataQwords)) {
            status = Rac1MobyAnimationStatus::InvalidSkinningProgram;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSkinningPacketIndex = static_cast<std::int32_t>(p);
            result.failureSkinningReason = Rac1MobySkinningFailure::VertexDataOutOfRange;
            return false;
        }

        std::size_t packedVertexCount = 0u;
        if (!checkedAdd(blend2, blend3, packedVertexCount) ||
            !checkedAdd(packedVertexCount, main, packedVertexCount)) {
            status = Rac1MobyAnimationStatus::InvalidSkinningProgram;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSkinningPacketIndex = static_cast<std::int32_t>(p);
            result.failureSkinningReason = Rac1MobySkinningFailure::VertexDataOutOfRange;
            return false;
        }
        std::size_t packedVertexBytes = 0u;
        std::size_t packedVertexBase = 0u;
        std::size_t packedVertexEnd = 0u;
        if (!checkedMul(packedVertexCount, 0x10u, packedVertexBytes) ||
            !checkedAdd(vertexTable, static_cast<std::size_t>(packedVertexOffsetSigned),
                        packedVertexBase) ||
            !checkedAdd(static_cast<std::size_t>(packedVertexOffsetSigned), packedVertexBytes,
                        packedVertexEnd) ||
            packedVertexEnd > static_cast<std::size_t>(packedVertexEndSigned) ||
            !fits(packedVertexBase, packedVertexBytes, core.size())) {
            status = Rac1MobyAnimationStatus::InvalidSkinningProgram;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSkinningPacketIndex = static_cast<std::int32_t>(p);
            result.failureSkinningReason = Rac1MobySkinningFailure::VertexDataOutOfRange;
            return false;
        }

        auto rejectExpectedAddress = [&](std::size_t vertexIndex,
                                         Rac1MobySkinningVertexKind kind,
                                         std::uint8_t address) -> bool {
            if (validVu0MatrixAddress(address)) {
                out.maxPackedVu0Address = std::max(out.maxPackedVu0Address, address);
                return false;
            }
            ++out.documentedLayoutInvalidAddresses;
            status = Rac1MobyAnimationStatus::InvalidSkinningProgram;
            result.failureOClassValid = true;
            result.failureOClass = entry.oClass;
            result.failureSkinningPacketIndex = static_cast<std::int32_t>(p);
            result.failureSkinningVertexIndex = static_cast<std::int32_t>(vertexIndex);
            result.failureSkinningAddress = static_cast<std::int32_t>(address);
            result.failureSkinningVertexKind = kind;
            result.failureSkinningReason = address > 0xfcu
                ? Rac1MobySkinningFailure::MatrixAddressOverflow
                : Rac1MobySkinningFailure::MatrixAddressMisaligned;
            return true;
        };

        out.skinningVertexCount += packedVertexCount;
        packetProgram.vertices.reserve(packedVertexCount);
        for (std::size_t i = 0u; i < packedVertexCount; ++i) {
            const std::size_t vertex = packedVertexBase + i * 0x10u;
            const std::uint16_t packedIdJoint = readU16(core, vertex);
            const std::uint8_t low7 = packedLow7(core, vertex);
            const std::uint8_t b2 = core[vertex + 0x02u];
            const std::uint8_t b3 = core[vertex + 0x03u];
            const std::uint8_t b4 = core[vertex + 0x04u];
            const std::uint8_t b5 = core[vertex + 0x05u];
            const std::uint8_t b6 = core[vertex + 0x06u];
            const std::uint8_t b7 = core[vertex + 0x07u];
            Rac1MobySkinningVertexProgram programVertex{};
            programVertex.id = static_cast<std::uint16_t>(packedIdJoint & 0x01ffu);
            programVertex.position = {readI16(core, vertex + 0x0au),
                                      readI16(core, vertex + 0x0cu),
                                      readI16(core, vertex + 0x0eu)};

            if (i < blend2) {
                programVertex.kind = Rac1MobySkinningVertexKind::TwoWay;
                programVertex.directJointIndex = low7;
                programVertex.l1 = b2;
                programVertex.l2 = b3;
                programVertex.w1 = b4;
                programVertex.w2 = b5;
                programVertex.directStore = b6;
                programVertex.blendStore = b7;
                out.maxFirstBlendLow7 = std::max(out.maxFirstBlendLow7, low7);

                // Two-way layout from the recovered pseudocode:
                // ID:9, JI:7, L1, L2, W1, W2, ST, SB, ...
                // JI is a direct SPR joint/matrix source. L1/L2/ST/SB are VU0 matrix slots.
                for (const std::uint8_t address : {b2, b3, b6, b7}) {
                    if (rejectExpectedAddress(i, Rac1MobySkinningVertexKind::TwoWay, address)) {
                        return false;
                    }
                }

                // Diagnostic only: score the contradictory/swapped table-label
                // interpretation. We never choose semantics heuristically from it.
                out.swappedLayoutInvalidAddresses +=
                    invalidAddressCount({low7, b2, b3, b7});
            } else if (i < blend2 + blend3) {
                programVertex.kind = Rac1MobySkinningVertexKind::ThreeWay;
                programVertex.l3 = static_cast<std::uint8_t>(decodePackedL3(low7));
                programVertex.l1 = b2;
                programVertex.l2 = b3;
                programVertex.w1 = b4;
                programVertex.w2 = b5;
                programVertex.w3 = b6;
                programVertex.blendStore = b7;
                out.maxSecondBlendLow7 = std::max(out.maxSecondBlendLow7, low7);

                // Three-way layout:
                // ID:9, L3:7, L1, L2, W1, W2, W3, SB, ...
                // L1/L2/SB are full-byte VU0 qword addresses. L3 shares the
                // first u16 with the 9-bit vertex ID: ID bit 8 is byte1 bit 0
                // and L3 occupies byte1 bits 1..7. Therefore the stored low7
                // value is the address with its guaranteed low zero bit omitted
                // and the exact reconstruction is low7 << 1.
                for (const std::uint8_t address : {b2, b3, b7}) {
                    if (rejectExpectedAddress(i, Rac1MobySkinningVertexKind::ThreeWay, address)) {
                        return false;
                    }
                }
                out.maxPackedL3Raw = std::max(out.maxPackedL3Raw, low7);
                const std::uint16_t decodedL3 = decodePackedL3(low7);
                if (decodedL3 > 0xfcu || (decodedL3 & 0x3u) != 0u) {
                    ++out.documentedLayoutInvalidAddresses;
                    status = Rac1MobyAnimationStatus::InvalidSkinningProgram;
                    result.failureOClassValid = true;
                    result.failureOClass = entry.oClass;
                    result.failureSkinningPacketIndex = static_cast<std::int32_t>(p);
                    result.failureSkinningVertexIndex = static_cast<std::int32_t>(i);
                    result.failureSkinningAddress = static_cast<std::int32_t>(decodedL3);
                    result.failureSkinningVertexKind = Rac1MobySkinningVertexKind::ThreeWay;
                    result.failureSkinningReason = decodedL3 > 0xfcu
                        ? Rac1MobySkinningFailure::MatrixAddressOverflow
                        : Rac1MobySkinningFailure::MatrixAddressMisaligned;
                    return false;
                }
                out.maxPackedVu0Address = std::max(
                    out.maxPackedVu0Address, static_cast<std::uint8_t>(decodedL3));

                // Diagnostic only: score the swapped interpretation.
                out.swappedLayoutInvalidAddresses +=
                    invalidAddressCount({b2, b3, b6, b7});
            } else {
                programVertex.kind = Rac1MobySkinningVertexKind::Main;
                programVertex.directJointIndex = low7;
                programVertex.l1 = b2;
                programVertex.directStore = b3;
                // Main/no-blend layout:
                // ID:9, JI:7, L1, ST, U, U, U, U, ...
                // JI is a direct SPR joint/matrix source; L1/ST are VU0 matrix slots.
                out.maxMainScratchpadMatrixIndex =
                    std::max(out.maxMainScratchpadMatrixIndex, low7);
                for (const std::uint8_t address : {b2, b3}) {
                    if (rejectExpectedAddress(i, Rac1MobySkinningVertexKind::Main, address)) {
                        return false;
                    }
                }
            }
            packetProgram.vertices.push_back(programVertex);
        }
    }

    return true;
}

std::array<float, 16> identityMatrix() noexcept {
    return {1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f};
}

std::array<float, 16> quaternionTranslationMatrix(float x,
                                                   float y,
                                                   float z,
                                                   float w,
                                                   float tx,
                                                   float ty,
                                                   float tz) noexcept {
    // FUN_002109b8 forms the same affine basis from the signed-16 quaternion.
    // Keep column-major storage to match its four consecutive 0x10-byte qwords.
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float xw = x * w, yw = y * w, zw = z * w;
    // Retail 0x211308..0x21136c builds these three basis vectors directly.
    // They are the matrix *columns*. The sign pattern is therefore not the
    // transposed textbook form; preserving it exactly is critical before any
    // vertex deformation is attempted.
    return {
        1.0f - 2.0f * (yy + zz), 2.0f * (xy - zw),        2.0f * (xz + yw),        0.0f,
        2.0f * (xy + zw),        1.0f - 2.0f * (xx + zz), 2.0f * (yz - xw),        0.0f,
        2.0f * (xz - yw),        2.0f * (yz + xw),        1.0f - 2.0f * (xx + yy), 0.0f,
        tx,                       ty,                       tz,                       1.0f,
    };
}

std::array<float, 16> multiplyAffine(const std::array<float, 16>& a,
                                     const std::array<float, 16>& b) noexcept {
    std::array<float, 16> out{};
    for (std::size_t c = 0u; c < 4u; ++c) {
        for (std::size_t r = 0u; r < 4u; ++r) {
            float value = 0.0f;
            for (std::size_t k = 0u; k < 4u; ++k) {
                value += a[k * 4u + r] * b[c * 4u + k];
            }
            out[c * 4u + r] = value;
        }
    }
    return out;
}

std::array<float, 16> retailSkeletonPostMatrix(
    const std::array<float, 16>& raw) noexcept {
    // Retail sub_0020E0E0 DMA-copies class/work +0x1c to SPR +0x1c00 and at
    // 0x20ed28..0x20ed8c post-multiplies every pose matrix by one 0x40-byte
    // record. Each qword is consumed as one matrix column, but only xyz are
    // ever used as coefficients; the record's four w lanes are never read by
    // the VU multiply. Force the affine lanes explicitly instead of treating
    // those auxiliary words as floats/matrix data.
    return {
        raw[0],  raw[1],  raw[2],  0.0f,
        raw[4],  raw[5],  raw[6],  0.0f,
        raw[8],  raw[9],  raw[10], 0.0f,
        raw[12], raw[13], raw[14], 1.0f,
    };
}

bool finiteMatrix(const std::array<float, 16>& m) noexcept {
    return std::all_of(m.begin(), m.end(), [](float v) { return std::isfinite(v); });
}

} // namespace

Rac1MobyAnimationMetadataResult inspectRac1MobyAnimationMetadata(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange mobyClasses) {
    Rac1MobyAnimationMetadataResult result{};

    std::unordered_map<std::int32_t, ClassEntry> entries;
    if (!parseClassEntries(coreIndex, mobyClasses, entries)) {
        result.status = Rac1MobyAnimationStatus::InvalidIndexTable;
        return result;
    }

    std::unordered_map<std::int32_t, std::size_t> instances;
    if (!parseInstanceCounts(gameplay, instances, result.metadata.instanceCount, result.status)) {
        return result;
    }
    if (instances.empty()) {
        result.status = Rac1MobyAnimationStatus::EmptyScene;
        return result;
    }

    std::vector<std::int32_t> referenced;
    referenced.reserve(instances.size());
    for (const auto& [oClass, count] : instances) {
        (void)count;
        referenced.push_back(oClass);
    }
    std::sort(referenced.begin(), referenced.end());

    result.metadata.referencedClassCount = referenced.size();
    result.metadata.classes.reserve(referenced.size());
    std::unordered_set<std::uint32_t> globalFrameStrides;
    for (const std::int32_t oClass : referenced) {
        const auto found = entries.find(oClass);
        if (found == entries.end()) {
            result.status = Rac1MobyAnimationStatus::MissingReferencedClass;
            result.failureOClassValid = true;
            result.failureOClass = oClass;
            return result;
        }

        Rac1MobyAnimationClass cls{};
        const std::size_t classEnd = findClassEnd(entries, found->second, core.size());
        if (!inspectClass(core, found->second, classEnd, instances[oClass], cls, result)) {
            return result;
        }

        if (cls.hasMesh()) ++result.metadata.renderableClassCount;
        if (cls.hasSkeleton()) {
            ++result.metadata.skeletalClassCount;
            result.metadata.skeletalInstanceCount += cls.instanceCount;
        }
        result.metadata.sequenceSlotCount += cls.sequenceCount;
        result.metadata.presentSequenceCount += cls.presentSequenceCount;
        result.metadata.nullSequenceCount += cls.nullSequenceCount;
        result.metadata.uniqueSequencePayloadCount += cls.uniqueSequencePayloadCount;
        result.metadata.aliasedSequenceSlotCount += cls.aliasedSequenceSlotCount;
        result.metadata.decodedSequenceCount += cls.sequenceLayouts.size();
        result.metadata.totalFrameCount += cls.totalFrameCount;
        result.metadata.nonFfHeaderByte11Count += cls.nonFfHeaderByte11Count;
        result.metadata.skeletonMatrixCount += cls.skeletonMatrices.size();
        result.metadata.commonTransformRecordCount += cls.commonTransformWords.size();
        result.metadata.oversizedFrameProbeCount += cls.oversizedFrameProbeCount;
        for (const auto& layout : cls.sequenceLayouts) {
            if (layout.minFrameStride != 0u &&
                (result.metadata.minFrameStride == 0u ||
                 layout.minFrameStride < result.metadata.minFrameStride)) {
                result.metadata.minFrameStride = layout.minFrameStride;
            }
            result.metadata.maxFrameStride =
                std::max(result.metadata.maxFrameStride, layout.maxFrameStride);
            for (const std::uint32_t stride : layout.uniqueFrameStrides) {
                globalFrameStrides.insert(stride);
            }
            result.metadata.frameProbeCount += layout.frameProbes.size();
            for (const auto& probe : layout.frameProbes) {
                result.metadata.maxProbedFrameBytes =
                    std::max(result.metadata.maxProbedFrameBytes, probe.payload.size());
            }
        }
        if (cls.hasSequences()) {
            ++result.metadata.sequencedClassCount;
            result.metadata.sequencedInstanceCount += cls.instanceCount;
        }
        result.metadata.packetCount += cls.highLodPacketCount;
        result.metadata.matrixTransferCount += cls.matrixTransferCount;
        result.metadata.twoWayBlendVertexCount += cls.twoWayBlendVertexCount;
        result.metadata.threeWayBlendVertexCount += cls.threeWayBlendVertexCount;
        result.metadata.mainVertexCount += cls.mainVertexCount;
        result.metadata.skinningVertexCount += cls.skinningVertexCount;
        result.metadata.documentedLayoutInvalidAddresses +=
            cls.documentedLayoutInvalidAddresses;
        result.metadata.swappedLayoutInvalidAddresses +=
            cls.swappedLayoutInvalidAddresses;
        result.metadata.maxPackedL3Raw =
            std::max(result.metadata.maxPackedL3Raw, cls.maxPackedL3Raw);
        result.metadata.classes.push_back(std::move(cls));
    }

    result.metadata.uniqueFrameStrideCount = globalFrameStrides.size();

    const std::size_t blendVertices =
        result.metadata.twoWayBlendVertexCount + result.metadata.threeWayBlendVertexCount;
    result.metadata.blendLayout = blendVertices == 0u
        ? Rac1MobyBlendLayout::NotApplicable
        : Rac1MobyBlendLayout::TwoWayThenThreeWay;

    if (result.metadata.threeWayBlendVertexCount == 0u) {
        result.metadata.packedL3Encoding = Rac1MobyPackedL3Encoding::NotApplicable;
    } else {
        // L3's seven stored bits are bits 9..15 of the first packed u16.
        // Reconstruct the one implicit low zero bit deterministically; do not
        // infer a scale from whichever values happen to occur in one level.
        result.metadata.packedL3Encoding =
            Rac1MobyPackedL3Encoding::ShiftLeft1QwordAddress;
        result.metadata.maxDecodedPackedL3Address = static_cast<std::uint8_t>(
            decodePackedL3(result.metadata.maxPackedL3Raw));
    }

    result.status = Rac1MobyAnimationStatus::Ok;
    return result;
}

Rac1RatchetAnimationBankResult inspectRac1RatchetAnimationBank(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::uint32_t sequenceTableOffset,
    const Rac1MobyAnimationClass& ratchetClass) {
    Rac1RatchetAnimationBankResult result{};
    result.bank.sequenceTableOffset = sequenceTableOffset;
    result.bank.animationClass = ratchetClass;
    auto& out = result.bank.animationClass;

    if (ratchetClass.oClass != 0) {
        result.status = Rac1RatchetAnimationBankStatus::WrongClass;
        return result;
    }
    if (!ratchetClass.hasSkeleton()) {
        result.status = Rac1RatchetAnimationBankStatus::MissingSkeleton;
        return result;
    }
    if (ratchetClass.sequenceCount == 0u ||
        ratchetClass.sequenceOffsets.size() != ratchetClass.sequenceCount) {
        result.status = Rac1RatchetAnimationBankStatus::InvalidSequenceTable;
        return result;
    }

    // The retail oClass-0 class record advertises 134 sequence IDs but all 134
    // class-local +0x48 pointers are zero. Step 12 proved that this is not an
    // absent animation set: LevelCoreHeader +0x78 selects the external table in
    // coreIndex instead. Reject a mixed local/external representation rather
    // than guessing which one takes precedence.
    if (std::any_of(ratchetClass.sequenceOffsets.begin(),
                    ratchetClass.sequenceOffsets.end(),
                    [](std::uint32_t offset) { return offset != 0u; })) {
        result.status = Rac1RatchetAnimationBankStatus::LocalSequenceTableNotEmpty;
        return result;
    }

    std::size_t tableBytes = 0u;
    if (!checkedMul(static_cast<std::size_t>(ratchetClass.sequenceCount), 4u, tableBytes) ||
        sequenceTableOffset == 0u ||
        !fits(static_cast<std::size_t>(sequenceTableOffset), tableBytes, coreIndex.size())) {
        result.status = Rac1RatchetAnimationBankStatus::InvalidSequenceTable;
        return result;
    }

    std::vector<std::uint32_t> absoluteSequenceOffsets;
    absoluteSequenceOffsets.reserve(ratchetClass.sequenceCount);
    std::uint32_t previous = 0u;
    for (std::size_t i = 0u; i < ratchetClass.sequenceCount; ++i) {
        const std::uint32_t absolute = readU32(
            coreIndex, static_cast<std::size_t>(sequenceTableOffset) + i * 4u);
        result.failureSequenceIndex = static_cast<std::int32_t>(i);
        result.failureSequencePointer = absolute;
        if (absolute == 0u) {
            result.status = Rac1RatchetAnimationBankStatus::SequencePointerZero;
            return result;
        }
        if ((absolute & 0x0fu) != 0u) {
            result.status = Rac1RatchetAnimationBankStatus::SequencePointerMisaligned;
            return result;
        }
        if (!fits(static_cast<std::size_t>(absolute), kMobySequenceHeaderBytes, core.size())) {
            result.status = Rac1RatchetAnimationBankStatus::SequencePointerOutOfRange;
            return result;
        }
        if (i != 0u && absolute <= previous) {
            result.status = Rac1RatchetAnimationBankStatus::SequencePointersNotIncreasing;
            return result;
        }
        absoluteSequenceOffsets.push_back(absolute);
        previous = absolute;
    }

    // Replace only the sequence side of the copied class. The rig, render
    // correction records and skinning program remain exactly those already
    // validated for oClass 0 by the ordinary moby metadata gate.
    out.externalSequenceTableOffset = sequenceTableOffset;
    out.sequenceOffsets.clear();
    out.sequenceProbes.clear();
    out.sequenceLayouts.clear();
    out.presentSequenceCount = 0u;
    out.nullSequenceCount = 0u;
    out.uniqueSequencePayloadCount = 0u;
    out.aliasedSequenceSlotCount = 0u;
    out.totalFrameCount = 0u;
    out.nonFfHeaderByte11Count = 0u;
    out.oversizedFrameProbeCount = 0u;
    out.sequenceOffsets.reserve(out.sequenceCount);
    out.sequenceProbes.reserve(out.sequenceCount);
    out.sequenceLayouts.reserve(out.sequenceCount);

    std::unordered_set<std::uint32_t> globalFrameStrides;
    for (std::size_t i = 0u; i < absoluteSequenceOffsets.size(); ++i) {
        const std::uint32_t sequenceAbsolute = absoluteSequenceOffsets[i];
        const std::uint32_t dataEnd = i + 1u < absoluteSequenceOffsets.size()
            ? absoluteSequenceOffsets[i + 1u]
            : static_cast<std::uint32_t>(core.size());
        out.sequenceOffsets.push_back(sequenceAbsolute);
        ++out.presentSequenceCount;

        Rac1MobySequenceProbe sequenceProbe{};
        sequenceProbe.sequenceIndex = static_cast<std::uint8_t>(i);
        sequenceProbe.offset = sequenceAbsolute;
        sequenceProbe.nextBoundaryOffset = dataEnd;
        sequenceProbe.aliasCount = 1u;
        capturePrefix(core,
                      sequenceAbsolute,
                      dataEnd,
                      sequenceProbe.prefix,
                      sequenceProbe.prefixSize,
                      sequenceProbe.prefixFnv1a);
        out.sequenceProbes.push_back(sequenceProbe);

        Rac1MobySequenceLayout layout{};
        layout.sequenceIndex = static_cast<std::uint8_t>(i);
        layout.offset = sequenceAbsolute;
        layout.storage = Rac1MobySequenceStorage::RatchetExternal;
        layout.sequenceAbsoluteOffset = sequenceAbsolute;
        layout.frameBaseOffset = sequenceAbsolute;
        layout.dataEndOffset = dataEnd;
        for (std::size_t c = 0u; c < layout.headerVec4.size(); ++c) {
            layout.headerVec4[c] = readF32(core, sequenceAbsolute + c * 4u);
        }
        layout.frameCount = core[sequenceAbsolute + 0x10u];
        layout.headerByte11 = core[sequenceAbsolute + 0x11u];
        layout.controlByte = core[sequenceAbsolute + 0x12u];
        layout.reservedByte = core[sequenceAbsolute + 0x13u];
        layout.reservedWord = readU32(core, sequenceAbsolute + 0x14u);
        layout.headerScalar = readF32(core, sequenceAbsolute + 0x18u);

        auto failSequence = [&](Rac1MobySequenceFailure reason,
                                std::int32_t frameIndex = -1,
                                std::uint32_t frameOffset = 0u) {
            result.status = Rac1RatchetAnimationBankStatus::InvalidSequenceLayout;
            result.failureSequenceIndex = static_cast<std::int32_t>(i);
            result.failureSequencePointer = sequenceAbsolute;
            result.failureSequenceReason = reason;
            result.failureFrameIndex = frameIndex;
            result.failureFrameOffset = frameOffset;
            return result;
        };

        if (layout.frameCount == 0u) {
            return failSequence(Rac1MobySequenceFailure::ZeroFrameCount);
        }
        if (layout.headerByte11 != 0xffu && layout.headerByte11 >= layout.frameCount) {
            return failSequence(Rac1MobySequenceFailure::HeaderByte11OutOfRange);
        }

        std::size_t frameTableBytes = 0u;
        std::size_t frameTableAbsolute = 0u;
        std::size_t frameTableEndAbsolute = 0u;
        std::size_t frameTableEndRelative = 0u;
        if (!checkedMul(static_cast<std::size_t>(layout.frameCount), 4u, frameTableBytes) ||
            !checkedAdd(static_cast<std::size_t>(sequenceAbsolute),
                        kMobySequenceHeaderBytes, frameTableAbsolute) ||
            !checkedAdd(frameTableAbsolute, frameTableBytes, frameTableEndAbsolute) ||
            !checkedAdd(kMobySequenceHeaderBytes, frameTableBytes, frameTableEndRelative) ||
            !fits(frameTableAbsolute, frameTableBytes, core.size()) ||
            frameTableEndAbsolute > dataEnd) {
            return failSequence(Rac1MobySequenceFailure::FrameTableOutOfRange);
        }

        layout.frameOffsets.reserve(layout.frameCount);
        std::uint32_t previousFrame = 0u;
        for (std::size_t frame = 0u; frame < layout.frameCount; ++frame) {
            const std::uint32_t frameRelative = readU32(core, frameTableAbsolute + frame * 4u);
            if (frameRelative == 0u) {
                return failSequence(Rac1MobySequenceFailure::FramePointerZero,
                                    static_cast<std::int32_t>(frame), frameRelative);
            }
            if (static_cast<std::size_t>(frameRelative) < frameTableEndRelative) {
                return failSequence(Rac1MobySequenceFailure::FramePointerBeforeTableEnd,
                                    static_cast<std::int32_t>(frame), frameRelative);
            }
            if ((frameRelative & 0x0fu) != 0u) {
                return failSequence(Rac1MobySequenceFailure::FramePointerMisaligned,
                                    static_cast<std::int32_t>(frame), frameRelative);
            }
            std::size_t frameAbsolute = 0u;
            if (!checkedAdd(static_cast<std::size_t>(sequenceAbsolute),
                            static_cast<std::size_t>(frameRelative), frameAbsolute) ||
                frameAbsolute >= dataEnd || frameAbsolute >= core.size()) {
                return failSequence(Rac1MobySequenceFailure::FramePointerOutOfRange,
                                    static_cast<std::int32_t>(frame), frameRelative);
            }
            if (frame != 0u && frameRelative <= previousFrame) {
                return failSequence(Rac1MobySequenceFailure::FramePointersNotIncreasing,
                                    static_cast<std::int32_t>(frame), frameRelative);
            }
            if (frame != 0u) {
                const std::uint32_t stride = frameRelative - previousFrame;
                if (layout.minFrameStride == 0u || stride < layout.minFrameStride) {
                    layout.minFrameStride = stride;
                }
                layout.maxFrameStride = std::max(layout.maxFrameStride, stride);
            }
            layout.frameOffsets.push_back(frameRelative);
            previousFrame = frameRelative;
        }
        layout.firstFrameOffset = layout.frameOffsets.front();
        layout.lastFrameOffset = layout.frameOffsets.back();

        for (std::size_t frame = 0u; frame + 1u < layout.frameOffsets.size(); ++frame) {
            const std::uint32_t frameOffset = layout.frameOffsets[frame];
            const std::uint32_t nextOffset = layout.frameOffsets[frame + 1u];
            const std::uint32_t stride = nextOffset - frameOffset;
            if (std::find(layout.uniqueFrameStrides.begin(),
                          layout.uniqueFrameStrides.end(), stride) !=
                layout.uniqueFrameStrides.end()) {
                continue;
            }
            layout.uniqueFrameStrides.push_back(stride);
            globalFrameStrides.insert(stride);

            if (static_cast<std::size_t>(stride) > kRac1MobyFrameProbeMaxBytes) {
                ++out.oversizedFrameProbeCount;
                continue;
            }

            std::size_t frameAbsolute = 0u;
            if (!checkedAdd(static_cast<std::size_t>(sequenceAbsolute),
                            static_cast<std::size_t>(frameOffset), frameAbsolute) ||
                !fits(frameAbsolute, static_cast<std::size_t>(stride), core.size()) ||
                frameAbsolute + static_cast<std::size_t>(stride) > dataEnd) {
                return failSequence(Rac1MobySequenceFailure::FramePointerOutOfRange,
                                    static_cast<std::int32_t>(frame), frameOffset);
            }

            Rac1MobyFrameProbe probe{};
            probe.sequenceIndex = static_cast<std::uint8_t>(i);
            probe.frameIndex = static_cast<std::uint8_t>(frame);
            probe.offset = frameOffset;
            probe.stride = stride;
            probe.payload.assign(core.begin() + static_cast<std::ptrdiff_t>(frameAbsolute),
                                 core.begin() + static_cast<std::ptrdiff_t>(
                                     frameAbsolute + static_cast<std::size_t>(stride)));
            probe.payloadFnv1a = fnv1a32(probe.payload);
            layout.frameProbes.push_back(std::move(probe));
        }
        std::sort(layout.uniqueFrameStrides.begin(), layout.uniqueFrameStrides.end());

        out.totalFrameCount += layout.frameCount;
        if (layout.headerByte11 != 0xffu) ++out.nonFfHeaderByte11Count;
        out.sequenceLayouts.push_back(std::move(layout));
    }

    out.uniqueSequencePayloadCount = out.presentSequenceCount;
    out.aliasedSequenceSlotCount = 0u;
    out.nullSequenceCount = 0u;

    result.bank.sequenceCount = out.presentSequenceCount;
    result.bank.totalFrameCount = out.totalFrameCount;
    result.bank.nonFfHeaderByte11Count = out.nonFfHeaderByte11Count;
    result.bank.uniqueFrameStrideCount = globalFrameStrides.size();
    result.bank.oversizedFrameProbeCount = out.oversizedFrameProbeCount;
    for (const auto& layout : out.sequenceLayouts) {
        if (layout.minFrameStride != 0u &&
            (result.bank.minFrameStride == 0u || layout.minFrameStride < result.bank.minFrameStride)) {
            result.bank.minFrameStride = layout.minFrameStride;
        }
        result.bank.maxFrameStride = std::max(result.bank.maxFrameStride, layout.maxFrameStride);
        result.bank.frameProbeCount += layout.frameProbes.size();
        for (const auto& probe : layout.frameProbes) {
            result.bank.maxProbedFrameBytes =
                std::max(result.bank.maxProbedFrameBytes, probe.payload.size());
        }
    }

    result.failureSequenceIndex = -1;
    result.failureSequencePointer = 0u;
    result.failureSequenceReason = Rac1MobySequenceFailure::None;
    result.failureFrameIndex = -1;
    result.failureFrameOffset = 0u;
    result.status = Rac1RatchetAnimationBankStatus::Ok;
    return result;
}

namespace {

struct PoseFrameView {
    std::span<const std::uint8_t> bytes{};
    std::size_t quaternionBase = 0u;
    std::size_t stream1Base = 0u;
    std::size_t stream2Base = 0u;
    std::uint16_t payloadQwordCount = 0u;
    std::uint16_t stream1Offset = 0u;
    std::uint16_t stream1Count = 0u;
    std::uint16_t stream2Offset = 0u;
    std::uint16_t stream2Count = 0u;
    std::size_t stream1ActiveCount = 0u;
};

bool resolvePosePacketView(
    std::span<const std::uint8_t> bytes,
    std::size_t packetBase,
    std::size_t packetLimit,
    const Rac1MobyAnimationClass& cls,
    bool exposePrimaryDiagnostics,
    PoseFrameView& view,
    Rac1MobyPoseResult& result) {
    if (packetLimit > bytes.size() || packetBase >= packetLimit ||
        !fits(packetBase, 0x10u, bytes.size()) || packetBase + 0x10u > packetLimit) {
        result.status = Rac1MobyPoseStatus::FrameOutOfRange;
        return false;
    }

    view.bytes = bytes;
    view.payloadQwordCount = readU16(bytes, packetBase + 0x06u);
    view.stream1Offset = readU16(bytes, packetBase + 0x08u);
    view.stream1Count = readU16(bytes, packetBase + 0x0au);
    view.stream2Offset = readU16(bytes, packetBase + 0x0cu);
    view.stream2Count = readU16(bytes, packetBase + 0x0eu);

    if (exposePrimaryDiagnostics) {
        result.pose.payloadQwordCount = view.payloadQwordCount;
        result.pose.stream1Offset = view.stream1Offset;
        result.pose.stream1Count = view.stream1Count;
        result.pose.stream2Offset = view.stream2Offset;
        result.pose.stream2Count = view.stream2Count;
    }

    std::size_t payloadBytes = 0u;
    std::size_t frameBytes = 0u;
    if (!checkedMul(view.payloadQwordCount, 0x10u, payloadBytes) ||
        !checkedAdd(0x10u, payloadBytes, frameBytes) ||
        !fits(packetBase, frameBytes, bytes.size()) ||
        packetBase + frameBytes > packetLimit) {
        result.status = Rac1MobyPoseStatus::FrameOutOfRange;
        return false;
    }

    std::size_t denseBytes = 0u;
    std::size_t stream1Bytes = 0u;
    std::size_t stream2Bytes = 0u;
    std::size_t expectedStream2Offset = 0u;
    std::size_t usedBytes = 0u;
    if (!checkedMul(cls.jointCount, 8u, denseBytes) ||
        !checkedMul(view.stream1Count, 8u, stream1Bytes) ||
        !checkedMul(view.stream2Count, 8u, stream2Bytes) ||
        !checkedAdd(denseBytes, stream1Bytes, expectedStream2Offset) ||
        !checkedAdd(expectedStream2Offset, stream2Bytes, usedBytes) ||
        denseBytes > payloadBytes) {
        result.status = Rac1MobyPoseStatus::InvalidSparseLayout;
        return false;
    }

    // FUN_002109b8 does not seek these streams through the header offsets: it
    // starts stream 1 at jointCount*8 and advances exactly count1*8 bytes to
    // stream 2. The header offsets are therefore an independently checkable
    // statement of that same retail layout. Payloads are qword-sized, so at
    // most one trailing 8-byte pad can follow the 8-byte records.
    if (view.stream1Offset != denseBytes ||
        view.stream2Offset != expectedStream2Offset ||
        usedBytes > payloadBytes || payloadBytes - usedBytes >= 0x10u ||
        ((payloadBytes - usedBytes) % 8u) != 0u) {
        result.status = Rac1MobyPoseStatus::InvalidSparseLayout;
        return false;
    }

    view.quaternionBase = packetBase + 0x10u;
    view.stream1Base = view.quaternionBase + view.stream1Offset;
    view.stream2Base = view.quaternionBase + view.stream2Offset;

    // Sparse stream 1's branch condition is the signed low 32-bit word of the
    // 8-byte entry (retail bgez at 0x210bf4/0x210cd8). Nonnegative entries are
    // explicitly skipped by retail. Negative entries enter a compressed-
    // quaternion reconstruction path that remains the next oracle task.
    for (std::size_t entryIndex = 0u; entryIndex < view.stream1Count; ++entryIndex) {
        const std::size_t entry = view.stream1Base + entryIndex * 8u;
        if (readI32(bytes, entry) < 0) {
            ++view.stream1ActiveCount;
            const std::uint8_t jointIndex = bytes[entry + 6u];
            if (jointIndex >= cls.jointCount) {
                result.status = Rac1MobyPoseStatus::InvalidSparseJointIndex;
                result.failureSparseEntryIndex = static_cast<std::int32_t>(entryIndex);
                result.failureSparseJointIndex = jointIndex;
                return false;
            }
        }
    }

    // Sparse stream 2 always dereferences byte 6 as jointIndex*0x40 and writes
    // signed-s16 xyz to that joint's local translation record.
    for (std::size_t entryIndex = 0u; entryIndex < view.stream2Count; ++entryIndex) {
        const std::size_t entry = view.stream2Base + entryIndex * 8u;
        const std::uint8_t jointIndex = bytes[entry + 6u];
        if (jointIndex >= cls.jointCount) {
            result.status = Rac1MobyPoseStatus::InvalidSparseJointIndex;
            result.failureSparseEntryIndex = static_cast<std::int32_t>(entryIndex);
            result.failureSparseJointIndex = jointIndex;
            return false;
        }
    }

    if (exposePrimaryDiagnostics) {
        result.pose.stream1ActiveCount = view.stream1ActiveCount;
        result.pose.stream2OverrideCount = view.stream2Count;
    }

    if (view.stream1ActiveCount != 0u) {
        result.status = Rac1MobyPoseStatus::UnsupportedSparseFrame;
        return false;
    }
    return true;
}

bool resolvePoseFrameView(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    const Rac1MobySequenceLayout& layout,
    std::uint8_t frameIndex,
    bool exposePrimaryDiagnostics,
    PoseFrameView& view,
    Rac1MobyPoseResult& result) {
    if (frameIndex >= layout.frameOffsets.size()) {
        result.status = Rac1MobyPoseStatus::FrameIndexOutOfRange;
        return false;
    }

    const std::size_t frameBase = layout.frameBaseOffset != 0u
        ? static_cast<std::size_t>(layout.frameBaseOffset)
        : static_cast<std::size_t>(cls.classOffset);
    const std::size_t dataEnd = layout.dataEndOffset != 0u
        ? static_cast<std::size_t>(layout.dataEndOffset)
        : static_cast<std::size_t>(cls.classEndOffset);
    std::size_t frameAbsolute = 0u;
    if (!checkedAdd(frameBase, layout.frameOffsets[frameIndex], frameAbsolute) ||
        dataEnd > core.size() || frameAbsolute >= dataEnd) {
        result.status = Rac1MobyPoseStatus::FrameOutOfRange;
        return false;
    }

    std::size_t packetLimit = dataEnd;
    if (static_cast<std::size_t>(frameIndex) + 1u < layout.frameOffsets.size()) {
        if (!checkedAdd(frameBase,
                        layout.frameOffsets[static_cast<std::size_t>(frameIndex) + 1u],
                        packetLimit) ||
            packetLimit > dataEnd || packetLimit <= frameAbsolute) {
            result.status = Rac1MobyPoseStatus::FrameOutOfRange;
            return false;
        }
    }

    return resolvePosePacketView(
        core, frameAbsolute, packetLimit, cls, exposePrimaryDiagnostics, view, result);
}

std::vector<std::array<float, 3>> localTranslationsForFrame(
    const Rac1MobyAnimationClass& cls,
    const PoseFrameView& view) {
    std::vector<std::array<float, 3>> translations;
    translations.reserve(cls.jointCount);
    for (std::size_t joint = 0u; joint < cls.jointCount; ++joint) {
        const auto& common = cls.commonTransformWords[joint];
        translations.push_back({
            std::bit_cast<float>(common[0]),
            std::bit_cast<float>(common[1]),
            std::bit_cast<float>(common[2]),
        });
    }
    for (std::size_t entryIndex = 0u; entryIndex < view.stream2Count; ++entryIndex) {
        const std::size_t entry = view.stream2Base + entryIndex * 8u;
        const std::size_t jointIndex = view.bytes[entry + 6u];
        translations[jointIndex] = {
            static_cast<float>(readI16(view.bytes, entry + 0u)),
            static_cast<float>(readI16(view.bytes, entry + 2u)),
            static_cast<float>(readI16(view.bytes, entry + 4u)),
        };
    }
    return translations;
}

const Rac1MobySequenceLayout* findPoseSequenceLayout(
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex) {
    const auto layoutIt = std::find_if(
        cls.sequenceLayouts.begin(), cls.sequenceLayouts.end(),
        [sequenceIndex](const Rac1MobySequenceLayout& layout) {
            return layout.sequenceIndex == sequenceIndex;
        });
    return layoutIt == cls.sequenceLayouts.end() ? nullptr : &*layoutIt;
}

Rac1MobyPoseResult blendResolvedPoseViews(
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha,
    const PoseFrameView& current,
    const PoseFrameView& next,
    Rac1MobyPoseResult result) {
    if (cls.commonTransformWords.size() != cls.jointCount) {
        result.status = Rac1MobyPoseStatus::InvalidCommonTransform;
        return result;
    }
    for (std::size_t joint = 0u; joint < cls.jointCount; ++joint) {
        const auto& common = cls.commonTransformWords[joint];
        const float tx = std::bit_cast<float>(common[0]);
        const float ty = std::bit_cast<float>(common[1]);
        const float tz = std::bit_cast<float>(common[2]);
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
            result.status = Rac1MobyPoseStatus::InvalidCommonTransform;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
    }

    const auto translationsA = localTranslationsForFrame(cls, current);
    const auto translationsB = localTranslationsForFrame(cls, next);

    result.pose.jointMatrices.reserve(cls.jointCount);
    constexpr float kInv32768 = 1.0f / 32768.0f;
    constexpr std::uint32_t kScratchpadBase = 0x70000000u;
    constexpr std::uint32_t kJointMatrixBytes = 0x40u;
    const float oneMinusAlpha = 1.0f - alpha;

    for (std::size_t joint = 0u; joint < cls.jointCount; ++joint) {
        const std::size_t qa = current.quaternionBase + joint * 8u;
        const std::size_t qb = next.quaternionBase + joint * 8u;
        std::array<float, 4> a = {
            static_cast<float>(readI16(current.bytes, qa + 0u)) * kInv32768,
            static_cast<float>(readI16(current.bytes, qa + 2u)) * kInv32768,
            static_cast<float>(readI16(current.bytes, qa + 4u)) * kInv32768,
            static_cast<float>(readI16(current.bytes, qa + 6u)) * kInv32768,
        };
        std::array<float, 4> b = {
            static_cast<float>(readI16(next.bytes, qb + 0u)) * kInv32768,
            static_cast<float>(readI16(next.bytes, qb + 2u)) * kInv32768,
            static_cast<float>(readI16(next.bytes, qb + 4u)) * kInv32768,
            static_cast<float>(readI16(next.bytes, qb + 6u)) * kInv32768,
        };

        const float normA2 = a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3];
        const float normB2 = b[0] * b[0] + b[1] * b[1] + b[2] * b[2] + b[3] * b[3];
        if (!std::isfinite(normA2) || !std::isfinite(normB2) ||
            normA2 <= 1.0e-12f || normB2 <= 1.0e-12f) {
            result.status = Rac1MobyPoseStatus::NonFinitePose;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
        const float normA = std::sqrt(normA2);
        const float normB = std::sqrt(normB2);
        result.pose.maxQuaternionNormError = std::max(
            result.pose.maxQuaternionNormError,
            std::max(std::abs(normA - 1.0f), std::abs(normB - 1.0f)));

        const float dot =
            a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        if (dot < 0.0f) {
            for (float& component : b) component = -component;
        }

        float x = a[0] * oneMinusAlpha + b[0] * alpha;
        float y = a[1] * oneMinusAlpha + b[1] * alpha;
        float z = a[2] * oneMinusAlpha + b[2] * alpha;
        float w = a[3] * oneMinusAlpha + b[3] * alpha;
        const float blendedNorm2 = x * x + y * y + z * z + w * w;
        if (!std::isfinite(blendedNorm2) || blendedNorm2 <= 1.0e-12f) {
            result.status = Rac1MobyPoseStatus::NonFinitePose;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
        const float invNorm = 1.0f / std::sqrt(blendedNorm2);
        x *= invNorm;
        y *= invNorm;
        z *= invNorm;
        w *= invNorm;

        const float tx = translationsA[joint][0] * oneMinusAlpha +
            translationsB[joint][0] * alpha;
        const float ty = translationsA[joint][1] * oneMinusAlpha +
            translationsB[joint][1] * alpha;
        const float tz = translationsA[joint][2] * oneMinusAlpha +
            translationsB[joint][2] * alpha;
        if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz)) {
            result.status = Rac1MobyPoseStatus::InvalidCommonTransform;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
        result.pose.maxAbsLocalTranslation = std::max(
            result.pose.maxAbsLocalTranslation,
            std::max({
                std::abs(translationsA[joint][0]), std::abs(translationsA[joint][1]),
                std::abs(translationsA[joint][2]), std::abs(translationsB[joint][0]),
                std::abs(translationsB[joint][1]), std::abs(translationsB[joint][2]),
            }));

        const std::uint32_t parentPointer = cls.commonTransformWords[joint][3];
        std::array<float, 16> parent = identityMatrix();
        if (parentPointer == 0u) {
            ++result.pose.rootJointCount;
        } else {
            result.failureParentPointer = parentPointer;
            if (parentPointer < kScratchpadBase) {
                result.status = Rac1MobyPoseStatus::InvalidParentPointer;
                result.failureJointIndex = static_cast<std::int32_t>(joint);
                return result;
            }
            const std::uint32_t parentOffset = parentPointer - kScratchpadBase;
            if ((parentOffset % kJointMatrixBytes) != 0u) {
                result.status = Rac1MobyPoseStatus::InvalidParentPointer;
                result.failureJointIndex = static_cast<std::int32_t>(joint);
                return result;
            }
            const std::size_t parentIndex = parentOffset / kJointMatrixBytes;
            if (parentIndex >= joint || parentIndex >= result.pose.jointMatrices.size()) {
                result.status = Rac1MobyPoseStatus::InvalidParentPointer;
                result.failureJointIndex = static_cast<std::int32_t>(joint);
                return result;
            }
            parent = result.pose.jointMatrices[parentIndex];
            ++result.pose.parentLinkCount;
        }

        const auto local = quaternionTranslationMatrix(x, y, z, w, tx, ty, tz);
        auto global = multiplyAffine(parent, local);
        if (!finiteMatrix(global)) {
            result.status = Rac1MobyPoseStatus::NonFinitePose;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
        result.pose.jointMatrices.push_back(std::move(global));
    }

    result.pose.sequenceIndex = sequenceIndex;
    result.pose.frameIndex = frameIndex;
    result.pose.nextSequenceIndex = nextSequenceIndex;
    result.pose.nextFrameIndex = nextFrameIndex;
    result.pose.interpolationAlpha = alpha;
    result.pose.interpolated =
        (sequenceIndex != nextSequenceIndex || frameIndex != nextFrameIndex) &&
        alpha > 0.0f && alpha < 1.0f;
    result.pose.jointCount = cls.jointCount;
    result.status = Rac1MobyPoseStatus::Ok;
    return result;
}

Rac1MobyPoseResult decodePoseInterpolatedImpl(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    Rac1MobyPoseResult result{};
    result.pose.oClass = cls.oClass;
    result.pose.sequenceIndex = sequenceIndex;
    result.pose.frameIndex = frameIndex;
    result.pose.nextSequenceIndex = nextSequenceIndex;
    result.pose.nextFrameIndex = nextFrameIndex;
    result.pose.interpolationAlpha = alpha;
    result.pose.interpolated =
        (sequenceIndex != nextSequenceIndex || frameIndex != nextFrameIndex) &&
        alpha > 0.0f && alpha < 1.0f;
    result.pose.jointCount = cls.jointCount;

    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) {
        result.status = Rac1MobyPoseStatus::InvalidInterpolationAlpha;
        return result;
    }

    const Rac1MobySequenceLayout* currentLayout =
        findPoseSequenceLayout(cls, sequenceIndex);
    const Rac1MobySequenceLayout* nextLayout =
        findPoseSequenceLayout(cls, nextSequenceIndex);
    if (currentLayout == nullptr || nextLayout == nullptr) {
        result.status = Rac1MobyPoseStatus::SequenceNotFound;
        return result;
    }

    PoseFrameView current{};
    PoseFrameView next{};
    if (!resolvePoseFrameView(
            core, cls, *currentLayout, frameIndex, true, current, result) ||
        !resolvePoseFrameView(
            core, cls, *nextLayout, nextFrameIndex, false, next, result)) {
        return result;
    }

    return blendResolvedPoseViews(
        cls, sequenceIndex, frameIndex, nextSequenceIndex, nextFrameIndex,
        alpha, current, next, std::move(result));
}

Rac1MobyPoseResult decodePoseInterpolatedFromPacketImpl(
    std::span<const std::uint8_t> framePacketA,
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t packetSequenceIndex,
    std::uint8_t packetFrameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    Rac1MobyPoseResult result{};
    result.pose.oClass = cls.oClass;
    result.pose.sequenceIndex = packetSequenceIndex;
    result.pose.frameIndex = packetFrameIndex;
    result.pose.nextSequenceIndex = nextSequenceIndex;
    result.pose.nextFrameIndex = nextFrameIndex;
    result.pose.interpolationAlpha = alpha;
    result.pose.interpolated =
        alpha > 0.0f && alpha < 1.0f;
    result.pose.jointCount = cls.jointCount;

    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) {
        result.status = Rac1MobyPoseStatus::InvalidInterpolationAlpha;
        return result;
    }

    const Rac1MobySequenceLayout* nextLayout =
        findPoseSequenceLayout(cls, nextSequenceIndex);
    if (nextLayout == nullptr) {
        result.status = Rac1MobyPoseStatus::SequenceNotFound;
        return result;
    }

    PoseFrameView current{};
    PoseFrameView next{};
    if (!resolvePosePacketView(
            framePacketA, 0u, framePacketA.size(), cls, true, current, result) ||
        !resolvePoseFrameView(
            core, cls, *nextLayout, nextFrameIndex, false, next, result)) {
        return result;
    }

    return blendResolvedPoseViews(
        cls, packetSequenceIndex, packetFrameIndex, nextSequenceIndex,
        nextFrameIndex, alpha, current, next, std::move(result));
}

Rac1MobyPoseResult decodePoseInterpolatedFromPacketsImpl(
    std::span<const std::uint8_t> framePacketA,
    std::span<const std::uint8_t> framePacketB,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t packetSequenceIndex,
    std::uint8_t packetFrameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    Rac1MobyPoseResult result{};
    result.pose.oClass = cls.oClass;
    result.pose.sequenceIndex = packetSequenceIndex;
    result.pose.frameIndex = packetFrameIndex;
    result.pose.nextSequenceIndex = nextSequenceIndex;
    result.pose.nextFrameIndex = nextFrameIndex;
    result.pose.interpolationAlpha = alpha;
    // Packet provenance, not host buffer identity, determines the endpoints.
    // Retail blends the two supplied packet streams whenever alpha is interior.
    result.pose.interpolated = alpha > 0.0f && alpha < 1.0f;
    result.pose.jointCount = cls.jointCount;

    if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) {
        result.status = Rac1MobyPoseStatus::InvalidInterpolationAlpha;
        return result;
    }

    PoseFrameView current{};
    PoseFrameView next{};
    if (!resolvePosePacketView(
            framePacketA, 0u, framePacketA.size(), cls, true, current, result) ||
        !resolvePosePacketView(
            framePacketB, 0u, framePacketB.size(), cls, false, next, result)) {
        return result;
    }

    return blendResolvedPoseViews(
        cls, packetSequenceIndex, packetFrameIndex, nextSequenceIndex,
        nextFrameIndex, alpha, current, next, std::move(result));
}

} // namespace

Rac1MobyPoseResult decodeRac1MobyPoseFrame(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex) {
    return decodePoseInterpolatedImpl(
        core, cls, sequenceIndex, frameIndex, sequenceIndex, frameIndex, 0.0f);
}

Rac1MobyPoseResult decodeRac1MobyPoseInterpolated(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    return decodePoseInterpolatedImpl(
        core, cls, sequenceIndex, frameIndex, sequenceIndex, nextFrameIndex, alpha);
}

Rac1MobyPoseResult decodeRac1MobyPoseInterpolatedAcrossSequences(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    return decodePoseInterpolatedImpl(
        core, cls, sequenceIndex, frameIndex, nextSequenceIndex, nextFrameIndex, alpha);
}

Rac1MobyPoseResult decodeRac1MobyPoseInterpolatedFromPacket(
    std::span<const std::uint8_t> framePacketA,
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t packetSequenceIndex,
    std::uint8_t packetFrameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    return decodePoseInterpolatedFromPacketImpl(
        framePacketA, core, cls, packetSequenceIndex, packetFrameIndex,
        nextSequenceIndex, nextFrameIndex, alpha);
}

Rac1MobyPoseResult decodeRac1MobyPoseInterpolatedFromPackets(
    std::span<const std::uint8_t> framePacketA,
    std::span<const std::uint8_t> framePacketB,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t packetSequenceIndex,
    std::uint8_t packetFrameIndex,
    std::uint8_t nextSequenceIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    return decodePoseInterpolatedFromPacketsImpl(
        framePacketA, framePacketB, cls, packetSequenceIndex, packetFrameIndex,
        nextSequenceIndex, nextFrameIndex, alpha);
}

Rac1MobyPoseResult decodeRac1MobyDensePoseFrame(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex) {
    return decodeRac1MobyPoseFrame(core, cls, sequenceIndex, frameIndex);
}

Rac1MobyPoseResult decodeRac1MobyDensePoseInterpolated(
    std::span<const std::uint8_t> core,
    const Rac1MobyAnimationClass& cls,
    std::uint8_t sequenceIndex,
    std::uint8_t frameIndex,
    std::uint8_t nextFrameIndex,
    float alpha) {
    return decodeRac1MobyPoseInterpolated(
        core, cls, sequenceIndex, frameIndex, nextFrameIndex, alpha);
}

Rac1MobyPosePaletteResult inspectRac1MobyPosePaletteContract(
    const Rac1MobyAnimationClass& cls) {
    Rac1MobyPosePaletteResult result{};
    result.contract.oClass = cls.oClass;
    result.contract.jointCount = cls.jointCount;
    result.contract.matrixTransferReferences = cls.matrixTransferCount;
    result.contract.twoWayDirectReferences = cls.twoWayBlendVertexCount;
    result.contract.mainDirectReferences = cls.mainVertexCount;
    result.contract.directSourceReferences = cls.matrixTransferCount +
        cls.twoWayBlendVertexCount + cls.mainVertexCount;

    if (!cls.hasSkeleton()) {
        result.status = Rac1MobyPosePaletteStatus::NoSkeleton;
        return result;
    }

    auto rejectOutOfRange = [&](std::size_t references,
                                std::uint8_t maxIndex,
                                Rac1MobyPosePaletteStatus status,
                                Rac1MobyPosePaletteSourceKind kind) -> bool {
        if (references == 0u) return false;
        result.contract.maxReferencedJointIndex =
            std::max(result.contract.maxReferencedJointIndex, maxIndex);
        if (maxIndex < cls.jointCount) return false;
        result.status = status;
        result.failureSourceKind = kind;
        result.failureJointIndex = static_cast<std::int32_t>(maxIndex);
        return true;
    };

    if (rejectOutOfRange(cls.matrixTransferCount, cls.maxScratchpadMatrixIndex,
                         Rac1MobyPosePaletteStatus::MatrixTransferJointOutOfRange,
                         Rac1MobyPosePaletteSourceKind::MatrixTransfer)) {
        return result;
    }
    if (rejectOutOfRange(cls.twoWayBlendVertexCount, cls.maxFirstBlendLow7,
                         Rac1MobyPosePaletteStatus::TwoWayJointOutOfRange,
                         Rac1MobyPosePaletteSourceKind::TwoWay)) {
        return result;
    }
    if (rejectOutOfRange(cls.mainVertexCount, cls.maxMainScratchpadMatrixIndex,
                         Rac1MobyPosePaletteStatus::MainJointOutOfRange,
                         Rac1MobyPosePaletteSourceKind::Main)) {
        return result;
    }

    result.status = Rac1MobyPosePaletteStatus::Ok;
    return result;
}

Rac1MobySkinExecutionResult executeRac1MobySkinningProgram(
    const Rac1MobyAnimationClass& cls,
    const Rac1MobyPoseFrame& pose) {
    Rac1MobySkinExecutionResult result{};
    result.execution.oClass = cls.oClass;

    if (!cls.hasSkeleton()) {
        result.status = Rac1MobySkinExecutionStatus::NoSkeleton;
        return result;
    }
    if (pose.jointCount != cls.jointCount || pose.jointMatrices.size() != cls.jointCount) {
        result.status = Rac1MobySkinExecutionStatus::PoseJointCountMismatch;
        return result;
    }
    if (cls.skeletonMatrices.size() != cls.jointCount) {
        result.status = Rac1MobySkinExecutionStatus::SkeletonMatrixCountMismatch;
        return result;
    }
    if (cls.skinningPackets.size() != cls.highLodPacketCount) {
        result.status = Rac1MobySkinExecutionStatus::ProgramPacketCountMismatch;
        return result;
    }

    // The attachment/bone helper exports FUN_002109b8 pose matrices directly,
    // but the mesh renderer takes one additional retail step. FUN_00211808 puts
    // class +0x14 into render-work +0x1c; sub_0020E0E0 then DMA-loads that table
    // and executes pose * correction at 0x20ed28..0x20ed94 before JI/VU0 skinning.
    // Build that exact render palette once per frame.
    std::vector<std::array<float, 16>> renderPalette;
    renderPalette.reserve(cls.jointCount);
    for (std::size_t joint = 0u; joint < cls.jointCount; ++joint) {
        const auto correction = retailSkeletonPostMatrix(cls.skeletonMatrices[joint]);
        auto renderMatrix = multiplyAffine(pose.jointMatrices[joint], correction);
        if (!finiteMatrix(renderMatrix)) {
            result.status = Rac1MobySkinExecutionStatus::NonFiniteMatrix;
            result.failureJointIndex = static_cast<std::int32_t>(joint);
            return result;
        }
        renderPalette.push_back(std::move(renderMatrix));
    }
    result.execution.skeletonPostComposeCount = renderPalette.size();

    std::array<std::array<float, 16>, 64> vu0{};
    std::array<std::int32_t, 64> writtenByPacket{};
    writtenByPacket.fill(-1);
    result.execution.packetCount = cls.skinningPackets.size();
    result.execution.vertices.reserve(cls.skinningVertexCount);

    auto rejectAddress = [&](std::uint8_t address,
                             std::size_t packetIndex,
                             std::size_t vertexIndex,
                             Rac1MobySkinningVertexKind kind) -> bool {
        if (validVu0MatrixAddress(address)) return false;
        result.status = Rac1MobySkinExecutionStatus::Vu0AddressInvalid;
        result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
        result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
        result.failureVertexKind = kind;
        result.failureVu0Address = static_cast<std::int32_t>(address);
        return true;
    };

    auto writeJoint = [&](std::uint8_t jointIndex,
                          std::uint8_t address,
                          std::size_t packetIndex,
                          std::size_t vertexIndex,
                          Rac1MobySkinningVertexKind kind) -> bool {
        if (jointIndex >= renderPalette.size()) {
            result.status = Rac1MobySkinExecutionStatus::JointIndexOutOfRange;
            result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
            result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
            result.failureVertexKind = kind;
            result.failureJointIndex = static_cast<std::int32_t>(jointIndex);
            return false;
        }
        if (rejectAddress(address, packetIndex, vertexIndex, kind)) return false;
        const auto& matrix = renderPalette[jointIndex];
        if (!finiteMatrix(matrix)) {
            result.status = Rac1MobySkinExecutionStatus::NonFiniteMatrix;
            result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
            result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
            result.failureVertexKind = kind;
            result.failureJointIndex = static_cast<std::int32_t>(jointIndex);
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(address / 4u);
        vu0[slot] = matrix;
        writtenByPacket[slot] = static_cast<std::int32_t>(packetIndex);
        ++result.execution.vu0MatrixWrites;
        return true;
    };

    auto readMatrix = [&](std::uint8_t address,
                          std::size_t packetIndex,
                          std::size_t vertexIndex,
                          Rac1MobySkinningVertexKind kind,
                          std::array<float, 16>& out) -> bool {
        if (rejectAddress(address, packetIndex, vertexIndex, kind)) return false;
        const std::size_t slot = static_cast<std::size_t>(address / 4u);
        if (writtenByPacket[slot] < 0) {
            result.status = Rac1MobySkinExecutionStatus::Vu0ReadBeforeWrite;
            result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
            result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
            result.failureVertexKind = kind;
            result.failureVu0Address = static_cast<std::int32_t>(address);
            return false;
        }
        if (writtenByPacket[slot] < static_cast<std::int32_t>(packetIndex)) {
            ++result.execution.vu0CrossPacketReads;
        }
        out = vu0[slot];
        return true;
    };

    auto writeMatrix = [&](std::uint8_t address,
                           const std::array<float, 16>& matrix,
                           std::size_t packetIndex,
                           std::size_t vertexIndex,
                           Rac1MobySkinningVertexKind kind) -> bool {
        if (rejectAddress(address, packetIndex, vertexIndex, kind)) return false;
        if (!finiteMatrix(matrix)) {
            result.status = Rac1MobySkinExecutionStatus::NonFiniteMatrix;
            result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
            result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
            result.failureVertexKind = kind;
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(address / 4u);
        vu0[slot] = matrix;
        writtenByPacket[slot] = static_cast<std::int32_t>(packetIndex);
        ++result.execution.vu0MatrixWrites;
        return true;
    };

    auto weighted = [](const std::array<float, 16>& a,
                       float wa,
                       const std::array<float, 16>& b,
                       float wb) noexcept {
        std::array<float, 16> out{};
        for (std::size_t i = 0u; i < out.size(); ++i) out[i] = a[i] * wa + b[i] * wb;
        return out;
    };
    auto weighted3 = [](const std::array<float, 16>& a,
                        float wa,
                        const std::array<float, 16>& b,
                        float wb,
                        const std::array<float, 16>& c,
                        float wc) noexcept {
        std::array<float, 16> out{};
        for (std::size_t i = 0u; i < out.size(); ++i) {
            out[i] = a[i] * wa + b[i] * wb + c[i] * wc;
        }
        return out;
    };
    auto transformPosition = [](const std::array<float, 16>& m,
                                const std::array<std::int16_t, 3>& p) noexcept {
        const float x = static_cast<float>(p[0]);
        const float y = static_cast<float>(p[1]);
        const float z = static_cast<float>(p[2]);
        return std::array<float, 3>{
            m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14],
        };
    };

    for (std::size_t packetIndex = 0u; packetIndex < cls.skinningPackets.size(); ++packetIndex) {
        const auto& packet = cls.skinningPackets[packetIndex];
        for (std::size_t transferIndex = 0u;
             transferIndex < packet.matrixTransfers.size(); ++transferIndex) {
            const auto& transfer = packet.matrixTransfers[transferIndex];
            if (!writeJoint(transfer.jointIndex, transfer.vu0Address, packetIndex,
                            transferIndex, Rac1MobySkinningVertexKind::None)) {
                return result;
            }
            ++result.execution.matrixTransferCount;
        }

        for (std::size_t vertexIndex = 0u; vertexIndex < packet.vertices.size(); ++vertexIndex) {
            const auto& vertex = packet.vertices[vertexIndex];
            std::array<float, 16> matrix{};
            if (vertex.kind == Rac1MobySkinningVertexKind::TwoWay) {
                if (!writeJoint(vertex.directJointIndex, vertex.directStore, packetIndex,
                                vertexIndex, vertex.kind)) return result;
                std::array<float, 16> l1{};
                std::array<float, 16> l2{};
                if (!readMatrix(vertex.l1, packetIndex, vertexIndex, vertex.kind, l1) ||
                    !readMatrix(vertex.l2, packetIndex, vertexIndex, vertex.kind, l2)) {
                    return result;
                }
                const float w1 = static_cast<float>(vertex.w1) / 255.0f;
                const float w2 = static_cast<float>(vertex.w2) / 255.0f;
                matrix = weighted(l1, w1, l2, w2);
                if (!writeMatrix(vertex.blendStore, matrix, packetIndex, vertexIndex, vertex.kind)) {
                    return result;
                }
                result.execution.maxWeightSumError = std::max(
                    result.execution.maxWeightSumError,
                    std::fabs(static_cast<float>(vertex.w1) + static_cast<float>(vertex.w2) - 255.0f));
                ++result.execution.twoWayVertexCount;
            } else if (vertex.kind == Rac1MobySkinningVertexKind::ThreeWay) {
                std::array<float, 16> l1{};
                std::array<float, 16> l2{};
                std::array<float, 16> l3{};
                if (!readMatrix(vertex.l1, packetIndex, vertexIndex, vertex.kind, l1) ||
                    !readMatrix(vertex.l2, packetIndex, vertexIndex, vertex.kind, l2) ||
                    !readMatrix(vertex.l3, packetIndex, vertexIndex, vertex.kind, l3)) {
                    return result;
                }
                const float w1 = static_cast<float>(vertex.w1) / 255.0f;
                const float w2 = static_cast<float>(vertex.w2) / 255.0f;
                const float w3 = static_cast<float>(vertex.w3) / 255.0f;
                matrix = weighted3(l1, w1, l2, w2, l3, w3);
                if (!writeMatrix(vertex.blendStore, matrix, packetIndex, vertexIndex, vertex.kind)) {
                    return result;
                }
                result.execution.maxWeightSumError = std::max(
                    result.execution.maxWeightSumError,
                    std::fabs(static_cast<float>(vertex.w1) + static_cast<float>(vertex.w2) +
                              static_cast<float>(vertex.w3) - 255.0f));
                ++result.execution.threeWayVertexCount;
            } else if (vertex.kind == Rac1MobySkinningVertexKind::Main) {
                if (!writeJoint(vertex.directJointIndex, vertex.directStore, packetIndex,
                                vertexIndex, vertex.kind)) return result;
                if (!readMatrix(vertex.l1, packetIndex, vertexIndex, vertex.kind, matrix)) {
                    return result;
                }
                ++result.execution.mainVertexCount;
            } else {
                result.status = Rac1MobySkinExecutionStatus::NonFiniteVertex;
                result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
                result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
                result.failureVertexKind = vertex.kind;
                return result;
            }

            if (!finiteMatrix(matrix)) {
                result.status = Rac1MobySkinExecutionStatus::NonFiniteMatrix;
                result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
                result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
                result.failureVertexKind = vertex.kind;
                return result;
            }
            const auto transformed = transformPosition(matrix, vertex.position);
            if (!std::isfinite(transformed[0]) || !std::isfinite(transformed[1]) ||
                !std::isfinite(transformed[2])) {
                result.status = Rac1MobySkinExecutionStatus::NonFiniteVertex;
                result.failurePacketIndex = static_cast<std::int32_t>(packetIndex);
                result.failureVertexIndex = static_cast<std::int32_t>(vertexIndex);
                result.failureVertexKind = vertex.kind;
                return result;
            }
            result.execution.maxAbsPosition = std::max(
                result.execution.maxAbsPosition,
                std::max({std::fabs(transformed[0]), std::fabs(transformed[1]),
                          std::fabs(transformed[2])}));
            result.execution.vertices.push_back({vertex.kind, vertex.id, transformed});
        }
    }

    result.status = Rac1MobySkinExecutionStatus::Ok;
    return result;
}

const char* rac1RatchetAnimationBankStatusName(Rac1RatchetAnimationBankStatus status) noexcept {
    switch (status) {
    case Rac1RatchetAnimationBankStatus::Ok: return "ok";
    case Rac1RatchetAnimationBankStatus::WrongClass: return "wrong-class";
    case Rac1RatchetAnimationBankStatus::MissingSkeleton: return "missing-skeleton";
    case Rac1RatchetAnimationBankStatus::LocalSequenceTableNotEmpty:
        return "local-sequence-table-not-empty";
    case Rac1RatchetAnimationBankStatus::InvalidSequenceTable: return "invalid-sequence-table";
    case Rac1RatchetAnimationBankStatus::SequencePointerZero: return "sequence-pointer-zero";
    case Rac1RatchetAnimationBankStatus::SequencePointerOutOfRange:
        return "sequence-pointer-out-of-range";
    case Rac1RatchetAnimationBankStatus::SequencePointerMisaligned:
        return "sequence-pointer-misaligned";
    case Rac1RatchetAnimationBankStatus::SequencePointersNotIncreasing:
        return "sequence-pointers-not-increasing";
    case Rac1RatchetAnimationBankStatus::InvalidSequenceLayout: return "invalid-sequence-layout";
    }
    return "unknown";
}

const char* rac1MobySequenceStorageName(Rac1MobySequenceStorage storage) noexcept {
    switch (storage) {
    case Rac1MobySequenceStorage::ClassRelative: return "class-relative";
    case Rac1MobySequenceStorage::RatchetExternal: return "ratchet-external";
    }
    return "unknown";
}

const char* rac1MobyPoseStatusName(Rac1MobyPoseStatus status) noexcept {
    switch (status) {
    case Rac1MobyPoseStatus::Ok: return "ok";
    case Rac1MobyPoseStatus::SequenceNotFound: return "sequence-not-found";
    case Rac1MobyPoseStatus::FrameIndexOutOfRange: return "frame-index-out-of-range";
    case Rac1MobyPoseStatus::FrameOutOfRange: return "frame-out-of-range";
    case Rac1MobyPoseStatus::UnsupportedSparseFrame: return "unsupported-sparse-frame";
    case Rac1MobyPoseStatus::InvalidDenseLayout: return "invalid-dense-layout";
    case Rac1MobyPoseStatus::InvalidSparseLayout: return "invalid-sparse-layout";
    case Rac1MobyPoseStatus::InvalidSparseJointIndex: return "invalid-sparse-joint-index";
    case Rac1MobyPoseStatus::InvalidCommonTransform: return "invalid-common-transform";
    case Rac1MobyPoseStatus::InvalidParentPointer: return "invalid-parent-pointer";
    case Rac1MobyPoseStatus::InvalidInterpolationAlpha: return "invalid-interpolation-alpha";
    case Rac1MobyPoseStatus::NonFinitePose: return "non-finite-pose";
    }
    return "unknown";
}

const char* rac1MobySkinExecutionStatusName(Rac1MobySkinExecutionStatus status) noexcept {
    switch (status) {
    case Rac1MobySkinExecutionStatus::Ok: return "ok";
    case Rac1MobySkinExecutionStatus::NoSkeleton: return "no-skeleton";
    case Rac1MobySkinExecutionStatus::PoseJointCountMismatch: return "pose-joint-count-mismatch";
    case Rac1MobySkinExecutionStatus::SkeletonMatrixCountMismatch:
        return "skeleton-matrix-count-mismatch";
    case Rac1MobySkinExecutionStatus::ProgramPacketCountMismatch: return "program-packet-count-mismatch";
    case Rac1MobySkinExecutionStatus::JointIndexOutOfRange: return "joint-index-out-of-range";
    case Rac1MobySkinExecutionStatus::Vu0ReadBeforeWrite: return "vu0-read-before-write";
    case Rac1MobySkinExecutionStatus::Vu0AddressInvalid: return "vu0-address-invalid";
    case Rac1MobySkinExecutionStatus::NonFiniteMatrix: return "non-finite-matrix";
    case Rac1MobySkinExecutionStatus::NonFiniteVertex: return "non-finite-vertex";
    }
    return "unknown";
}

const char* rac1MobyPosePaletteStatusName(Rac1MobyPosePaletteStatus status) noexcept {
    switch (status) {
    case Rac1MobyPosePaletteStatus::Ok: return "ok";
    case Rac1MobyPosePaletteStatus::NoSkeleton: return "no-skeleton";
    case Rac1MobyPosePaletteStatus::MatrixTransferJointOutOfRange:
        return "matrix-transfer-joint-out-of-range";
    case Rac1MobyPosePaletteStatus::TwoWayJointOutOfRange:
        return "two-way-joint-out-of-range";
    case Rac1MobyPosePaletteStatus::MainJointOutOfRange:
        return "main-joint-out-of-range";
    }
    return "unknown";
}

const char* rac1MobyPosePaletteSourceKindName(Rac1MobyPosePaletteSourceKind kind) noexcept {
    switch (kind) {
    case Rac1MobyPosePaletteSourceKind::None: return "none";
    case Rac1MobyPosePaletteSourceKind::MatrixTransfer: return "matrix-transfer";
    case Rac1MobyPosePaletteSourceKind::TwoWay: return "two-way";
    case Rac1MobyPosePaletteSourceKind::Main: return "main";
    }
    return "unknown";
}

const char* rac1MobySequenceFailureName(Rac1MobySequenceFailure failure) noexcept {
    switch (failure) {
    case Rac1MobySequenceFailure::None: return "none";
    case Rac1MobySequenceFailure::HeaderOutOfRange: return "header-out-of-range";
    case Rac1MobySequenceFailure::ZeroFrameCount: return "zero-frame-count";
    case Rac1MobySequenceFailure::HeaderByte11OutOfRange: return "header-byte11-out-of-range";
    case Rac1MobySequenceFailure::FrameTableOutOfRange: return "frame-table-out-of-range";
    case Rac1MobySequenceFailure::FramePointerZero: return "frame-pointer-zero";
    case Rac1MobySequenceFailure::FramePointerOutOfRange: return "frame-pointer-out-of-range";
    case Rac1MobySequenceFailure::FramePointerBeforeTableEnd: return "frame-pointer-before-table-end";
    case Rac1MobySequenceFailure::FramePointerMisaligned: return "frame-pointer-misaligned";
    case Rac1MobySequenceFailure::FramePointersNotIncreasing: return "frame-pointers-not-increasing";
    }
    return "unknown";
}

const char* rac1MobyRigFailureName(Rac1MobyRigFailure failure) noexcept {
    switch (failure) {
    case Rac1MobyRigFailure::None: return "none";
    case Rac1MobyRigFailure::SkeletonRangeMismatch: return "skeleton-range-mismatch";
    case Rac1MobyRigFailure::CommonTransformRangeMismatch: return "common-transform-range-mismatch";
    }
    return "unknown";
}

const char* rac1MobyMatrixTransferFailureName(Rac1MobyMatrixTransferFailure failure) noexcept {
    switch (failure) {
    case Rac1MobyMatrixTransferFailure::None: return "none";
    case Rac1MobyMatrixTransferFailure::TableOutOfRange: return "table-out-of-range";
    case Rac1MobyMatrixTransferFailure::DestinationMisaligned: return "destination-misaligned";
    case Rac1MobyMatrixTransferFailure::DestinationOverflow: return "destination-overflow";
    }
    return "unknown";
}

const char* rac1MobyBlendLayoutName(Rac1MobyBlendLayout layout) noexcept {
    switch (layout) {
    case Rac1MobyBlendLayout::NotApplicable: return "not-applicable";
    case Rac1MobyBlendLayout::TwoWayThenThreeWay: return "two-way-then-three-way";
    }
    return "unknown";
}

const char* rac1MobySkinningFailureName(Rac1MobySkinningFailure failure) noexcept {
    switch (failure) {
    case Rac1MobySkinningFailure::None: return "none";
    case Rac1MobySkinningFailure::VertexDataOutOfRange: return "vertex-data-out-of-range";
    case Rac1MobySkinningFailure::MatrixAddressMisaligned: return "matrix-address-misaligned";
    case Rac1MobySkinningFailure::MatrixAddressOverflow: return "matrix-address-overflow";
    }
    return "unknown";
}

const char* rac1MobyPackedL3EncodingName(Rac1MobyPackedL3Encoding encoding) noexcept {
    switch (encoding) {
    case Rac1MobyPackedL3Encoding::NotApplicable: return "not-applicable";
    case Rac1MobyPackedL3Encoding::ShiftLeft1QwordAddress: return "packed-bit9-shl1";
    }
    return "unknown";
}

const char* rac1MobyRigProbeKindName(Rac1MobyRigProbeKind kind) noexcept {
    switch (kind) {
    case Rac1MobyRigProbeKind::Skeleton: return "skeleton";
    case Rac1MobyRigProbeKind::CommonTransforms: return "common";
    case Rac1MobyRigProbeKind::Joints: return "joints";
    }
    return "unknown";
}

const char* rac1MobySkinningVertexKindName(Rac1MobySkinningVertexKind kind) noexcept {
    switch (kind) {
    case Rac1MobySkinningVertexKind::None: return "none";
    case Rac1MobySkinningVertexKind::TwoWay: return "two-way";
    case Rac1MobySkinningVertexKind::ThreeWay: return "three-way";
    case Rac1MobySkinningVertexKind::Main: return "main";
    }
    return "unknown";
}

const char* rac1MobyAnimationStatusName(Rac1MobyAnimationStatus status) noexcept {
    switch (status) {
    case Rac1MobyAnimationStatus::Ok: return "ok";
    case Rac1MobyAnimationStatus::InvalidIndexTable: return "invalid-index-table";
    case Rac1MobyAnimationStatus::InvalidGameplayHeader: return "invalid-gameplay-header";
    case Rac1MobyAnimationStatus::InvalidInstanceBlock: return "invalid-instance-block";
    case Rac1MobyAnimationStatus::MissingReferencedClass: return "missing-referenced-class";
    case Rac1MobyAnimationStatus::InvalidClassHeader: return "invalid-class-header";
    case Rac1MobyAnimationStatus::InvalidSequenceTable: return "invalid-sequence-table";
    case Rac1MobyAnimationStatus::InvalidSequenceLayout: return "invalid-sequence-layout";
    case Rac1MobyAnimationStatus::InvalidRigLayout: return "invalid-rig-layout";
    case Rac1MobyAnimationStatus::InvalidPacketTable: return "invalid-packet-table";
    case Rac1MobyAnimationStatus::InvalidVertexTable: return "invalid-vertex-table";
    case Rac1MobyAnimationStatus::InvalidMatrixTransfer: return "invalid-matrix-transfer";
    case Rac1MobyAnimationStatus::InvalidSkinningProgram: return "invalid-skinning-program";
    case Rac1MobyAnimationStatus::EmptyScene: return "empty-scene";
    }
    return "unknown";
}

} // namespace ratchet::assets
