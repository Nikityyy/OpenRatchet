#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

struct Rac1CollisionVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::int16_t type = 0;
};

struct Rac1CollisionBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

enum class Rac1CollisionStatus : std::uint8_t {
    Ok,
    OffsetOutOfRange,
    HeaderOutOfRange,
    MeshOutOfRange,
    AxisOutOfRange,
    OctantOutOfRange,
    FaceIndexOutOfRange,
    HeroGroupsOutOfRange,
    EmptyMesh,
};

struct Rac1CollisionMesh {
    std::vector<Rac1CollisionVertex> triangleVertices;
    Rac1CollisionBounds bounds{};
    std::size_t octantCount = 0u;
    std::size_t faceCount = 0u;
    std::size_t quadCount = 0u;
    std::size_t heroGroupCount = 0u;
    std::size_t heroFaceCount = 0u;
};

struct Rac1CollisionResult {
    Rac1CollisionStatus status = Rac1CollisionStatus::OffsetOutOfRange;
    Rac1CollisionMesh mesh{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1CollisionStatus::Ok;
    }
};

// Decodes R&C1's level collision structure directly from decompressed level
// core data. The output is an expanded triangle list in original Ratchet world
// coordinates; no PS2 renderer, VIF, VU or GS emulation is involved.
Rac1CollisionResult decodeRac1Collision(std::span<const std::uint8_t> core,
                                        std::uint32_t collisionOffset);

const char* rac1CollisionStatusName(Rac1CollisionStatus status) noexcept;

} // namespace ratchet::assets
