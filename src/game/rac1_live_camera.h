#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::game {

// Retail R&C1 gameplay-camera state rooted at 0x00186F40. The field meanings
// below are pinned by generated Retail producers/consumers; this bridge does
// not infer a host Camera3D, Euler order, FOV, handedness or axis conversion.
struct Rac1LiveCameraLayout {
    static constexpr std::uint32_t kStateBase = 0x00186f40u;
    static constexpr std::size_t kStateBytes = 0x03a0u;

    // FUN_0022BF94 consumes these four qwords as
    // clipX*x + clipY*y + clipZ*z + clipW*w before vclipw/perspective divide.
    // FUN_001F2260 builds them without folding in camera world position; the
    // input domain is therefore camera-relative, not absolute world xyz.
    static constexpr std::uint32_t kClipXOffset = 0x0100u;
    static constexpr std::uint32_t kClipYOffset = 0x0110u;
    static constexpr std::uint32_t kClipZOffset = 0x0120u;
    static constexpr std::uint32_t kClipWOffset = 0x0130u;

    // FUN_0020D868 subtracts this qword's xyz from Moby world position.
    static constexpr std::uint32_t kWorldPositionOffset = 0x0140u;

    // FUN_001ED2B0/sub_001EDAA8 publish the selected camera transform here;
    // FUN_001F2260 consumes the same qwords as the camera orientation source.
    // Only xyz is part of the proved three-vector orientation contract.
    static constexpr std::uint32_t kOrientationXOffset = 0x0350u;
    static constexpr std::uint32_t kOrientationYOffset = 0x0360u;
    static constexpr std::uint32_t kOrientationZOffset = 0x0370u;
};

struct Rac1LiveCameraState {
    std::array<float, 3> worldPosition{};
    std::array<float, 3> orientationX{};
    std::array<float, 3> orientationY{};
    std::array<float, 3> orientationZ{};

    // Preserve the exact Retail qword order rather than converting it to a host
    // graphics convention. FUN_0022BF94 proves the X/Y/Z/W combination below;
    // camera world translation remains the independent +0x140 field above.
    std::array<float, 4> clipX{};
    std::array<float, 4> clipY{};
    std::array<float, 4> clipZ{};
    std::array<float, 4> clipW{};
};

enum class Rac1LiveCameraStatus {
    Ok,
    GuestMemoryTooSmall,
    OrientationNotMaterialized,
    ClipTransformNotMaterialized,
    NonFinitePosition,
    NonFiniteOrientation,
    NonFiniteClipTransform,
};

struct Rac1LiveCameraResult {
    Rac1LiveCameraStatus status = Rac1LiveCameraStatus::GuestMemoryTooSmall;
    Rac1LiveCameraState camera{};

    [[nodiscard]] bool ok() const { return status == Rac1LiveCameraStatus::Ok; }
};

[[nodiscard]] Rac1LiveCameraResult
inspectRac1LiveCamera(std::span<const std::uint8_t> guestRdram);

// Exact camera-relative vector combination used by FUN_0022BF94. No camera
// world-position subtraction, perspective divide or host viewport convention is
// applied here.
[[nodiscard]] std::array<float, 4> transformRac1LiveCameraPointToClip(
    const Rac1LiveCameraState& camera,
    const std::array<float, 4>& point);

[[nodiscard]] const char* rac1LiveCameraStatusName(Rac1LiveCameraStatus status);

} // namespace ratchet::game
