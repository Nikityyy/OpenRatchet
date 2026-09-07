#include "game/rac1_live_sky.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace ratchet::game {
namespace {

constexpr float fromBits(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

// Keep every arithmetic stage separately rounded to binary32, matching the
// scalar R5900 FPU / VU0 operation sequence used by FUN_001FA070/001FA580.
float fadd(float a, float b) noexcept {
    volatile float result = a + b;
    return result;
}
float fsub(float a, float b) noexcept {
    volatile float result = a - b;
    return result;
}
float fmul(float a, float b) noexcept {
    volatile float result = a * b;
    return result;
}

float retailSinApprox(float angle) noexcept {
    constexpr float kPi = fromBits(0x40490fdbu);
    constexpr float kHalfPi = fromBits(0x3fc90fdbu);
    constexpr float kC3 = fromBits(0xbe2aaaa4u);
    constexpr float kC5 = fromBits(0x3c08873eu);
    constexpr float kC7 = fromBits(0xb94fb21fu);
    constexpr float kC9 = fromBits(0x362e9c14u);

    float x = angle;
    if (!(x < kHalfPi)) {
        x = fsub(kPi, x);
    } else if (x < -kHalfPi) {
        x = fsub(-kPi, x);
    }

    const float x2 = fmul(x, x);
    const float x3 = fmul(x, x2);
    const float x5 = fmul(x3, x2);
    const float x7 = fmul(x5, x2);
    const float x9 = fmul(x7, x2);

    float result = x;
    result = fadd(result, fmul(x3, kC3));
    result = fadd(result, fmul(x5, kC5));
    result = fadd(result, fmul(x7, kC7));
    result = fadd(result, fmul(x9, kC9));
    return result;
}

std::array<float, 3> leftMultiplyY(const std::array<float, 3>& column,
                                   float sine,
                                   float cosine) noexcept {
    // FUN_001FA070 basis columns for Y are
    // (cos,0,-sin), (0,1,0), (sin,0,cos). It forms each result column as
    // basisX*old.x + basisY*old.y + basisZ*old.z.
    return {
        fadd(fmul(cosine, column[0]), fmul(sine, column[2])),
        column[1],
        fadd(fmul(-sine, column[0]), fmul(cosine, column[2])),
    };
}

struct ShellParameters {
    float y = 0.0f;
    float zOffset = 0.0f;
    float scale = 1.0f;
};

ShellParameters shellParameters(std::size_t shellIndex) noexcept {
    switch (shellIndex) {
    case 0u:
    case 1u:
        return {};
    case 2u:
        return {fromBits(0xbd99999au), fromBits(0xbe19999au),
                fromBits(0x3fa00000u)};
    case 3u:
        return {fromBits(0x3d4ccccdu), fromBits(0x3e000000u),
                fromBits(0x3fc00000u)};
    case 4u:
        return {fromBits(0x3dcccccdu), fromBits(0xbd4ccccdu),
                fromBits(0x3fe00000u)};
    case 5u:
        return {fromBits(0xbe19999au), fromBits(0x3dcccccdu),
                fromBits(0x40000000u)};
    default:
        return {};
    }
}

std::uint32_t readU32(std::span<const std::uint8_t> guestRdram,
                      std::uint32_t address) noexcept {
    std::uint32_t value = 0u;
    std::memcpy(&value, guestRdram.data() + address, sizeof(value));
    return value;
}

std::int16_t readI16(std::span<const std::uint8_t> guestRdram,
                     std::uint32_t address) noexcept {
    std::int16_t value = 0;
    std::memcpy(&value, guestRdram.data() + address, sizeof(value));
    return value;
}

std::int32_t readI32(std::span<const std::uint8_t> guestRdram,
                     std::uint32_t address) noexcept {
    std::int32_t value = 0;
    std::memcpy(&value, guestRdram.data() + address, sizeof(value));
    return value;
}

float readFloat(std::span<const std::uint8_t> guestRdram,
                std::uint32_t address) noexcept {
    float value = 0.0f;
    std::memcpy(&value, guestRdram.data() + address, sizeof(value));
    return value;
}

bool fits(std::span<const std::uint8_t> guestRdram,
          std::uint32_t address,
          std::size_t bytes) noexcept {
    const std::size_t start = static_cast<std::size_t>(address);
    return start <= guestRdram.size() && bytes <= guestRdram.size() - start;
}

bool allExactlyZero(const std::array<float, 4>& values) noexcept {
    for (float value : values) {
        if (value != 0.0f) return false;
    }
    return true;
}

} // namespace

float wrapRac1RetailSkyAngle(float base, float offset) noexcept {
    constexpr float kPi = fromBits(0x40490fdbu);
    float value = fadd(base, offset);
    if (!(value < kPi)) {
        value = fsub(value, kPi);
        value = fsub(value, kPi);
    }
    if (value < -kPi) {
        value = fadd(value, kPi);
        value = fadd(value, kPi);
    }
    return value;
}

std::array<float, 16> buildRac1RetailSkyObjectMatrix(
    std::size_t shellIndex,
    float baseAngle,
    const std::array<float, 4>& translation) noexcept {
    if (shellIndex >= Rac1LiveSkyLayout::kProvedShellTransformCount) return {};

    constexpr float kHalfPi = fromBits(0x3fc90fdbu);
    const ShellParameters parameters = shellParameters(shellIndex);
    // Shell 0 falls through the shell-1 case in FUN_0022B288, so both 0 and 1
    // call FUN_001FA580(baseAngle, 0) before FUN_001FA070.
    const float z = wrapRac1RetailSkyAngle(baseAngle, parameters.zOffset);
    const float sz = retailSinApprox(z);
    const float cz = retailSinApprox(fadd(z, kHalfPi));
    const float sy = retailSinApprox(parameters.y);
    const float cy = retailSinApprox(fadd(parameters.y, kHalfPi));

    std::array<float, 3> column0{cz, sz, 0.0f};
    std::array<float, 3> column1{-sz, cz, 0.0f};
    std::array<float, 3> column2{0.0f, 0.0f, 1.0f};

    // FUN_001FA070 applies Z first, then Y, then X. The sky's X input is zero,
    // so only the exact Y left-multiply remains after the Z basis is formed.
    column0 = leftMultiplyY(column0, sy, cy);
    column1 = leftMultiplyY(column1, sy, cy);
    column2 = leftMultiplyY(column2, sy, cy);

    for (std::size_t component = 0u; component < 3u; ++component) {
        column0[component] = fmul(column0[component], parameters.scale);
        column1[component] = fmul(column1[component], parameters.scale);
        column2[component] = fmul(column2[component], parameters.scale);
    }

    return {
        column0[0], column0[1], column0[2], 0.0f,
        column1[0], column1[1], column1[2], 0.0f,
        column2[0], column2[1], column2[2], 0.0f,
        translation[0], translation[1], translation[2], translation[3],
    };
}

Rac1LiveSkyResult inspectRac1LiveSky(std::span<const std::uint8_t> guestRdram) {
    using Layout = Rac1LiveSkyLayout;
    Rac1LiveSkyResult result{};

    if (!fits(guestRdram, Layout::kSkyPointerAddress, sizeof(std::uint32_t)) ||
        !fits(guestRdram, Layout::kBaseAngleAddress, sizeof(float)) ||
        !fits(guestRdram, Layout::kTranslationAddress, sizeof(float) * 4u)) {
        result.status = Rac1LiveSkyStatus::GuestMemoryTooSmall;
        return result;
    }

    result.sky.skyGuestAddress = readU32(guestRdram, Layout::kSkyPointerAddress);
    if (result.sky.skyGuestAddress == 0u) {
        result.status = Rac1LiveSkyStatus::SkyPointerNotMaterialized;
        return result;
    }
    if (!fits(guestRdram, result.sky.skyGuestAddress, Layout::kShellPointerTableOffset)) {
        result.status = Rac1LiveSkyStatus::SkyPointerOutOfRange;
        return result;
    }

    const std::int16_t shellCount = readI16(
        guestRdram, result.sky.skyGuestAddress + Layout::kShellCountOffset);
    if (shellCount == 0) {
        result.status = Rac1LiveSkyStatus::ShellCountNotMaterialized;
        return result;
    }
    if (shellCount < 0 ||
        static_cast<std::size_t>(shellCount) > Layout::kProvedShellTransformCount) {
        result.status = Rac1LiveSkyStatus::UnsupportedShellCount;
        return result;
    }
    result.sky.shellCount = static_cast<std::size_t>(shellCount);
    const std::size_t shellPointerBytes = result.sky.shellCount * sizeof(std::uint32_t);
    if (!fits(guestRdram,
              result.sky.skyGuestAddress + Layout::kShellPointerTableOffset,
              shellPointerBytes)) {
        result.status = Rac1LiveSkyStatus::SkyPointerOutOfRange;
        return result;
    }

    for (std::size_t shell = 0u; shell < result.sky.shellCount; ++shell) {
        const std::uint32_t pointer = readU32(
            guestRdram,
            result.sky.skyGuestAddress + Layout::kShellPointerTableOffset +
                static_cast<std::uint32_t>(shell * sizeof(std::uint32_t)));
        result.sky.shellGuestAddresses[shell] = pointer;
        if (pointer == 0u || !fits(guestRdram, pointer, Layout::kShellHeaderBytes)) {
            result.status = Rac1LiveSkyStatus::ShellPointerOutOfRange;
            return result;
        }
        result.sky.shellClusterCounts[shell] = readI32(guestRdram, pointer + 0x00u);
        result.sky.shellFlags[shell] = readI32(guestRdram, pointer + 0x04u);
    }
    result.identityMaterialized = true;

    result.sky.baseAngle = readFloat(guestRdram, Layout::kBaseAngleAddress);
    if (!std::isfinite(result.sky.baseAngle)) {
        result.status = Rac1LiveSkyStatus::NonFiniteBaseAngle;
        return result;
    }
    for (std::size_t component = 0u; component < result.sky.translation.size(); ++component) {
        result.sky.translation[component] = readFloat(
            guestRdram,
            Layout::kTranslationAddress +
                static_cast<std::uint32_t>(component * sizeof(float)));
    }
    for (float value : result.sky.translation) {
        if (!std::isfinite(value)) {
            result.status = Rac1LiveSkyStatus::NonFiniteTranslation;
            return result;
        }
    }

    // sub_0022EAA8 -> FUN_001F9A68 writes 0x160460 from one of the proved
    // 0x1D9AE0 source vectors. XYZ are scaled but W is copied untouched; all
    // five source vectors have W==1. The ELF initializes this qword to zero.
    if (allExactlyZero(result.sky.translation)) {
        result.status = Rac1LiveSkyStatus::TranslationNotMaterialized;
        return result;
    }
    if (result.sky.translation[3] != 1.0f) {
        result.status = Rac1LiveSkyStatus::InvalidTranslationW;
        return result;
    }

    for (std::size_t shell = 0u; shell < result.sky.shellCount; ++shell) {
        result.sky.shellObjectMatrices[shell] = buildRac1RetailSkyObjectMatrix(
            shell, result.sky.baseAngle, result.sky.translation);
    }
    result.status = Rac1LiveSkyStatus::Ok;
    return result;
}

bool rac1LiveSkyStatusIsDeferred(Rac1LiveSkyStatus status) noexcept {
    switch (status) {
    case Rac1LiveSkyStatus::SkyPointerNotMaterialized:
    case Rac1LiveSkyStatus::ShellCountNotMaterialized:
    case Rac1LiveSkyStatus::TranslationNotMaterialized:
        return true;
    default:
        return false;
    }
}

const char* rac1LiveSkyStatusName(Rac1LiveSkyStatus status) noexcept {
    switch (status) {
    case Rac1LiveSkyStatus::Ok: return "ok";
    case Rac1LiveSkyStatus::GuestMemoryTooSmall: return "guest-memory-too-small";
    case Rac1LiveSkyStatus::SkyPointerNotMaterialized: return "sky-pointer-not-materialized";
    case Rac1LiveSkyStatus::SkyPointerOutOfRange: return "sky-pointer-out-of-range";
    case Rac1LiveSkyStatus::ShellCountNotMaterialized: return "shell-count-not-materialized";
    case Rac1LiveSkyStatus::UnsupportedShellCount: return "unsupported-shell-count";
    case Rac1LiveSkyStatus::ShellPointerOutOfRange: return "shell-pointer-out-of-range";
    case Rac1LiveSkyStatus::NonFiniteBaseAngle: return "non-finite-base-angle";
    case Rac1LiveSkyStatus::NonFiniteTranslation: return "non-finite-translation";
    case Rac1LiveSkyStatus::TranslationNotMaterialized: return "translation-not-materialized";
    case Rac1LiveSkyStatus::InvalidTranslationW: return "invalid-translation-w";
    }
    return "unknown";
}

} // namespace ratchet::game
