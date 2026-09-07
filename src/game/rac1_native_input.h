#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Retail controller parser FUN_00217328 consumes a compact report whose first
// six bytes are, in order: active-low 16-bit buttons (big-endian on the wire),
// right X/Y, left X/Y. Reports shorter than 0x12 deliberately skip the optional
// pressure-byte path, so Phase 12 begins with the exact six-byte digital+stick
// contract instead of inventing pressure data.
struct Rac1RetailPadReport {
    static constexpr std::size_t kBytes = 6u;
    std::array<std::uint8_t, kBytes> bytes{0xffu, 0xffu, 0x7fu, 0x7fu, 0x7fu, 0x7fu};

    bool operator==(const Rac1RetailPadReport&) const = default;
};

enum class Rac1PadButton : std::uint16_t {
    Select   = 0x0001u,
    L3       = 0x0002u,
    R3       = 0x0004u,
    Start    = 0x0008u,
    Up       = 0x0010u,
    Right    = 0x0020u,
    Down     = 0x0040u,
    Left     = 0x0080u,
    L2       = 0x0100u,
    R2       = 0x0200u,
    L1       = 0x0400u,
    R1       = 0x0800u,
    Triangle = 0x1000u,
    Circle   = 0x2000u,
    Cross    = 0x4000u,
    Square   = 0x8000u,
};

constexpr std::uint16_t rac1PadButtonMask(Rac1PadButton button) noexcept {
    return static_cast<std::uint16_t>(button);
}

struct Rac1NativeInputSample {
    // Logical pressed-button bits in the same post-inversion domain that
    // FUN_00217328 stores at controllerState+0x1A0.
    std::uint16_t pressedButtons = 0u;
    float rightX = 0.0f;
    float rightY = 0.0f;
    float leftX = 0.0f;
    float leftY = 0.0f;
};

struct Rac1RetailParsedInputLayout {
    // FUN_00217328 writes these exact fields in the primary controller state:
    //   0x217388/0x21738C -> current button word,
    //   0x217640/0x217648 -> rising/pressed edges,
    //   0x217644/0x21764C -> falling/released edges.
    // The four normalized/deadzoned floats at +0x100..+0x10C retain the raw
    // report order right-X, right-Y, left-X, left-Y. The Phase-12.2 inspector
    // is read-only; Retail remains the sole writer of every field below.
    static constexpr std::uint32_t kControllerStateAddress = 0x0013c940u;
    static constexpr std::uint32_t kRightXOffset = 0x0100u;
    static constexpr std::uint32_t kRightYOffset = 0x0104u;
    static constexpr std::uint32_t kLeftXOffset = 0x0108u;
    static constexpr std::uint32_t kLeftYOffset = 0x010cu;
    static constexpr std::uint32_t kCurrentButtonsOffset = 0x01a0u;
    static constexpr std::uint32_t kPressedEdgesOffset = 0x01a4u;
    static constexpr std::uint32_t kReleasedEdgesOffset = 0x01a8u;
    static constexpr std::uint32_t kRequiredBytes = kReleasedEdgesOffset + 4u;
};

enum class Rac1RetailParsedInputStatus {
    Ok,
    GuestMemoryTooSmall,
};

struct Rac1RetailParsedInputSnapshot {
    Rac1RetailParsedInputStatus status = Rac1RetailParsedInputStatus::GuestMemoryTooSmall;
    std::uint32_t currentButtons = 0u;
    std::uint32_t pressedEdges = 0u;
    std::uint32_t releasedEdges = 0u;
    float rightX = 0.0f;
    float rightY = 0.0f;
    float leftX = 0.0f;
    float leftY = 0.0f;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1RetailParsedInputStatus::Ok;
    }
    bool operator==(const Rac1RetailParsedInputSnapshot&) const = default;
};

struct Rac1NativeInputContract {
    static constexpr std::uint32_t kGuestRamBytes = 0x02000000u;

    // libdbc-facing EE boundaries used by the original controller update
    // sub_002170C8. The game-side state machine and FUN_00217328 parser remain
    // untouched; only these PS2 transport transactions become native.
    static constexpr std::uint32_t kReceiveDataFunction = 0x00124bd8u;
    static constexpr std::uint32_t kConnectionInfoFunction = 0x00124cb0u;
    static constexpr std::uint32_t kDeviceStatusFunction = 0x00124da0u;
    static constexpr std::uint32_t kAuxInfoFunction = 0x001250d8u;

    static constexpr std::size_t kTransitionInfoBytes = 4u;
};

[[nodiscard]] std::uint8_t quantizeRac1PadAxis(float value) noexcept;
[[nodiscard]] Rac1RetailPadReport encodeRac1RetailPadReport(
    const Rac1NativeInputSample& sample) noexcept;

// Host/UI thread publishes one coherent sample. Guest replacement callbacks
// consume an atomic six-byte snapshot on the next Retail controller update.
void publishRac1NativeInputSample(const Rac1NativeInputSample& sample) noexcept;
[[nodiscard]] Rac1RetailPadReport currentRac1RetailPadReport() noexcept;

// Direct Phase-12.2 gate: observe the unchanged Retail parser's authoritative
// output in guest RDRAM without mutating controller/game state.
[[nodiscard]] Rac1RetailParsedInputSnapshot inspectRac1RetailParsedInput(
    std::span<const std::uint8_t> guestRdram) noexcept;
[[nodiscard]] const char* rac1RetailParsedInputStatusName(
    Rac1RetailParsedInputStatus status) noexcept;

void declareRac1NativeInputReplacements(
    runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
