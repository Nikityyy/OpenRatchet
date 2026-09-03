#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class PS2Runtime;
struct R5900Context;

namespace ratchet::runtime {

// OpenRatchet owns this boundary. PS2Recomp is only the fallback implementation
// for EE functions that have not been replaced with native PC-side semantics yet.
using GuestFunction = void (*)(std::uint8_t*, R5900Context*, PS2Runtime*);

enum class NativeReplacementStage : std::uint8_t {
    Bootstrap,
    Runtime,
};

struct NativeReplacement {
    std::uint32_t address = 0u;
    const char* name = nullptr;
    NativeReplacementStage stage = NativeReplacementStage::Runtime;
    GuestFunction function = nullptr;

    // Optional storage for the PS2Recomp implementation that this replacement
    // wraps. Capturing the fallback here keeps that dependency explicit and
    // lets clean native replacements omit it entirely.
    GuestFunction* fallbackStorage = nullptr;
};

struct NativeReplacementInstallSummary {
    std::size_t declared = 0u;
    std::size_t installed = 0u;
    std::size_t failed = 0u;
};

class NativeReplacementRegistry final {
public:
    // Returns false for an invalid entry or a duplicate address within the
    // same installation stage. Cross-stage reuse remains legal because a
    // bootstrap bridge may deliberately be superseded after the ELF loads.
    bool add(std::uint32_t address,
             const char* name,
             NativeReplacementStage stage,
             GuestFunction function,
             GuestFunction* fallbackStorage = nullptr) {
        if (address == 0u || name == nullptr || name[0] == '\0' || function == nullptr) {
            return false;
        }

        const auto duplicate = std::find_if(
            entries_.begin(), entries_.end(),
            [address, stage](const NativeReplacement& entry) {
                return entry.address == address && entry.stage == stage;
            });
        if (duplicate != entries_.end()) {
            return false;
        }

        entries_.push_back({address, name, stage, function, fallbackStorage});
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return entries_.size();
    }

    [[nodiscard]] std::size_t size(NativeReplacementStage stage) const noexcept {
        return static_cast<std::size_t>(std::count_if(
            entries_.begin(), entries_.end(),
            [stage](const NativeReplacement& entry) { return entry.stage == stage; }));
    }

    [[nodiscard]] const std::vector<NativeReplacement>& entries() const noexcept {
        return entries_;
    }

    // Installs one stage into the temporary PS2Recomp EE fallback backend.
    // PS2Recomp's replacement API writes the generated dense dispatch slot;
    // OpenRatchet owns when and why that replacement happens.
    NativeReplacementInstallSummary install(PS2Runtime& fallbackRuntime,
                                              NativeReplacementStage stage) const;

private:
    std::vector<NativeReplacement> entries_;
};

} // namespace ratchet::runtime
