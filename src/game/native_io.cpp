#include "game/native_io.h"

#include "game/native_services.h"
#include "guest_range.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
constexpr std::uint32_t kDiscRegionProbeSector = 0x121u;
constexpr std::uint32_t kBootSourceGlobal = 0x138e40u;
constexpr std::uint32_t kBootSectorCountGlobal = 0x138e44u;
constexpr std::uint32_t kLegacyBootTop = 0x1ff8000u;

PS2Runtime::RecompiledFunction g_sectorReadFallback = nullptr;
std::uint32_t g_nativeReadDiagnostics = 0u;
std::uint32_t g_fallbackReadDiagnostics = 0u;

void returnToGuestCaller(R5900Context* ctx, std::uint32_t result) {
    SET_GPR_U32(ctx, 2, result);
    ctx->pc = GPR_U32(ctx, 31);
}

void publishBootAssetMetadata(std::uint8_t* rdram,
                              R5900Context* ctx,
                              PS2Runtime* runtime,
                              const platform::NativeVfs& vfs) {
    const platform::NativeAssetLocation* boot =
        vfs.findAsset(platform::NativeAssetKind::Wad2, 0u);
    if (boot == nullptr) {
        return;
    }

    WRITE32(kBootSourceGlobal, boot->startSector);
    WRITE32(kBootSectorCountGlobal, boot->sectorCount);
    std::cerr << "[OpenRatchet:VFS] published boot metadata sector=0x"
              << std::hex << boot->startSector
              << " sectors=0x" << boot->sectorCount << std::dec << '\n';
}

bool tryNativeIndexedRead(std::uint8_t* rdram,
                          R5900Context* ctx,
                          const platform::NativeVfs& vfs,
                          std::uint32_t sourceSector,
                          std::uint32_t sectorCount,
                          std::uint32_t destination) {
    if (sectorCount == 0u ||
        sectorCount > std::numeric_limits<std::uint32_t>::max() /
                          platform::NativeVfs::kSectorBytes) {
        return false;
    }

    const std::uint32_t byteCount = sectorCount * platform::NativeVfs::kSectorBytes;
    if (!isRangeWithin(destination, byteCount, kGuestRamBytes)) {
        return false;
    }

    std::string source;
    if (!vfs.readSectors(sourceSector,
                         sectorCount,
                         rdram + destination,
                         kGuestRamBytes - destination,
                         &source)) {
        return false;
    }

    ++g_nativeReadDiagnostics;
    if (g_nativeReadDiagnostics <= 12u) {
        std::cerr << "[OpenRatchet:VFS] native sector read count="
                  << g_nativeReadDiagnostics
                  << " source=0x" << std::hex << sourceSector
                  << " sectors=0x" << sectorCount
                  << " destination=0x" << destination
                  << " bytes=0x" << byteCount
                  << " asset=" << source << std::dec << '\n';
    }
    returnToGuestCaller(ctx, byteCount);
    return true;
}

bool tryLegacyBootAlias(std::uint8_t* rdram,
                        R5900Context* ctx,
                        const platform::NativeVfs& vfs,
                        std::uint32_t sourceSector,
                        std::uint32_t sectorCount,
                        std::uint32_t destination) {
    // Preserve the old bounded safety net for the already-observed state where
    // the guest reaches its boot buffer before sector metadata has propagated.
    // The data itself is now owned by the VFS rather than guest_overrides.cpp.
    if (sourceSector != 0u || sectorCount != 0u || destination != kLegacyBootTop ||
        destination >= kGuestRamBytes) {
        return false;
    }

    std::size_t bytesRead = 0u;
    if (!vfs.readAssetPrefix(platform::NativeAssetKind::Wad2,
                             0u,
                             rdram + destination,
                             kGuestRamBytes - destination,
                             bytesRead) ||
        bytesRead < 0x2000u) {
        return false;
    }

    std::cerr << "[OpenRatchet:VFS] legacy boot alias destination=0x"
              << std::hex << destination << " bytes=0x" << bytesRead << std::dec << '\n';
    returnToGuestCaller(ctx, static_cast<std::uint32_t>(bytesRead));
    return true;
}

void nativeSectorRead(std::uint8_t* rdram,
                      R5900Context* ctx,
                      PS2Runtime* runtime) {
    const std::uint32_t sourceSector = GPR_U32(ctx, 4);
    const std::uint32_t sectorCount = GPR_U32(ctx, 5);
    const std::uint32_t destination = GPR_U32(ctx, 6);

    const NativeGameServices& services = nativeGameServices();
    if (services.vfs != nullptr && services.vfs->ready()) {
        if (tryNativeIndexedRead(rdram,
                                 ctx,
                                 *services.vfs,
                                 sourceSector,
                                 sectorCount,
                                 destination) ||
            tryLegacyBootAlias(rdram,
                               ctx,
                               *services.vfs,
                               sourceSector,
                               sectorCount,
                               destination)) {
            return;
        }
    }

    // Not every raw disc sector has been promoted into the extracted VFS yet.
    // Keep the original EE/CDVD path only for those unresolved ranges. This is
    // deliberately a fallback, not the owner of indexed WAD/WAD2 reads.
    ++g_fallbackReadDiagnostics;
    if (g_fallbackReadDiagnostics <= 8u) {
        std::cerr << "[OpenRatchet:VFS] fallback sector read count="
                  << g_fallbackReadDiagnostics
                  << " source=0x" << std::hex << sourceSector
                  << " sectors=0x" << sectorCount
                  << " destination=0x" << destination << std::dec << '\n';
    }

    if (g_sectorReadFallback != nullptr) {
        g_sectorReadFallback(rdram, ctx, runtime);
    } else {
        returnToGuestCaller(ctx, 0u);
    }

    // The current boot still obtains the R&C data-file location through an
    // unresolved raw disc metadata path. Publish the same game state from the
    // authoritative TOC index, without hard-coding 0x3809 or the WAD length.
    if (services.vfs != nullptr && services.vfs->ready() &&
        sourceSector == kDiscRegionProbeSector && sectorCount == 1u &&
        READ32(kBootSectorCountGlobal) == 0u) {
        publishBootAssetMetadata(rdram, ctx, runtime, *services.vfs);
    }
}

} // namespace

void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry) {
    registry.add(0x12f208u,
                 "native.io.read-sectors",
                 runtime::NativeReplacementStage::Runtime,
                 nativeSectorRead,
                 &g_sectorReadFallback);
}

} // namespace ratchet::game
