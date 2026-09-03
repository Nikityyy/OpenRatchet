#include "game/native_io.h"

#include "game/native_services.h"
#include "guest_range.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
constexpr std::uint32_t kGameDiscTocBase = 0x00137b80u;
constexpr std::uint32_t kLegacyBootTop = 0x01ff8000u;

PS2Runtime::RecompiledFunction g_sectorReadFallback = nullptr;
PS2Runtime::RecompiledFunction g_discTocFallback = nullptr;
std::uint32_t g_nativeReadDiagnostics = 0u;
std::uint32_t g_fallbackReadDiagnostics = 0u;
std::uint32_t g_nativeTocDiagnostics = 0u;

void returnToGuestCaller(R5900Context* ctx, std::uint32_t result) {
    SET_GPR_U32(ctx, 2, result);
    ctx->pc = GPR_U32(ctx, 31);
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
    // Bounded compatibility path retained only while Phase 3 validates the
    // native disc-TOC owner. A correct TOC causes the caller to request WAD2/0
    // with a real sector/count pair and this branch is never entered.
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

    // Raw ranges that have not yet been promoted into the extracted VFS remain
    // on the generated EE/CDVD path. Indexed game assets never take this path.
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
}

void nativeLoadDiscToc(std::uint8_t* rdram,
                       R5900Context* ctx,
                       PS2Runtime* runtime) {
    const NativeGameServices& services = nativeGameServices();
    if (services.vfs == nullptr || !services.vfs->ready() ||
        services.vfs->discTocSize() == 0u ||
        !isRangeWithin(kGameDiscTocBase,
                       services.vfs->discTocSize(),
                       kGuestRamBytes)) {
        if (g_discTocFallback != nullptr) {
            g_discTocFallback(rdram, ctx, runtime);
        } else {
            returnToGuestCaller(ctx, 0u);
        }
        return;
    }

    std::size_t bytesWritten = 0u;
    if (!services.vfs->copyDiscToc(rdram + kGameDiscTocBase,
                                   kGuestRamBytes - kGameDiscTocBase,
                                   bytesWritten)) {
        if (g_discTocFallback != nullptr) {
            g_discTocFallback(rdram, ctx, runtime);
        } else {
            returnToGuestCaller(ctx, 0u);
        }
        return;
    }

    ++g_nativeTocDiagnostics;
    if (g_nativeTocDiagnostics <= 4u) {
        std::cerr << "[OpenRatchet:VFS] native disc TOC count="
                  << g_nativeTocDiagnostics
                  << " destination=0x" << std::hex << kGameDiscTocBase
                  << " bytes=0x" << bytesWritten
                  << " known=0x" << services.vfs->discTocKnownBytes()
                  << " complete=" << (services.vfs->discTocComplete() ? 1 : 0)
                  << std::dec << '\n';
    }

    // FUN_0012f2b8 returns 1 after copying the 0x2960-byte table. Reproduce the
    // game-visible contract directly; no SIF/IOP transaction is required.
    returnToGuestCaller(ctx, 1u);
}

} // namespace

void declareNativeIoReplacements(runtime::NativeReplacementRegistry& registry) {
    registry.add(0x12f208u,
                 "native.io.read-sectors",
                 runtime::NativeReplacementStage::Runtime,
                 nativeSectorRead,
                 &g_sectorReadFallback);

    registry.add(0x12f2b8u,
                 "native.io.load-disc-toc",
                 runtime::NativeReplacementStage::Runtime,
                 nativeLoadDiscToc,
                 &g_discTocFallback);
}

} // namespace ratchet::game
