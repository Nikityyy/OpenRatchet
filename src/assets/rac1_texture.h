#pragma once

#include "assets/rac1_level.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1TextureStatus : std::uint8_t {
    Ok,
    IndexTableOutOfRange,
    InvalidEntry,
    PixelDataOutOfRange,
    PaletteOutOfRange,
};

struct Rac1Texture {
    std::uint16_t width = 0u;
    std::uint16_t height = 0u;
    bool hasAlpha = false;
    std::vector<std::uint8_t> rgba;
};

struct Rac1TextureSetResult {
    Rac1TextureStatus status = Rac1TextureStatus::IndexTableOutOfRange;
    std::vector<Rac1Texture> textures;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1TextureStatus::Ok;
    }
};

// Decodes the R&C1 8-bit paletted tfrag texture table into ordinary RGBA8
// host images. Pixel indices live in the decompressed core at
// texturesBaseOffset + TextureEntry::dataOffset. Palettes live in the level's
// raw GS-RAM blob at paletteIndex * 0x100 and use the PS2 palette swizzle.
Rac1TextureSetResult decodeRac1TfragTextures(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gsRam,
    Rac1ArrayRange tfragTextures,
    std::uint32_t texturesBaseOffset);

const char* rac1TextureStatusName(Rac1TextureStatus status) noexcept;

} // namespace ratchet::assets
