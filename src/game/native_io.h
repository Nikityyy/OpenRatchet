#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Native replacements for game-facing storage APIs. OpenRatchet owns the
// asynchronous level/resource submission boundary at 0x216728, the 989snd-
// backed game sector start helper at 0x216788, its synchronous wrapper at
// 0x216828, the lower synchronous sector reader at 0x12f208, and the disc-TOC
// loader at 0x12f2b8. Indexed assets and validated native-level sector spans
// are sourced from the extracted host VFS. 0x216728 and 0x12f208 retain an
// explicit generated fallback only for unresolved/unindexed ranges; proved
// Level-0 ranges are required to stay on NativeVfs.
void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
