#include "game/rac1_overlay_coherence.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void writeLe16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeLe32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(offset + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(offset + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::vector<std::uint8_t> makeStaticElf() {
    std::vector<std::uint8_t> bytes(0x140u, 0u);
    bytes[0] = 0x7fu;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 1u;
    bytes[5] = 1u;
    writeLe16(bytes, 16u, 2u);
    writeLe16(bytes, 18u, 8u);
    writeLe32(bytes, 28u, 0x34u);
    writeLe16(bytes, 40u, 0x34u);
    writeLe16(bytes, 42u, 0x20u);
    writeLe16(bytes, 44u, 1u);

    writeLe32(bytes, 0x34u + 0u, 1u);
    writeLe32(bytes, 0x34u + 4u, 0x100u);
    writeLe32(bytes, 0x34u + 8u, 0x1000u);
    writeLe32(bytes, 0x34u + 16u, 0x40u);
    writeLe32(bytes, 0x34u + 20u, 0x40u);
    writeLe32(bytes, 0x34u + 24u, 5u);
    for (std::size_t i = 0u; i < 0x40u; ++i) {
        bytes[0x100u + i] = static_cast<std::uint8_t>(0x40u + i);
    }
    return bytes;
}

} // namespace

int main() {
    using namespace ratchet;

    const auto staticElf = game::parseRac1StaticElfImage(makeStaticElf());
    if (!staticElf.ok() || staticElf.loadSegments.size() != 1u) {
        std::cerr << "rac1_overlay_coherence_tests: static ELF parse failed\n";
        return 1;
    }

    std::vector<std::uint8_t> overlay(0x20u, 0u);
    writeLe32(overlay, 0x00u, 0x1008u);
    writeLe32(overlay, 0x04u, 0x10u);
    writeLe32(overlay, 0x08u, 1u);
    for (std::size_t i = 0u; i < 0x10u; ++i) {
        overlay[0x10u + i] = static_cast<std::uint8_t>(0xa0u + i);
    }
    const auto parsed = assets::parseRac1LevelOverlay(overlay);
    if (!parsed.ok() || parsed.segments.size() != 1u) {
        std::cerr << "rac1_overlay_coherence_tests: overlay parse failed\n";
        return 1;
    }

    std::vector<std::uint8_t> guest(0x2000u, 0u);
    for (std::size_t i = 0u; i < 0x10u; ++i) guest[0x1008u + i] = overlay[0x10u + i];
    const std::vector<std::uint32_t> fallbackEntries{0x1008u};
    const auto conflict = game::inspectRac1OverlayCoherence(
        guest, overlay, parsed.segments, staticElf, fallbackEntries);
    if (!conflict.conflictProved() || conflict.segmentCount != 1u ||
        conflict.materializedSegments != 1u || conflict.payloadBytes != 0x10u ||
        conflict.registeredFallbackEntries != 1u ||
        conflict.comparableFallbackEntries != 1u ||
        conflict.conflictingFallbackEntries != 1u ||
        !conflict.hasFirstConflictPc || conflict.firstConflictPc != 0x1008u) {
        std::cerr << "rac1_overlay_coherence_tests: stale fallback conflict not proved\n";
        return 1;
    }

    guest[0x1008u] ^= 0xffu;
    const auto notMaterialized = game::inspectRac1OverlayCoherence(
        guest, overlay, parsed.segments, staticElf, fallbackEntries);
    if (notMaterialized.status != game::Rac1OverlayCoherenceStatus::OverlayNotMaterialized ||
        notMaterialized.materializedSegments != 0u ||
        notMaterialized.registeredFallbackEntries != 1u ||
        notMaterialized.comparableFallbackEntries != 1u ||
        notMaterialized.conflictingFallbackEntries != 1u ||
        !notMaterialized.hasFirstConflictPc || notMaterialized.firstConflictPc != 0x1008u) {
        std::cerr << "rac1_overlay_coherence_tests: materialization/static split misclassified\n";
        return 1;
    }

    // Restore the exact overlay before exercising statuses that occur after the
    // materialization gate.
    guest[0x1008u] ^= 0xffu;

    const auto noFallback = game::inspectRac1OverlayCoherence(
        guest, overlay, parsed.segments, staticElf, std::span<const std::uint32_t>{});
    if (noFallback.status != game::Rac1OverlayCoherenceStatus::NoRegisteredFallbackOverlap) {
        std::cerr << "rac1_overlay_coherence_tests: empty fallback set misclassified\n";
        return 1;
    }

    const game::Rac1StaticElfImage missingElf{};
    const auto missingStatic = game::inspectRac1OverlayCoherence(
        guest, overlay, parsed.segments, missingElf, fallbackEntries);
    if (missingStatic.status != game::Rac1OverlayCoherenceStatus::StaticElfUnavailable) {
        std::cerr << "rac1_overlay_coherence_tests: missing static ELF accepted\n";
        return 1;
    }

    std::vector<std::uint8_t> matchingOverlay = overlay;
    const auto staticBytes = makeStaticElf();
    for (std::size_t i = 0u; i < 0x10u; ++i) {
        matchingOverlay[0x10u + i] = staticBytes[0x108u + i];
        guest[0x1008u + i] = matchingOverlay[0x10u + i];
    }
    const auto matchingParsed = assets::parseRac1LevelOverlay(matchingOverlay);
    const auto noConflict = game::inspectRac1OverlayCoherence(
        guest, matchingOverlay, matchingParsed.segments, staticElf, fallbackEntries);
    if (noConflict.status != game::Rac1OverlayCoherenceStatus::NoStaticConflictObserved ||
        noConflict.comparableFallbackEntries != 1u ||
        noConflict.conflictingFallbackEntries != 0u) {
        std::cerr << "rac1_overlay_coherence_tests: identical static bytes reported as conflict\n";
        return 1;
    }

    std::vector<std::uint8_t> tinyGuest(0x1008u, 0u);
    const auto guestOutOfRange = game::inspectRac1OverlayCoherence(
        tinyGuest, overlay, parsed.segments, staticElf, fallbackEntries);
    if (guestOutOfRange.status != game::Rac1OverlayCoherenceStatus::GuestDestinationOutOfRange) {
        std::cerr << "rac1_overlay_coherence_tests: out-of-range guest destination accepted\n";
        return 1;
    }

    // Boot -> first-generation progress is observation-only. The shared 0x15EE4C
    // pointer has two source-proved lifetimes relevant here: sub_001EB798's
    // resource workspace first, then FUN_002043B0's 0x60-prefixed overlay stream.
    {
        std::vector<std::uint8_t> progressGuest(0x160000u, 0u);
        writeLe32(progressGuest, 0x13CB04u, 0x20u);
        writeLe32(progressGuest, 0x15F5B0u, 0u);
        writeLe32(progressGuest, 0x13CAE4u, 0u);
        writeLe32(progressGuest, 0x15ED84u, 0xFFFFFFFFu);
        writeLe32(progressGuest, 0x15ED88u, 7u);
        writeLe32(progressGuest, 0x15F600u, 0xFFFFFFFFu);
        writeLe32(progressGuest, 0x15F604u, 3u);
        writeLe32(progressGuest, 0x15F618u, 1u);
        writeLe32(progressGuest, 0x13D364u, 2u);
        writeLe32(progressGuest, 0x13D36Cu, 0xFFFFFFFFu);
        writeLe32(progressGuest, 0x15EE4Cu, 0x1000u);
        writeLe32(progressGuest, 0x1000u, 0x60u);
        writeLe32(progressGuest, 0x1060u + 0x00u, 0x0015EF00u);
        writeLe32(progressGuest, 0x1060u + 0x04u, 0x2328u);
        writeLe32(progressGuest, 0x1060u + 0x08u, 1u);
        writeLe32(progressGuest, 0x1060u + 0x0Cu, 0x001E9658u);
        const auto progress = game::inspectRac1BootOverlayProgress(progressGuest);
        if (progress.status != game::Rac1BootOverlayProgressStatus::Ok ||
            progress.processedPressedEdges != 0x20u || !progress.processedRightEdge ||
            progress.bootExitFlag != 0u || progress.state13CAE4 != 0u ||
            progress.state15ED84 != 0xFFFFFFFFu ||
            progress.state15ED88 != 7u || progress.state15F600 != 0xFFFFFFFFu ||
            progress.state15F604 != 3u ||
            progress.state15F618 != 1u || progress.state13D364 != 2u ||
            progress.state13D36C != 0xFFFFFFFFu || progress.branch2321A4Bypass ||
            progress.branch2321B8Wait || !progress.branch2321C4EnterSetup ||
            progress.sharedLoaderPointer != 0x1000u ||
            progress.sharedLoaderPrefixBytes != 0x60u ||
            !progress.firstGenerationStreamSignatureObserved ||
            !progress.wad158FirstRecordHeaderMatched || progress.firstRecordAddress != 0x1060u ||
            progress.firstDestination != 0x0015EF00u || progress.firstPayloadBytes != 0x2328u ||
            progress.firstField8 != 1u || progress.firstGenerationEntry != 0x001E9658u) {
            std::cerr << "rac1_overlay_coherence_tests: valid first-generation progress misdecoded\n";
            return 1;
        }

        // Exact branch predicates from FUN_00231FF0:
        // 0x2321A4 bgez 0x15F600, 0x2321B8 waits when signed D4 >= 3,
        // and 0x2321C4 enters the setup arm when signed DC < 0.
        writeLe32(progressGuest, 0x15F600u, 0u);
        writeLe32(progressGuest, 0x15F604u, 2u);
        writeLe32(progressGuest, 0x13D364u, 3u);
        writeLe32(progressGuest, 0x13D36Cu, 0u);
        writeLe32(progressGuest, 0x13CAE4u, 0x840u);
        const auto branchOpposites = game::inspectRac1BootOverlayProgress(progressGuest);
        if (!branchOpposites.branch2321A4Bypass || !branchOpposites.branch2321B8Wait ||
            branchOpposites.branch2321C4EnterSetup || branchOpposites.state13CAE4 != 0x840u ||
            branchOpposites.state15F604 != 2u) {
            std::cerr << "rac1_overlay_coherence_tests: FUN_00231FF0 branch predicates misdecoded\n";
            return 1;
        }

        writeLe32(progressGuest, 0x13CB04u, 0u);
        const auto bitClear = game::inspectRac1BootOverlayProgress(progressGuest);
        if (bitClear.status != game::Rac1BootOverlayProgressStatus::Ok ||
            bitClear.processedRightEdge) {
            std::cerr << "rac1_overlay_coherence_tests: processed Right edge misdecoded\n";
            return 1;
        }

        writeLe32(progressGuest, 0x15EE4Cu, 0u);
        const auto missingSharedPointer = game::inspectRac1BootOverlayProgress(progressGuest);
        if (missingSharedPointer.status !=
            game::Rac1BootOverlayProgressStatus::SharedLoaderPointerUnavailable) {
            std::cerr << "rac1_overlay_coherence_tests: missing shared loader pointer accepted\n";
            return 1;
        }
    }

    {
        // sub_002192A8's 0x2195C0..0x219600 loop walks 14 pointers at
        // [0x1D5BF4]+0x44 and JALRs object[0]. Prove the read-only sampler
        // follows that exact owner/table/object chain and fails closed on bad objects.
        std::vector<std::uint8_t> callbackGuest(0x200000u, 0u);
        writeLe32(callbackGuest, 0x15EE4Cu, 0x1000u);
        writeLe32(callbackGuest, 0x1000u, 0x30000009u);
        writeLe32(callbackGuest, 0x1D5BF4u, 0x180000u);
        writeLe32(callbackGuest, 0x180044u + 0u * 4u, 0x181000u);
        writeLe32(callbackGuest, 0x181000u, 0x0021E7C8u);
        writeLe32(callbackGuest, 0x180044u + 2u * 4u, 0x181100u);
        writeLe32(callbackGuest, 0x181100u, 0x002192A8u);
        writeLe32(callbackGuest, 0x180044u + 3u * 4u, 0x3000000u);
        writeLe32(callbackGuest, 0x180044u + 13u * 4u, 0x181200u);
        writeLe32(callbackGuest, 0x181200u, 0x0021E890u);
        // A fifteenth pointer is deliberately populated immediately after the
        // Retail loop's table. It must not be sampled: $s1 starts at 13 and the
        // bgez loop terminates after slots 0..13.
        writeLe32(callbackGuest, 0x180044u + 14u * 4u, 0x181300u);
        writeLe32(callbackGuest, 0x181300u, 0x00ABCDEFu);

        const auto callbacks = game::inspectRac1BootOverlayProgress(callbackGuest);
        if (callbacks.status !=
                game::Rac1BootOverlayProgressStatus::FirstGenerationStreamSignatureAbsent ||
            callbacks.callback2195OwnerPointer != 0x180000u ||
            callbacks.callback2195TablePointer != 0x180044u ||
            !callbacks.callback2195TableReadable ||
            callbacks.callback2195NonNullObjects != 4u ||
            callbacks.callback2195ReadableObjects != 3u ||
            callbacks.callback2195NonNullTargets != 3u ||
            callbacks.callback2195Targets[0] != 0x0021E7C8u ||
            callbacks.callback2195Targets[1] != 0u ||
            callbacks.callback2195Targets[2] != 0x002192A8u ||
            callbacks.callback2195Targets[3] != 0u ||
            callbacks.callback2195Targets[13] != 0x0021E890u ||
            !callbacks.callback21E7C8Present || callbacks.callback21E7C8Slot != 0u) {
            std::cerr << "rac1_overlay_coherence_tests: 0x2195E8 callback walk misdecoded\n";
            return 1;
        }

        writeLe32(callbackGuest, 0x1D5BF4u, 0x1FFFF0u);
        const auto badTable = game::inspectRac1BootOverlayProgress(callbackGuest);
        if (badTable.callback2195OwnerPointer != 0x1FFFF0u ||
            badTable.callback2195TablePointer != 0u || badTable.callback2195TableReadable ||
            badTable.callback2195NonNullObjects != 0u || badTable.callback2195NonNullTargets != 0u ||
            badTable.callback21E7C8Present || badTable.callback21E7C8Slot != 0xFFFFFFFFu) {
            std::cerr << "rac1_overlay_coherence_tests: out-of-range 0x2195E8 callback table accepted\n";
            return 1;
        }
    }

    {
        std::vector<std::uint8_t> progressGuest(0x160000u, 0u);

        // This is the precise class of state observed in the accepted Windows run:
        // 0x15EE4C can still refer to sub_001EB798's resource workspace. A non-0x60
        // first word must therefore stop record decoding rather than being interpreted
        // as an enormous record offset.
        writeLe32(progressGuest, 0x15EE4Cu, 0x1000u);
        writeLe32(progressGuest, 0x1000u, 0x30000009u);
        const auto workspace = game::inspectRac1BootOverlayProgress(progressGuest);
        if (workspace.status !=
                game::Rac1BootOverlayProgressStatus::FirstGenerationStreamSignatureAbsent ||
            workspace.sharedLoaderPointer != 0x1000u ||
            workspace.sharedLoaderPrefixBytes != 0x30000009u ||
            workspace.firstGenerationStreamSignatureObserved || workspace.wad158FirstRecordHeaderMatched ||
            workspace.firstRecordAddress != 0u ||
            workspace.firstDestination != 0u || workspace.firstGenerationEntry != 0u) {
            std::cerr << "rac1_overlay_coherence_tests: resource workspace misclassified as overlay stream\n";
            return 1;
        }

        writeLe32(progressGuest, 0x15EE4Cu, 0x15FFFFu);
        const auto headerOutOfRange = game::inspectRac1BootOverlayProgress(progressGuest);
        if (headerOutOfRange.status !=
            game::Rac1BootOverlayProgressStatus::SharedLoaderHeaderOutOfRange) {
            std::cerr << "rac1_overlay_coherence_tests: out-of-range shared header accepted\n";
            return 1;
        }

        writeLe32(progressGuest, 0x15EE4Cu, 0x1000u);
        writeLe32(progressGuest, 0x1000u, 0x60u);
        writeLe32(progressGuest, 0x1060u + 0x00u, 0u);
        writeLe32(progressGuest, 0x1060u + 0x04u, 0u);
        writeLe32(progressGuest, 0x1060u + 0x08u, 0u);
        writeLe32(progressGuest, 0x1060u + 0x0Cu, 0u);
        const auto prefixOnly = game::inspectRac1BootOverlayProgress(progressGuest);
        if (prefixOnly.status != game::Rac1BootOverlayProgressStatus::Wad158FirstRecordHeaderMismatch ||
            !prefixOnly.firstGenerationStreamSignatureObserved || prefixOnly.wad158FirstRecordHeaderMatched) {
            std::cerr << "rac1_overlay_coherence_tests: 0x60 setup signature confused with completed WAD158 read\n";
            return 1;
        }

        writeLe32(progressGuest, 0x15EE4Cu, 0x15FFC0u);
        writeLe32(progressGuest, 0x15FFC0u, 0x60u);
        const auto recordOutOfRange = game::inspectRac1BootOverlayProgress(progressGuest);
        if (recordOutOfRange.status !=
            game::Rac1BootOverlayProgressStatus::FirstRecordOutOfRange) {
            std::cerr << "rac1_overlay_coherence_tests: out-of-range first record accepted\n";
            return 1;
        }

        writeLe32(progressGuest, 0x15EE4Cu, 0x15FF90u);
        writeLe32(progressGuest, 0x15FF90u, 0x60u);
        writeLe32(progressGuest, 0x15FFF0u + 0x00u, 0x0015EF00u);
        writeLe32(progressGuest, 0x15FFF0u + 0x04u, 0x2328u);
        writeLe32(progressGuest, 0x15FFF0u + 0x08u, 1u);
        writeLe32(progressGuest, 0x15FFF0u + 0x0Cu, 0x001E9658u);
        const auto payloadOutOfRange = game::inspectRac1BootOverlayProgress(progressGuest);
        if (payloadOutOfRange.status !=
            game::Rac1BootOverlayProgressStatus::FirstPayloadOutOfRange) {
            std::cerr << "rac1_overlay_coherence_tests: out-of-range first payload accepted\n";
            return 1;
        }
    }

    {
        // Highest fixed read is 0x15F618..0x15F61B. One byte less than the
        // required end must fail closed before any address is dereferenced.
        std::vector<std::uint8_t> tinyProgressGuest(0x15F61Bu, 0u);
        const auto tinyProgress = game::inspectRac1BootOverlayProgress(tinyProgressGuest);
        if (tinyProgress.status != game::Rac1BootOverlayProgressStatus::GuestMemoryTooSmall) {
            std::cerr << "rac1_overlay_coherence_tests: undersized progress RDRAM accepted\n";
            return 1;
        }
    }

    std::cout << "R&C1 level-overlay coherence tests passed\n";
    return 0;
}
