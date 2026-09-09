#include "render/rac1_presentation_ownership.h"

namespace ratchet::render {

Rac1PresentationTransition Rac1PresentationOwnership::observe(
    const Rac1PresentationReadiness& readiness) noexcept {
    if (owner_ == Rac1PresentationOwner::NativeGameplay) {
        return Rac1PresentationTransition::None;
    }

    const bool readyForNativeGameplay =
        readiness.level0GenerationActive &&
        readiness.level0Mapped &&
        readiness.nativeRendererReady &&
        readiness.retailCameraReady;
    if (!readyForNativeGameplay) {
        return Rac1PresentationTransition::None;
    }

    owner_ = Rac1PresentationOwner::NativeGameplay;
    return Rac1PresentationTransition::AcquiredNativeGameplay;
}

const char* rac1PresentationOwnerName(Rac1PresentationOwner owner) noexcept {
    switch (owner) {
    case Rac1PresentationOwner::DevelopmentFrontend:
        return "frontend-dev";
    case Rac1PresentationOwner::NativeGameplay:
        return "native-level0";
    }
    return "unknown";
}

} // namespace ratchet::render
