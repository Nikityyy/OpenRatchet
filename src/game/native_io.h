#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Native replacements for game-facing storage APIs. OpenRatchet now owns the
// synchronous sector reader at 0x12f208, the 989snd-backed game sector start
// helper at 0x216788, its synchronous wrapper at 0x216828, and the disc-TOC
// loader at 0x12f2b8. Indexed assets and their metadata are sourced from the
// extracted host VFS; only the lower 0x12f208 raw-disc reader retains a
// generated fallback for unresolved ranges.
void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
