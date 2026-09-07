#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::game {

// Retail gameplay-sky state used by FUN_0022B288. The game rebuilds the
// object matrix at 0x001D96E0 independently for each shell immediately before
// FUN_0022B690/FUN_0022BF94 consume it. OpenRatchet therefore reconstructs the
// same per-shell matrix from the live inputs instead of sampling the transient
// scratch matrix or substituting the viewer camera.
struct Rac1LiveSkyLayout {
    static constexpr std::uint32_t kBaseAngleAddress = 0x00160404u;
    static constexpr std::uint32_t kSkyPointerAddress = 0x0016045cu;
    static constexpr std::uint32_t kTranslationAddress = 0x00160460u;
    static constexpr std::uint32_t kObjectMatrixScratchAddress = 0x001d96e0u;

    static constexpr std::uint32_t kShellCountOffset = 0x06u;
    static constexpr std::uint32_t kShellPointerTableOffset = 0x20u;
    static constexpr std::size_t kShellHeaderBytes = 0x10u;

    // FUN_0022B288 has exact transform cases for indices 0..5. Level 0 uses
    // only 0..4, but retaining all six proved cases keeps the bridge faithful.
    static constexpr std::size_t kProvedShellTransformCount = 6u;
};

struct Rac1LiveSkyState {
    std::uint32_t skyGuestAddress = 0u;
    std::size_t shellCount = 0u;
    std::array<std::uint32_t, Rac1LiveSkyLayout::kProvedShellTransformCount>
        shellGuestAddresses{};
    // FUN_0022B690 immediately consumes the shell header selected from the
    // sky+0x20 pointer table. Preserve the first two signed words as permanent
    // provenance so runtime shell identity can be joined to native topology
    // without assuming live and level-core shell counts are identical.
    std::array<std::int32_t, Rac1LiveSkyLayout::kProvedShellTransformCount>
        shellClusterCounts{};
    std::array<std::int32_t, Rac1LiveSkyLayout::kProvedShellTransformCount>
        shellFlags{};
    float baseAngle = 0.0f;
    std::array<float, 4> translation{};
    std::array<std::array<float, 16>, Rac1LiveSkyLayout::kProvedShellTransformCount>
        shellObjectMatrices{};
};

enum class Rac1LiveSkyStatus : std::uint8_t {
    Ok,
    GuestMemoryTooSmall,
    SkyPointerNotMaterialized,
    SkyPointerOutOfRange,
    ShellCountNotMaterialized,
    UnsupportedShellCount,
    ShellPointerOutOfRange,
    NonFiniteBaseAngle,
    NonFiniteTranslation,
    TranslationNotMaterialized,
    InvalidTranslationW,
};

struct Rac1LiveSkyResult {
    Rac1LiveSkyStatus status = Rac1LiveSkyStatus::GuestMemoryTooSmall;
    Rac1LiveSkyState sky{};
    bool identityMaterialized = false;

    [[nodiscard]] bool ok() const noexcept { return status == Rac1LiveSkyStatus::Ok; }
};

[[nodiscard]] Rac1LiveSkyResult
inspectRac1LiveSky(std::span<const std::uint8_t> guestRdram);

// Exact single-wrap behavior of FUN_001FA580 for the range used by the sky
// shell table. The pi literal is Retail's 0x40490FDB float.
[[nodiscard]] float wrapRac1RetailSkyAngle(float base, float offset) noexcept;

// Exact FUN_001FA070/FUN_0022B288 shell-object transform reconstruction for
// the six Retail cases. The returned matrix is column-major, exactly matching
// the qword columns consumed by FUN_0022BF94.
[[nodiscard]] std::array<float, 16> buildRac1RetailSkyObjectMatrix(
    std::size_t shellIndex,
    float baseAngle,
    const std::array<float, 4>& translation) noexcept;

[[nodiscard]] bool rac1LiveSkyStatusIsDeferred(Rac1LiveSkyStatus status) noexcept;
[[nodiscard]] const char* rac1LiveSkyStatusName(Rac1LiveSkyStatus status) noexcept;

} // namespace ratchet::game
