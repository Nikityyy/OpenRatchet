#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

struct Rac1NativeAudioBootstrapContract {
    static constexpr std::uint32_t kGuestRamBytes = 0x02000000u;

    // sub_0022C8D0 is the game-side audio startup wrapper. Its first platform
    // dependency is Sony 989snd, whose generated library function at 0x12DA28
    // binds the two RPC services 0x00123456 / 0x00123457 and identifies itself
    // through the embedded source path /usr/local/989snd/ee/989snd.c.
    static constexpr std::uint32_t kGameAudioBootstrapFunction = 0x0022c8d0u;
    static constexpr std::uint32_t kLevelSoundBankLoadFunction = 0x0022d708u;
    static constexpr std::uint32_t k989SndCommandService = 0x00123456u;
    static constexpr std::uint32_t k989SndLoaderService = 0x00123457u;

    // Game-owned state written directly by sub_0022C8D0 before/after 989snd
    // setup. Native HLE preserves these writes while deliberately omitting the
    // PS2 audio-library/IOP transactions until Phase 18 NativeAudio owns them.
    static constexpr std::uint32_t kAudioStateBase = 0x0013e550u;
    static constexpr std::uint32_t kAudioStateRecordSpan = 0x70u;
    static constexpr std::uint32_t kAudioStateRecordRegionBytes = 0x0d20u;
    static constexpr std::uint32_t kAudioOutputModeAddress = 0x0015edecu;
    static constexpr std::uint32_t kAudioVolumeAddress = 0x0015edf0u;

    // FUN_00215390 is the game-side manager initialization reached inside the
    // same wrapper. Its direct state writes are preserved; its calls back into
    // 989snd are not.
    static constexpr std::uint32_t kGameSoundManagerBase = 0x001516d0u;

    // Highest guest byte read by the proved wrapper state transition.
    static constexpr std::uint32_t kRequiredGuestBytes = kAudioVolumeAddress + 4u;
};

// Reproduces only the direct game-visible state writes from sub_0022C8D0 and
// FUN_00215390. It does not synthesize 989snd RPC state, SPU state, audio banks,
// or sound output. All ranges are validated before the first write.
[[nodiscard]] bool applyRac1NativeAudioBootstrapState(
    std::span<std::uint8_t> guestMemory);

// Installs the game-level Phase-11 audio boundaries. Actual audio playback,
// bank loading and streaming remain explicit Phase-18 NativeAudio work.
void declareNativeAudioBootstrapReplacements(
    runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
