#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ratchet::assets {

struct Rac1SectorRange {
    std::uint32_t startSector = 0u;
    std::uint32_t sectorCount = 0u;
};

struct Rac1ByteRange {
    std::uint32_t offset = 0u;
    std::uint32_t size = 0u;
};

struct Rac1ArrayRange {
    std::uint32_t offset = 0u;
    std::uint32_t count = 0u;
};

enum class Rac1LevelInspectStatus : std::uint8_t {
    Ok,
    FileOpenFailed,
    FileSizeMismatch,
    HeaderTooSmall,
    InvalidHeaderSize,
    InvalidDataRange,
    InvalidLevelDataHeader,
    InvalidCoreIndexRange,
    InvalidGsRamRange,
    InvalidCoreDataRange,
    InvalidCoreHeader,
    InvalidCoreWad,
    CoreDecompressionFailed,
    InvalidGameplayWad,
    GameplayDecompressionFailed,
    InvalidRenderOffsets,
    InvalidIndexArrays,
};

struct Rac1LevelSummary {
    std::uint32_t tocIndex = 0u;
    std::uint32_t levelId = 0u;
    std::uint32_t discStartSector = 0u;
    std::uint32_t discSectorCount = 0u;
    std::uint32_t discHeaderSector = 0u;
    std::uint32_t headerSize = 0u;

    Rac1SectorRange data{};
    Rac1SectorRange gameplayNtsc{};
    Rac1SectorRange gameplayPal{};
    Rac1SectorRange occlusion{};

    Rac1ByteRange overlay{};
    Rac1ByteRange soundBank{};
    Rac1ByteRange coreIndex{};
    Rac1ByteRange gsRam{};
    Rac1ByteRange hudHeader{};
    std::array<Rac1ByteRange, 5> hudBanks{};
    Rac1ByteRange coreData{};

    // LevelCoreHeader (0xbc bytes). The array pairs are stored on disc as
    // {count, offset}; Rac1ArrayRange exposes them in the host-friendly
    // {offset, count} order.
    Rac1ArrayRange gsRamTable{};
    std::uint32_t tfragsOffset = 0u;
    std::uint32_t occlusionOffset = 0u;
    std::uint32_t skyOffset = 0u;
    std::uint32_t collisionOffset = 0u;

    Rac1ArrayRange mobyClasses{};
    Rac1ArrayRange tieClasses{};
    Rac1ArrayRange shrubClasses{};
    Rac1ArrayRange tfragTextures{};
    Rac1ArrayRange mobyTextures{};
    Rac1ArrayRange tieTextures{};
    Rac1ArrayRange shrubTextures{};
    Rac1ArrayRange particleTextures{};
    Rac1ArrayRange effectTextures{};

    std::uint32_t texturesBaseOffset = 0u;
    std::uint32_t particleBankOffset = 0u;
    std::uint32_t effectBankOffset = 0u;
    std::uint32_t particleDefsOffset = 0u;
    std::uint32_t soundRemapOffset = 0u;
    // Raw LevelCoreHeader +0x74 value. Earlier Phase-10 notes assigned this
    // to Ratchet animation data. Step 12 disproved that assignment, so keep
    // the lane explicitly unnamed until a retail consumer proves its role.
    std::uint32_t coreHeader74 = 0u;
    // LevelCoreHeader +0x78: core-index-relative table of Ratchet's external
    // animation sequence pointers. Level 0 contains exactly 134 entries, matching
    // oClass 0's sequenceCount byte.
    std::uint32_t ratchetSequenceTableOffset = 0u;
    // Raw LevelCoreHeader +0x7c value. The previous sceneViewSize name was
    // not backed by a verified consumer and is intentionally retired.
    std::uint32_t coreHeader7c = 0u;
    std::uint32_t gadgetCount = 0u;
    std::uint32_t gadgetOffset = 0u;
    std::uint32_t assetsCompressedSize = 0u;
    std::uint32_t assetsDecompressedSize = 0u;
    std::uint32_t heightmapOffset = 0u;
    std::uint32_t occlusionOctOffset = 0u;
    std::uint32_t mobyGsStashListOffset = 0u;
    std::uint32_t occlusionRadOffset = 0u;
    std::uint32_t mobySoundRemapOffset = 0u;
    std::uint32_t occlusionRad2Offset = 0u;

    std::uint32_t coreEncodedSize = 0u;
    std::size_t coreDecompressedBytes = 0u;
    std::uint32_t gameplayEncodedSize = 0u;
    std::size_t gameplayDecompressedBytes = 0u;
};

struct Rac1LevelInspectResult {
    Rac1LevelInspectStatus status = Rac1LevelInspectStatus::FileOpenFailed;
    Rac1LevelSummary summary{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LevelInspectStatus::Ok;
    }
};

struct Rac1LevelCoreLoadResult {
    Rac1LevelInspectStatus status = Rac1LevelInspectStatus::FileOpenFailed;
    Rac1LevelSummary summary{};
    std::vector<std::uint8_t> core;
    std::vector<std::uint8_t> coreIndex;
    std::vector<std::uint8_t> gsRam;
    std::vector<std::uint8_t> gameplay;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LevelInspectStatus::Ok;
    }
};

// Parses a native contiguous span extracted from the retail R&C1 disc and
// returns the decompressed level core. The original 0x2434 amalgamated header
// may live inside that span, so its absolute disc sector is supplied
// separately. No PS2 runtime, DMA, VIF, GS or guest address is involved.
Rac1LevelCoreLoadResult loadRac1LevelCore(const std::filesystem::path& path,
                                          std::uint32_t tocIndex,
                                          std::uint32_t discStartSector,
                                          std::uint32_t discSectorCount,
                                          std::uint32_t discHeaderSector);

// Metadata-only convenience wrapper around loadRac1LevelCore().
Rac1LevelInspectResult inspectRac1Level(const std::filesystem::path& path,
                                        std::uint32_t tocIndex,
                                        std::uint32_t discStartSector,
                                        std::uint32_t discSectorCount,
                                        std::uint32_t discHeaderSector);

const char* rac1LevelInspectStatusName(Rac1LevelInspectStatus status) noexcept;

} // namespace ratchet::assets
