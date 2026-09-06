#include "game/native_audio_bootstrap.h"

#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

using Contract = Rac1NativeAudioBootstrapContract;

bool g_audioBootstrapLogged = false;
bool g_levelSoundBankLogged = false;

std::uint32_t readLe32(std::span<const std::uint8_t> memory,
                       std::uint32_t address) {
    return static_cast<std::uint32_t>(memory[address + 0u]) |
           (static_cast<std::uint32_t>(memory[address + 1u]) << 8u) |
           (static_cast<std::uint32_t>(memory[address + 2u]) << 16u) |
           (static_cast<std::uint32_t>(memory[address + 3u]) << 24u);
}

void writeLe16(std::span<std::uint8_t> memory,
               std::uint32_t address,
               std::uint16_t value) {
    memory[address + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    memory[address + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
}

void writeLe32(std::span<std::uint8_t> memory,
               std::uint32_t address,
               std::uint32_t value) {
    memory[address + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    memory[address + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    memory[address + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    memory[address + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::uint32_t signedDivideByTen(std::uint32_t rawValue) {
    const std::int32_t signedValue = static_cast<std::int32_t>(rawValue);
    return static_cast<std::uint32_t>(signedValue / 10);
}

void returnToGuestCaller(R5900Context* ctx) {
    ctx->pc = getRegU32(ctx, 31);
}

void nativeLevelSoundBankLoad(std::uint8_t*,
                              R5900Context* ctx,
                              PS2Runtime*) {
    if (!g_levelSoundBankLogged) {
        g_levelSoundBankLogged = true;
        std::cerr << "[OpenRatchet:platform] audio-bootstrap component=level-sound-bank"
                  << " source=native-hle policy=deferred-to-phase18"
                  << " retailResult=0 bankLoadByLocBypass=1 sifBypass=1 status=ok\n";
    }

    // sub_0022D708 is the game-level level-bank wrapper. It synchronizes CD,
    // enters 989snd's snd_BankLoadByLoc path, performs 989snd follow-up commands
    // and returns the bank handle. Its own already-busy retail path returns 0
    // without publishing game state. Since Phase 18 owns native bank loading,
    // Phase 11 selects that existing no-bank result rather than synthesizing an
    // IOP sound bank or any 989snd RPC state.
    SET_GPR_U32(ctx, 2, 0u);
    returnToGuestCaller(ctx);
}

void nativeAudioBootstrap(std::uint8_t* rdram,
                          R5900Context* ctx,
                          PS2Runtime*) {
    const bool stateApplied = applyRac1NativeAudioBootstrapState(
        std::span<std::uint8_t>(rdram, Contract::kGuestRamBytes));
    if (!stateApplied) {
        std::cerr << "[OpenRatchet:platform] audio-bootstrap component=989snd-startup"
                  << " source=native-hle guestState=out-of-range status=error\n";
        returnToGuestCaller(ctx);
        return;
    }

    if (!g_audioBootstrapLogged) {
        g_audioBootstrapLogged = true;
        std::cerr << "[OpenRatchet:platform] audio-bootstrap component=989snd-startup"
                  << " source=native-hle services=0x123456,0x123457"
                  << " guestState=retail-game-wrapper"
                  << " policy=deferred-to-phase18 sifBypass=1 status=ok\n";
    }

    // sub_0022C8D0 has no game-consumed return value. Preserve the caller's
    // register state and only return to the original RA.
    returnToGuestCaller(ctx);
}

} // namespace

bool applyRac1NativeAudioBootstrapState(
    std::span<std::uint8_t> guestMemory) {
    if (guestMemory.size() < Contract::kRequiredGuestBytes) {
        return false;
    }

    const std::uint32_t base = Contract::kAudioStateBase;

    // sub_0022C8D0 0x22C8E8..0x22C934: exact direct game-owned clears.
    std::fill_n(guestMemory.begin() + static_cast<std::ptrdiff_t>(base),
                0x40,
                std::uint8_t{0});
    writeLe32(guestMemory, base + 0x40u, 0u);
    writeLe32(guestMemory, base + 0x44u, 0u);
    writeLe32(guestMemory, base + 0x70u, 0u);

    const std::uint32_t recordEnd =
        base + Contract::kAudioStateRecordRegionBytes;
    for (std::uint32_t record = base;
         record < recordEnd;
         record += Contract::kAudioStateRecordSpan) {
        guestMemory[record + 0x74u] = 0u;
        const std::uint32_t next = record + Contract::kAudioStateRecordSpan;
        if (next < recordEnd) {
            // This is the branch-likely delay-slot write at 0x22C938.
            writeLe32(guestMemory, next + 0x70u, 0u);
        }
    }

    // sub_0022C8D0 0x22C99C..0x22C9F0 derives game-side mixer values from
    // existing configuration globals. The MIPS shifts/subtractions are 32-bit
    // wrapping operations and the two divisions are signed by constant 10.
    const std::uint32_t outputMode =
        readLe32(guestMemory, Contract::kAudioOutputModeAddress);
    const std::uint32_t volume =
        readLe32(guestMemory, Contract::kAudioVolumeAddress);
    const std::uint32_t volumeTimesEight = volume << 3u;
    const std::uint32_t volumeTimesSeven = volumeTimesEight - volume;
    const std::uint32_t scaledEightTenths = signedDivideByTen(volumeTimesEight);
    const std::uint32_t scaledSevenTenths = signedDivideByTen(volumeTimesSeven);

    writeLe32(guestMemory, base + 0x48u, scaledEightTenths);
    writeLe32(guestMemory, base + 0x4cu, outputMode);
    writeLe32(guestMemory, base + 0x50u, scaledEightTenths);
    writeLe32(guestMemory, base + 0x54u, scaledSevenTenths);
    writeLe32(guestMemory, base + 0x58u, scaledSevenTenths);
    writeLe32(guestMemory, base + 0x5cu, volume);

    // FUN_00215390 direct game-owned sound-manager state. Calls from that
    // function into 989snd (0x12EB20/0x12DC80/0x12EF28) are intentionally not
    // reproduced here because Phase 18 will own the native audio backend.
    const std::uint32_t manager = Contract::kGameSoundManagerBase;
    guestMemory[manager + 0x22u] = 0xffu;
    guestMemory[manager + 0x30u] = 0x20u;
    writeLe32(guestMemory, manager + 0x00u, 0u);
    guestMemory[manager + 0x31u] = 0u;
    guestMemory[manager + 0x32u] = 0u;
    guestMemory[manager + 0x33u] = 0u;
    writeLe32(guestMemory, manager + 0x34u, 0u);
    writeLe16(guestMemory, manager + 0x3eu, 0u);
    writeLe32(guestMemory, manager + 0x50u, 0u);
    writeLe16(guestMemory, manager + 0x5au, 0u);
    writeLe32(guestMemory, manager + 0x6cu, 0u);
    writeLe16(guestMemory, manager + 0x76u, 0u);
    writeLe32(guestMemory, manager + 0x1cu, 0xffffffffu);

    return true;
}

void declareNativeAudioBootstrapReplacements(
    runtime::NativeReplacementRegistry& registry) {
    registry.add(Contract::kGameAudioBootstrapFunction,
                 "native.platform.audio-bootstrap",
                 runtime::NativeReplacementStage::Runtime,
                 nativeAudioBootstrap);
    registry.add(Contract::kLevelSoundBankLoadFunction,
                 "native.platform.level-sound-bank-deferred",
                 runtime::NativeReplacementStage::Runtime,
                 nativeLevelSoundBankLoad);
}

} // namespace ratchet::game
