#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Native replacements for game-facing storage APIs. The first migrated entry
// is the synchronous sector reader at 0x12f208: indexed extracted resources are
// read directly from the host VFS, while unknown disc ranges temporarily fall
// back to the recompiled implementation.
void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
