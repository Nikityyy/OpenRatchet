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
};

struct NativeAssetLocation {
    NativeAssetKind kind = NativeAssetKind::Wad;
    std::uint32_t index = 0u;
    std::uint32_t startSector = 0u;
    std::uint32_t sectorCount = 0u;
    std::filesystem::path path;
};

struct NativeVfsSummary {
    std::size_t indexedAssets = 0u;
    std::size_t presentAssets = 0u;
    std::size_t missingAssets = 0u;
};

// Native view over the files already extracted from the user's disc.  The
// current phase indexes sector-addressed WAD/WAD2 resources from build/toc.json
// and resolves guest sector reads directly to those host files.  Higher-level
// resource APIs can be layered on top without reintroducing CDVD/SIF hardware.
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

private:
    std::filesystem::path extractedRoot_;
    std::filesystem::path tocPath_;
    std::vector<NativeAssetLocation> assets_;
    NativeVfsSummary summary_{};
    bool ready_ = false;
};

const char* nativeAssetKindName(NativeAssetKind kind) noexcept;
std::string nativeAssetName(const NativeAssetLocation& asset);

} // namespace ratchet::platform
