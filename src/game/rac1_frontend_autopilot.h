#pragma once

#include "game/rac1_native_input.h"

#include <cstdint>

namespace ratchet::game {

// Temporary Phase-12 development helper. It never mutates guest state and never
// bypasses FUN_00217328: while the Retail frontend is not natively visible yet,
// it contributes ordinary Start/Cross host-pad pulses to the same native input
// sample consumed by the unchanged Retail controller path. It stops permanently
// as soon as the authentic Level-0 AOT generation becomes active.
enum class Rac1FrontendAutopilotPulse : std::uint8_t {
    None,
    Start,
    Cross,
};

struct Rac1FrontendAutopilotFrame {
    Rac1NativeInputSample input{};
    Rac1FrontendAutopilotPulse pulse = Rac1FrontendAutopilotPulse::None;
    std::uint64_t frame = 0u;
    bool active = true;

    bool operator==(const Rac1FrontendAutopilotFrame&) const = default;
};

class Rac1FrontendAutopilot final {
public:
    // Give Retail one second of neutral input before beginning the conservative
    // frontend pulse cadence. At 60 Hz each pulse lasts 100 ms; Start and Cross
    // are separated by 400 ms and repeat once per second.
    static constexpr std::uint64_t kInitialNeutralFrames = 60u;
    static constexpr std::uint64_t kCycleFrames = 60u;
    static constexpr std::uint64_t kPulseFrames = 6u;
    static constexpr std::uint64_t kCrossOffsetFrames = 30u;

    [[nodiscard]] Rac1FrontendAutopilotFrame step(
        bool level0GenerationActive,
        const Rac1NativeInputSample& liveInput) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool active() const noexcept { return !stopped_; }

private:
    std::uint64_t frame_ = 0u;
    bool stopped_ = false;
};

[[nodiscard]] const char* rac1FrontendAutopilotPulseName(
    Rac1FrontendAutopilotPulse pulse) noexcept;

} // namespace ratchet::game
