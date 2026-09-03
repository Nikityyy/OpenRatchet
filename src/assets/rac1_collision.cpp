#include "assets/rac1_collision.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1u] << 8u);
}

std::int16_t readI16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::int16_t>(readU16(bytes, offset));
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::int32_t readI32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::int32_t>(readU32(bytes, offset));
}

bool fits(std::size_t offset, std::size_t size, std::size_t capacity) noexcept {
    return offset <= capacity && size <= capacity - offset;
}

std::int32_t signExtend(std::uint32_t value, unsigned bits) noexcept {
    const std::uint32_t sign = 1u << (bits - 1u);
    const std::uint32_t mask = (1u << bits) - 1u;
    value &= mask;
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

void includeBounds(Rac1CollisionBounds& bounds,
                   bool& initialized,
                   float x,
                   float y,
                   float z) noexcept {
    if (!initialized) {
        bounds = {x, y, z, x, y, z};
        initialized = true;
        return;
    }
    bounds.minX = std::min(bounds.minX, x);
    bounds.minY = std::min(bounds.minY, y);
    bounds.minZ = std::min(bounds.minZ, z);
    bounds.maxX = std::max(bounds.maxX, x);
    bounds.maxY = std::max(bounds.maxY, y);
    bounds.maxZ = std::max(bounds.maxZ, z);
}

struct LocalVertex {
    float x;
    float y;
    float z;
};

struct Face {
    std::uint8_t v0;
    std::uint8_t v1;
    std::uint8_t v2;
    std::uint8_t v3;
    std::uint8_t type;
    bool quad;
};

Rac1CollisionResult fail(Rac1CollisionStatus status, Rac1CollisionMesh mesh = {}) {
    return {status, std::move(mesh)};
}

bool appendTriangle(Rac1CollisionMesh& mesh,
                    bool& boundsInitialized,
                    const LocalVertex& a,
                    const LocalVertex& b,
                    const LocalVertex& c,
                    float baseX,
                    float baseY,
                    float baseZ,
                    std::int16_t type) {
    const std::array<LocalVertex, 3> vertices{a, b, c};
    for (const LocalVertex& v : vertices) {
        const float x = baseX + v.x;
        const float y = baseY + v.y;
        const float z = baseZ + v.z;
        mesh.triangleVertices.push_back({x, y, z, type});
        includeBounds(mesh.bounds, boundsInitialized, x, y, z);
    }
    return true;
}

} // namespace

const char* rac1CollisionStatusName(Rac1CollisionStatus status) noexcept {
    switch (status) {
    case Rac1CollisionStatus::Ok:
        return "ok";
    case Rac1CollisionStatus::OffsetOutOfRange:
        return "offset-out-of-range";
    case Rac1CollisionStatus::HeaderOutOfRange:
        return "header-out-of-range";
    case Rac1CollisionStatus::MeshOutOfRange:
        return "mesh-out-of-range";
    case Rac1CollisionStatus::AxisOutOfRange:
        return "axis-out-of-range";
    case Rac1CollisionStatus::OctantOutOfRange:
        return "octant-out-of-range";
    case Rac1CollisionStatus::FaceIndexOutOfRange:
        return "face-index-out-of-range";
    case Rac1CollisionStatus::HeroGroupsOutOfRange:
        return "hero-groups-out-of-range";
    case Rac1CollisionStatus::EmptyMesh:
        return "empty-mesh";
    }
    return "unknown";
}

Rac1CollisionResult decodeRac1Collision(std::span<const std::uint8_t> core,
                                        std::uint32_t collisionOffset) {
    if (!fits(collisionOffset, 8u, core.size())) {
        return fail(Rac1CollisionStatus::OffsetOutOfRange);
    }

    const std::span<const std::uint8_t> collision = core.subspan(collisionOffset);
    const std::int32_t meshOffsetSigned = readI32(collision, 0u);
    const std::int32_t heroGroupsOffsetSigned = readI32(collision, 4u);
    if (meshOffsetSigned < 0) {
        return fail(Rac1CollisionStatus::HeaderOutOfRange);
    }
    const std::size_t meshOffset = static_cast<std::size_t>(meshOffsetSigned);
    if (!fits(meshOffset, 4u, collision.size())) {
        return fail(Rac1CollisionStatus::MeshOutOfRange);
    }

    const std::span<const std::uint8_t> meshData = collision.subspan(meshOffset);
    Rac1CollisionMesh output{};
    bool boundsInitialized = false;

    // Z axis: int16 start coordinate, uint16 count, then uint16 offsets in
    // units of four bytes.
    if (meshData.size() < 4u) {
        return fail(Rac1CollisionStatus::AxisOutOfRange);
    }
    const std::int16_t zCoord = readI16(meshData, 0u);
    const std::uint16_t zCount = readU16(meshData, 2u);
    if (!fits(4u, static_cast<std::size_t>(zCount) * 2u, meshData.size())) {
        return fail(Rac1CollisionStatus::AxisOutOfRange);
    }

    float worldZ = static_cast<float>(zCoord) * 4.0f + 2.0f;
    for (std::uint32_t zi = 0u; zi < zCount; ++zi, worldZ += 4.0f) {
        const std::size_t yOffset =
            static_cast<std::size_t>(readU16(meshData, 4u + zi * 2u)) * 4u;
        if (yOffset == 0u) {
            continue;
        }
        if (!fits(yOffset, 4u, meshData.size())) {
            return fail(Rac1CollisionStatus::AxisOutOfRange, std::move(output));
        }

        const std::span<const std::uint8_t> axisY = meshData.subspan(yOffset);
        const std::int16_t yCoord = readI16(axisY, 0u);
        const std::uint16_t yCount = readU16(axisY, 2u);
        if (!fits(4u, static_cast<std::size_t>(yCount) * 4u, axisY.size())) {
            return fail(Rac1CollisionStatus::AxisOutOfRange, std::move(output));
        }

        float worldY = static_cast<float>(yCoord) * 4.0f + 2.0f;
        for (std::uint32_t yi = 0u; yi < yCount; ++yi, worldY += 4.0f) {
            const std::int32_t xOffsetSigned = readI32(axisY, 4u + yi * 4u);
            if (xOffsetSigned == 0) {
                continue;
            }
            if (xOffsetSigned < 0) {
                return fail(Rac1CollisionStatus::AxisOutOfRange, std::move(output));
            }
            const std::size_t xOffset = static_cast<std::size_t>(xOffsetSigned);
            if (!fits(xOffset, 4u, meshData.size())) {
                return fail(Rac1CollisionStatus::AxisOutOfRange, std::move(output));
            }

            const std::span<const std::uint8_t> axisX = meshData.subspan(xOffset);
            const std::int16_t xCoord = readI16(axisX, 0u);
            const std::uint16_t xCount = readU16(axisX, 2u);
            if (!fits(4u, static_cast<std::size_t>(xCount) * 4u, axisX.size())) {
                return fail(Rac1CollisionStatus::AxisOutOfRange, std::move(output));
            }

            float worldX = static_cast<float>(xCoord) * 4.0f + 2.0f;
            for (std::uint32_t xi = 0u; xi < xCount; ++xi, worldX += 4.0f) {
                const std::int32_t packedSigned = readI32(axisX, 4u + xi * 4u);
                if (packedSigned == 0) {
                    continue;
                }
                if (packedSigned < 0) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                const std::uint32_t packed = static_cast<std::uint32_t>(packedSigned);
                const std::uint32_t maxLength16 = packed & 0xffu;
                const std::size_t octantOffset = static_cast<std::size_t>(packed >> 8u);
                if (octantOffset == 0u || maxLength16 == 0u ||
                    !fits(octantOffset, 4u, meshData.size())) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                const std::span<const std::uint8_t> octant = meshData.subspan(octantOffset);
                const std::uint16_t faceCount = readU16(octant, 0u);
                const std::uint8_t vertCount = octant[2u];
                const std::uint8_t quadCount = octant[3u];
                if (quadCount > faceCount) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                std::size_t ptr = 4u;
                const std::size_t vertexBytes = static_cast<std::size_t>(vertCount) * 4u;
                const std::size_t faceBytes = static_cast<std::size_t>(faceCount) * 4u;
                const std::size_t quadBytes = static_cast<std::size_t>(quadCount);
                if (!fits(ptr, vertexBytes, octant.size())) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                std::vector<LocalVertex> vertices;
                vertices.reserve(vertCount);
                for (std::uint32_t vi = 0u; vi < vertCount; ++vi) {
                    const std::uint32_t value = readU32(octant, ptr + vi * 4u);
                    vertices.push_back({
                        static_cast<float>(signExtend(value, 10u)) / 16.0f,
                        static_cast<float>(signExtend(value >> 10u, 10u)) / 16.0f,
                        static_cast<float>(signExtend(value >> 20u, 12u)) / 64.0f,
                    });
                }
                ptr += vertexBytes;
                if (!fits(ptr, faceBytes, octant.size())) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                std::vector<Face> faces;
                faces.reserve(faceCount);
                for (std::uint32_t fi = 0u; fi < faceCount; ++fi) {
                    const std::size_t faceOffset = ptr + fi * 4u;
                    faces.push_back({octant[faceOffset + 0u],
                                     octant[faceOffset + 1u],
                                     octant[faceOffset + 2u],
                                     0u,
                                     octant[faceOffset + 3u],
                                     false});
                }
                ptr += faceBytes;
                if (!fits(ptr, quadBytes, octant.size())) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }
                for (std::uint32_t qi = 0u; qi < quadCount; ++qi) {
                    faces[qi].v3 = octant[ptr + qi];
                    faces[qi].quad = true;
                }
                ptr += quadBytes;

                // The low byte in the packed X-axis entry is the occupied
                // octant length in 16-byte units, rounded up.
                const std::size_t occupied16 = (ptr + 0x0fu) / 0x10u;
                if (occupied16 != maxLength16) {
                    return fail(Rac1CollisionStatus::OctantOutOfRange, std::move(output));
                }

                for (const Face& face : faces) {
                    const auto validIndex = [vertCount](std::uint8_t index) {
                        return index < vertCount;
                    };
                    if (!validIndex(face.v0) || !validIndex(face.v1) ||
                        !validIndex(face.v2) || (face.quad && !validIndex(face.v3))) {
                        return fail(Rac1CollisionStatus::FaceIndexOutOfRange, std::move(output));
                    }
                    const std::int16_t type = static_cast<std::int16_t>(face.type & 0x0fu);
                    appendTriangle(output,
                                   boundsInitialized,
                                   vertices[face.v0],
                                   vertices[face.v1],
                                   vertices[face.v2],
                                   worldX,
                                   worldY,
                                   worldZ,
                                   type);
                    if (face.quad) {
                        appendTriangle(output,
                                       boundsInitialized,
                                       vertices[face.v0],
                                       vertices[face.v3],
                                       vertices[face.v2],
                                       worldX,
                                       worldY,
                                       worldZ,
                                       type);
                        ++output.quadCount;
                    }
                    ++output.faceCount;
                }
                ++output.octantCount;
            }
        }
    }

    // Ratchet-only collision groups are stored separately and use unsigned
    // 16-bit positions scaled by 1/64. They are appended with type -1.
    if (heroGroupsOffsetSigned != 0) {
        if (heroGroupsOffsetSigned < 0) {
            return fail(Rac1CollisionStatus::HeroGroupsOutOfRange, std::move(output));
        }
        const std::size_t heroOffset = static_cast<std::size_t>(heroGroupsOffsetSigned);
        if (!fits(heroOffset, 0x10u, collision.size())) {
            return fail(Rac1CollisionStatus::HeroGroupsOutOfRange, std::move(output));
        }
        const std::span<const std::uint8_t> hero = collision.subspan(heroOffset);
        const std::uint32_t groupCount = readU32(hero, 0u);
        if (!fits(0x10u, static_cast<std::size_t>(groupCount) * 0x10u, hero.size())) {
            return fail(Rac1CollisionStatus::HeroGroupsOutOfRange, std::move(output));
        }
        output.heroGroupCount = groupCount;
        for (std::uint32_t gi = 0u; gi < groupCount; ++gi) {
            const std::size_t h = 0x10u + static_cast<std::size_t>(gi) * 0x10u;
            const std::uint16_t triangleCount = readU16(hero, h + 0x08u);
            const std::uint16_t vertexCount = readU16(hero, h + 0x0au);
            const std::uint32_t dataOffset = readU32(hero, h + 0x0cu);
            const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * 0x08u;
            const std::size_t faceBytes = static_cast<std::size_t>(triangleCount) * 0x04u;
            if (!fits(dataOffset, vertexBytes + faceBytes, hero.size())) {
                return fail(Rac1CollisionStatus::HeroGroupsOutOfRange, std::move(output));
            }

            const std::span<const std::uint8_t> group = hero.subspan(dataOffset);
            std::vector<LocalVertex> vertices;
            vertices.reserve(vertexCount);
            for (std::uint32_t vi = 0u; vi < vertexCount; ++vi) {
                const std::size_t v = static_cast<std::size_t>(vi) * 0x08u;
                vertices.push_back({static_cast<float>(readU16(group, v + 0u)) / 64.0f,
                                    static_cast<float>(readU16(group, v + 2u)) / 64.0f,
                                    static_cast<float>(readU16(group, v + 4u)) / 64.0f});
            }
            const std::size_t facesBase = vertexBytes;
            for (std::uint32_t fi = 0u; fi < triangleCount; ++fi) {
                const std::size_t f = facesBase + static_cast<std::size_t>(fi) * 0x04u;
                const std::uint8_t v0 = group[f + 0u];
                const std::uint8_t v1 = group[f + 1u];
                const std::uint8_t v2 = group[f + 2u];
                if (v0 >= vertexCount || v1 >= vertexCount || v2 >= vertexCount) {
                    return fail(Rac1CollisionStatus::FaceIndexOutOfRange, std::move(output));
                }
                appendTriangle(output,
                               boundsInitialized,
                               vertices[v0],
                               vertices[v1],
                               vertices[v2],
                               0.0f,
                               0.0f,
                               0.0f,
                               -1);
                ++output.heroFaceCount;
            }
        }
    }

    if (output.triangleVertices.empty() || !boundsInitialized) {
        return fail(Rac1CollisionStatus::EmptyMesh, std::move(output));
    }

    return {Rac1CollisionStatus::Ok, std::move(output)};
}

} // namespace ratchet::assets
