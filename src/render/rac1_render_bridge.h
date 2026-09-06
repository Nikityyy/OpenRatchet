#pragma once

#include "game/rac1_live_camera.h"
#include "platform/native_vfs.h"

#include <array>
#include <cstdint>
#include <optional>

namespace ratchet::render {

// The only currently proved retail level-identity bridge. Phase 11.2 proved
// that this exact WAD2 request is Level-0 initialization. No adjacent WAD2
// index is inferred to be another level.
[[nodiscard]] std::optional<std::uint32_t> nativeLevelForRetailAssetRead(
    const platform::NativeAssetLocation& asset,
    std::uint32_t sourceSector,
    std::uint32_t sectorCount,
    std::uint32_t destination) noexcept;

// OpenGL/rlgl column-major form of Retail's already-materialized clip transform.
// FUN_0022BF94 proves that the four qwords are columns: X*x + Y*y + Z*z + W*w.
[[nodiscard]] std::array<float, 16> rac1RetailClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept;

[[nodiscard]] std::array<float, 4> transformColumnMajor(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) noexcept;

} // namespace ratchet::render
