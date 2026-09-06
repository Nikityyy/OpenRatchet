#include "render/rac1_render_bridge.h"

namespace ratchet::render {
namespace {

constexpr std::uint32_t kLevel0Wad2Index = 69u;
constexpr std::uint32_t kLevel0Wad2StartSector = 0x38f6u;
constexpr std::uint32_t kLevel0Wad2SectorCount = 0x0834u;
constexpr std::uint32_t kLevel0Destination = 0x01654000u;
constexpr std::uint32_t kNativeLevel0Index = 0u;

} // namespace

std::optional<std::uint32_t> nativeLevelForRetailAssetRead(
    const platform::NativeAssetLocation& asset,
    std::uint32_t sourceSector,
    std::uint32_t sectorCount,
    std::uint32_t destination) noexcept {
    if (asset.kind != platform::NativeAssetKind::Wad2 ||
        asset.index != kLevel0Wad2Index ||
        asset.startSector != kLevel0Wad2StartSector ||
        asset.sectorCount != kLevel0Wad2SectorCount ||
        sourceSector != asset.startSector ||
        sectorCount != asset.sectorCount ||
        destination != kLevel0Destination) {
        return std::nullopt;
    }
    return kNativeLevel0Index;
}

std::array<float, 16> rac1RetailClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept {
    return {
        camera.clipX[0], camera.clipX[1], camera.clipX[2], camera.clipX[3],
        camera.clipY[0], camera.clipY[1], camera.clipY[2], camera.clipY[3],
        camera.clipZ[0], camera.clipZ[1], camera.clipZ[2], camera.clipZ[3],
        camera.clipW[0], camera.clipW[1], camera.clipW[2], camera.clipW[3],
    };
}

std::array<float, 4> transformColumnMajor(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) noexcept {
    std::array<float, 4> result{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        result[row] = matrix[0u * 4u + row] * point[0] +
                      matrix[1u * 4u + row] * point[1] +
                      matrix[2u * 4u + row] * point[2] +
                      matrix[3u * 4u + row] * point[3];
    }
    return result;
}

} // namespace ratchet::render
