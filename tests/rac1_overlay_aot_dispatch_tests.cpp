#include "game/rac1_overlay_aot_dispatch.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void fnBootA(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void fnBootB(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void fnOverlayEntry(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void fnOverlayInterior(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void fnOtherGeneration(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void fnNativeOverride(std::uint8_t*, R5900Context*, PS2Runtime*) {}

ratchet::game::Rac1OverlayDispatchTableView makeTable(
    std::vector<ratchet::game::Rac1OverlayGuestFunction>& slots,
    std::uint32_t base = 0x1000u) {
    return {
        base,
        base + static_cast<std::uint32_t>(slots.size() * 4u),
        slots.size(),
        slots.data(),
    };
}

void testActivationOwnsWholeMaterializedRangeAndRestore() {
    using namespace ratchet::game;
    std::vector<Rac1OverlayGuestFunction> slots(16u, nullptr);
    slots[2] = fnBootA;          // 0x1008
    slots[4] = fnBootB;          // 0x1010
    slots[5] = fnNativeOverride; // 0x1014: overwritten data, not callable in overlay
    const auto table = makeTable(slots);
    const Rac1OverlayFunctionEntry entries[] = {
        {0x1008u, fnOverlayEntry},
        {0x1010u, fnOverlayInterior},
    };
    const Rac1OverlayMaterializedRange ranges[] = {
        {0x1008u, 0x1018u},
    };

    Rac1OverlayDispatchManager manager;
    const auto activated = manager.activate(table, entries, 2u, ranges, 1u, 0x1008u);
    expect(activated.status == Rac1OverlayActivationStatus::Activated,
           "valid generation must activate");
    expect(activated.materializedRangeCount == 1u && activated.materializedSlotCount == 4u,
           "activation must account for the exact materialized interval");
    expect(activated.baselineCapturedCount == 4u,
           "first materialization must capture every touched baseline slot");
    expect(activated.clearedOwnerCount == 3u,
           "activation must count every stale non-null owner cleared by Retail bytes");
    expect(activated.installedCount == 2u,
           "activation must install every proved callable entry");
    expect(slots[2] == fnOverlayEntry && slots[4] == fnOverlayInterior,
           "overlay functions must own proved callable slots while resident");
    expect(slots[3] == nullptr && slots[5] == nullptr,
           "materialized bytes without proved callable entries must have no dispatch owner");
    expect(manager.activeGenerationEntry() == 0x1008u && manager.activeFunctionCount() == 2u,
           "manager must record the latest Retail generation exactly");

    const auto second = manager.activate(table, entries, 2u, ranges, 1u, 0x1008u);
    expect(second.status == Rac1OverlayActivationStatus::AlreadyActive,
           "identical intact materialization must be idempotent");

    const std::size_t restored = manager.deactivate(table);
    expect(restored == 4u, "deactivation must restore every address ever touched by an overlay");
    expect(slots[2] == fnBootA && slots[4] == fnBootB && slots[5] == fnNativeOverride,
           "deactivation must restore exact pre-overlay boot/native owners");
    expect(slots[3] == nullptr && !manager.active(),
           "deactivation must restore null baselines and clear manager state");
}

void testGenerationSwitchLayersOnlyNewlyMaterializedRanges() {
    using namespace ratchet::game;
    std::vector<Rac1OverlayGuestFunction> slots(16u, nullptr);
    slots[2] = fnBootA;          // 0x1008
    slots[4] = fnBootB;          // 0x1010
    slots[5] = fnNativeOverride; // 0x1014
    const auto table = makeTable(slots);
    const Rac1OverlayFunctionEntry first[] = {
        {0x1008u, fnOverlayEntry},
        {0x1010u, fnOverlayInterior},
    };
    const Rac1OverlayMaterializedRange firstRanges[] = {
        {0x1008u, 0x1018u},
    };
    const Rac1OverlayFunctionEntry second[] = {
        {0x1014u, fnOtherGeneration},
    };
    const Rac1OverlayMaterializedRange secondRanges[] = {
        {0x1010u, 0x1018u},
    };

    Rac1OverlayDispatchManager manager;
    (void)manager.activate(table, first, 2u, firstRanges, 1u, 0x1008u);
    const auto switched = manager.activate(table, second, 1u, secondRanges, 1u, 0x1014u);
    expect(switched.status == Rac1OverlayActivationStatus::Activated,
           "later Retail materialization must activate");
    expect(switched.baselineCapturedCount == 0u,
           "overlapping later ranges must retain the original baseline snapshot");
    expect(slots[2] == fnOverlayEntry,
           "older-generation code outside the later materialized ranges must remain resident");
    expect(slots[4] == nullptr,
           "later data/bytes must clear stale callable owners inside overwritten ranges");
    expect(slots[5] == fnOtherGeneration,
           "later generation must own its proved callable slot");

    const std::size_t restored = manager.deactivate(table);
    expect(restored == 4u,
           "full reset must restore the union of addresses touched across the generation chain");
    expect(slots[2] == fnBootA && slots[4] == fnBootB && slots[5] == fnNativeOverride,
           "full reset must return directly to pre-overlay owners, not an intermediate generation");
}

void testRetailVeldinRangeLayeringWitness() {
    using namespace ratchet::game;
    constexpr std::uint32_t base = 0x0015EF00u;
    constexpr std::uint32_t end = 0x002F0CD0u;
    std::vector<Rac1OverlayGuestFunction> slots((end - base) / 4u, nullptr);
    const auto table = makeTable(slots, base);
    auto slotFor = [&](std::uint32_t pc) -> Rac1OverlayGuestFunction& {
        return slots[(pc - base) / 4u];
    };

    // Concrete proved Veldin witnesses.  WAD158 has callable code at both PCs.
    // Level 0 later writes DATA over 0x1E9128 but leaves 0x1EA830 in a record gap.
    slotFor(0x001E9128u) = fnBootA;
    slotFor(0x001EA830u) = fnBootB;
    const Rac1OverlayFunctionEntry wadFunctions[] = {
        {0x001E9128u, fnOverlayInterior},
        {0x001E9658u, fnOverlayEntry},
        {0x001EA830u, fnNativeOverride},
    };
    const Rac1OverlayMaterializedRange wadRanges[] = {
        {0x0015EF00u, 0x00161228u},
        {0x00161280u, 0x00165430u},
        {0x00165480u, 0x001E8B78u},
        {0x001E8B80u, 0x001E8B8Cu},
        {0x001E8C00u, 0x001E8C14u},
        {0x001E8C80u, 0x001E8C88u},
        {0x001E8D00u, 0x0023CD18u},
    };
    const Rac1OverlayFunctionEntry levelFunctions[] = {
        {0x00245C28u, fnOtherGeneration},
    };
    const Rac1OverlayMaterializedRange levelRanges[] = {
        {0x0015EF00u, 0x00161DF0u},
        {0x00161E00u, 0x00165FB0u},
        {0x00166000u, 0x001EA2B0u},
        {0x001EA300u, 0x001EA804u},
        {0x001EA880u, 0x001EA920u},
        {0x001EA980u, 0x001EA998u},
        {0x001EAA00u, 0x002F0CD0u},
    };

    Rac1OverlayDispatchManager manager;
    const auto wad = manager.activate(
        table, wadFunctions, std::size(wadFunctions),
        wadRanges, std::size(wadRanges), 0x001E9658u);
    expect(wad.status == Rac1OverlayActivationStatus::Activated,
           "proved WAD158 materialized ranges must activate");
    expect(slotFor(0x001E9128u) == fnOverlayInterior &&
           slotFor(0x001EA830u) == fnNativeOverride,
           "WAD158 must own its proved callable witnesses");

    const auto level = manager.activate(
        table, levelFunctions, std::size(levelFunctions),
        levelRanges, std::size(levelRanges), 0x00245C28u);
    expect(level.status == Rac1OverlayActivationStatus::Activated,
           "proved Level-0 materialized ranges must activate");
    expect(slotFor(0x001E9128u) == nullptr,
           "Level-0 data materialization must clear stale WAD code at 0x1E9128");
    expect(slotFor(0x001EA830u) == fnNativeOverride,
           "WAD code in the proved Level-0 record gap at 0x1EA830 must remain resident");
    expect(slotFor(0x00245C28u) == fnOtherGeneration,
           "Level-0 generation entry must become dispatchable");
}

void testSameGenerationRepairsCoveredNonFunctionSlots() {
    using namespace ratchet::game;
    std::vector<Rac1OverlayGuestFunction> slots(8u, nullptr);
    slots[1] = fnBootA;
    const auto table = makeTable(slots);
    const Rac1OverlayFunctionEntry entries[] = {{0x1004u, fnOverlayEntry}};
    const Rac1OverlayMaterializedRange ranges[] = {{0x1004u, 0x100Cu}};

    Rac1OverlayDispatchManager manager;
    (void)manager.activate(table, entries, 1u, ranges, 1u, 0x1004u);
    slots[2] = fnNativeOverride; // corrupt/stale owner inside resident bytes
    const auto repaired = manager.activate(table, entries, 1u, ranges, 1u, 0x1004u);
    expect(repaired.status == Rac1OverlayActivationStatus::Activated,
           "same generation with a stale covered slot must be re-applied, not called already-active");
    expect(slots[1] == fnOverlayEntry && slots[2] == nullptr,
           "re-application must restore exact dispatch ownership for the resident bytes");
}

void testValidationIsSideEffectFree() {
    using namespace ratchet::game;
    std::vector<Rac1OverlayGuestFunction> slots(16u, nullptr);
    slots[1] = fnBootA;
    const auto table = makeTable(slots);
    Rac1OverlayDispatchManager manager;
    const Rac1OverlayMaterializedRange validRange[] = {{0x1004u, 0x1010u}};

    const Rac1OverlayFunctionEntry duplicate[] = {
        {0x1004u, fnOverlayEntry},
        {0x1004u, fnOverlayInterior},
    };
    const auto duplicateResult = manager.activate(table, duplicate, 2u, validRange, 1u, 0x1004u);
    expect(duplicateResult.status == Rac1OverlayActivationStatus::DuplicatePc,
           "duplicate overlay PCs must fail closed");
    expect(slots[1] == fnBootA && !manager.active(),
           "failed validation must not mutate dispatch table");

    const Rac1OverlayFunctionEntry missingEntry[] = {{0x1004u, fnOverlayEntry}};
    const auto missing = manager.activate(table, missingEntry, 1u, validRange, 1u, 0x1008u);
    expect(missing.status == Rac1OverlayActivationStatus::GenerationEntryMissing,
           "generation entry must itself be dispatchable");
    expect(slots[1] == fnBootA, "missing-entry failure must be side-effect free");

    const Rac1OverlayFunctionEntry outsideMaterialization[] = {{0x1010u, fnOverlayEntry}};
    const auto outside = manager.activate(
        table, outsideMaterialization, 1u, validRange, 1u, 0x1010u);
    expect(outside.status == Rac1OverlayActivationStatus::EntryOutsideMaterializedRange,
           "callable entries outside Retail materialized bytes must fail closed");
    expect(slots[1] == fnBootA, "outside-materialization failure must be side-effect free");

    const Rac1OverlayFunctionEntry outOfTable[] = {{0x2000u, fnOverlayEntry}};
    const Rac1OverlayMaterializedRange outOfTableRange[] = {{0x2000u, 0x2004u}};
    const auto tableRange = manager.activate(
        table, outOfTable, 1u, outOfTableRange, 1u, 0x2000u);
    expect(tableRange.status == Rac1OverlayActivationStatus::InvalidMaterializedRange,
           "materialized ranges outside dense dispatch storage must fail before mutation");

    const Rac1OverlayMaterializedRange overlapping[] = {
        {0x1004u, 0x1010u},
        {0x100Cu, 0x1014u},
    };
    const auto overlap = manager.activate(
        table, missingEntry, 1u, overlapping, 2u, 0x1004u);
    expect(overlap.status == Rac1OverlayActivationStatus::OverlappingMaterializedRange,
           "overlapping Retail record ownership intervals must fail closed");
    expect(slots[1] == fnBootA && !manager.active(),
           "all validation failures must remain side-effect free");
}

void testOverlayOwnsSameNumericalPcInItsGeneration() {
    using namespace ratchet::game;
    std::vector<Rac1OverlayGuestFunction> slots(16u, nullptr);
    // The previous generation may have installed a native HLE at this numerical
    // PC. A later Retail overlay can reuse that address for unrelated code.
    slots[2] = fnNativeOverride;
    const auto table = makeTable(slots);
    const Rac1OverlayFunctionEntry entries[] = {{0x1008u, fnOverlayEntry}};
    const Rac1OverlayMaterializedRange ranges[] = {{0x1008u, 0x100Cu}};

    Rac1OverlayDispatchManager manager;
    const auto activated = manager.activate(table, entries, 1u, ranges, 1u, 0x1008u);
    expect(activated.status == Rac1OverlayActivationStatus::Activated,
           "overlay generation must activate over a prior-generation native owner");
    expect(slots[2] == fnOverlayEntry,
           "resident generation must own a reused numerical PC");

    const std::size_t restored = manager.deactivate(table);
    expect(restored == 1u && slots[2] == fnNativeOverride,
           "full reset must restore the exact pre-overlay native owner");
}

} // namespace

int main() {
    testActivationOwnsWholeMaterializedRangeAndRestore();
    testGenerationSwitchLayersOnlyNewlyMaterializedRanges();
    testRetailVeldinRangeLayeringWitness();
    testSameGenerationRepairsCoveredNonFunctionSlots();
    testValidationIsSideEffectFree();
    testOverlayOwnsSameNumericalPcInItsGeneration();
    if (g_failures != 0) {
        std::cerr << g_failures << " overlay AOT dispatch test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "rac1_overlay_aot_dispatch_tests: PASS\n";
    return EXIT_SUCCESS;
}
