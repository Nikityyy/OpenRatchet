#pragma once

#include <cstdint>

namespace ratchet::render {

// Temporary Phase-12 presentation state. DevelopmentFrontend is a native
// host-only migration screen used while Retail Boot/frontend logic is still
// running but Phase 13/14 have not implemented the actual 2D frontend yet.
// The known-broken PS2Runtime GS fallback is never presented. NativeGameplay
// remains a one-way ownership transfer once authentic Level-0 gameplay is ready.
enum class Rac1PresentationOwner : std::uint8_t {
    DevelopmentFrontend,
    NativeGameplay,
};

struct Rac1PresentationReadiness {
    bool level0GenerationActive = false;
    bool level0Mapped = false;
    bool nativeRendererReady = false;
    bool retailCameraReady = false;

    bool operator==(const Rac1PresentationReadiness&) const = default;
};

enum class Rac1PresentationTransition : std::uint8_t {
    None,
    AcquiredNativeGameplay,
};

class Rac1PresentationOwnership final {
public:
    [[nodiscard]] Rac1PresentationOwner owner() const noexcept { return owner_; }
    [[nodiscard]] bool developmentFrontendVisible() const noexcept {
        return owner_ == Rac1PresentationOwner::DevelopmentFrontend;
    }
    [[nodiscard]] bool nativeGameplayVisible() const noexcept {
        return owner_ == Rac1PresentationOwner::NativeGameplay;
    }

    Rac1PresentationTransition observe(const Rac1PresentationReadiness& readiness) noexcept;
    void reset() noexcept { owner_ = Rac1PresentationOwner::DevelopmentFrontend; }

private:
    Rac1PresentationOwner owner_ = Rac1PresentationOwner::DevelopmentFrontend;
};

const char* rac1PresentationOwnerName(Rac1PresentationOwner owner) noexcept;

} // namespace ratchet::render
