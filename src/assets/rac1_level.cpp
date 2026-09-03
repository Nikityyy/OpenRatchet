#include "assets/rac1_level.h"

#include "assets/wad_decompressor.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::uint64_t kSectorBytes = 0x800u;
constexpr std::size_t kAmalgamatedHeaderBytes = 0x2434u;
constexpr std::size_t kLevelDataHeaderBytes = 0x58u;
constexpr std::size_t kLevelCoreHeaderBytes = 0xbcu;
constexpr std::size_t kWadHeaderBytes = 0x10u;
constexpr std::size_t kMaxCoreOutputBytes = 64u * 1024u * 1024u;

std::uint32_t readLe32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

Rac1SectorRange readSectorRange(std::span<const std::uint8_t> bytes,
                                std::size_t offset) noexcept {
    return {readLe32(bytes.data() + offset),
            readLe32(bytes.data() + offset + 4u)};
}

Rac1ByteRange readByteRange(std::span<const std::uint8_t> bytes,
                            std::size_t offset) noexcept {
    return {readLe32(bytes.data() + offset),
            readLe32(bytes.data() + offset + 4u)};
}

Rac1ArrayRange readArrayRange(std::span<const std::uint8_t> bytes,
                              std::size_t offset) noexcept {
    return {readLe32(bytes.data() + offset),
            readLe32(bytes.data() + offset + 4u)};
}

bool checkedMul(std::uint64_t lhs,
                std::uint64_t rhs,
                std::uint64_t& result) noexcept {
    if (rhs != 0u && lhs > std::numeric_limits<std::uint64_t>::max() / rhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

bool checkedAdd(std::uint64_t lhs,
                std::uint64_t rhs,
                std::uint64_t& result) noexcept {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

bool byteRangeFits(std::uint32_t offset,
                   std::uint32_t size,
                   std::uint64_t capacity) noexcept {
    return static_cast<std::uint64_t>(offset) <= capacity &&
           static_cast<std::uint64_t>(size) <=
               capacity - static_cast<std::uint64_t>(offset);
}

bool readExact(std::ifstream& input,
               std::uint64_t offset,
               std::span<std::uint8_t> destination) {
    if (destination.empty() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    input.clear();
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return false;
    }
    input.read(reinterpret_cast<char*>(destination.data()),
               static_cast<std::streamsize>(destination.size()));
    return input.gcount() == static_cast<std::streamsize>(destination.size());
}

bool sectorRangeToFileRange(const Rac1SectorRange& range,
                            std::uint32_t levelStartSector,
                            std::uint64_t fileBytes,
                            std::uint64_t& offset,
                            std::uint64_t& size) noexcept {
    if (range.startSector < levelStartSector || range.sectorCount == 0u) {
        return false;
    }

    const std::uint64_t relativeSector =
        static_cast<std::uint64_t>(range.startSector) - levelStartSector;
    if (!checkedMul(relativeSector, kSectorBytes, offset) ||
        !checkedMul(range.sectorCount, kSectorBytes, size)) {
        return false;
    }

    std::uint64_t end = 0u;
    return checkedAdd(offset, size, end) && end <= fileBytes;
}

bool renderOffsetFits(std::uint32_t offset, std::size_t coreBytes) noexcept {
    // Zero is a valid "not present" value for optional blocks. Tfrags are
    // expected to be non-zero on renderable retail levels but are not treated
    // as mandatory here so non-gameplay/special levels remain inspectable.
    return offset == 0u || offset < coreBytes;
}

Rac1LevelInspectResult fail(Rac1LevelInspectStatus status,
                            Rac1LevelSummary summary) noexcept {
    return {status, summary};
}

} // namespace

const char* rac1LevelInspectStatusName(Rac1LevelInspectStatus status) noexcept {
    switch (status) {
    case Rac1LevelInspectStatus::Ok:
        return "ok";
    case Rac1LevelInspectStatus::FileOpenFailed:
        return "file-open-failed";
    case Rac1LevelInspectStatus::FileSizeMismatch:
        return "file-size-mismatch";
    case Rac1LevelInspectStatus::HeaderTooSmall:
        return "header-too-small";
    case Rac1LevelInspectStatus::InvalidHeaderSize:
        return "invalid-header-size";
    case Rac1LevelInspectStatus::InvalidDataRange:
        return "invalid-data-range";
    case Rac1LevelInspectStatus::InvalidLevelDataHeader:
        return "invalid-level-data-header";
    case Rac1LevelInspectStatus::InvalidCoreIndexRange:
        return "invalid-core-index-range";
    case Rac1LevelInspectStatus::InvalidCoreDataRange:
        return "invalid-core-data-range";
    case Rac1LevelInspectStatus::InvalidCoreHeader:
        return "invalid-core-header";
    case Rac1LevelInspectStatus::InvalidCoreWad:
        return "invalid-core-wad";
    case Rac1LevelInspectStatus::CoreDecompressionFailed:
        return "core-decompression-failed";
    case Rac1LevelInspectStatus::InvalidRenderOffsets:
        return "invalid-render-offsets";
    }
    return "unknown";
}

Rac1LevelInspectResult inspectRac1Level(const std::filesystem::path& path,
                                        std::uint32_t tocIndex,
                                        std::uint32_t discStartSector,
                                        std::uint32_t discSectorCount,
                                        std::uint32_t discHeaderSector) {
    Rac1LevelSummary summary{};
    summary.tocIndex = tocIndex;
    summary.discStartSector = discStartSector;
    summary.discSectorCount = discSectorCount;
    summary.discHeaderSector = discHeaderSector;

    std::error_code sizeError;
    const std::uint64_t fileBytes = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        return fail(Rac1LevelInspectStatus::FileOpenFailed, summary);
    }

    std::uint64_t expectedBytes = 0u;
    if (!checkedMul(discSectorCount, kSectorBytes, expectedBytes) ||
        fileBytes != expectedBytes) {
        return fail(Rac1LevelInspectStatus::FileSizeMismatch, summary);
    }
    if (fileBytes < kAmalgamatedHeaderBytes ||
        discHeaderSector < discStartSector) {
        return fail(Rac1LevelInspectStatus::HeaderTooSmall, summary);
    }

    const std::uint64_t headerRelativeSector =
        static_cast<std::uint64_t>(discHeaderSector) - discStartSector;
    std::uint64_t headerFileOffset = 0u;
    if (!checkedMul(headerRelativeSector, kSectorBytes, headerFileOffset) ||
        headerFileOffset > fileBytes ||
        kAmalgamatedHeaderBytes > fileBytes - headerFileOffset) {
        return fail(Rac1LevelInspectStatus::HeaderTooSmall, summary);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail(Rac1LevelInspectStatus::FileOpenFailed, summary);
    }

    std::vector<std::uint8_t> header(kAmalgamatedHeaderBytes, 0u);
    if (!readExact(input, headerFileOffset, header)) {
        return fail(Rac1LevelInspectStatus::HeaderTooSmall, summary);
    }

    summary.levelId = readLe32(header.data() + 0x000u);
    summary.headerSize = readLe32(header.data() + 0x004u);
    summary.data = readSectorRange(header, 0x008u);
    summary.gameplayNtsc = readSectorRange(header, 0x010u);
    summary.gameplayPal = readSectorRange(header, 0x018u);
    summary.occlusion = readSectorRange(header, 0x020u);

    if (summary.headerSize != kAmalgamatedHeaderBytes) {
        return fail(Rac1LevelInspectStatus::InvalidHeaderSize, summary);
    }

    std::uint64_t dataFileOffset = 0u;
    std::uint64_t dataBytes = 0u;
    if (!sectorRangeToFileRange(summary.data,
                                discStartSector,
                                fileBytes,
                                dataFileOffset,
                                dataBytes) ||
        dataBytes < kLevelDataHeaderBytes) {
        return fail(Rac1LevelInspectStatus::InvalidDataRange, summary);
    }

    std::array<std::uint8_t, kLevelDataHeaderBytes> dataHeader{};
    if (!readExact(input, dataFileOffset, dataHeader)) {
        return fail(Rac1LevelInspectStatus::InvalidLevelDataHeader, summary);
    }

    summary.overlay = readByteRange(dataHeader, 0x00u);
    summary.soundBank = readByteRange(dataHeader, 0x08u);
    summary.coreIndex = readByteRange(dataHeader, 0x10u);
    summary.gsRam = readByteRange(dataHeader, 0x18u);
    summary.hudHeader = readByteRange(dataHeader, 0x20u);
    for (std::size_t i = 0u; i < summary.hudBanks.size(); ++i) {
        summary.hudBanks[i] = readByteRange(dataHeader, 0x28u + i * 8u);
    }
    summary.coreData = readByteRange(dataHeader, 0x50u);

    if (summary.coreIndex.size < kLevelCoreHeaderBytes ||
        !byteRangeFits(summary.coreIndex.offset, summary.coreIndex.size, dataBytes)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreIndexRange, summary);
    }
    if (summary.coreData.size < kWadHeaderBytes ||
        !byteRangeFits(summary.coreData.offset, summary.coreData.size, dataBytes)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreDataRange, summary);
    }

    std::uint64_t coreIndexFileOffset = 0u;
    if (!checkedAdd(dataFileOffset, summary.coreIndex.offset, coreIndexFileOffset)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreIndexRange, summary);
    }
    std::array<std::uint8_t, kLevelCoreHeaderBytes> coreHeader{};
    if (!readExact(input, coreIndexFileOffset, coreHeader)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreHeader, summary);
    }

    summary.tfragsOffset = readLe32(coreHeader.data() + 0x08u);
    summary.occlusionOffset = readLe32(coreHeader.data() + 0x0cu);
    summary.skyOffset = readLe32(coreHeader.data() + 0x10u);
    summary.collisionOffset = readLe32(coreHeader.data() + 0x14u);
    summary.mobyClasses = readArrayRange(coreHeader, 0x18u);
    summary.tieClasses = readArrayRange(coreHeader, 0x20u);
    summary.shrubClasses = readArrayRange(coreHeader, 0x28u);
    summary.tfragTextures = readArrayRange(coreHeader, 0x30u);
    summary.mobyTextures = readArrayRange(coreHeader, 0x38u);
    summary.tieTextures = readArrayRange(coreHeader, 0x40u);
    summary.shrubTextures = readArrayRange(coreHeader, 0x48u);
    summary.particleTextures = readArrayRange(coreHeader, 0x50u);
    summary.effectTextures = readArrayRange(coreHeader, 0x58u);
    summary.assetsCompressedSize = readLe32(coreHeader.data() + 0x88u);
    summary.assetsDecompressedSize = readLe32(coreHeader.data() + 0x8cu);

    std::uint64_t coreDataFileOffset = 0u;
    if (!checkedAdd(dataFileOffset, summary.coreData.offset, coreDataFileOffset)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreDataRange, summary);
    }

    std::array<std::uint8_t, kWadHeaderBytes> wadHeader{};
    if (!readExact(input, coreDataFileOffset, wadHeader) ||
        wadHeader[0] != 'W' || wadHeader[1] != 'A' || wadHeader[2] != 'D') {
        return fail(Rac1LevelInspectStatus::InvalidCoreWad, summary);
    }
    summary.coreEncodedSize = readLe32(wadHeader.data() + 3u);
    if (summary.coreEncodedSize < kWadHeaderBytes ||
        summary.coreEncodedSize > summary.coreData.size) {
        return fail(Rac1LevelInspectStatus::InvalidCoreWad, summary);
    }

    std::vector<std::uint8_t> encoded(summary.coreEncodedSize, 0u);
    if (!readExact(input, coreDataFileOffset, encoded)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreWad, summary);
    }

    // The retail game only has 32 MiB of EE RAM. A 64 MiB host scratch output
    // is deliberately generous while still bounding malformed input. Once the
    // renderer owns these assets this vector will become the native level-core
    // storage rather than a diagnostic scratch buffer.
    std::vector<std::uint8_t> core(kMaxCoreOutputBytes, 0u);
    const WadDecompressResult decompressed = decompressWad(encoded, core);
    if (!decompressed.ok()) {
        return fail(Rac1LevelInspectStatus::CoreDecompressionFailed, summary);
    }
    summary.coreDecompressedBytes = decompressed.bytesWritten;

    if (!renderOffsetFits(summary.tfragsOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.occlusionOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.skyOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.collisionOffset, summary.coreDecompressedBytes)) {
        return fail(Rac1LevelInspectStatus::InvalidRenderOffsets, summary);
    }

    return {Rac1LevelInspectStatus::Ok, summary};
}

} // namespace ratchet::assets
