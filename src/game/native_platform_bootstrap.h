#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

struct Rac1NativePlatformBootstrapContract {
    static constexpr std::uint32_t kGuestRamBytes = 0x02000000u;

    // Successful retail sub_00124510 startup state:
    //   - DBCMAN SetWorkAddr publishes its initial zero 0x80-byte snapshot to
    //     0x15B480.
    //   - sub_00124510 clears the adjacent 16-word EE state table at
    //     0x15B500..0x15B53F.
    // These ranges are contiguous, so native initialization can reproduce the
    // game-visible state directly without SIF/IOP transport.
    static constexpr std::uint32_t kDbcWorkStateAddress = 0x0015b480u;
    static constexpr std::uint32_t kDbcWorkSnapshotBytes = 0x80u;
    static constexpr std::uint32_t kDbcEeStateBytes = 0x40u;
    static constexpr std::uint32_t kDbcInitialStateBytes =
        kDbcWorkSnapshotBytes + kDbcEeStateBytes;

    static constexpr std::uint32_t kDbcInitFunction = 0x00124510u;
    static constexpr std::uint32_t kDbcLinkAllocateFunction = 0x00124718u;
    static constexpr std::uint32_t kMemoryCardBootstrapFunction = 0x0020ac58u;
    static constexpr std::uint32_t kMemoryCardPreflightFunction = 0x00209168u;
    static constexpr std::uint32_t kMoviePlaybackFunction = 0x0023a3b8u;

    // Sony SDK library probes that reach FILEIO only to inspect rom0:ROMVER.
    // The embedded PS2Recomp SCE symbol database identifies these exact retail
    // functions as libgraph::checkModelVersion and libscf::IsT10K. Their
    // underlying questions are host capabilities/profile properties, not game
    // filesystem operations, so OpenRatchet answers them at this higher boundary.
    static constexpr std::uint32_t kGsDxDyCapabilityProbeFunction = 0x00121a18u;
    static constexpr std::uint32_t kIsT10KProbeFunction = 0x0012d200u;

    static constexpr std::uint32_t kDbcLinkCapacity = 16u;
};

// Pure state transition used by both the runtime replacement and unit tests.
// It reproduces only the proved EE-visible successful DBC initialization state.
[[nodiscard]] bool applyRac1NativeDbcInitialState(std::span<std::uint8_t> guestMemory);

// Installs Phase-11 platform HLE boundaries. These bypass PS2 controller,
// memory-card, ROMVER/FILEIO and PS2 MPEG transport while preserving original
// game-side logic. Save persistence remains deferred to Phase 19; movie playback
// remains deferred to Phase 17. The Phase-11 boundaries select only proved
// existing retail outcomes and never synthesize save state, controller input or
// MPEG/SIF transport.
void declareNativePlatformBootstrapReplacements(
    runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
