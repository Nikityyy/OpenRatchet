#include "assets/rac1_texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::size_t kTextureEntryBytes = 0x10u;
constexpr std::size_t kPaletteBytes = 256u * 4u;
constexpr std::uint32_t kMaxTextureSide = 4096u;

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
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

std::uint8_t mapPaletteIndex(std::uint8_t index) noexcept {
    // PS2 PSMT8 CLUT order swaps the middle two bits for the palette rows.
    const bool bit4Shifted = ((index & 0x10u) >> 1u) != 0u;
    const bool bit3 = (index & 0x08u) != 0u;
    return bit4Shifted != bit3 ? static_cast<std::uint8_t>(index ^ 0x18u) : index;
}

Rac1TextureSetResult fail(Rac1TextureStatus status,
                          std::vector<Rac1Texture> textures = {}) {
    return {status, std::move(textures)};
}

} // namespace

const char* rac1TextureStatusName(Rac1TextureStatus status) noexcept {
    switch (status) {
    case Rac1TextureStatus::Ok:
        return "ok";
    case Rac1TextureStatus::IndexTableOutOfRange:
        return "index-table-out-of-range";
    case Rac1TextureStatus::InvalidEntry:
        return "invalid-entry";
    case Rac1TextureStatus::PixelDataOutOfRange:
        return "pixel-data-out-of-range";
    case Rac1TextureStatus::PaletteOutOfRange:
        return "palette-out-of-range";
    }
    return "unknown";
}

Rac1TextureSetResult decodeRac1PaletteTextures(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gsRam,
    Rac1ArrayRange textureTable,
    std::uint32_t texturesBaseOffset) {
    if (textureTable.count == 0u) {
        return {Rac1TextureStatus::Ok, {}};
    }
    if (textureTable.count >
        std::numeric_limits<std::size_t>::max() / kTextureEntryBytes) {
        return fail(Rac1TextureStatus::IndexTableOutOfRange);
    }
    const std::size_t tableBytes =
        static_cast<std::size_t>(textureTable.count) * kTextureEntryBytes;
    if (!fits(textureTable.offset, tableBytes, coreIndex.size()) ||
        texturesBaseOffset > core.size()) {
        return fail(Rac1TextureStatus::IndexTableOutOfRange);
    }

    std::vector<Rac1Texture> textures;
    textures.reserve(textureTable.count);

    for (std::uint32_t i = 0u; i < textureTable.count; ++i) {
        const std::size_t entryOffset =
            static_cast<std::size_t>(textureTable.offset) +
            static_cast<std::size_t>(i) * kTextureEntryBytes;
        const std::int32_t dataOffsetSigned = readI32(coreIndex, entryOffset + 0x0u);
        const std::int16_t widthSigned = readI16(coreIndex, entryOffset + 0x4u);
        const std::int16_t heightSigned = readI16(coreIndex, entryOffset + 0x6u);
        const std::int16_t paletteSigned = readI16(coreIndex, entryOffset + 0xau);
        if (dataOffsetSigned < 0 || widthSigned <= 0 || heightSigned <= 0 ||
            paletteSigned < 0 ||
            static_cast<std::uint32_t>(widthSigned) > kMaxTextureSide ||
            static_cast<std::uint32_t>(heightSigned) > kMaxTextureSide) {
            return fail(Rac1TextureStatus::InvalidEntry, std::move(textures));
        }

        const std::size_t width = static_cast<std::uint16_t>(widthSigned);
        const std::size_t height = static_cast<std::uint16_t>(heightSigned);
        if (width > std::numeric_limits<std::size_t>::max() / height) {
            return fail(Rac1TextureStatus::InvalidEntry, std::move(textures));
        }
        const std::size_t pixelCount = width * height;
        const std::size_t pixelOffset =
            static_cast<std::size_t>(texturesBaseOffset) +
            static_cast<std::size_t>(dataOffsetSigned);
        if (pixelOffset < texturesBaseOffset ||
            !fits(pixelOffset, pixelCount, core.size())) {
            return fail(Rac1TextureStatus::PixelDataOutOfRange, std::move(textures));
        }

        const std::size_t paletteOffset =
            static_cast<std::size_t>(static_cast<std::uint16_t>(paletteSigned)) * 0x100u;
        if (!fits(paletteOffset, kPaletteBytes, gsRam.size())) {
            return fail(Rac1TextureStatus::PaletteOutOfRange, std::move(textures));
        }

        Rac1Texture texture{};
        texture.width = static_cast<std::uint16_t>(width);
        texture.height = static_cast<std::uint16_t>(height);
        texture.rgba.resize(pixelCount * 4u);

        bool hasAlpha = false;
        for (std::size_t p = 0u; p < pixelCount; ++p) {
            const std::uint8_t paletteIndex =
                mapPaletteIndex(core[pixelOffset + p]);
            const std::size_t color = paletteOffset +
                                      static_cast<std::size_t>(paletteIndex) * 4u;
            const std::uint8_t r = gsRam[color + 0u];
            const std::uint8_t g = gsRam[color + 1u];
            const std::uint8_t b = gsRam[color + 2u];
            const std::uint8_t a = static_cast<std::uint8_t>(
                std::min<unsigned>(255u,
                                   static_cast<unsigned>(gsRam[color + 3u]) * 2u));
            texture.rgba[p * 4u + 0u] = r;
            texture.rgba[p * 4u + 1u] = g;
            texture.rgba[p * 4u + 2u] = b;
            texture.rgba[p * 4u + 3u] = a;
            hasAlpha |= a < 255u;
        }
        texture.hasAlpha = hasAlpha;
        textures.push_back(std::move(texture));
    }

    return {Rac1TextureStatus::Ok, std::move(textures)};
}

Rac1TextureSetResult decodeRac1TfragTextures(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gsRam,
    Rac1ArrayRange tfragTextures,
    std::uint32_t texturesBaseOffset) {
    return decodeRac1PaletteTextures(core, coreIndex, gsRam, tfragTextures, texturesBaseOffset);
}

} // namespace ratchet::assets
