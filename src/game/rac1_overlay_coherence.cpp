#include "game/rac1_overlay_coherence.h"
#include "game/rac1_native_input.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace ratchet::game {
namespace {

constexpr std::size_t kElf32HeaderBytes = 52u;
constexpr std::size_t kElf32ProgramHeaderBytes = 32u;
constexpr std::uint32_t kElfPtLoad = 1u;
constexpr std::uint16_t kElfTypeExec = 2u;
constexpr std::uint16_t kElfMachineMips = 8u;
constexpr std::size_t kConflictProbeBytes = 16u;

std::uint16_t readLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8u);
}

std::uint32_t readLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) |
           (static_cast<std::uint32_t>(p[3]) << 24u);
}

bool checkedRange(std::size_t offset, std::size_t size, std::size_t capacity) noexcept {
    return offset <= capacity && size <= capacity - offset;
}

std::span<const std::uint8_t> staticElfBytesAt(const Rac1StaticElfImage& image,
                                               std::uint32_t guestAddress,
                                               std::size_t requested) noexcept {
    for (const auto& segment : image.loadSegments) {
        if (guestAddress < segment.guestAddress) continue;
        const std::uint64_t relative =
            static_cast<std::uint64_t>(guestAddress) - segment.guestAddress;
        if (relative >= segment.fileSize) continue;
        const std::size_t available =
            static_cast<std::size_t>(segment.fileSize - relative);
        const std::size_t count = std::min(requested, available);
        const std::size_t fileOffset =
            static_cast<std::size_t>(segment.fileOffset) + static_cast<std::size_t>(relative);
        if (!checkedRange(fileOffset, count, image.fileBytes.size())) return {};
        return std::span<const std::uint8_t>(image.fileBytes).subspan(fileOffset, count);
    }
    return {};
}

const assets::Rac1LevelOverlaySegment* overlaySegmentForPc(
    std::span<const assets::Rac1LevelOverlaySegment> segments,
    std::uint32_t pc) noexcept {
    for (const auto& segment : segments) {
        const std::uint64_t begin = segment.destination;
        const std::uint64_t end = begin + segment.payloadSize;
        if (pc >= begin && static_cast<std::uint64_t>(pc) < end) return &segment;
    }
    return nullptr;
}

} // namespace

Rac1StaticElfImage parseRac1StaticElfImage(std::span<const std::uint8_t> fileBytes) {
    Rac1StaticElfImage result{};
    if (fileBytes.size() < kElf32HeaderBytes) {
        result.status = Rac1StaticElfStatus::FileTooSmall;
        return result;
    }

    const bool magic = fileBytes[0] == 0x7fu && fileBytes[1] == 'E' &&
                       fileBytes[2] == 'L' && fileBytes[3] == 'F';
    const bool elf32LittleEndian = fileBytes[4] == 1u && fileBytes[5] == 1u;
    const std::uint16_t type = readLe16(fileBytes.data() + 16u);
    const std::uint16_t machine = readLe16(fileBytes.data() + 18u);
    if (!magic || !elf32LittleEndian || type != kElfTypeExec || machine != kElfMachineMips) {
        result.status = Rac1StaticElfStatus::InvalidHeader;
        return result;
    }

    const std::uint32_t phoff = readLe32(fileBytes.data() + 28u);
    const std::uint16_t phentsize = readLe16(fileBytes.data() + 42u);
    const std::uint16_t phnum = readLe16(fileBytes.data() + 44u);
    if (phentsize < kElf32ProgramHeaderBytes) {
        result.status = Rac1StaticElfStatus::InvalidProgramHeaders;
        return result;
    }
    const std::uint64_t phBytes = static_cast<std::uint64_t>(phentsize) * phnum;
    if (phoff > fileBytes.size() || phBytes > fileBytes.size() - phoff) {
        result.status = Rac1StaticElfStatus::InvalidProgramHeaders;
        return result;
    }

    result.fileBytes.assign(fileBytes.begin(), fileBytes.end());
    for (std::uint16_t i = 0u; i < phnum; ++i) {
        const std::size_t offset =
            static_cast<std::size_t>(phoff) + static_cast<std::size_t>(i) * phentsize;
        const std::uint8_t* ph = fileBytes.data() + offset;
        const std::uint32_t typeWord = readLe32(ph + 0u);
        const std::uint32_t fileOffset = readLe32(ph + 4u);
        const std::uint32_t guestAddress = readLe32(ph + 8u);
        const std::uint32_t fileSize = readLe32(ph + 16u);
        const std::uint32_t memorySize = readLe32(ph + 20u);
        if (typeWord != kElfPtLoad || memorySize == 0u) continue;
        if (fileSize > memorySize ||
            !checkedRange(fileOffset, fileSize, fileBytes.size()) ||
            fileSize > std::numeric_limits<std::uint32_t>::max() - guestAddress) {
            result.status = Rac1StaticElfStatus::InvalidLoadSegment;
            result.loadSegments.clear();
            result.fileBytes.clear();
            return result;
        }
        if (fileSize != 0u) {
            result.loadSegments.push_back({guestAddress, fileOffset, fileSize});
        }
    }

    if (result.loadSegments.empty()) {
        result.status = Rac1StaticElfStatus::NoLoadSegments;
        result.fileBytes.clear();
        return result;
    }
    result.status = Rac1StaticElfStatus::Ok;
    return result;
}

Rac1StaticElfImage loadRac1StaticElfImage(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const std::streamoff end = input.tellg();
    if (end < 0) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()))) {
        return {};
    }
    return parseRac1StaticElfImage(bytes);
}

const char* rac1StaticElfStatusName(Rac1StaticElfStatus status) noexcept {
    switch (status) {
    case Rac1StaticElfStatus::Ok: return "ok";
    case Rac1StaticElfStatus::FileOpenFailed: return "file-open-failed";
    case Rac1StaticElfStatus::FileTooSmall: return "file-too-small";
    case Rac1StaticElfStatus::InvalidHeader: return "invalid-header";
    case Rac1StaticElfStatus::InvalidProgramHeaders: return "invalid-program-headers";
    case Rac1StaticElfStatus::InvalidLoadSegment: return "invalid-load-segment";
    case Rac1StaticElfStatus::NoLoadSegments: return "no-load-segments";
    }
    return "unknown";
}

Rac1OverlayCoherenceResult inspectRac1OverlayCoherence(
    std::span<const std::uint8_t> guestRdram,
    std::span<const std::uint8_t> overlay,
    std::span<const assets::Rac1LevelOverlaySegment> segments,
    const Rac1StaticElfImage& staticElf,
    std::span<const std::uint32_t> registeredFallbackAddresses) {
    Rac1OverlayCoherenceResult result{};
    result.segmentCount = segments.size();
    if (overlay.empty() || segments.empty()) return result;
    if (!staticElf.ok()) {
        result.status = Rac1OverlayCoherenceStatus::StaticElfUnavailable;
        return result;
    }

    for (const auto& segment : segments) {
        if (!checkedRange(segment.payloadOffset, segment.payloadSize, overlay.size())) {
            result.status = Rac1OverlayCoherenceStatus::SegmentPayloadOutOfRange;
            return result;
        }
        if (!checkedRange(segment.destination, segment.payloadSize, guestRdram.size())) {
            result.status = Rac1OverlayCoherenceStatus::GuestDestinationOutOfRange;
            return result;
        }
        result.payloadBytes += segment.payloadSize;
        const auto expected = overlay.subspan(segment.payloadOffset, segment.payloadSize);
        const auto live = guestRdram.subspan(segment.destination, segment.payloadSize);
        if (std::equal(expected.begin(), expected.end(), live.begin(), live.end())) {
            ++result.materializedSegments;
            result.materializedBytes += segment.payloadSize;
        }
    }

    // Static code-generation coherence is independent of whether the Retail
    // loader has already materialized the overlay into guest RDRAM. Keep these
    // counts available even for OverlayNotMaterialized: they prove that the
    // level payload represents a different code generation from the boot ELF,
    // while the status below still distinguishes a prospective static conflict
    // from one that is actually resident in live guest memory.
    result.registeredFallbackEntries = registeredFallbackAddresses.size();
    for (const std::uint32_t pc : registeredFallbackAddresses) {
        const auto* segment = overlaySegmentForPc(segments, pc);
        if (segment == nullptr) continue;
        const std::size_t relative = static_cast<std::size_t>(pc - segment->destination);
        const std::size_t overlayAvailable = segment->payloadSize - relative;
        const std::size_t probeRequest = std::min(kConflictProbeBytes, overlayAvailable);
        if (probeRequest < 4u) continue;
        const auto staticBytes = staticElfBytesAt(staticElf, pc, probeRequest);
        if (staticBytes.size() < 4u) continue;
        const std::size_t probeBytes = std::min(probeRequest, staticBytes.size());
        const std::size_t overlayOffset =
            static_cast<std::size_t>(segment->payloadOffset) + relative;
        if (!checkedRange(overlayOffset, probeBytes, overlay.size())) {
            result.status = Rac1OverlayCoherenceStatus::SegmentPayloadOutOfRange;
            return result;
        }
        ++result.comparableFallbackEntries;
        const auto overlayBytes = overlay.subspan(overlayOffset, probeBytes);
        if (!std::equal(staticBytes.begin(), staticBytes.begin() + probeBytes,
                        overlayBytes.begin(), overlayBytes.end())) {
            ++result.conflictingFallbackEntries;
            if (!result.hasFirstConflictPc) {
                result.hasFirstConflictPc = true;
                result.firstConflictPc = pc;
            }
        }
    }

    const bool fullyMaterialized =
        result.materializedSegments == result.segmentCount &&
        result.materializedBytes == result.payloadBytes;
    if (!fullyMaterialized) {
        result.status = Rac1OverlayCoherenceStatus::OverlayNotMaterialized;
    }
    else if (registeredFallbackAddresses.empty()) {
        result.status = Rac1OverlayCoherenceStatus::NoRegisteredFallbackOverlap;
    }
    else if (result.comparableFallbackEntries == 0u) {
        result.status = Rac1OverlayCoherenceStatus::NoComparableFallbackOverlap;
    }
    else if (result.conflictingFallbackEntries == 0u) {
        result.status = Rac1OverlayCoherenceStatus::NoStaticConflictObserved;
    }
    else {
        result.status = Rac1OverlayCoherenceStatus::StaleFallbackConflictProved;
    }
    return result;
}

Rac1BootOverlayProgress inspectRac1BootOverlayProgress(
    std::span<const std::uint8_t> guestRdram) noexcept {
    using InputLayout = Rac1RetailParsedInputLayout;
    constexpr std::uint32_t kProcessedPressedEdgesAddress =
        InputLayout::kControllerStateAddress + InputLayout::kProcessedPressedEdgesOffset;
    static_assert(kProcessedPressedEdgesAddress == 0x0013CB04u);

    // Source ledger for the fixed addresses below:
    // - sub_001EB798 @ 0x1EB81C publishes its boot resource workspace at 0x15EE4C.
    // - FUN_001E9658 @ 0x1E9A6C loads 0x15ED84 and @ 0x1E9A84 stores it to 0x15F600.
    // - FUN_00231FF0 reads 0x15ED88 @ 0x232024, branches on 0x15F600 @ 0x2321A4,
    //   then reads 0x13D364/0x13D36C @ 0x2321B0/0x2321C0 before its 0x232218
    //   call to FUN_002043B0. The same function consumes 0x15F618 later at
    //   0x232638 and 0x2326D4.
    // - FUN_002043B0 @ 0x2043F4 replaces 0x15EE4C and @ 0x2043F8 writes 0x60
    //   to the new buffer before the async sector read into buffer+0x60.
    // - 0x13CAE4 is sampled as a raw lifecycle word. Existing generated
    //   consumers use different masks/branches, so no higher-level role is
    //   assigned here without a matching Retail control-flow edge.
    constexpr std::uint32_t kSharedLoaderPointerAddress = 0x0015EE4Cu;
    constexpr std::uint32_t kBootExitFlagAddress = 0x0015F5B0u;
    constexpr std::uint32_t kState13CAE4Address = 0x0013CAE4u;
    constexpr std::uint32_t kState15ED84Address = 0x0015ED84u;
    constexpr std::uint32_t kState15ED88Address = 0x0015ED88u;
    constexpr std::uint32_t kState15F600Address = 0x0015F600u;
    constexpr std::uint32_t kState15F604Address = 0x0015F604u;
    constexpr std::uint32_t kState15F618Address = 0x0015F618u;
    constexpr std::uint32_t kState13D364Address = 0x0013D364u;
    constexpr std::uint32_t kState13D36CAddress = 0x0013D36Cu;
    constexpr std::uint32_t kCallback2195OwnerAddress = 0x001D5BF4u;
    constexpr std::uint32_t kCallback2195TableOffset = 0x44u;
    constexpr std::uint32_t kCallback21E7C8 = 0x0021E7C8u;
    constexpr std::uint32_t kFirstGenerationRecordOffset = 0x60u;
    constexpr std::uint32_t kWad158FirstDestination = 0x0015EF00u;
    constexpr std::uint32_t kWad158FirstPayloadBytes = 0x2328u;
    constexpr std::uint32_t kWad158FirstField8 = 1u;
    constexpr std::uint32_t kWad158GenerationEntry = 0x001E9658u;
    constexpr std::size_t kRecordHeaderBytes = 0x10u;

    Rac1BootOverlayProgress result{};
    const auto readable32 = [&](std::uint32_t address) noexcept {
        return checkedRange(static_cast<std::size_t>(address), 4u, guestRdram.size());
    };
    const std::array<std::uint32_t, 11u> fixedReads{
        kProcessedPressedEdgesAddress,
        kBootExitFlagAddress,
        kState13CAE4Address,
        kSharedLoaderPointerAddress,
        kState15ED84Address,
        kState15ED88Address,
        kState15F600Address,
        kState15F604Address,
        kState15F618Address,
        kState13D364Address,
        kState13D36CAddress,
    };
    if (std::any_of(fixedReads.begin(), fixedReads.end(),
                    [&](std::uint32_t address) { return !readable32(address); })) {
        result.status = Rac1BootOverlayProgressStatus::GuestMemoryTooSmall;
        return result;
    }

    result.processedPressedEdges =
        readLe32(guestRdram.data() + kProcessedPressedEdgesAddress);
    result.processedRightEdge =
        (result.processedPressedEdges & rac1PadButtonMask(Rac1PadButton::Right)) != 0u;
    result.bootExitFlag = readLe32(guestRdram.data() + kBootExitFlagAddress);
    result.state13CAE4 = readLe32(guestRdram.data() + kState13CAE4Address);
    result.state15ED84 = readLe32(guestRdram.data() + kState15ED84Address);
    result.state15ED88 = readLe32(guestRdram.data() + kState15ED88Address);
    result.state15F600 = readLe32(guestRdram.data() + kState15F600Address);
    result.state15F604 = readLe32(guestRdram.data() + kState15F604Address);
    result.state15F618 = readLe32(guestRdram.data() + kState15F618Address);
    result.state13D364 = readLe32(guestRdram.data() + kState13D364Address);
    result.state13D36C = readLe32(guestRdram.data() + kState13D36CAddress);

    // These names intentionally mirror the exact branch PCs rather than assigning
    // an unproved higher-level game meaning to the underlying state words.
    result.branch2321A4Bypass = (result.state15F600 & 0x80000000u) == 0u;
    const bool state13D364LessThan3 =
        (result.state13D364 & 0x80000000u) != 0u || result.state13D364 < 3u;
    result.branch2321B8Wait = !state13D364LessThan3;
    result.branch2321C4EnterSetup = (result.state13D36C & 0x80000000u) != 0u;

    // Mirror the exact sub_002192A8 callback walk at 0x2195AC..0x219600.
    // This is intentionally optional: early/synthetic snapshots smaller than the
    // callback-global address remain valid for the loader-state inspection above.
    if (readable32(kCallback2195OwnerAddress)) {
        result.callback2195OwnerPointer =
            readLe32(guestRdram.data() + kCallback2195OwnerAddress);
        if (result.callback2195OwnerPointer != 0u) {
            const std::uint64_t table64 =
                static_cast<std::uint64_t>(result.callback2195OwnerPointer) +
                kCallback2195TableOffset;
            constexpr std::size_t tableBytes =
                Rac1BootOverlayProgress::kCallback2195SlotCount * sizeof(std::uint32_t);
            if (table64 <= std::numeric_limits<std::uint32_t>::max() &&
                checkedRange(static_cast<std::size_t>(table64), tableBytes, guestRdram.size())) {
                result.callback2195TablePointer = static_cast<std::uint32_t>(table64);
                result.callback2195TableReadable = true;
                for (std::size_t slot = 0u;
                     slot < Rac1BootOverlayProgress::kCallback2195SlotCount; ++slot) {
                    const std::uint32_t object = readLe32(
                        guestRdram.data() + result.callback2195TablePointer + slot * 4u);
                    if (object == 0u) continue;
                    ++result.callback2195NonNullObjects;
                    if (!readable32(object)) continue;
                    ++result.callback2195ReadableObjects;
                    const std::uint32_t target = readLe32(guestRdram.data() + object);
                    result.callback2195Targets[slot] = target;
                    if (target == 0u) continue;
                    ++result.callback2195NonNullTargets;
                    if (!result.callback21E7C8Present && target == kCallback21E7C8) {
                        result.callback21E7C8Present = true;
                        result.callback21E7C8Slot = static_cast<std::uint32_t>(slot);
                    }
                }
            }
        }
    }

    result.sharedLoaderPointer =
        readLe32(guestRdram.data() + kSharedLoaderPointerAddress);
    if (result.sharedLoaderPointer == 0u) {
        result.status = Rac1BootOverlayProgressStatus::SharedLoaderPointerUnavailable;
        return result;
    }
    if (!readable32(result.sharedLoaderPointer)) {
        result.status = Rac1BootOverlayProgressStatus::SharedLoaderHeaderOutOfRange;
        return result;
    }

    result.sharedLoaderPrefixBytes =
        readLe32(guestRdram.data() + result.sharedLoaderPointer);
    result.firstGenerationStreamSignatureObserved =
        result.sharedLoaderPrefixBytes == kFirstGenerationRecordOffset;
    if (!result.firstGenerationStreamSignatureObserved) {
        // In particular, sub_001EB798's resource workspace can legitimately occupy
        // 0x15EE4C here. Do not reinterpret its first word as a record offset.
        result.status = Rac1BootOverlayProgressStatus::FirstGenerationStreamSignatureAbsent;
        return result;
    }

    const std::uint64_t firstRecord64 =
        static_cast<std::uint64_t>(result.sharedLoaderPointer) +
        kFirstGenerationRecordOffset;
    if (firstRecord64 > std::numeric_limits<std::uint32_t>::max() ||
        !checkedRange(static_cast<std::size_t>(firstRecord64),
                      kRecordHeaderBytes, guestRdram.size())) {
        result.status = Rac1BootOverlayProgressStatus::FirstRecordOutOfRange;
        return result;
    }

    result.firstRecordAddress = static_cast<std::uint32_t>(firstRecord64);
    const std::uint8_t* record = guestRdram.data() + result.firstRecordAddress;
    result.firstDestination = readLe32(record + 0x00u);
    result.firstPayloadBytes = readLe32(record + 0x04u);
    result.firstField8 = readLe32(record + 0x08u);
    result.firstGenerationEntry = readLe32(record + 0x0Cu);
    result.wad158FirstRecordHeaderMatched =
        result.firstDestination == kWad158FirstDestination &&
        result.firstPayloadBytes == kWad158FirstPayloadBytes &&
        result.firstField8 == kWad158FirstField8 &&
        result.firstGenerationEntry == kWad158GenerationEntry;
    if (!result.wad158FirstRecordHeaderMatched) {
        // FUN_002043B0 writes the 0x60 prefix before it submits the async read.
        // A matching prefix therefore proves the setup signature, not completion.
        // Require the exact first four WAD158 words before marking the record header matched.
        result.status = Rac1BootOverlayProgressStatus::Wad158FirstRecordHeaderMismatch;
        return result;
    }

    const std::uint64_t payloadBegin64 = firstRecord64 + kRecordHeaderBytes;
    if (payloadBegin64 > guestRdram.size() ||
        result.firstPayloadBytes > guestRdram.size() -
            static_cast<std::size_t>(payloadBegin64)) {
        result.status = Rac1BootOverlayProgressStatus::FirstPayloadOutOfRange;
        return result;
    }

    result.status = Rac1BootOverlayProgressStatus::Ok;
    return result;
}

const char* rac1BootOverlayProgressStatusName(
    Rac1BootOverlayProgressStatus status) noexcept {
    switch (status) {
    case Rac1BootOverlayProgressStatus::Ok: return "ok";
    case Rac1BootOverlayProgressStatus::GuestMemoryTooSmall: return "guest-memory-too-small";
    case Rac1BootOverlayProgressStatus::SharedLoaderPointerUnavailable:
        return "shared-loader-pointer-unavailable";
    case Rac1BootOverlayProgressStatus::SharedLoaderHeaderOutOfRange:
        return "shared-loader-header-out-of-range";
    case Rac1BootOverlayProgressStatus::FirstGenerationStreamSignatureAbsent:
        return "first-generation-stream-signature-absent";
    case Rac1BootOverlayProgressStatus::Wad158FirstRecordHeaderMismatch:
        return "wad158-first-record-header-mismatch";
    case Rac1BootOverlayProgressStatus::FirstRecordOutOfRange:
        return "first-record-out-of-range";
    case Rac1BootOverlayProgressStatus::FirstPayloadOutOfRange:
        return "first-payload-out-of-range";
    }
    return "unknown";
}

const char* rac1OverlayCoherenceStatusName(Rac1OverlayCoherenceStatus status) noexcept {
    switch (status) {
    case Rac1OverlayCoherenceStatus::OverlayUnavailable: return "overlay-unavailable";
    case Rac1OverlayCoherenceStatus::StaticElfUnavailable: return "static-elf-unavailable";
    case Rac1OverlayCoherenceStatus::SegmentPayloadOutOfRange: return "segment-payload-out-of-range";
    case Rac1OverlayCoherenceStatus::GuestDestinationOutOfRange: return "guest-destination-out-of-range";
    case Rac1OverlayCoherenceStatus::OverlayNotMaterialized: return "overlay-not-materialized";
    case Rac1OverlayCoherenceStatus::NoRegisteredFallbackOverlap: return "no-registered-fallback-overlap";
    case Rac1OverlayCoherenceStatus::NoComparableFallbackOverlap: return "no-comparable-fallback-overlap";
    case Rac1OverlayCoherenceStatus::NoStaticConflictObserved: return "no-static-conflict-observed";
    case Rac1OverlayCoherenceStatus::StaleFallbackConflictProved: return "stale-fallback-conflict-proved";
    }
    return "unknown";
}

} // namespace ratchet::game
