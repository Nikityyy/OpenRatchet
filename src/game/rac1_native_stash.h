#pragma once

#include <cstddef>
#include <cstdint>

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Retail's EE-side stash API is a game/resource staging boundary above SIF:
//   0x232D00 initializes the remote stash service,
//   0x232E40 stores one 16-byte-unit range and returns a slot id,
//   0x232F20 reads a slot range back into EE memory, and
//   0x233038 returns the slot length in 16-byte units.
// The native PC backend preserves that game-visible contract while keeping the
// staged bytes in host memory. No SID 0x11 service or synthetic IOP address is
// manufactured.
struct Rac1NativeStashContract {
    static constexpr std::uint32_t kInitializeFunction = 0x00232d00u;
    static constexpr std::uint32_t kStoreFunction = 0x00232e40u;
    static constexpr std::uint32_t kReadFunction = 0x00232f20u;
    static constexpr std::uint32_t kSizeFunction = 0x00233038u;

    static constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
    static constexpr std::size_t kSlotCount = 64u;
    static constexpr std::size_t kUnitBytes = 16u;

    static constexpr std::int32_t kStoreOutOfSpace = -1;
    static constexpr std::int32_t kStoreNoSlot = -2;
    static constexpr std::int32_t kInvalidSlot = -3;
};

void declareRac1NativeStashReplacements(
    runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
