#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Native game-semantic asset operations. The R&C1 WAD decompressor at 0x20b618
// is owned here and executes directly over guest RAM through the host decoder;
// it no longer performs or emulates SPR DMA/scratchpad transfers.
void declareNativeAssetReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
