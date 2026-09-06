#include "game/native_audio_bootstrap.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "ps2_runtime.h"

namespace {

using Contract = ratchet::game::Rac1NativeAudioBootstrapContract;

struct TestContext {
    int failures = 0;

    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

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

void applyGeneratedWrapperReference(std::span<std::uint8_t> memory,
                                    std::uint32_t outputMode,
                                    std::uint32_t volume) {
    const std::uint32_t base = Contract::kAudioStateBase;

    std::fill_n(memory.begin() + base, 0x40, std::uint8_t{0});
    writeLe32(memory, base + 0x40u, 0u);
    writeLe32(memory, base + 0x44u, 0u);
    writeLe32(memory, base + 0x70u, 0u);

    const std::uint32_t end = base + Contract::kAudioStateRecordRegionBytes;
    for (std::uint32_t record = base;
         record < end;
         record += Contract::kAudioStateRecordSpan) {
        memory[record + 0x74u] = 0u;
        const std::uint32_t next = record + Contract::kAudioStateRecordSpan;
        if (next < end) {
            writeLe32(memory, next + 0x70u, 0u);
        }
    }

    const std::uint32_t eight = volume << 3u;
    const std::uint32_t seven = eight - volume;
    const auto q8 = static_cast<std::uint32_t>(static_cast<std::int32_t>(eight) / 10);
    const auto q7 = static_cast<std::uint32_t>(static_cast<std::int32_t>(seven) / 10);
    writeLe32(memory, base + 0x48u, q8);
    writeLe32(memory, base + 0x4cu, outputMode);
    writeLe32(memory, base + 0x50u, q8);
    writeLe32(memory, base + 0x54u, q7);
    writeLe32(memory, base + 0x58u, q7);
    writeLe32(memory, base + 0x5cu, volume);

    const std::uint32_t manager = Contract::kGameSoundManagerBase;
    memory[manager + 0x22u] = 0xffu;
    memory[manager + 0x30u] = 0x20u;
    writeLe32(memory, manager + 0x00u, 0u);
    memory[manager + 0x31u] = 0u;
    memory[manager + 0x32u] = 0u;
    memory[manager + 0x33u] = 0u;
    writeLe32(memory, manager + 0x34u, 0u);
    writeLe16(memory, manager + 0x3eu, 0u);
    writeLe32(memory, manager + 0x50u, 0u);
    writeLe16(memory, manager + 0x5au, 0u);
    writeLe32(memory, manager + 0x6cu, 0u);
    writeLe16(memory, manager + 0x76u, 0u);
    writeLe32(memory, manager + 0x1cu, 0xffffffffu);
}

void setGuestRegister(R5900Context& ctx, std::uint32_t index, std::uint32_t value) {
    ctx.r[index] = _mm_set_epi64x(0, static_cast<std::int64_t>(value));
}

} // namespace

int main() {
    TestContext test;

    test.expect(Contract::kGameAudioBootstrapFunction == 0x0022c8d0u,
                "audio HLE owns the game-side startup wrapper, not a SIF packet");
    test.expect(Contract::k989SndCommandService == 0x00123456u &&
                    Contract::k989SndLoaderService == 0x00123457u,
                "audio HLE documents both generated 989snd RPC service IDs");

    constexpr std::uint32_t kOutputMode = 0x01020304u;
    constexpr std::uint32_t kVolume = 100u;

    std::vector<std::uint8_t> guest(Contract::kGuestRamBytes, 0x5au);
    writeLe32(guest, Contract::kAudioOutputModeAddress, kOutputMode);
    writeLe32(guest, Contract::kAudioVolumeAddress, kVolume);
    std::vector<std::uint8_t> expected = guest;
    applyGeneratedWrapperReference(expected, kOutputMode, kVolume);

    test.expect(ratchet::game::applyRac1NativeAudioBootstrapState(guest),
                "retail-sized guest RAM accepts native audio bootstrap state");
    test.expect(guest == expected,
                "native audio bootstrap matches every direct game-visible wrapper write and no others");

    std::vector<std::uint8_t> shortGuest(Contract::kRequiredGuestBytes - 1u, 0x5au);
    const std::vector<std::uint8_t> shortBefore = shortGuest;
    test.expect(!ratchet::game::applyRac1NativeAudioBootstrapState(shortGuest),
                "undersized guest RAM is rejected before audio bootstrap writes");
    test.expect(shortGuest == shortBefore,
                "rejected audio bootstrap performs no partial guest-memory mutation");

    ratchet::runtime::NativeReplacementRegistry registry;
    ratchet::game::declareNativeAudioBootstrapReplacements(registry);
    test.expect(registry.size(ratchet::runtime::NativeReplacementStage::Runtime) == 2u,
                "audio bootstrap declares exactly two game-level native boundaries");
    test.expect(registry.entries().size() == 2u &&
                    registry.entries()[0].address == Contract::kGameAudioBootstrapFunction &&
                    registry.entries()[1].address == Contract::kLevelSoundBankLoadFunction,
                "audio replacement addresses match the proved startup and level-bank wrappers");
    test.expect(registry.entries().size() == 2u &&
                    registry.entries()[1].fallbackStorage == nullptr,
                "deferred level-bank load has no legacy 989snd/SIF fallback");

    if (registry.entries().size() == 2u) {
        std::vector<std::uint8_t> callbackGuest(Contract::kGuestRamBytes, 0x5au);
        writeLe32(callbackGuest, Contract::kAudioOutputModeAddress, kOutputMode);
        writeLe32(callbackGuest, Contract::kAudioVolumeAddress, kVolume);
        std::vector<std::uint8_t> callbackExpected = callbackGuest;
        applyGeneratedWrapperReference(callbackExpected, kOutputMode, kVolume);

        constexpr std::uint32_t kReturnAddress = 0x00abcdefu;
        constexpr std::uint32_t kOriginalV0 = 0x13579bdfu;
        R5900Context ctx;
        setGuestRegister(ctx, 31u, kReturnAddress);
        setGuestRegister(ctx, 2u, kOriginalV0);
        registry.entries()[0].function(callbackGuest.data(), &ctx, nullptr);

        test.expect(ctx.pc == kReturnAddress,
                    "native audio bootstrap returns directly to the original guest caller");
        test.expect(getRegU32(&ctx, 2) == kOriginalV0,
                    "native audio bootstrap does not invent a return value the caller never consumes");
        test.expect(callbackGuest == callbackExpected,
                    "runtime audio bootstrap callback publishes the exact proved game-side state");

        const std::vector<std::uint8_t> beforeLevelBank = callbackGuest;
        R5900Context levelBankContext;
        setGuestRegister(levelBankContext, 31u, kReturnAddress);
        setGuestRegister(levelBankContext, 2u, 0xdeadbeefu);
        registry.entries()[1].function(callbackGuest.data(), &levelBankContext, nullptr);
        test.expect(getRegU32(&levelBankContext, 2) == 0u,
                    "deferred level-bank wrapper selects the proved retail no-bank result 0");
        test.expect(levelBankContext.pc == kReturnAddress,
                    "deferred level-bank wrapper returns directly to the game caller");
        test.expect(callbackGuest == beforeLevelBank,
                    "deferred level-bank wrapper has zero guest-memory side effects");
    }

    if (test.failures != 0) {
        return 1;
    }
    std::cout << "Native audio bootstrap contract tests passed\n";
    return 0;
}
