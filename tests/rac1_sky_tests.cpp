#include "assets/rac1_sky.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void writeU16(std::vector<std::uint8_t>& bytes, std::size_t o, std::uint16_t v) {
    bytes[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    bytes[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
}
void writeI16(std::vector<std::uint8_t>& bytes, std::size_t o, std::int16_t v) {
    writeU16(bytes, o, static_cast<std::uint16_t>(v));
}
void writeU32(std::vector<std::uint8_t>& bytes, std::size_t o, std::uint32_t v) {
    bytes[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    bytes[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
    bytes[o + 2u] = static_cast<std::uint8_t>((v >> 16u) & 0xffu);
    bytes[o + 3u] = static_cast<std::uint8_t>((v >> 24u) & 0xffu);
}
void writeI32(std::vector<std::uint8_t>& bytes, std::size_t o, std::int32_t v) {
    writeU32(bytes, o, static_cast<std::uint32_t>(v));
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    constexpr std::size_t base = 0x100u;
    std::vector<std::uint8_t> core(0xa00u, 0u);

    core[base + 0u] = 12u;
    core[base + 1u] = 34u;
    core[base + 2u] = 56u;
    core[base + 3u] = 255u;
    writeI16(core, base + 0x06u, 1); // shell count
    writeI16(core, base + 0x0cu, 1); // texture count
    writeI32(core, base + 0x10u, 0x40);  // texture defs
    writeI32(core, base + 0x14u, 0x100); // texture data
    writeI32(core, base + 0x20u, 0x600); // shell 0

    // 2x2 paletted sky texture. Palette and pixels are both relative to
    // header.textureData.
    writeI32(core, base + 0x40u + 0x0u, 0x000);
    writeI32(core, base + 0x40u + 0x4u, 0x400);
    writeI32(core, base + 0x40u + 0x8u, 2);
    writeI32(core, base + 0x40u + 0xcu, 2);
    core[base + 0x100u + 0u] = 200u;
    core[base + 0x100u + 1u] = 100u;
    core[base + 0x100u + 2u] = 50u;
    core[base + 0x100u + 3u] = 0x80u;
    core[base + 0x500u + 0u] = 0u;
    core[base + 0x500u + 1u] = 0u;
    core[base + 0x500u + 2u] = 0u;
    core[base + 0x500u + 3u] = 0u;

    // One textured shell and one triangle cluster.
    writeI32(core, base + 0x600u + 0x0u, 1);
    writeI32(core, base + 0x600u + 0x4u, 0); // flags: textured
    constexpr std::size_t cluster = base + 0x610u;
    writeI32(core, cluster + 0x10u, 0x700);
    writeI16(core, cluster + 0x14u, 3);
    writeI16(core, cluster + 0x16u, 1);
    writeI16(core, cluster + 0x18u, 0x00);
    writeI16(core, cluster + 0x1au, 0x20);
    writeI16(core, cluster + 0x1cu, 0x30);
    writeI16(core, cluster + 0x1eu, 0x40);

    constexpr std::size_t data = base + 0x700u;
    // Positions are fixed 1/1024 and carry the shell alpha.
    writeI16(core, data + 0x00u, 0);    writeI16(core, data + 0x02u, 0);    writeI16(core, data + 0x04u, 0);    writeI16(core, data + 0x06u, 0x80);
    writeI16(core, data + 0x08u, 1024); writeI16(core, data + 0x0au, 0);    writeI16(core, data + 0x0cu, 0);    writeI16(core, data + 0x0eu, 0x80);
    writeI16(core, data + 0x10u, 0);    writeI16(core, data + 0x12u, 1024); writeI16(core, data + 0x14u, 0);    writeI16(core, data + 0x16u, 0x80);
    writeU16(core, data + 0x20u, 0);    writeU16(core, data + 0x22u, 0);
    writeU16(core, data + 0x24u, 4096); writeU16(core, data + 0x26u, 0);
    writeU16(core, data + 0x28u, 0);    writeU16(core, data + 0x2au, 4096);
    core[data + 0x30u] = 0u;
    core[data + 0x31u] = 1u;
    core[data + 0x32u] = 2u;
    core[data + 0x33u] = 0u;

    const auto result = ratchet::assets::decodeRac1Sky(core, static_cast<std::uint32_t>(base));
    if (!result.ok()) {
        std::cerr << "sky decode failed: "
                  << ratchet::assets::rac1SkyStatusName(result.status) << '\n';
        return 1;
    }
    const auto& mesh = result.mesh;
    if (mesh.shellCount != 1u || mesh.clusterCount != 1u ||
        mesh.triangleCount != 1u || mesh.texturedTriangleCount != 1u ||
        mesh.textures.size() != 1u || mesh.batches.size() != 1u ||
        mesh.batches[0].triangleVertices.size() != 3u ||
        mesh.batches[0].materialIndex != 0u ||
        mesh.clearColor[0] != 12u || mesh.clearColor[1] != 34u || mesh.clearColor[2] != 56u) {
        std::cerr << "unexpected sky metadata\n";
        return 1;
    }
    const auto& texture = mesh.textures[0];
    if (texture.width != 2u || texture.height != 2u || texture.rgba.size() != 16u ||
        texture.rgba[0] != 200u || texture.rgba[1] != 100u ||
        texture.rgba[2] != 50u || texture.rgba[3] != 255u) {
        std::cerr << "sky palette decode mismatch\n";
        return 1;
    }

    bool sawX = false;
    bool sawY = false;
    for (const auto& vertex : mesh.batches[0].triangleVertices) {
        sawX |= near(vertex.x, 1.0f) && near(vertex.u, 1.0f);
        sawY |= near(vertex.y, 1.0f) && near(vertex.v, 1.0f);
    }
    if (!sawX || !sawY) {
        std::cerr << "sky position/uv scaling mismatch\n";
        return 1;
    }

    std::cout << "rac1_sky_tests: ok\n";
    return 0;
}
