#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ratchet::platform {

enum class NativeAssetKind : std::uint8_t {
    Wad,
    Wad2,
    Level,
};

struct NativeAssetLocation {
    NativeAssetKind kind = NativeAssetKind::Wad;
    std::uint32_t index = 0u;
    std::uint32_t startSector = 0u;
    std::uint32_t sectorCount = 0u;
    // For normalized/extracted level spans, the original 0x2434 retail level
    // envelope can live inside the span instead of at its first sector. Global
    // WAD/WAD2 assets leave this as zero.
    std::uint32_t headerSector = 0u;
    std::filesystem::path path;
};

struct NativeVfsSummary {
    std::size_t indexedAssets = 0u;
    std::size_t presentAssets = 0u;
    std::size_t missingAssets = 0u;
    std::size_t indexedLevels = 0u;
    std::size_t presentLevels = 0u;
    std::size_t missingLevels = 0u;
};

// Native view over the files already extracted from the user's disc. The VFS
// owns both host-file resolution for extracted assets and a host-side image of
// the game's disc TOC. Guest code can therefore consume file metadata without
// routing through CDVD/SIF/IOP services.
class NativeVfs final {
public:
    static constexpr std::uint32_t kSectorBytes = 0x800u;

    bool initialize(const std::filesystem::path& extractedRoot,
                    const std::filesystem::path& tocPath);

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const std::filesystem::path& extractedRoot() const noexcept {
        return extractedRoot_;
    }
    [[nodiscard]] const std::filesystem::path& tocPath() const noexcept {
        return tocPath_;
    }
    [[nodiscard]] const NativeVfsSummary& summary() const noexcept {
        return summary_;
    }

    [[nodiscard]] const NativeAssetLocation* findAsset(
        NativeAssetKind kind,
        std::uint32_t index) const noexcept;

    [[nodiscard]] const NativeAssetLocation* findAssetContainingSector(
        std::uint32_t sector) const noexcept;

    // R&C1 preserves 19 raw SectorRange entries in the final 0x98 bytes of the
    // retail disc TOC. Native level files are catalogued separately from those
    // raw bytes: their extraction spans are discovered from validated 0x2434
    // amalgamated headers and stored as `native_levels` metadata.
    [[nodiscard]] const NativeAssetLocation* findLevel(
        std::uint32_t index) const noexcept;
    [[nodiscard]] const std::vector<NativeAssetLocation>& levels() const noexcept {
        return levelAssets_;
    }

    // Returns true only when the complete requested disc range can be resolved
    // to indexed extracted assets. No destination bytes are modified on a
    // resolution failure.
    bool readSectors(std::uint32_t startSector,
                     std::uint32_t sectorCount,
                     std::uint8_t* destination,
                     std::size_t destinationCapacity,
                     std::string* sourceDescription = nullptr) const;

    // Compatibility helper for a resource-level load when old guest state did
    // not carry a usable sector/count pair. It still reads through the VFS and
    // never reaches into guest_overrides.cpp for host file I/O.
    bool readAssetPrefix(NativeAssetKind kind,
                         std::uint32_t index,
                         std::uint8_t* destination,
                         std::size_t destinationCapacity,
                         std::size_t& bytesRead) const;

    // Copies the native host-side image of the retail disc TOC. The final 0x98
    // bytes are 19 SectorRange level entries. Older OpenRatchet toc.json files
    // omitted them (or exposed the 38 raw words as legacy leveldirs); both forms
    // remain readable during migration.
    bool copyDiscToc(std::uint8_t* destination,
                     std::size_t destinationCapacity,
                     std::size_t& bytesWritten) const;

    [[nodiscard]] std::size_t discTocSize() const noexcept {
        return tocImage_.size();
    }
    [[nodiscard]] std::size_t discTocKnownBytes() const noexcept {
        return tocKnownBytes_;
    }
    [[nodiscard]] bool discTocComplete() const noexcept {
        return tocComplete_;
    }

private:
    std::filesystem::path extractedRoot_;
    std::filesystem::path tocPath_;
    std::vector<NativeAssetLocation> assets_;
    std::vector<NativeAssetLocation> levelAssets_;
    std::vector<std::uint8_t> tocImage_;
    std::size_t tocKnownBytes_ = 0u;
    NativeVfsSummary summary_{};
    bool tocComplete_ = false;
    bool ready_ = false;
};

const char* nativeAssetKindName(NativeAssetKind kind) noexcept;
std::string nativeAssetName(const NativeAssetLocation& asset);

} // namespace ratchet::platform
