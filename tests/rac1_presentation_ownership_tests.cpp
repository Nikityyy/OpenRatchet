#include "render/rac1_presentation_ownership.h"

#include <iostream>

namespace {

bool require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "rac1_presentation_ownership_tests: " << message << '\n';
    return false;
}

} // namespace

int main() {
    using namespace ratchet::render;

    Rac1PresentationOwnership ownership;
    if (!require(ownership.owner() == Rac1PresentationOwner::DevelopmentFrontend,
                 "initial owner must be development frontend") ||
        !require(ownership.developmentFrontendVisible(),
                 "development frontend must initially remain visible") ||
        !require(!ownership.nativeGameplayVisible(),
                 "native gameplay must not own the initial frame")) {
        return 1;
    }

    Rac1PresentationReadiness readiness{};
    if (!require(ownership.observe(readiness) == Rac1PresentationTransition::None,
                 "empty readiness unexpectedly acquired native ownership")) {
        return 1;
    }

    readiness.level0GenerationActive = true;
    if (!require(ownership.observe(readiness) == Rac1PresentationTransition::None,
                 "generation alone must not acquire native ownership")) {
        return 1;
    }
    readiness.level0Mapped = true;
    if (!require(ownership.observe(readiness) == Rac1PresentationTransition::None,
                 "generation+mapping alone must not acquire native ownership")) {
        return 1;
    }
    readiness.nativeRendererReady = true;
    if (!require(ownership.observe(readiness) == Rac1PresentationTransition::None,
                 "camera readiness must remain mandatory")) {
        return 1;
    }
    readiness.retailCameraReady = true;
    if (!require(ownership.observe(readiness) ==
                     Rac1PresentationTransition::AcquiredNativeGameplay,
                 "complete Level-0 readiness did not acquire native ownership") ||
        !require(ownership.nativeGameplayVisible(),
                 "native gameplay owner not retained after acquisition")) {
        return 1;
    }

    // Final-frame ownership is deliberately one-way. Transient gameplay gaps or
    // later streaming must never make the development frontend visible again.
    readiness = {};
    if (!require(ownership.observe(readiness) == Rac1PresentationTransition::None,
                 "native owner emitted a second transition") ||
        !require(ownership.owner() == Rac1PresentationOwner::NativeGameplay,
                 "native ownership regressed to development frontend")) {
        return 1;
    }

    ownership.reset();
    if (!require(ownership.owner() == Rac1PresentationOwner::DevelopmentFrontend,
                 "reset did not restore startup owner")) {
        return 1;
    }

    std::cout << "R&C1 presentation ownership tests passed\n";
    return 0;
}
