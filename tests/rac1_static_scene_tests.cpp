#include "assets/rac1_static_scene.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

void writeF32(std::vector<std::uint8_t>& bytes, std::size_t o, float v) {
    std::uint32_t u = 0u;
    static_assert(sizeof(u) == sizeof(v));
    std::memcpy(&u, &v, sizeof(u));
    writeU32(bytes, o, u);
}

void writeIdentity(std::vector<std::uint8_t>& bytes,
                   std::size_t o,
                   float tx,
                   float ty,
                   float tz) {
    for (std::size_t i = 0u; i < 16u; ++i) writeF32(bytes, o + i * 4u, 0.0f);
    writeF32(bytes, o + 0u * 4u, 1.0f);
    writeF32(bytes, o + 5u * 4u, 1.0f);
    writeF32(bytes, o + 10u * 4u, 1.0f);
    writeF32(bytes, o + 15u * 4u, 1.0f);
    writeF32(bytes, o + 12u * 4u, tx);
    writeF32(bytes, o + 13u * 4u, ty);
    writeF32(bytes, o + 14u * 4u, tz);
}

void writeTieVertex(std::vector<std::uint8_t>& core,
                    std::size_t o,
                    std::int16_t x,
                    std::int16_t y,
                    std::int16_t z,
                    std::uint16_t s,
                    std::uint16_t t,
                    std::uint16_t writeOffset) {
    writeI16(core, o + 0x0u, x);
    writeI16(core, o + 0x2u, y);
    writeI16(core, o + 0x4u, z);
    writeU16(core, o + 0x6u, writeOffset);
    writeU16(core, o + 0x8u, s);
    writeU16(core, o + 0xau, t);
    writeU16(core, o + 0xcu, 0x1000u);
    writeU16(core, o + 0xeu, 0u);
}

std::vector<std::uint8_t> makeCore() {
    std::vector<std::uint8_t> core(0x1000u, 0u);

    // Tie class at 0x100. One LOD0 packet, scale 1.0 after /1024.
    constexpr std::size_t tie = 0x100u;
    writeU32(core, tie + 0x00u, 0x50u);
    core[tie + 0x20u] = 1u;
    writeF32(core, tie + 0x40u, 1024.0f);
    constexpr std::size_t packetHeader = tie + 0x50u;
    writeI32(core, packetHeader + 0x0u, 0x10);
    // Retail TiePacketHeader points at the serialized vertex buffer in 0x10-byte units.
    core[packetHeader + 0x8u] = 8u;
    // Five QWs: four 0x10-byte regular vertices plus one 0x10-byte tail pad.
    // The old decoder incorrectly derived a morph vertex from the remaining
    // allocation instead of using the explicit counts in the VU header.
    core[packetHeader + 0x9u] = 5u;
    constexpr std::size_t body = tie + 0x60u;
    writeI32(core, body + 0x10u, 0); // first AD GIF = local material 0
    core[body + 0x23u] = 1u; // strip count
    core[body + 0x28u] = 12u; // compact size hint
    core[body + 0x29u] = 4u;  // compact morphing-size hint
    core[body + 0x2au] = 4u; // authoritative regular vertex count
    core[body + 0x2bu] = 0u; // authoritative morphing vertex count
    core[body + 0x2cu] = 3u; // three unique vertices in strip
    core[body + 0x2eu] = 6u; // strip reset after implicit material command
    constexpr std::size_t tieVertices = body + 0x80u;
    writeTieVertex(core, tieVertices + 0x00u, 0, 0, 0, 0, 0, 7u);
    writeTieVertex(core, tieVertices + 0x10u, 1, 0, 0, 4096, 0, 10u);
    writeTieVertex(core, tieVertices + 0x20u, 0, 1, 0, 0, 4096, 13u);
    // Minimum-size retail packets can pad with a duplicate write address. This
    // must overwrite the same GS slot rather than becoming a fourth vertex.
    writeTieVertex(core, tieVertices + 0x30u, 0, 1, 0, 0, 4096, 13u);

    // Shrub class at 0x400. One VIF packet with the three retail UNPACKs.
    constexpr std::size_t shrub = 0x400u;
    writeF32(core, shrub + 0x20u, 1024.0f);
    writeI16(core, shrub + 0x28u, 1);
    writeI32(core, shrub + 0x40u, 0x50);
    writeI32(core, shrub + 0x44u, 0xe0);
    std::size_t o = shrub + 0x50u;

    // UNPACK V4_32 x6 => 0x60-byte packet header payload.
    writeU32(core, o, (0x6cu << 24u) | (6u << 16u));
    o += 4u;
    const std::size_t header = o;
    writeI32(core, header + 0x0u, 1); // texture primitives
    writeI32(core, header + 0x4u, 1); // GIF tags
    writeI32(core, header + 0x8u, 3); // vertices
    writeU32(core, header + 0x10u + 0x4u, 4u << 15u); // triangle strip
    writeI32(core, header + 0x10u + 0xcu, 0); // primitive reset GS offset
    const std::size_t material = header + 0x20u;
    writeI32(core, material + 0x0cu, 1); // material change GS offset
    writeI32(core, material + 0x30u, 1); // local texture slot encoded in TEX0 low
    o += 0x60u;

    // UNPACK V4_16 x3 => xyz + GS write offset.
    writeU32(core, o, (0x6du << 24u) | (3u << 16u));
    o += 4u;
    const std::size_t p1 = o;
    writeI16(core, p1 + 0x00u, 0); writeI16(core, p1 + 0x02u, 0); writeI16(core, p1 + 0x04u, 0); writeI16(core, p1 + 0x06u, 6);
    writeI16(core, p1 + 0x08u, 2); writeI16(core, p1 + 0x0au, 0); writeI16(core, p1 + 0x0cu, 0); writeI16(core, p1 + 0x0eu, 9);
    writeI16(core, p1 + 0x10u, 0); writeI16(core, p1 + 0x12u, 2); writeI16(core, p1 + 0x14u, 0); writeI16(core, p1 + 0x16u, 12);
    o += 0x18u;

    // UNPACK V4_16 x3 => s, t, h, n/stop.
    writeU32(core, o, (0x6du << 24u) | (3u << 16u));
    o += 4u;
    const std::size_t p2 = o;
    writeI16(core, p2 + 0x00u, 0); writeI16(core, p2 + 0x02u, 0);
    writeI16(core, p2 + 0x08u, 4096); writeI16(core, p2 + 0x0au, 0);
    writeI16(core, p2 + 0x10u, 0); writeI16(core, p2 + 0x12u, 4096);

    return core;
}

std::vector<std::uint8_t> makeIndex() {
    std::vector<std::uint8_t> index(0x100u, 0xffu);
    // Tie class table at 0x00.
    writeU32(index, 0x00u, 0x100u);
    writeI32(index, 0x04u, 10);
    index[0x10u] = 0u;
    index[0x11u] = 0xffu;
    // Shrub class table at 0x30.
    writeU32(index, 0x30u, 0x400u);
    writeI32(index, 0x34u, 20);
    index[0x40u] = 0u;
    index[0x41u] = 2u;
    index[0x42u] = 0xffu;
    return index;
}

std::vector<std::uint8_t> makeGameplay() {
    std::vector<std::uint8_t> gameplay(0x300u, 0u);
    writeI32(gameplay, 0x34u, 0x100);
    writeI32(gameplay, 0x3cu, 0x200);

    writeI32(gameplay, 0x100u, 1);
    constexpr std::size_t tieInstance = 0x110u;
    writeI32(gameplay, tieInstance + 0x0u, 10);
    writeIdentity(gameplay, tieInstance + 0x10u, 10.0f, 20.0f, 30.0f);

    writeI32(gameplay, 0x200u, 1);
    constexpr std::size_t shrubInstance = 0x210u;
    writeI32(gameplay, shrubInstance + 0x0u, 20);
    writeIdentity(gameplay, shrubInstance + 0x10u, -5.0f, 7.0f, 9.0f);
    gameplay[shrubInstance + 0x50u] = 128u;
    gameplay[shrubInstance + 0x51u] = 192u;
    gameplay[shrubInstance + 0x52u] = 255u;
    return gameplay;
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const auto core = makeCore();
    const auto index = makeIndex();
    const auto gameplay = makeGameplay();

    const auto result = ratchet::assets::decodeRac1StaticScene(
        core,
        index,
        gameplay,
        {0x00u, 1u},
        {0x30u, 1u},
        1u,
        3u);
    if (!result.ok()) {
        std::cerr << "decode failed: "
                  << ratchet::assets::rac1StaticSceneStatusName(result.status) << '\n';
        return 1;
    }
    const auto& mesh = result.mesh;
    if (mesh.tieClassCount != 1u || mesh.shrubClassCount != 1u ||
        mesh.tieInstanceCount != 1u || mesh.shrubInstanceCount != 1u ||
        mesh.tieTriangleCount != 1u || mesh.shrubTriangleCount != 1u ||
        mesh.batches.size() != 2u) {
        std::cerr << "unexpected static-scene counts\n";
        return 1;
    }

    const ratchet::assets::Rac1StaticBatch* tie = nullptr;
    const ratchet::assets::Rac1StaticBatch* shrub = nullptr;
    for (const auto& batch : mesh.batches) {
        if (batch.kind == ratchet::assets::Rac1StaticMaterialKind::Tie) tie = &batch;
        if (batch.kind == ratchet::assets::Rac1StaticMaterialKind::Shrub) shrub = &batch;
    }
    if (tie == nullptr || shrub == nullptr || tie->triangleVertices.size() != 3u ||
        shrub->triangleVertices.size() != 3u || tie->materialIndex != 0u ||
        shrub->materialIndex != 2u) {
        std::cerr << "missing expected material batches\n";
        return 1;
    }

    bool sawTieOrigin = false;
    for (const auto& v : tie->triangleVertices) {
        sawTieOrigin |= near(v.x, 10.0f) && near(v.y, 20.0f) && near(v.z, 30.0f);
    }
    bool sawShrubOrigin = false;
    for (const auto& v : shrub->triangleVertices) {
        sawShrubOrigin |= near(v.x, -5.0f) && near(v.y, 7.0f) && near(v.z, 9.0f) &&
                          v.r == 128u && v.g == 192u && v.b == 255u;
    }
    if (!sawTieOrigin || !sawShrubOrigin) {
        std::cerr << "instance transform/color was not applied\n";
        return 1;
    }

    std::cout << "rac1_static_scene_tests: ok\n";
    return 0;
}
