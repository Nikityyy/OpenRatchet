#pragma once

#include "game/rac1_live_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ratchet::game {

// FUN_0020cca8 and sub_0020CD48 multiply the live Moby scale at +0x2c by
// 0x3A800000, exactly 1/1024. FUN_0020def8 instead keeps the raw value and
// multiplies the world translation by 1024 before combining both domains.
inline constexpr float kRac1LiveMobyRawScaleToWorld = 1.0f / 1024.0f;

enum class Rac1LiveRatchetTransformStatus : std::uint8_t {
    Ok,
    PoolNotReady,
    RatchetCandidateAccountingMismatch,
    RatchetNotFound,
    MultipleRatchets,
    NonFinitePosition,
    NonFiniteScale,
    NonFiniteRotationInput,
    RotationBasisNotMaterialized,
    NonFiniteRotationBasis,
};

struct Rac1LiveMobyWorldTransform {
    std::uint32_t mobyGuestAddress = 0u;
    std::int16_t oClass = 0;
    std::array<float, 3> position{};
    float rawModelScale = 0.0f;
    float worldModelScale = 0.0f;
    std::array<float, 3> rotationInput{};

    // FUN_0020def8 stores vf20/vf21/vf22 to +0xc0/+0xd0/+0xe0 and then uses
    // them as columns: basisX*x + basisY*y + basisZ*z. We preserve exactly
    // that Retail orientation rather than reconstructing it from Euler angles.
    std::array<float, 3> basisX{};
    std::array<float, 3> basisY{};
    std::array<float, 3> basisZ{};
};

struct Rac1LiveRatchetTransformResult {
    Rac1LiveRatchetTransformStatus status = Rac1LiveRatchetTransformStatus::PoolNotReady;
    std::size_t ratchetCandidates = 0u;
    Rac1LiveMobyWorldTransform transform{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveRatchetTransformStatus::Ok;
    }
};

// Select exactly one traversed oClass-0 record and validate its proved Retail
// world-transform fields. A completely zero +0xc0/+0xd0/+0xe0 block is the
// exact allocation/precompute state (the 0x100-byte Moby is zeroed before the
// basis producer runs), and is therefore reported without fabricating a host
// rotation. All materialized values are consumed exactly as stored by Retail.
[[nodiscard]] Rac1LiveRatchetTransformResult inspectRac1LiveRatchetWorldTransform(
    const Rac1LiveMobyPoolSnapshot& poolSnapshot);

// Direct translation of FUN_0020def8's world transform after dividing its
// 1024-scaled output domain back to the native Phase-9/10 world units:
//   world = position + basis * (rawPosition * mobyScale/1024).
// `rawPosition` is a Phase-10 raw skinned position, before class/instance world
// scale, exactly like transformRac1MobySkinnedPositionToWorld's input.
[[nodiscard]] std::array<float, 3> transformRac1LiveMobyRawPositionToWorld(
    const Rac1LiveMobyWorldTransform& transform,
    const std::array<float, 3>& rawPosition) noexcept;

[[nodiscard]] const char* rac1LiveRatchetTransformStatusName(
    Rac1LiveRatchetTransformStatus status) noexcept;

} // namespace ratchet::game
