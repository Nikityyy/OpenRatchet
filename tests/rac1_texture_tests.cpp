#include "assets/rac1_texture.h"

#include <cstddef>
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

void writeColor(std::vector<std::uint8_t>& gs,
                std::size_t paletteBase,
                std::size_t index,
                std::uint8_t r,
                std::uint8_t g,
                std::uint8_t b,
                std::uint8_t a) {
    const std::size_t o = paletteBase + index * 4u;
    gs.at(o + 0u) = r;
    gs.at(o + 1u) = g;
    gs.at(o + 2u) = b;
    gs.at(o + 3u) = a;
}

} // namespace

int main() {
    std::vector<std::uint8_t> core(0x200u, 0u);
    std::vector<std::uint8_t> index(0x40u, 0u);
    std::vector<std::uint8_t> gs(0x600u, 0u);

    constexpr std::size_t kTextureBase = 0x100u;
    core[kTextureBase + 0u] = 0u;
    core[kTextureBase + 1u] = 8u;
    core[kTextureBase + 2u] = 16u;
    core[kTextureBase + 3u] = 24u;

    writeU32(index, 0x00u, 0u); // dataOffset
    writeU16(index, 0x04u, 2u);
    writeU16(index, 0x06u, 2u);
    writeU16(index, 0x0au, 1u); // palette index

    constexpr std::size_t kPalette = 0x100u;
    writeColor(gs, kPalette, 0u, 10u, 20u, 30u, 128u);
    writeColor(gs, kPalette, 16u, 40u, 50u, 60u, 64u); // input index 8 -> 16
    writeColor(gs, kPalette, 8u, 70u, 80u, 90u, 127u); // input index 16 -> 8
    writeColor(gs, kPalette, 24u, 100u, 110u, 120u, 128u);

    const auto result = ratchet::assets::decodeRac1TfragTextures(
        core, index, gs, {0u, 1u}, static_cast<std::uint32_t>(kTextureBase));
    if (!result.ok() || result.textures.size() != 1u) {
        std::cerr << "rac1_texture_tests: decode failed status="
                  << ratchet::assets::rac1TextureStatusName(result.status) << '\n';
        return 1;
    }

    const auto& texture = result.textures[0];
    const std::vector<std::uint8_t> expected = {
        10u, 20u, 30u, 255u,
        40u, 50u, 60u, 128u,
        70u, 80u, 90u, 254u,
        100u, 110u, 120u, 255u,
    };
    if (texture.width != 2u || texture.height != 2u || !texture.hasAlpha ||
        texture.rgba != expected) {
        std::cerr << "rac1_texture_tests: decoded RGBA/palette swizzle mismatch\n";
        return 1;
    }

    std::cout << "R&C1 native tfrag texture tests passed\n";
    return 0;
}
