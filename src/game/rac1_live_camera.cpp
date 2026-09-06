#include "game/rac1_live_camera.h"

#include <cmath>
#include <cstring>

namespace ratchet::game {
namespace {

float readFloat(std::span<const std::uint8_t> guestRdram, std::uint32_t address) {
    float value = 0.0f;
    std::memcpy(&value, guestRdram.data() + address, sizeof(value));
    return value;
}

std::array<float, 3> readVec3(std::span<const std::uint8_t> guestRdram,
                              std::uint32_t address) {
    return {
        readFloat(guestRdram, address + 0u),
        readFloat(guestRdram, address + 4u),
        readFloat(guestRdram, address + 8u),
    };
}

std::array<float, 4> readVec4(std::span<const std::uint8_t> guestRdram,
                              std::uint32_t address) {
    return {
        readFloat(guestRdram, address + 0u),
        readFloat(guestRdram, address + 4u),
        readFloat(guestRdram, address + 8u),
        readFloat(guestRdram, address + 12u),
    };
}

template <std::size_t N>
bool allFinite(const std::array<float, N>& values) {
    for (const float value : values) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool allExactlyZero(const std::array<float, N>& values) {
    for (const float value : values) {
        if (value != 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace

Rac1LiveCameraResult
inspectRac1LiveCamera(std::span<const std::uint8_t> guestRdram) {
    using Layout = Rac1LiveCameraLayout;

    Rac1LiveCameraResult result;
    const std::size_t stateEnd =
        static_cast<std::size_t>(Layout::kStateBase) + Layout::kStateBytes;
    if (stateEnd > guestRdram.size()) {
        result.status = Rac1LiveCameraStatus::GuestMemoryTooSmall;
        return result;
    }

    const std::uint32_t base = Layout::kStateBase;
    result.camera.worldPosition =
        readVec3(guestRdram, base + Layout::kWorldPositionOffset);
    result.camera.orientationX =
        readVec3(guestRdram, base + Layout::kOrientationXOffset);
    result.camera.orientationY =
        readVec3(guestRdram, base + Layout::kOrientationYOffset);
    result.camera.orientationZ =
        readVec3(guestRdram, base + Layout::kOrientationZOffset);
    result.camera.clipX = readVec4(guestRdram, base + Layout::kClipXOffset);
    result.camera.clipY = readVec4(guestRdram, base + Layout::kClipYOffset);
    result.camera.clipZ = readVec4(guestRdram, base + Layout::kClipZOffset);
    result.camera.clipW = readVec4(guestRdram, base + Layout::kClipWOffset);

    if (!allFinite(result.camera.worldPosition)) {
        result.status = Rac1LiveCameraStatus::NonFinitePosition;
        return result;
    }
    if (!allFinite(result.camera.orientationX) ||
        !allFinite(result.camera.orientationY) ||
        !allFinite(result.camera.orientationZ)) {
        result.status = Rac1LiveCameraStatus::NonFiniteOrientation;
        return result;
    }
    if (!allFinite(result.camera.clipX) || !allFinite(result.camera.clipY) ||
        !allFinite(result.camera.clipZ) || !allFinite(result.camera.clipW)) {
        result.status = Rac1LiveCameraStatus::NonFiniteClipTransform;
        return result;
    }

    // sub_001E9B10 zeroes the whole 0x3A0-byte state before camera setup. Keep
    // that authentic construction state explicit instead of inventing identity.
    if (allExactlyZero(result.camera.orientationX) &&
        allExactlyZero(result.camera.orientationY) &&
        allExactlyZero(result.camera.orientationZ)) {
        result.status = Rac1LiveCameraStatus::OrientationNotMaterialized;
        return result;
    }

    // The orientation can already be initialized while FUN_001F2260 has not yet
    // published the Retail clip transform. Do not synthesize projection/FOV.
    if (allExactlyZero(result.camera.clipX) && allExactlyZero(result.camera.clipY) &&
        allExactlyZero(result.camera.clipZ) && allExactlyZero(result.camera.clipW)) {
        result.status = Rac1LiveCameraStatus::ClipTransformNotMaterialized;
        return result;
    }

    result.status = Rac1LiveCameraStatus::Ok;
    return result;
}

std::array<float, 4> transformRac1LiveCameraPointToClip(
    const Rac1LiveCameraState& camera,
    const std::array<float, 4>& point) {
    std::array<float, 4> clip{};
    for (std::size_t component = 0; component < clip.size(); ++component) {
        clip[component] =
            camera.clipX[component] * point[0] +
            camera.clipY[component] * point[1] +
            camera.clipZ[component] * point[2] +
            camera.clipW[component] * point[3];
    }
    return clip;
}

const char* rac1LiveCameraStatusName(Rac1LiveCameraStatus status) {
    switch (status) {
    case Rac1LiveCameraStatus::Ok:
        return "ok";
    case Rac1LiveCameraStatus::GuestMemoryTooSmall:
        return "guest-memory-too-small";
    case Rac1LiveCameraStatus::OrientationNotMaterialized:
        return "orientation-not-materialized";
    case Rac1LiveCameraStatus::ClipTransformNotMaterialized:
        return "clip-transform-not-materialized";
    case Rac1LiveCameraStatus::NonFinitePosition:
        return "non-finite-position";
    case Rac1LiveCameraStatus::NonFiniteOrientation:
        return "non-finite-orientation";
    case Rac1LiveCameraStatus::NonFiniteClipTransform:
        return "non-finite-clip-transform";
    }
    return "unknown";
}

} // namespace ratchet::game
