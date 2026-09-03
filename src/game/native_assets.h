#pragma once

#include <cstdint>

namespace ratchet::game {

// Phase-3 migration gate. The native decoder executes against the authentic
// guest input after the legacy decompressor returns, but it does not mutate
// guest state. Correctness is anchored to an independently established boot-WAD
// oracle; the legacy SPR/DMAC bridge is retained only as a diagnostic comparison
// because it is known to overrun the authentic decompressed result.
void validateNativeWadDecompressorShadow(std::uint8_t* rdram,
                                         std::uint32_t inputAddress,
                                         std::uint32_t outputAddress,
                                         std::uint32_t legacyBytes);

} // namespace ratchet::game
