#include "runtime/native_replacements.h"

#include <iostream>

#include "ps2_runtime.h"

namespace ratchet::runtime {

NativeReplacementInstallSummary NativeReplacementRegistry::install(
    PS2Runtime& fallbackRuntime,
    NativeReplacementStage stage) const {
    NativeReplacementInstallSummary summary;
    summary.declared = size(stage);

    for (const NativeReplacement& entry : entries_) {
        if (entry.stage != stage) {
            continue;
        }

        if (entry.fallbackStorage != nullptr) {
            *entry.fallbackStorage = fallbackRuntime.lookupFunction(entry.address);
        }

        // replaceFunction is also valid for the existing interior guest PCs:
        // PS2Recomp dispatches from a dense PC-indexed table, so this preserves
        // the exact old registerFunction behavior without using its deprecated
        // compatibility alias.
        if (fallbackRuntime.replaceFunction(entry.address, entry.function)) {
            ++summary.installed;
        } else {
            ++summary.failed;
            std::cerr << "[OpenRatchet:native] failed to install replacement name="
                      << entry.name << " address=0x" << std::hex << entry.address
                      << std::dec << '\n';
        }
    }

    return summary;
}

} // namespace ratchet::runtime
