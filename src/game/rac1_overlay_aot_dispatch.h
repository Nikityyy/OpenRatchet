#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class PS2Runtime;
struct R5900Context;

namespace ratchet::runtime {
class NativeReplacementRegistry;
}

namespace ratchet::game {

using Rac1OverlayGuestFunction = void (*)(std::uint8_t*, R5900Context*, PS2Runtime*);

struct Rac1OverlayFunctionEntry {
    std::uint32_t pc = 0u;
    Rac1OverlayGuestFunction function = nullptr;
};

// Exact guest-RDRAM interval overwritten by one Retail overlay record.  These
// ranges deliberately include both executable and non-executable records: if a
// later generation writes data over an address that used to contain code, the
// stale dispatch owner at that numerical PC must disappear as well.
struct Rac1OverlayMaterializedRange {
    std::uint32_t begin = 0u;
    std::uint32_t end = 0u;
};

struct Rac1OverlayDispatchTableView {
    std::uint32_t base = 0u;
    std::uint32_t end = 0u;
    std::size_t slotCount = 0u;
    Rac1OverlayGuestFunction* slots = nullptr;
};

enum class Rac1OverlayActivationStatus : std::uint8_t {
    Activated,
    AlreadyActive,
    InvalidTable,
    InvalidGeneration,
    GenerationEntryMissing,
    InvalidEntry,
    DuplicatePc,
    InvalidMaterializedRange,
    OverlappingMaterializedRange,
    EntryOutsideMaterializedRange,
};

struct Rac1OverlayActivationSummary {
    Rac1OverlayActivationStatus status = Rac1OverlayActivationStatus::InvalidTable;
    std::uint32_t generationEntry = 0u;
    std::size_t functionCount = 0u;
    std::size_t materializedRangeCount = 0u;
    std::size_t materializedSlotCount = 0u;
    std::size_t clearedOwnerCount = 0u;
    std::size_t baselineCapturedCount = 0u;
    std::size_t installedCount = 0u;
};

// Mirrors Retail's in-place overlay materializer rather than treating a code
// generation as a globally exclusive module.  A new generation owns exactly
// the addresses its record group overwrites.  Slots outside those ranges keep
// the owner matching the bytes still resident in RDRAM; overwritten addresses
// are cleared first and then populated only with callable AOT entries proved
// for the newly materialized generation.
class Rac1OverlayDispatchManager final {
public:
    Rac1OverlayActivationSummary activate(
        const Rac1OverlayDispatchTableView& table,
        const Rac1OverlayFunctionEntry* entries,
        std::size_t entryCount,
        const Rac1OverlayMaterializedRange* materializedRanges,
        std::size_t materializedRangeCount,
        std::uint32_t generationEntry);

    // Test/shutdown reset only. Runtime generation switches must go through
    // activate(), because Retail does not restore the previous overlay before
    // copying the next record group.
    std::size_t deactivate(const Rac1OverlayDispatchTableView& table);

    [[nodiscard]] bool active() const noexcept { return activeGenerationEntry_ != 0u; }
    [[nodiscard]] std::uint32_t activeGenerationEntry() const noexcept {
        return activeGenerationEntry_;
    }
    [[nodiscard]] std::size_t activeFunctionCount() const noexcept {
        return activeFunctionCount_;
    }
    [[nodiscard]] std::size_t touchedSlotCount() const noexcept {
        return savedBaselineSlots_.size();
    }

private:
    struct SavedSlot {
        std::size_t index = 0u;
        Rac1OverlayGuestFunction baseline = nullptr;
    };

    bool tableMatchesBoundView(const Rac1OverlayDispatchTableView& table) const noexcept;
    void clearBoundView() noexcept;

    std::vector<SavedSlot> savedBaselineSlots_;
    std::vector<std::uint8_t> baselineCaptured_;
    std::vector<Rac1OverlayMaterializedRange> activeRanges_;
    Rac1OverlayGuestFunction* boundSlots_ = nullptr;
    std::uint32_t boundBase_ = 0u;
    std::uint32_t boundEnd_ = 0u;
    std::size_t boundSlotCount_ = 0u;
    std::uint32_t activeGenerationEntry_ = 0u;
    std::size_t activeFunctionCount_ = 0u;
};

struct Rac1OverlayAotRuntimeState {
    std::uint64_t materializerCalls = 0u;
    std::uint64_t successfulActivations = 0u;
    std::uint32_t lastMaterializerGenerationEntry = 0u;
    std::uint32_t activeGenerationEntry = 0u;
    std::size_t activeFunctionCount = 0u;
    std::size_t touchedSlotCount = 0u;

    bool operator==(const Rac1OverlayAotRuntimeState&) const = default;
};

const char* rac1OverlayActivationStatusName(Rac1OverlayActivationStatus status) noexcept;

// Read-only diagnostic state for proving whether Retail has actually reached
// the stable sub_0012D8F8 generation boundary. No activation is inferred from
// asset reads or renderer state.
Rac1OverlayAotRuntimeState inspectRac1OverlayAotRuntimeState() noexcept;

// Installs the exact Retail generation-boundary wrapper around sub_0012D8F8.
// The generated boot implementation still owns overlay record interpretation and
// byte copies; OpenRatchet changes only which statically recompiled code
// generation is dispatchable after Retail returns its generation entry.
void declareRac1OverlayAotReplacements(runtime::NativeReplacementRegistry& registry);

} // namespace ratchet::game
