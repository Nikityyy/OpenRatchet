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

struct Rac1SkyBatch {
    // UINT32_MAX means an untextured vertex-colour sky primitive.
    std::uint32_t materialIndex = UINT32_MAX;
    std::vector<Rac1SkyVertex> triangleVertices;
};

struct Rac1SkyMesh {
    std::array<std::uint8_t, 4> clearColor{0u, 0u, 0u, 255u};
    std::vector<Rac1Texture> textures;
    std::vector<Rac1SkyBatch> batches;
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

// Decodes the R&C1 sky block directly from the decompressed level core.
// Sky shell geometry is stored as ordinary indexed triangle clusters and its
// PSMT8 palettes/pixels are self-contained in the sky block, so this produces
// host RGBA8 textures and host triangle lists without GS/VIF/VU emulation.
Rac1SkyResult decodeRac1Sky(std::span<const std::uint8_t> core,
                            std::uint32_t skyOffset);

const char* rac1SkyStatusName(Rac1SkyStatus status) noexcept;

} // namespace ratchet::assets
