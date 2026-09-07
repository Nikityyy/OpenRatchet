#pragma once

#include "assets/rac1_static_scene.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::render {

enum class Rac1RenderParityStatus : std::uint8_t {
    Ok,
    EmptyWorld,
    InvalidTerrainBatch,
    InvalidStaticBatch,
    InvalidMaterialIndex,
    NonFiniteVertex,
};

struct Rac1StaticWorldParityDigest {
    Rac1RenderParityStatus status = Rac1RenderParityStatus::EmptyWorld;
    std::size_t batchCount = 0u;
    std::size_t vertexCount = 0u;
    std::size_t indexCount = 0u;
    std::size_t triangleCount = 0u;
    std::uint64_t vertexHash = 0u;
    std::uint64_t indexHash = 0u;
    std::uint64_t materialHash = 0u;
    std::uint64_t textureHash = 0u;
    std::uint64_t transformHash = 0u;
    std::uint64_t topologyHash = 0u;
    std::uint64_t renderStateHash = 0u;
    std::uint64_t drawOrderHash = 0u;
    std::uint64_t combinedHash = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1RenderParityStatus::Ok;
    }
};

// Canonical parity boundary shared by native_level_viewer and openratchet.exe.
// The digest intentionally stops before frontend camera/view transforms: tfrag
// and tie/shrub vertices are hashed in the authoritative Retail world-space
// domain emitted by the native decoders. The current native mesh path is an
// unindexed triangle list, so indexCount is exactly zero and indexHash pins that
// absence explicitly rather than inventing an index stream.
[[nodiscard]] Rac1StaticWorldParityDigest buildRac1StaticWorldParityDigest(
    const assets::Rac1TfragMesh& terrain,
    std::span<const assets::Rac1Texture> tfragTextures,
    const assets::Rac1StaticSceneMesh& staticScene,
    std::span<const assets::Rac1Texture> tieTextures,
    std::span<const assets::Rac1Texture> shrubTextures) noexcept;

[[nodiscard]] const char* rac1RenderParityStatusName(
    Rac1RenderParityStatus status) noexcept;

} // namespace ratchet::render
