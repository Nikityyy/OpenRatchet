#pragma once

#include "assets/rac1_level.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1StaticMaterialKind : std::uint8_t {
    Tie,
    Shrub,
};

enum class Rac1StaticSceneStatus : std::uint8_t {
    Ok,
    InvalidIndexTable,
    InvalidGameplayHeader,
    InvalidInstanceBlock,
    MissingClass,
    InvalidClass,
    InvalidPacket,
    InvalidMaterial,
    EmptyScene,
};

struct Rac1StaticVertex {
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

struct Rac1StaticBatch {
    Rac1StaticMaterialKind kind = Rac1StaticMaterialKind::Tie;
    std::uint32_t materialIndex = 0u;
    std::vector<Rac1StaticVertex> triangleVertices;
};

struct Rac1StaticSceneMesh {
    std::vector<Rac1StaticBatch> batches;
    std::size_t tieClassCount = 0u;
    std::size_t tieInstanceCount = 0u;
    std::size_t tieTriangleCount = 0u;
    std::size_t shrubClassCount = 0u;
    std::size_t shrubInstanceCount = 0u;
    std::size_t shrubTriangleCount = 0u;
};

struct Rac1StaticSceneResult {
    Rac1StaticSceneStatus status = Rac1StaticSceneStatus::InvalidIndexTable;
    Rac1StaticSceneMesh mesh{};

    [[nodiscard]] bool ok() const noexcept { return status == Rac1StaticSceneStatus::Ok; }
};

// Joins R&C1 static class geometry from the decompressed level core with the
// retail gameplay instance blocks. Ties and shrubs are converted to ordinary
// world-space triangles and global texture-table indices; no VU/GS execution
// is performed. Shrub VIF packets are parsed only as serialized asset storage.
Rac1StaticSceneResult decodeRac1StaticScene(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange tieClasses,
    Rac1ArrayRange shrubClasses,
    std::uint32_t tieTextureCount,
    std::uint32_t shrubTextureCount);

const char* rac1StaticSceneStatusName(Rac1StaticSceneStatus status) noexcept;

} // namespace ratchet::assets
