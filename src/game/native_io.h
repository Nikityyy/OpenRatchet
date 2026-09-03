#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Native replacements for game-facing storage APIs. OpenRatchet now owns the
// synchronous sector reader at 0x12f208 and the disc-TOC loader at 0x12f2b8.
// Indexed assets and their metadata are sourced from the extracted host VFS;
// unresolved raw disc ranges alone retain a generated fallback.
void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
