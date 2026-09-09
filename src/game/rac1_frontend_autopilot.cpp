#include "game/rac1_frontend_autopilot.h"

namespace ratchet::game {

Rac1FrontendAutopilotFrame Rac1FrontendAutopilot::step(
    bool level0GenerationActive,
    const Rac1NativeInputSample& liveInput) noexcept {
    Rac1FrontendAutopilotFrame result;
    result.input = liveInput;
    result.frame = frame_;

    if (level0GenerationActive) {
        stopped_ = true;
    }
    if (stopped_) {
        result.active = false;
        ++frame_;
        return result;
    }

    result.active = true;
    if (frame_ >= kInitialNeutralFrames) {
        const std::uint64_t phase =
            (frame_ - kInitialNeutralFrames) % kCycleFrames;
        if (phase < kPulseFrames) {
            result.pulse = Rac1FrontendAutopilotPulse::Start;
            result.input.pressedButtons |= rac1PadButtonMask(Rac1PadButton::Start);
        } else if (phase >= kCrossOffsetFrames &&
                   phase < kCrossOffsetFrames + kPulseFrames) {
            result.pulse = Rac1FrontendAutopilotPulse::Cross;
            result.input.pressedButtons |= rac1PadButtonMask(Rac1PadButton::Cross);
        }
    }

    ++frame_;
    return result;
}

void Rac1FrontendAutopilot::reset() noexcept {
    frame_ = 0u;
    stopped_ = false;
}

const char* rac1FrontendAutopilotPulseName(
    Rac1FrontendAutopilotPulse pulse) noexcept {
    switch (pulse) {
    case Rac1FrontendAutopilotPulse::None:
        return "none";
    case Rac1FrontendAutopilotPulse::Start:
        return "start";
    case Rac1FrontendAutopilotPulse::Cross:
        return "cross";
    }
    return "unknown";
}

} // namespace ratchet::game
