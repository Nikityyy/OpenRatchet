#include "game/native_replacements.h"

#include "guest_overrides.h"

namespace ratchet::game {

void declareNativeReplacements(runtime::NativeReplacementRegistry& registry) {
    declareLegacyGuestCompatibilityReplacements(registry);
}

} // namespace ratchet::game
