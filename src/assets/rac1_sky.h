#pragma once

#include "assets/rac1_texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1SkyStatus : std::uint8_t {
    Ok,
    OffsetOutOfRange,
    InvalidHeader,
    InvalidTexture,
    InvalidShell,
    InvalidCluster,
    InvalidFace,
    EmptySky,
};

struct Rac1SkyVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t r = 255u;
    std::uint8_t g = 255u;
    std::uint8_t b = 255u;
    std::uint8_t a = 255u;
};

struct Rac1SkyShellIdentity {
    std::uint32_t shellIndex = 0u;
    std::uint32_t sourceOffset = 0u;
    std::int32_t clusterCount = 0;
    std::int32_t flags = 0;
};

struct Rac1SkyBatch {
    // Retail renders each shell with a separate object transform. Preserve the
    // source shell identity even when multiple shells reuse the same material.
    std::uint32_t shellIndex = 0u;
    // UINT32_MAX means an untextured vertex-colour sky primitive.
    std::uint32_t materialIndex = UINT32_MAX;
    std::vector<Rac1SkyVertex> triangleVertices;
};

struct Rac1SkyMesh {
    std::array<std::uint8_t, 4> clearColor{0u, 0u, 0u, 255u};
    std::vector<Rac1Texture> textures;
    std::vector<Rac1SkyBatch> batches;
    std::vector<Rac1SkyShellIdentity> shellIdentities;
    std::size_t shellCount = 0u;
    std::size_t clusterCount = 0u;
    std::size_t triangleCount = 0u;
    std::size_t texturedTriangleCount = 0u;
    std::size_t colorTriangleCount = 0u;
};

struct Rac1SkyResult {
    Rac1SkyStatus status = Rac1SkyStatus::OffsetOutOfRange;
    Rac1SkyMesh mesh{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1SkyStatus::Ok;
    }
};


enum class Rac1RuntimeSkySourceStatus : std::uint8_t {
    Ok,
    HeaderTooSmall,
    DataOffsetOutOfRange,
    SkyOffsetOutOfRange,
};

struct Rac1RuntimeSkySource {
    Rac1RuntimeSkySourceStatus status = Rac1RuntimeSkySourceStatus::HeaderTooSmall;
    std::uint32_t dataOffset = 0u;
    std::uint32_t skyRelativeOffset = 0u;
    std::uint32_t skyOffset = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1RuntimeSkySourceStatus::Ok;
    }
};

// sub_001EA830 is the active Retail Level-0 loader. After WAD2/69 is
// decompressed it forms s5 = wadBase + *(u32 *)(wadBase+0x04), then passes
// s5 + *(u32 *)(wadBase+0x14) to FUN_002028E0 as the gameplay-sky resource.
// Resolve that exact pre-relocation sky offset in a native decompressed WAD.
[[nodiscard]] Rac1RuntimeSkySource locateRac1RuntimeSky(
    std::span<const std::uint8_t> decompressedRuntimeWad) noexcept;

const char* rac1RuntimeSkySourceStatusName(
    Rac1RuntimeSkySourceStatus status) noexcept;

// Decodes an R&C1 sky block directly from an unrelocated decompressed resource.
// Sky shell geometry is stored as ordinary indexed triangle clusters and its
// PSMT8 palettes/pixels are self-contained in the sky block, so this produces
// host RGBA8 textures and host triangle lists without GS/VIF/VU emulation.
Rac1SkyResult decodeRac1Sky(std::span<const std::uint8_t> core,
                            std::uint32_t skyOffset);

const char* rac1SkyStatusName(Rac1SkyStatus status) noexcept;

} // namespace ratchet::assets
