#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1TfragStatus : std::uint8_t {
    Ok,
    OffsetOutOfRange,
    InvalidBlockHeader,
    InvalidTfragHeader,
    InvalidVifLayout,
    UnsupportedVifCycle,
    MissingVifData,
    InvalidVuHeader,
    InvalidStrip,
    InvalidVertexIndex,
    InvalidMaterialIndex,
    EmptyMesh,
};

struct Rac1TfragBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

struct Rac1TfragVertex {
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

struct Rac1TfragBatch {
    std::uint32_t materialIndex = 0u;
    std::vector<Rac1TfragVertex> triangleVertices;
};

struct Rac1TfragMesh {
    std::vector<Rac1TfragBatch> batches;
    Rac1TfragBounds bounds{};
    std::size_t tfragCount = 0u;
    std::size_t stripCount = 0u;
    std::size_t triangleCount = 0u;
    std::size_t sourceVertexReferences = 0u;
};

struct Rac1TfragResult {
    Rac1TfragStatus status = Rac1TfragStatus::OffsetOutOfRange;
    Rac1TfragMesh mesh{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1TfragStatus::Ok;
    }
};

// Decodes R&C1 tfrag LOD0 packets directly from the decompressed level core.
// The five embedded VIF buffers are parsed as storage packets only; no VU/VIF
// emulator is involved. Output is ordinary host triangles grouped by the
// global tfrag texture index selected by the packet's TEX0 primitive.
Rac1TfragResult decodeRac1TfragTerrain(std::span<const std::uint8_t> core,
                                       std::uint32_t tfragsOffset,
                                       std::uint32_t textureCount);

const char* rac1TfragStatusName(Rac1TfragStatus status) noexcept;

} // namespace ratchet::assets
