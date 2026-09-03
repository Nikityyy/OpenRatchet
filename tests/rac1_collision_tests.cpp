#include "assets/rac1_collision.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void writeU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(offset + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(offset + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t packVertex(std::int32_t x16, std::int32_t y16, std::int32_t z64) {
    return (static_cast<std::uint32_t>(x16) & 0x3ffu) |
           ((static_cast<std::uint32_t>(y16) & 0x3ffu) << 10u) |
           ((static_cast<std::uint32_t>(z64) & 0xfffu) << 20u);
}

bool close(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    std::vector<std::uint8_t> core(0x100u, 0u);

    // Collision header at 0. Mesh grid begins at +8, no hero-only groups.
    writeU32(core, 0x00u, 0x08u);
    writeU32(core, 0x04u, 0x00u);
    const std::size_t mesh = 0x08u;

    // Z axis -> Y axis at mesh+0x10.
    writeU16(core, mesh + 0x00u, 0u);
    writeU16(core, mesh + 0x02u, 1u);
    writeU16(core, mesh + 0x04u, 0x04u); // 0x04 * 4 = 0x10

    // Y axis -> X axis at mesh+0x20.
    writeU16(core, mesh + 0x10u, 0u);
    writeU16(core, mesh + 0x12u, 1u);
    writeU32(core, mesh + 0x14u, 0x20u);

    // X axis -> octant at mesh+0x40. Low byte is rounded octant size / 16.
    writeU16(core, mesh + 0x20u, 0u);
    writeU16(core, mesh + 0x22u, 1u);
    writeU32(core, mesh + 0x24u, (0x40u << 8u) | 0x02u);

    const std::size_t oct = mesh + 0x40u;
    writeU16(core, oct + 0x00u, 1u); // faces
    core[oct + 0x02u] = 3u;          // vertices
    core[oct + 0x03u] = 0u;          // quads
    writeU32(core, oct + 0x04u, packVertex(0, 0, 0));
    writeU32(core, oct + 0x08u, packVertex(16, 0, 0));
    writeU32(core, oct + 0x0cu, packVertex(0, 16, 0));
    core[oct + 0x10u] = 0u;
    core[oct + 0x11u] = 1u;
    core[oct + 0x12u] = 2u;
    core[oct + 0x13u] = 0x0fu;

    const auto result = ratchet::assets::decodeRac1Collision(core, 0u);
    if (!result.ok()) {
        std::cerr << "rac1_collision_tests: decode failed status="
                  << ratchet::assets::rac1CollisionStatusName(result.status) << '\n';
        return 1;
    }

    const auto& meshOut = result.mesh;
    if (meshOut.octantCount != 1u || meshOut.faceCount != 1u ||
        meshOut.quadCount != 0u || meshOut.triangleVertices.size() != 3u) {
        std::cerr << "rac1_collision_tests: mesh counts mismatch\n";
        return 1;
    }
    const auto& a = meshOut.triangleVertices[0];
    const auto& b = meshOut.triangleVertices[1];
    const auto& c = meshOut.triangleVertices[2];
    if (!close(a.x, 2.0f) || !close(a.y, 2.0f) || !close(a.z, 2.0f) ||
        !close(b.x, 3.0f) || !close(b.y, 2.0f) || !close(b.z, 2.0f) ||
        !close(c.x, 2.0f) || !close(c.y, 3.0f) || !close(c.z, 2.0f) ||
        a.type != 15 || b.type != 15 || c.type != 15) {
        std::cerr << "rac1_collision_tests: decoded triangle mismatch\n";
        return 1;
    }

    std::cout << "R&C1 collision parser tests passed\n";
    return 0;
}
