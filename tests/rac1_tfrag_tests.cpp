#include "assets/rac1_tfrag.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

void writeU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeI16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int16_t value) {
    writeU16(bytes, offset, static_cast<std::uint16_t>(value));
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(offset + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(offset + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t vifWord(std::uint8_t cmd, std::uint8_t num = 0u, std::uint16_t imm = 0u) {
    return static_cast<std::uint32_t>(imm) |
           (static_cast<std::uint32_t>(num) << 16u) |
           (static_cast<std::uint32_t>(cmd) << 24u);
}

void appendCommand(std::vector<std::uint8_t>& out,
                   std::uint8_t cmd,
                   std::uint8_t num,
                   std::span<const std::uint8_t> payload,
                   std::uint16_t imm = 0u) {
    const std::size_t start = out.size();
    out.resize(start + 4u);
    writeU32(out, start, vifWord(cmd, num, imm));
    out.insert(out.end(), payload.begin(), payload.end());
    while ((out.size() & 3u) != 0u) out.push_back(0u);
}

void appendNopsTo(std::vector<std::uint8_t>& out, std::size_t size) {
    while (out.size() < size) {
        const std::size_t start = out.size();
        out.resize(start + 4u, 0u);
    }
}

std::vector<std::uint8_t> makeFixture() {
    std::vector<std::uint8_t> core(0x400u, 0u);
    writeU32(core, 0x00u, 0x10u); // table offset
    writeU32(core, 0x04u, 1u);    // tfrag count

    constexpr std::size_t h = 0x10u;
    writeU32(core, h + 0x10u, 0x40u); // data relative to table => 0x50
    writeU16(core, h + 0x14u, 0x100u); // buffer 1
    writeU16(core, h + 0x16u, 0x120u); // buffer 2
    writeU16(core, h + 0x18u, 0x120u); // not directly used by pointer formula
    writeU16(core, h + 0x1au, 0x240u); // buffer 4
    writeU16(core, h + 0x1eu, 0x020u); // rgba
    core[h + 0x20u] = 0x10u; // buffer2 ends at 0x220
    core[h + 0x22u] = 0x14u; // buffer5 starts at 0x260
    core[h + 0x23u] = 0x08u; // buffer5 ends at 0x2c0
    core[h + 0x28u] = 1u;    // one local texture primitive
    core[h + 0x29u] = 1u;    // four RGBA entries

    constexpr std::size_t data = 0x50u;
    for (std::size_t i = 0u; i < 4u; ++i) {
        core[data + 0x20u + i * 4u + 0u] = 128u;
        core[data + 0x20u + i * 4u + 1u] = 64u;
        core[data + 0x20u + i * 4u + 2u] = 32u;
        core[data + 0x20u + i * 4u + 3u] = 128u;
    }

    std::vector<std::uint8_t> strips(8u, 0u);
    strips[0] = 0x83u; // three vertices + material change
    strips[1] = 0x00u;
    strips[2] = 0x00u; // primitive 0 (offset / 5)
    strips[4] = 0x00u;
    strips[5] = 0xffu; // sentinel
    strips[6] = 0xffu;
    const std::vector<std::uint8_t> indices = {0u, 1u, 2u};

    std::vector<std::uint8_t> b1;
    appendCommand(b1, 0x62u, 3u, indices);
    appendCommand(b1, 0x6eu, 2u, strips);
    appendNopsTo(b1, 0x20u);

    std::vector<std::uint8_t> base(16u, 0u);
    writeU32(base, 0u, 10u * 1024u);
    writeU32(base, 4u, 20u * 1024u);
    writeU32(base, 8u, 30u * 1024u);

    std::vector<std::uint8_t> vu(40u, 0u);
    writeU16(vu, 0x00u, 3u); // three common positions

    std::vector<std::uint8_t> primitive(0x50u, 0u);
    writeU32(primitive, 0x00u, 0u); // TEX0 low -> global material 0

    std::vector<std::uint8_t> info(24u, 0u);
    for (std::size_t i = 0u; i < 3u; ++i) {
        writeI16(info, i * 8u + 0u, i == 1u ? 4096 : 0);
        writeI16(info, i * 8u + 2u, i == 2u ? 4096 : 0);
        writeI16(info, i * 8u + 4u, 4096);
        writeI16(info, i * 8u + 6u, static_cast<std::int16_t>(i * 2u));
    }

    std::vector<std::uint8_t> positions(18u, 0u);
    writeI16(positions, 6u + 0u, 1024);
    writeI16(positions, 12u + 2u, 1024);

    // Match the retail common-buffer command order. In particular there are
    // two STROW commands: command 2 is a VU helper row beginning with
    // 0x45000000, while command 5 is the actual world-space base position.
    // This fixture deliberately makes the helper row absurd as a coordinate so
    // accidentally selecting the first STROW is caught immediately.
    std::vector<std::uint8_t> helperRow(16u, 0u);
    writeU32(helperRow, 0u, 0x45000000u);
    writeU32(helperRow, 4u, 0x45000000u);

    std::vector<std::uint8_t> b2;
    appendCommand(b2, 0x6du, 5u, vu);         // command 0: V4_16 VU header
    appendCommand(b2, 0x6cu, 5u, primitive);  // command 1: V4_32 texture primitive
    appendCommand(b2, 0x30u, 0u, helperRow);  // command 2: STROW VU helper (not position)
    appendCommand(b2, 0x05u, 0u, {} , 1u);    // command 3: STMOD 1
    appendCommand(b2, 0x6du, 3u, info);       // command 4: V4_16 vertex info
    appendCommand(b2, 0x30u, 0u, base);       // command 5: STROW world-space base
    appendCommand(b2, 0x01u, 0u, {}, 0x0102u);// command 6: STCYCL WL=1 CL=2
    appendCommand(b2, 0x69u, 3u, positions);  // command 7: V3_16 positions
    appendCommand(b2, 0x01u, 0u, {}, 0x0404u);// command 8: STCYCL WL=4 CL=4
    appendCommand(b2, 0x05u, 0u, {}, 0u);     // command 9: STMOD 0
    appendNopsTo(b2, 0x100u);

    std::vector<std::uint8_t> b3;
    appendCommand(b3, 0x6eu, 2u, strips);
    appendCommand(b3, 0x62u, 3u, indices);
    appendNopsTo(b3, 0x20u);

    std::vector<std::uint8_t> b4;
    appendNopsTo(b4, 0x20u);

    std::vector<std::uint8_t> b5;
    appendCommand(b5, 0x6eu, 2u, strips);
    appendCommand(b5, 0x62u, 3u, indices);
    appendNopsTo(b5, 0x60u);

    const auto copy = [&](std::size_t at, const std::vector<std::uint8_t>& src) {
        for (std::size_t i = 0u; i < src.size(); ++i) core.at(data + at + i) = src[i];
    };
    copy(0x100u, b1);
    copy(0x120u, b2);
    copy(0x220u, b3);
    copy(0x240u, b4);
    copy(0x260u, b5);
    return core;
}

bool close(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const auto core = makeFixture();
    const auto result = ratchet::assets::decodeRac1TfragTerrain(core, 0u, 1u);
    if (!result.ok()) {
        std::cerr << "rac1_tfrag_tests: decode failed status="
                  << ratchet::assets::rac1TfragStatusName(result.status) << '\n';
        return 1;
    }
    const auto& mesh = result.mesh;
    if (mesh.tfragCount != 1u || mesh.stripCount != 1u || mesh.triangleCount != 1u ||
        mesh.batches.size() != 1u || mesh.batches[0].materialIndex != 0u ||
        mesh.batches[0].triangleVertices.size() != 3u) {
        std::cerr << "rac1_tfrag_tests: mesh counts mismatch\n";
        return 1;
    }
    const auto& a = mesh.batches[0].triangleVertices[0];
    const auto& b = mesh.batches[0].triangleVertices[1];
    const auto& c = mesh.batches[0].triangleVertices[2];
    if (!close(a.x, 10.0f) || !close(a.y, 20.0f) || !close(a.z, 30.0f) ||
        !close(b.x, 11.0f) || !close(b.u, 1.0f) ||
        !close(c.y, 21.0f) || !close(c.v, 1.0f) ||
        a.r != 255u || a.g != 128u || a.b != 64u || a.a != 255u) {
        std::cerr << "rac1_tfrag_tests: vertex reconstruction mismatch\n";
        return 1;
    }

    std::cout << "R&C1 native tfrag terrain tests passed\n";
    return 0;
}
