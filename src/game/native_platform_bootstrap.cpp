#include "game/native_platform_bootstrap.h"

#include "runtime/native_replacements.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

using Contract = Rac1NativePlatformBootstrapContract;

std::array<bool, Contract::kDbcLinkCapacity> g_dbcLinkSlots{};
bool g_dbcInitLogged = false;
bool g_dbcLinkLogged = false;
bool g_memoryCardBootstrapLogged = false;
bool g_memoryCardPreflightLogged = false;
bool g_moviePlaybackLogged = false;
bool g_gsDxDyCapabilityLogged = false;
bool g_isT10KLogged = false;

void returnToGuestCaller(R5900Context* ctx, std::uint32_t result) {
    SET_GPR_U32(ctx, 2, result);
    ctx->pc = GPR_U32(ctx, 31);
}

void nativeDbcInit(std::uint8_t* rdram,
                   R5900Context* ctx,
                   PS2Runtime*) {
    const bool stateApplied = applyRac1NativeDbcInitialState(
        std::span<std::uint8_t>(rdram, Contract::kGuestRamBytes));
    if (!stateApplied) {
        std::cerr << "[OpenRatchet:platform] input-bootstrap component=dbc-init"
                  << " source=native-hle guestState=out-of-range status=error\n";
        returnToGuestCaller(ctx, 0u);
        return;
    }

    g_dbcLinkSlots.fill(false);
    if (!g_dbcInitLogged) {
        g_dbcInitLogged = true;
        std::cerr << "[OpenRatchet:platform] input-bootstrap component=dbc-init"
                  << " source=native-hle guestState=retail-success"
                  << " zeroRange=0x" << std::hex << Contract::kDbcWorkStateAddress
                  << ":0x" << Contract::kDbcInitialStateBytes
                  << " sifBypass=1 status=ok" << std::dec << '\n';
    }

    // Retail sub_00124510 returns 1 on the accepted DBCMAN 2.x path.
    returnToGuestCaller(ctx, 1u);
}

void nativeDbcLinkAllocate(std::uint8_t*,
                           R5900Context* ctx,
                           PS2Runtime*) {
    std::uint32_t slot = Contract::kDbcLinkCapacity;
    for (std::uint32_t i = 0u; i < g_dbcLinkSlots.size(); ++i) {
        if (!g_dbcLinkSlots[i]) {
            slot = i;
            g_dbcLinkSlots[i] = true;
            break;
        }
    }

    if (slot == Contract::kDbcLinkCapacity) {
        // Retail DBCMAN's allocator returns a negative failure after all 16
        // link records are occupied. This path is not expected during Phase 11,
        // but failing explicitly is preferable to fabricating another slot.
        std::cerr << "[OpenRatchet:platform] input-bootstrap component=dbc-link"
                  << " source=native-hle capacity=" << Contract::kDbcLinkCapacity
                  << " status=exhausted\n";
        returnToGuestCaller(ctx, 0xffffffffu);
        return;
    }

    if (!g_dbcLinkLogged) {
        g_dbcLinkLogged = true;
        std::cerr << "[OpenRatchet:platform] input-bootstrap component=dbc-link"
                  << " source=native-hle allocator=retail-first-free"
                  << " slot=" << slot << " sifBypass=1 status=ok\n";
    }
    returnToGuestCaller(ctx, slot);
}

void nativeMoviePlaybackDeferred(std::uint8_t*,
                                 R5900Context* ctx,
                                 PS2Runtime*) {
    if (!g_moviePlaybackLogged) {
        g_moviePlaybackLogged = true;
        std::cerr << "[OpenRatchet:platform] video-bootstrap component=movie-playback"
                  << " source=native-hle policy=deferred-to-phase17"
                  << " retailResult=0 mpegBypass=1 sifBypass=1 status=ok\n";
    }

    // Retail FUN_0023A3B8 is the complete synchronous movie wrapper:
    // setup -> sceMpegInit/playback -> sceMpegDelete/cleanup. All known callers
    // ignore its result, and its epilogue returns 0 after clearing the two
    // temporary movie-buffer globals at 0x161208/0x16120C. Phase 17 owns native
    // movie playback, so Phase 11 takes that already-existing completed/no-movie
    // outcome at the wrapper boundary. We deliberately do not enter libmpeg,
    // fabricate SIF clients, consume controller input or mutate guest memory.
    returnToGuestCaller(ctx, 0u);
}

void nativeGsDxDyCapabilityProbe(std::uint8_t*,
                                 R5900Context* ctx,
                                 PS2Runtime*) {
    if (!g_gsDxDyCapabilityLogged) {
        g_gsDxDyCapabilityLogged = true;
        std::cerr << "[OpenRatchet:platform] display-probe component=gs-dxdy-kernel-offset"
                  << " source=native-hle syscall=0x80 available=0"
                  << " fileioBypass=1 status=ok\n";
    }

    // Retail libgraph::checkModelVersion returns whether ROMVER's kernel date
    // is newer than 2001-06-08 solely to decide whether syscall 0x80
    // (_GetGsDxDyOffset) may be used. PS2Recomp's active numeric syscall
    // dispatcher has no 0x80 implementation, so the truthful host capability
    // answer is false and libgraph takes its built-in pre-syscall fallback.
    returnToGuestCaller(ctx, 0u);
}

void nativeIsT10KProbe(std::uint8_t*,
                        R5900Context* ctx,
                        PS2Runtime*) {
    if (!g_isT10KLogged) {
        g_isT10KLogged = true;
        std::cerr << "[OpenRatchet:platform] system-probe component=is-t10k"
                  << " source=native-hle developmentTool=0"
                  << " fileioBypass=1 status=ok\n";
    }

    // Retail libscf::IsT10K returns true iff ROMVER[4] == 'T', i.e. Sony's
    // DTL-T10000 development TOOL profile. OpenRatchet executes the retail
    // game profile on a PC host, not T10K hardware, so false is the exact
    // platform answer. No synthetic ROMVER or BIOS filesystem is introduced.
    returnToGuestCaller(ctx, 0u);
}

void nativeMemoryCardBootstrap(std::uint8_t*,
                               R5900Context* ctx,
                               PS2Runtime*) {
    if (!g_memoryCardBootstrapLogged) {
        g_memoryCardBootstrapLogged = true;
        std::cerr << "[OpenRatchet:platform] save-bootstrap component=memory-card-init"
                  << " source=native-hle policy=deferred-to-phase19"
                  << " sifBypass=1 status=ok\n";
    }

    // FUN_0020AC58 exists only to call sceMcInit and log when it returns a
    // nonzero error. Its caller ignores the result and the successful path has
    // no game-state side effect beyond allowing startup to continue. Phase 19
    // will replace actual save/load operations with NativeSave semantics.
    returnToGuestCaller(ctx, 0u);
}

void nativeMemoryCardPreflight(std::uint8_t*,
                               R5900Context* ctx,
                               PS2Runtime*) {
    if (!g_memoryCardPreflightLogged) {
        g_memoryCardPreflightLogged = true;
        std::cerr << "[OpenRatchet:platform] save-bootstrap component=memory-card-preflight"
                  << " source=native-hle policy=deferred-to-phase19"
                  << " retailResult=0 inputAckBypass=1 sifBypass=1 status=ok\n";
    }

    // Retail sub_00209168 is a boot-time memory-card preflight. Static command
    // reconstruction proves it issues libmc GetInfo (command 0x01), Sync and
    // GetDir (command 0x0D) against /BA****-*****RATCHET. Its own return value
    // 0 is the non-blocking path: the caller exits the retry/UI loop immediately.
    // Non-zero values are memory-card warning states; after 11 frames the caller
    // waits for a newly-pressed pad bit at global pad-state + 0x1A4. Because
    // NativeSave is intentionally deferred to Phase 19, Phase 11 exposes no
    // persistent card and selects this existing non-blocking retail outcome.
    // We do not fabricate card contents, libmc/MCSERV RPC state or controller
    // input.
    returnToGuestCaller(ctx, 0u);
}

} // namespace

bool applyRac1NativeDbcInitialState(std::span<std::uint8_t> guestMemory) {
    const std::size_t begin = Contract::kDbcWorkStateAddress;
    const std::size_t size = Contract::kDbcInitialStateBytes;
    if (begin > guestMemory.size() || size > guestMemory.size() - begin) {
        return false;
    }

    std::fill_n(guestMemory.begin() + static_cast<std::ptrdiff_t>(begin),
                static_cast<std::ptrdiff_t>(size),
                std::uint8_t{0});
    return true;
}

void declareNativePlatformBootstrapReplacements(
    runtime::NativeReplacementRegistry& registry) {
    using runtime::NativeReplacementStage;

    registry.add(Contract::kDbcInitFunction,
                 "native.platform.dbc-init",
                 NativeReplacementStage::Runtime,
                 nativeDbcInit);
    registry.add(Contract::kDbcLinkAllocateFunction,
                 "native.platform.dbc-link-allocate",
                 NativeReplacementStage::Runtime,
                 nativeDbcLinkAllocate);
    registry.add(Contract::kMemoryCardBootstrapFunction,
                 "native.platform.memory-card-bootstrap",
                 NativeReplacementStage::Runtime,
                 nativeMemoryCardBootstrap);
    registry.add(Contract::kMemoryCardPreflightFunction,
                 "native.platform.memory-card-preflight",
                 NativeReplacementStage::Runtime,
                 nativeMemoryCardPreflight);
    registry.add(Contract::kMoviePlaybackFunction,
                 "native.platform.movie-playback-deferred",
                 NativeReplacementStage::Runtime,
                 nativeMoviePlaybackDeferred);
    registry.add(Contract::kGsDxDyCapabilityProbeFunction,
                 "native.platform.gs-dxdy-capability-probe",
                 NativeReplacementStage::Runtime,
                 nativeGsDxDyCapabilityProbe);
    registry.add(Contract::kIsT10KProbeFunction,
                 "native.platform.is-t10k-probe",
                 NativeReplacementStage::Runtime,
                 nativeIsT10KProbe);
}

} // namespace ratchet::game
