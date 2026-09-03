#pragma once

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

// Single declaration point for PC-native implementations of original Ratchet
// game functions. During the migration this also exposes the legacy root-owned
// compatibility wrappers through the same explicit replacement boundary.
void declareNativeReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
