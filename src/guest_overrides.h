#pragma once

class PS2Runtime;

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet {

// Temporary compatibility layer retained while OpenRatchet migrates PS2
// platform behavior to native PC subsystems. Address-based wrappers are
// declared through the project-owned replacement registry instead of being
// installed directly from main().
void declareLegacyGuestCompatibilityReplacements(
    runtime::NativeReplacementRegistry& registry);

// Non-function legacy bridges (currently graphics diagnostics/presentation)
// still need direct access to the PS2Recomp fallback backend. These are kept
// explicit so later phases can remove them subsystem-by-subsystem.
void installLegacyGuestDeviceBridges(PS2Runtime& fallbackRuntime);

} // namespace ratchet
