#include "game/rac1_overlay_aot_dispatch.h"

#include <cstdint>
#include <iostream>
#include <mutex>

#include "runtime/native_replacements.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#ifndef OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE
#define OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE 0
#endif
#ifndef OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE
#define OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE 0
#endif

#if OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE
#include "overlays/wad_158/openratchet_overlay_table.h"
#endif
#if OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE
#include "overlays/level_00/openratchet_overlay_table.h"
#endif

namespace ratchet::game {
namespace {

constexpr std::uint32_t kRetailOverlayMaterializeAddress = 0x0012D8F8u;
constexpr std::uint32_t kWad158GenerationEntry = 0x001E9658u;
constexpr std::uint32_t kLevel0GenerationEntry = 0x00245C28u;

runtime::GuestFunction g_retailOverlayMaterializeFallback = nullptr;
Rac1OverlayDispatchManager g_overlayDispatchManager;
std::mutex g_overlayDispatchMutex;
std::uint64_t g_overlayActivationDiagnostics = 0u;
std::uint64_t g_overlayMaterializerCalls = 0u;
std::uint64_t g_overlaySuccessfulActivations = 0u;
std::uint32_t g_lastMaterializerGenerationEntry = 0u;

Rac1OverlayDispatchTableView generatedDispatchTable() noexcept {
    return {
        g_ps2RecompiledFunctionTableBase,
        g_ps2RecompiledFunctionTableEnd,
        static_cast<std::size_t>(g_ps2RecompiledFunctionTableSlotCount),
        g_ps2RecompiledFunctionTable,
    };
}

void reportOverlayActivation(const Rac1OverlayActivationSummary& summary,
                             const char* source) {
    ++g_overlayActivationDiagnostics;
    if (g_overlayActivationDiagnostics > 16u &&
        summary.status == Rac1OverlayActivationStatus::AlreadyActive) {
        return;
    }

    std::cerr << "[OpenRatchet:overlay-aot]"
              << " source=" << source
              << " generationEntry=0x" << std::hex << summary.generationEntry
              << std::dec
              << " functions=" << summary.functionCount
              << " ranges=" << summary.materializedRangeCount
              << " materializedSlots=" << summary.materializedSlotCount
              << " clearedOwners=" << summary.clearedOwnerCount
              << " baselineCaptured=" << summary.baselineCapturedCount
              << " installed=" << summary.installedCount
              << " tableBase=0x" << std::hex << g_ps2RecompiledFunctionTableBase
              << " tableEnd=0x" << g_ps2RecompiledFunctionTableEnd
              << std::dec
              << " status=" << rac1OverlayActivationStatusName(summary.status)
              << '\n';
}

[[maybe_unused]] bool activateGeneration(
                        const Rac1OverlayFunctionEntry* entries,
                        std::size_t entryCount,
                        const Rac1OverlayMaterializedRange* materializedRanges,
                        std::size_t materializedRangeCount,
                        std::uint32_t generationEntry,
                        const char* source) {
    const Rac1OverlayActivationSummary summary = g_overlayDispatchManager.activate(
        generatedDispatchTable(), entries, entryCount, materializedRanges,
        materializedRangeCount, generationEntry);
    reportOverlayActivation(summary, source);

    const bool ok = summary.status == Rac1OverlayActivationStatus::Activated ||
                    summary.status == Rac1OverlayActivationStatus::AlreadyActive;
    if (ok) ++g_overlaySuccessfulActivations;
    return ok;
}

void retailOverlayMaterializeBridge(std::uint8_t* rdram,
                                    R5900Context* ctx,
                                    PS2Runtime* runtime) {
    if (g_retailOverlayMaterializeFallback == nullptr) {
        std::cerr << "[OpenRatchet:overlay-aot] source=retail-loader"
                  << " address=0x12d8f8 status=missing-generated-fallback\n";
        if (runtime != nullptr) {
            runtime->requestStop();
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_overlayDispatchMutex);
        ++g_overlayMaterializerCalls;
    }

    // Retail owns record interpretation and byte materialization.  This bridge
    // only changes which statically recompiled generation is dispatchable after
    // sub_0012D8F8 returns the generation entry in v0.
    g_retailOverlayMaterializeFallback(rdram, ctx, runtime);

    if (runtime == nullptr) {
        return;
    }
    const std::uint32_t generationEntry = GPR_U32(ctx, 2);

    std::lock_guard<std::mutex> lock(g_overlayDispatchMutex);
    g_lastMaterializerGenerationEntry = generationEntry;
    bool activated = false;
    switch (generationEntry) {
    case kWad158GenerationEntry:
#if OPENRATCHET_WAD158_OVERLAY_AOT_AVAILABLE
        static_assert(ratchet::generated::wad158::kGenerationEntry == kWad158GenerationEntry);
        activated = activateGeneration(
            ratchet::generated::wad158::kFunctions,
            ratchet::generated::wad158::kFunctionCount,
            ratchet::generated::wad158::kMaterializedRanges,
            ratchet::generated::wad158::kMaterializedRangeCount,
            generationEntry,
            "wad_158.wad");
#else
        std::cerr << "[OpenRatchet:overlay-aot] source=wad_158.wad"
                  << " generationEntry=0x" << std::hex << generationEntry << std::dec
                  << " status=aot-module-missing\n";
#endif
        break;

    case kLevel0GenerationEntry:
#if OPENRATCHET_LEVEL0_OVERLAY_AOT_AVAILABLE
        static_assert(ratchet::generated::level0::kGenerationEntry == kLevel0GenerationEntry);
        activated = activateGeneration(
            ratchet::generated::level0::kFunctions,
            ratchet::generated::level0::kFunctionCount,
            ratchet::generated::level0::kMaterializedRanges,
            ratchet::generated::level0::kMaterializedRangeCount,
            generationEntry,
            "level_00.wad");
#else
        std::cerr << "[OpenRatchet:overlay-aot] source=level_00.wad"
                  << " generationEntry=0x" << std::hex << generationEntry << std::dec
                  << " status=aot-module-missing\n";
#endif
        break;

    default:
        std::cerr << "[OpenRatchet:overlay-aot] source=retail-loader"
                  << " generationEntry=0x" << std::hex << generationEntry << std::dec
                  << " status=unsupported-generation\n";
        break;
    }

    if (!activated) {
        runtime->requestStop();
    }
}

} // namespace

Rac1OverlayAotRuntimeState inspectRac1OverlayAotRuntimeState() noexcept {
    std::lock_guard<std::mutex> lock(g_overlayDispatchMutex);
    return {
        g_overlayMaterializerCalls,
        g_overlaySuccessfulActivations,
        g_lastMaterializerGenerationEntry,
        g_overlayDispatchManager.activeGenerationEntry(),
        g_overlayDispatchManager.activeFunctionCount(),
        g_overlayDispatchManager.touchedSlotCount(),
    };
}

void declareRac1OverlayAotReplacements(runtime::NativeReplacementRegistry& registry) {
    registry.add(kRetailOverlayMaterializeAddress,
                 "rac1.overlay.materialize-generation",
                 runtime::NativeReplacementStage::Runtime,
                 retailOverlayMaterializeBridge,
                 &g_retailOverlayMaterializeFallback);
}

} // namespace ratchet::game
