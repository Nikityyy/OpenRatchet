#include "assets/rac1_level.h"

#include "assets/wad_decompressor.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <span>
#include <utility>
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
    // Retail LevelCoreHeader stores every table pair as {count, offset}.
    // OpenRatchet exposes {offset, count} to make range checks less error-prone.
    return {readLe32(bytes.data() + offset + 4u),
            readLe32(bytes.data() + offset)};
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

bool arrayRangeFits(const Rac1ArrayRange& range,
                    std::uint32_t elementSize,
                    std::uint64_t capacity) noexcept {
    if (range.count == 0u) {
        return true;
    }
    std::uint64_t bytes = 0u;
    return checkedMul(range.count, elementSize, bytes) &&
           static_cast<std::uint64_t>(range.offset) <= capacity &&
           bytes <= capacity - static_cast<std::uint64_t>(range.offset);
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
    // Zero is a valid address in the decompressed level core. R&C1 commonly
    // places the TfragBlockHeader at core offset zero.
    return static_cast<std::size_t>(offset) < coreBytes;
}

Rac1LevelCoreLoadResult fail(Rac1LevelInspectStatus status,
                             Rac1LevelSummary summary) noexcept {
    return {status, summary, {}, {}, {}, {}};
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
    case Rac1LevelInspectStatus::InvalidGsRamRange:
        return "invalid-gs-ram-range";
    case Rac1LevelInspectStatus::InvalidCoreDataRange:
        return "invalid-core-data-range";
    case Rac1LevelInspectStatus::InvalidCoreHeader:
        return "invalid-core-header";
    case Rac1LevelInspectStatus::InvalidCoreWad:
        return "invalid-core-wad";
    case Rac1LevelInspectStatus::CoreDecompressionFailed:
        return "core-decompression-failed";
    case Rac1LevelInspectStatus::InvalidGameplayWad:
        return "invalid-gameplay-wad";
    case Rac1LevelInspectStatus::GameplayDecompressionFailed:
        return "gameplay-decompression-failed";
    case Rac1LevelInspectStatus::InvalidRenderOffsets:
        return "invalid-render-offsets";
    case Rac1LevelInspectStatus::InvalidIndexArrays:
        return "invalid-index-arrays";
    }
    return "unknown";
}

Rac1LevelCoreLoadResult loadRac1LevelCore(const std::filesystem::path& path,
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
    if (summary.gsRam.size != 0u &&
        !byteRangeFits(summary.gsRam.offset, summary.gsRam.size, dataBytes)) {
        return fail(Rac1LevelInspectStatus::InvalidGsRamRange, summary);
    }
    if (summary.coreData.size < kWadHeaderBytes ||
        !byteRangeFits(summary.coreData.offset, summary.coreData.size, dataBytes)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreDataRange, summary);
    }

    std::uint64_t coreIndexFileOffset = 0u;
    if (!checkedAdd(dataFileOffset, summary.coreIndex.offset, coreIndexFileOffset)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreIndexRange, summary);
    }
    std::vector<std::uint8_t> coreIndex(summary.coreIndex.size, 0u);
    if (!readExact(input, coreIndexFileOffset, coreIndex)) {
        return fail(Rac1LevelInspectStatus::InvalidCoreHeader, summary);
    }
    const std::span<const std::uint8_t> coreHeader(coreIndex.data(), kLevelCoreHeaderBytes);

    std::vector<std::uint8_t> gsRam;
    if (summary.gsRam.size != 0u) {
        std::uint64_t gsRamFileOffset = 0u;
        if (!checkedAdd(dataFileOffset, summary.gsRam.offset, gsRamFileOffset)) {
            return fail(Rac1LevelInspectStatus::InvalidGsRamRange, summary);
        }
        gsRam.resize(summary.gsRam.size);
        if (!readExact(input, gsRamFileOffset, gsRam)) {
            return fail(Rac1LevelInspectStatus::InvalidGsRamRange, summary);
        }
    }

    summary.gsRamTable = readArrayRange(coreHeader, 0x00u);
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
    summary.texturesBaseOffset = readLe32(coreHeader.data() + 0x60u);
    summary.particleBankOffset = readLe32(coreHeader.data() + 0x64u);
    summary.effectBankOffset = readLe32(coreHeader.data() + 0x68u);
    summary.particleDefsOffset = readLe32(coreHeader.data() + 0x6cu);
    summary.soundRemapOffset = readLe32(coreHeader.data() + 0x70u);
    summary.ratchetSequencesOffset = readLe32(coreHeader.data() + 0x74u);
    summary.sceneViewSize = readLe32(coreHeader.data() + 0x7cu);
    summary.gadgetCount = readLe32(coreHeader.data() + 0x80u);
    summary.gadgetOffset = readLe32(coreHeader.data() + 0x84u);
    summary.assetsCompressedSize = readLe32(coreHeader.data() + 0x88u);
    summary.assetsDecompressedSize = readLe32(coreHeader.data() + 0x8cu);
    summary.heightmapOffset = readLe32(coreHeader.data() + 0xa4u);
    summary.occlusionOctOffset = readLe32(coreHeader.data() + 0xa8u);
    summary.mobyGsStashListOffset = readLe32(coreHeader.data() + 0xacu);
    summary.occlusionRadOffset = readLe32(coreHeader.data() + 0xb0u);
    summary.mobySoundRemapOffset = readLe32(coreHeader.data() + 0xb4u);
    summary.occlusionRad2Offset = readLe32(coreHeader.data() + 0xb8u);

    // These tables live in the separate core-index blob, not the decompressed
    // core-data WAD. Validate the sizes now so later renderer stages can trust
    // the typed metadata.
    if (!arrayRangeFits(summary.gsRamTable, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.mobyClasses, 0x20u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.tieClasses, 0x20u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.shrubClasses, 0x30u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.tfragTextures, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.mobyTextures, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.tieTextures, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.shrubTextures, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.particleTextures, 0x10u, summary.coreIndex.size) ||
        !arrayRangeFits(summary.effectTextures, 0x10u, summary.coreIndex.size)) {
        return fail(Rac1LevelInspectStatus::InvalidIndexArrays, summary);
    }

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

    std::vector<std::uint8_t> core(kMaxCoreOutputBytes, 0u);
    const WadDecompressResult decompressed = decompressWad(encoded, core);
    if (!decompressed.ok()) {
        return fail(Rac1LevelInspectStatus::CoreDecompressionFailed, summary);
    }
    summary.coreDecompressedBytes = decompressed.bytesWritten;
    core.resize(summary.coreDecompressedBytes);

    // Gameplay is a second WAD stored in the amalgamated level sector span.
    // It owns the live instance lists (ties/shrubs/mobys), while class geometry
    // lives in the decompressed level core. Decode it now so native scene stages
    // can join class data with authentic retail transforms without guest RAM.
    std::uint64_t gameplayFileOffset = 0u;
    std::uint64_t gameplayRangeBytes = 0u;
    if (!sectorRangeToFileRange(summary.gameplayNtsc,
                                discStartSector,
                                fileBytes,
                                gameplayFileOffset,
                                gameplayRangeBytes) ||
        gameplayRangeBytes < kWadHeaderBytes) {
        return fail(Rac1LevelInspectStatus::InvalidGameplayWad, summary);
    }
    std::array<std::uint8_t, kWadHeaderBytes> gameplayHeader{};
    if (!readExact(input, gameplayFileOffset, gameplayHeader) ||
        gameplayHeader[0] != 'W' || gameplayHeader[1] != 'A' || gameplayHeader[2] != 'D') {
        return fail(Rac1LevelInspectStatus::InvalidGameplayWad, summary);
    }
    summary.gameplayEncodedSize = readLe32(gameplayHeader.data() + 3u);
    if (summary.gameplayEncodedSize < kWadHeaderBytes ||
        summary.gameplayEncodedSize > gameplayRangeBytes) {
        return fail(Rac1LevelInspectStatus::InvalidGameplayWad, summary);
    }
    std::vector<std::uint8_t> gameplayEncoded(summary.gameplayEncodedSize, 0u);
    if (!readExact(input, gameplayFileOffset, gameplayEncoded)) {
        return fail(Rac1LevelInspectStatus::InvalidGameplayWad, summary);
    }
    std::vector<std::uint8_t> gameplay(kMaxCoreOutputBytes, 0u);
    const WadDecompressResult gameplayDecompressed = decompressWad(gameplayEncoded, gameplay);
    if (!gameplayDecompressed.ok()) {
        return fail(Rac1LevelInspectStatus::GameplayDecompressionFailed, summary);
    }
    summary.gameplayDecompressedBytes = gameplayDecompressed.bytesWritten;
    gameplay.resize(summary.gameplayDecompressedBytes);

    if (!renderOffsetFits(summary.tfragsOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.occlusionOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.skyOffset, summary.coreDecompressedBytes) ||
        !renderOffsetFits(summary.collisionOffset, summary.coreDecompressedBytes) ||
        summary.texturesBaseOffset >= summary.coreDecompressedBytes) {
        return fail(Rac1LevelInspectStatus::InvalidRenderOffsets, summary);
    }

    return {Rac1LevelInspectStatus::Ok, summary, std::move(core),
            std::move(coreIndex), std::move(gsRam), std::move(gameplay)};
}

Rac1LevelInspectResult inspectRac1Level(const std::filesystem::path& path,
                                        std::uint32_t tocIndex,
                                        std::uint32_t discStartSector,
                                        std::uint32_t discSectorCount,
                                        std::uint32_t discHeaderSector) {
    Rac1LevelCoreLoadResult loaded = loadRac1LevelCore(path,
                                                       tocIndex,
                                                       discStartSector,
                                                       discSectorCount,
                                                       discHeaderSector);
    return {loaded.status, loaded.summary};
}

} // namespace ratchet::assets
