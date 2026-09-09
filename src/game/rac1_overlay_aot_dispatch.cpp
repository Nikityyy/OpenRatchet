#include "game/rac1_overlay_aot_dispatch.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ratchet::game {
namespace {

bool validDispatchTable(const Rac1OverlayDispatchTableView& table) noexcept {
    return table.slots != nullptr && table.base < table.end &&
           (table.base & 3u) == 0u && (table.end & 3u) == 0u &&
           table.slotCount == static_cast<std::size_t>((table.end - table.base) >> 2u);
}

bool pcInsideRanges(std::uint32_t pc,
                    const Rac1OverlayMaterializedRange* ranges,
                    std::size_t rangeCount) noexcept {
    for (std::size_t i = 0u; i < rangeCount; ++i) {
        if (pc >= ranges[i].begin && pc < ranges[i].end) return true;
    }
    return false;
}

} // namespace

const char* rac1OverlayActivationStatusName(Rac1OverlayActivationStatus status) noexcept {
    switch (status) {
    case Rac1OverlayActivationStatus::Activated:
        return "activated";
    case Rac1OverlayActivationStatus::AlreadyActive:
        return "already-active";
    case Rac1OverlayActivationStatus::InvalidTable:
        return "invalid-table";
    case Rac1OverlayActivationStatus::InvalidGeneration:
        return "invalid-generation";
    case Rac1OverlayActivationStatus::GenerationEntryMissing:
        return "generation-entry-missing";
    case Rac1OverlayActivationStatus::InvalidEntry:
        return "invalid-entry";
    case Rac1OverlayActivationStatus::DuplicatePc:
        return "duplicate-pc";
    case Rac1OverlayActivationStatus::InvalidMaterializedRange:
        return "invalid-materialized-range";
    case Rac1OverlayActivationStatus::OverlappingMaterializedRange:
        return "overlapping-materialized-range";
    case Rac1OverlayActivationStatus::EntryOutsideMaterializedRange:
        return "entry-outside-materialized-range";
    }
    return "unknown";
}

bool Rac1OverlayDispatchManager::tableMatchesBoundView(
    const Rac1OverlayDispatchTableView& table) const noexcept {
    return boundSlots_ == table.slots && boundBase_ == table.base &&
           boundEnd_ == table.end && boundSlotCount_ == table.slotCount;
}

void Rac1OverlayDispatchManager::clearBoundView() noexcept {
    baselineCaptured_.clear();
    activeRanges_.clear();
    boundSlots_ = nullptr;
    boundBase_ = 0u;
    boundEnd_ = 0u;
    boundSlotCount_ = 0u;
    activeGenerationEntry_ = 0u;
    activeFunctionCount_ = 0u;
}

Rac1OverlayActivationSummary Rac1OverlayDispatchManager::activate(
    const Rac1OverlayDispatchTableView& table,
    const Rac1OverlayFunctionEntry* entries,
    std::size_t entryCount,
    const Rac1OverlayMaterializedRange* materializedRanges,
    std::size_t materializedRangeCount,
    std::uint32_t generationEntry) {
    Rac1OverlayActivationSummary summary{};
    summary.generationEntry = generationEntry;
    summary.functionCount = entryCount;
    summary.materializedRangeCount = materializedRangeCount;

    if (!validDispatchTable(table) ||
        (boundSlots_ != nullptr && !tableMatchesBoundView(table))) {
        summary.status = Rac1OverlayActivationStatus::InvalidTable;
        return summary;
    }
    if (generationEntry == 0u || (generationEntry & 3u) != 0u) {
        summary.status = Rac1OverlayActivationStatus::InvalidGeneration;
        return summary;
    }
    if (entries == nullptr || entryCount == 0u) {
        summary.status = Rac1OverlayActivationStatus::InvalidEntry;
        return summary;
    }
    if (materializedRanges == nullptr || materializedRangeCount == 0u) {
        summary.status = Rac1OverlayActivationStatus::InvalidMaterializedRange;
        return summary;
    }

    // Validate every Retail record range before touching the dense dispatch
    // table.  Record order is not semantically relevant, so overlap detection
    // is pairwise rather than relying on an ordering assumption.
    for (std::size_t i = 0u; i < materializedRangeCount; ++i) {
        const Rac1OverlayMaterializedRange& range = materializedRanges[i];
        if (range.begin >= range.end || (range.begin & 3u) != 0u ||
            (range.end & 3u) != 0u || range.begin < table.base || range.end > table.end) {
            summary.status = Rac1OverlayActivationStatus::InvalidMaterializedRange;
            return summary;
        }
        summary.materializedSlotCount +=
            static_cast<std::size_t>((range.end - range.begin) >> 2u);
        for (std::size_t j = 0u; j < i; ++j) {
            const Rac1OverlayMaterializedRange& prior = materializedRanges[j];
            if (range.begin < prior.end && prior.begin < range.end) {
                summary.status = Rac1OverlayActivationStatus::OverlappingMaterializedRange;
                return summary;
            }
        }
    }

    std::unordered_set<std::uint32_t> seen;
    seen.reserve(entryCount * 2u);
    std::unordered_map<std::size_t, Rac1OverlayGuestFunction> expectedBySlot;
    expectedBySlot.reserve(entryCount * 2u);
    bool generationEntryPresent = false;
    for (std::size_t i = 0u; i < entryCount; ++i) {
        const Rac1OverlayFunctionEntry& entry = entries[i];
        if (entry.function == nullptr || (entry.pc & 3u) != 0u ||
            entry.pc < table.base || entry.pc >= table.end) {
            summary.status = Rac1OverlayActivationStatus::InvalidEntry;
            return summary;
        }
        if (!seen.insert(entry.pc).second) {
            summary.status = Rac1OverlayActivationStatus::DuplicatePc;
            return summary;
        }
        if (!pcInsideRanges(entry.pc, materializedRanges, materializedRangeCount)) {
            summary.status = Rac1OverlayActivationStatus::EntryOutsideMaterializedRange;
            return summary;
        }
        const std::size_t slot = static_cast<std::size_t>((entry.pc - table.base) >> 2u);
        expectedBySlot.emplace(slot, entry.function);
        generationEntryPresent = generationEntryPresent || entry.pc == generationEntry;
    }
    if (!generationEntryPresent) {
        summary.status = Rac1OverlayActivationStatus::GenerationEntryMissing;
        return summary;
    }

    if (activeGenerationEntry_ == generationEntry &&
        activeFunctionCount_ == entryCount &&
        activeRanges_.size() == materializedRangeCount) {
        bool sameRanges = true;
        for (std::size_t i = 0u; sameRanges && i < materializedRangeCount; ++i) {
            sameRanges = activeRanges_[i].begin == materializedRanges[i].begin &&
                         activeRanges_[i].end == materializedRanges[i].end;
        }
        bool intact = sameRanges;
        for (std::size_t i = 0u; intact && i < materializedRangeCount; ++i) {
            const std::size_t first =
                static_cast<std::size_t>((materializedRanges[i].begin - table.base) >> 2u);
            const std::size_t last =
                static_cast<std::size_t>((materializedRanges[i].end - table.base) >> 2u);
            for (std::size_t slot = first; intact && slot < last; ++slot) {
                const auto expected = expectedBySlot.find(slot);
                const Rac1OverlayGuestFunction function =
                    expected == expectedBySlot.end() ? nullptr : expected->second;
                intact = table.slots[slot] == function;
            }
        }
        if (intact) {
            summary.status = Rac1OverlayActivationStatus::AlreadyActive;
            summary.installedCount = entryCount;
            return summary;
        }
    }

    // Allocate all bookkeeping before the first dispatch-table write.  The
    // manager stores the pre-overlay owner only on the first materializer touch
    // of a slot; later generations may overwrite that same address many times,
    // but deactivate() still returns to the exact original boot/HLE owner.
    if (boundSlots_ == nullptr) {
        baselineCaptured_.assign(table.slotCount, 0u);
        boundSlots_ = table.slots;
        boundBase_ = table.base;
        boundEnd_ = table.end;
        boundSlotCount_ = table.slotCount;
    }
    savedBaselineSlots_.reserve(savedBaselineSlots_.size() + summary.materializedSlotCount);
    activeRanges_.assign(materializedRanges, materializedRanges + materializedRangeCount);

    for (std::size_t i = 0u; i < materializedRangeCount; ++i) {
        const std::size_t first =
            static_cast<std::size_t>((materializedRanges[i].begin - table.base) >> 2u);
        const std::size_t last =
            static_cast<std::size_t>((materializedRanges[i].end - table.base) >> 2u);
        for (std::size_t slot = first; slot < last; ++slot) {
            if (baselineCaptured_[slot] == 0u) {
                savedBaselineSlots_.push_back({slot, table.slots[slot]});
                baselineCaptured_[slot] = 1u;
                ++summary.baselineCapturedCount;
            }
            if (table.slots[slot] != nullptr) ++summary.clearedOwnerCount;
            table.slots[slot] = nullptr;
        }
    }

    // Every function and range was validated before the first write.  The
    // callable entries now become the only dispatch owners inside bytes that
    // Retail just materialized.
    for (std::size_t i = 0u; i < entryCount; ++i) {
        const Rac1OverlayFunctionEntry& entry = entries[i];
        const std::size_t slot = static_cast<std::size_t>((entry.pc - table.base) >> 2u);
        table.slots[slot] = entry.function;
    }

    activeGenerationEntry_ = generationEntry;
    activeFunctionCount_ = entryCount;
    summary.installedCount = entryCount;
    summary.status = Rac1OverlayActivationStatus::Activated;
    return summary;
}

std::size_t Rac1OverlayDispatchManager::deactivate(const Rac1OverlayDispatchTableView& table) {
    if (savedBaselineSlots_.empty()) {
        clearBoundView();
        return 0u;
    }
    if (!validDispatchTable(table) || !tableMatchesBoundView(table)) {
        return 0u;
    }

    std::size_t restored = 0u;
    for (const SavedSlot& saved : savedBaselineSlots_) {
        if (saved.index >= table.slotCount) continue;
        table.slots[saved.index] = saved.baseline;
        ++restored;
    }
    savedBaselineSlots_.clear();
    clearBoundView();
    return restored;
}

} // namespace ratchet::game
