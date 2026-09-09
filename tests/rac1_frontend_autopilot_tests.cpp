#include "game/rac1_frontend_autopilot.h"

#include <iostream>

namespace {

bool require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "rac1_frontend_autopilot_tests: " << message << '\n';
    return false;
}

bool sameInput(const ratchet::game::Rac1NativeInputSample& lhs,
               const ratchet::game::Rac1NativeInputSample& rhs) {
    return lhs.pressedButtons == rhs.pressedButtons &&
           lhs.rightX == rhs.rightX && lhs.rightY == rhs.rightY &&
           lhs.leftX == rhs.leftX && lhs.leftY == rhs.leftY;
}

} // namespace

int main() {
    using namespace ratchet::game;

    Rac1FrontendAutopilot autopilot;
    Rac1NativeInputSample live{};
    live.pressedButtons = rac1PadButtonMask(Rac1PadButton::L1);
    live.leftX = 0.5f;

    Rac1FrontendAutopilotFrame frame{};
    for (std::uint64_t i = 0u; i < Rac1FrontendAutopilot::kInitialNeutralFrames; ++i) {
        frame = autopilot.step(false, live);
        if (!require(frame.active, "autopilot stopped during initial neutral window") ||
            !require(frame.pulse == Rac1FrontendAutopilotPulse::None,
                     "initial neutral window emitted a synthetic pulse") ||
            !require(sameInput(frame.input, live), "initial neutral window did not preserve live input")) {
            return 1;
        }
    }

    frame = autopilot.step(false, live);
    if (!require(frame.pulse == Rac1FrontendAutopilotPulse::Start,
                 "first synthetic pulse must be Start") ||
        !require((frame.input.pressedButtons & rac1PadButtonMask(Rac1PadButton::Start)) != 0u,
                 "Start pulse bit missing") ||
        !require((frame.input.pressedButtons & rac1PadButtonMask(Rac1PadButton::L1)) != 0u,
                 "synthetic pulse discarded live button state") ||
        !require(frame.input.leftX == live.leftX,
                 "synthetic pulse discarded live analog state")) {
        return 1;
    }

    for (std::uint64_t i = 1u; i < Rac1FrontendAutopilot::kCrossOffsetFrames; ++i) {
        frame = autopilot.step(false, live);
    }
    frame = autopilot.step(false, live);
    if (!require(frame.pulse == Rac1FrontendAutopilotPulse::Cross,
                 "Cross pulse did not begin at the documented cycle offset") ||
        !require((frame.input.pressedButtons & rac1PadButtonMask(Rac1PadButton::Cross)) != 0u,
                 "Cross pulse bit missing")) {
        return 1;
    }

    frame = autopilot.step(true, live);
    if (!require(!frame.active, "Level-0 generation did not stop autopilot") ||
        !require(frame.pulse == Rac1FrontendAutopilotPulse::None,
                 "stopped autopilot emitted a pulse") ||
        !require(sameInput(frame.input, live), "stopped autopilot did not return pure live input")) {
        return 1;
    }

    frame = autopilot.step(false, live);
    if (!require(!frame.active,
                 "autopilot restarted after the Level-0 one-way stop") ||
        !require(sameInput(frame.input, live),
                 "one-way stopped autopilot changed live input")) {
        return 1;
    }

    autopilot.reset();
    frame = autopilot.step(false, live);
    if (!require(frame.active, "reset did not reactivate autopilot") ||
        !require(frame.frame == 0u, "reset did not restore frame zero")) {
        return 1;
    }

    std::cout << "R&C1 frontend autopilot tests passed\n";
    return 0;
}
