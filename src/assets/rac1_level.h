#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

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
    InvalidCoreDataRange,
    InvalidCoreHeader,
    InvalidCoreWad,
    CoreDecompressionFailed,
    InvalidRenderOffsets,
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

    std::uint32_t assetsCompressedSize = 0u;
    std::uint32_t assetsDecompressedSize = 0u;
    std::uint32_t coreEncodedSize = 0u;
    std::size_t coreDecompressedBytes = 0u;
};

struct Rac1LevelInspectResult {
    Rac1LevelInspectStatus status = Rac1LevelInspectStatus::FileOpenFailed;
    Rac1LevelSummary summary{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LevelInspectStatus::Ok;
    }
};

// Parses a native contiguous span extracted from the retail R&C1 disc. The
// original 0x2434 amalgamated header may live inside that span, so its absolute
// disc sector is supplied separately. Sector ranges are translated to offsets
// in the extracted file and the embedded level
// core WAD is decompressed with OpenRatchet's native decoder. No PS2 runtime,
// DMA, VIF, GS or guest address is involved.
Rac1LevelInspectResult inspectRac1Level(const std::filesystem::path& path,
                                        std::uint32_t tocIndex,
                                        std::uint32_t discStartSector,
                                        std::uint32_t discSectorCount,
                                        std::uint32_t discHeaderSector);

const char* rac1LevelInspectStatusName(Rac1LevelInspectStatus status) noexcept;

} // namespace ratchet::assets
