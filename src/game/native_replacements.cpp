#include "game/native_replacements.h"

#include "game/native_assets.h"
#include "game/native_audio_bootstrap.h"
#include "game/native_io.h"
#include "game/native_platform_bootstrap.h"
#include "game/rac1_native_input.h"
#include "game/rac1_overlay_aot_dispatch.h"
#include "game/rac1_native_stash.h"
#include "guest_overrides.h"

namespace ratchet::game {

void declareNativeReplacements(runtime::NativeReplacementRegistry& registry) {
    // Native game/platform-facing implementations are declared before the
    // shrinking legacy compatibility layer. Each address may have only one
    // owner per stage, so migration mistakes fail at declaration time.
    declareNativeIoReplacements(registry);
    declareNativeAssetReplacements(registry);
    declareNativePlatformBootstrapReplacements(registry);
    declareRac1NativeStashReplacements(registry);
    declareRac1NativeInputReplacements(registry);
    declareRac1OverlayAotReplacements(registry);
    declareNativeAudioBootstrapReplacements(registry);
    declareLegacyGuestCompatibilityReplacements(registry);
}

} // namespace ratchet::game
