#include "game/native_platform_bootstrap.h"
#include "runtime/native_replacements.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

#include "ps2_runtime.h"

namespace {

struct TestContext {
    int failures = 0;

    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void setGuestReturnAddress(R5900Context& ctx, std::uint32_t address) {
    ctx.r[31] = _mm_set_epi64x(0, static_cast<std::int64_t>(address));
}

} // namespace

int main() {
    using Contract = ratchet::game::Rac1NativePlatformBootstrapContract;
    using ratchet::runtime::NativeReplacementStage;
    TestContext test;

    std::vector<std::uint8_t> guest(Contract::kGuestRamBytes, 0x5au);
    const std::size_t begin = Contract::kDbcWorkStateAddress;
    const std::size_t end = begin + Contract::kDbcInitialStateBytes;

    test.expect(ratchet::game::applyRac1NativeDbcInitialState(guest),
                "retail-sized guest RAM accepts native DBC initialization");
    test.expect(begin > 0u && guest[begin - 1u] == 0x5au,
                "byte immediately before DBC state remains untouched");
    for (std::size_t i = begin; i < end; ++i) {
        test.expect(guest[i] == 0u,
                    "entire proved DBC initial state range is zeroed");
    }
    test.expect(end < guest.size() && guest[end] == 0x5au,
                "byte immediately after DBC state remains untouched");

    std::vector<std::uint8_t> shortGuest(end - 1u, 0x5au);
    test.expect(!ratchet::game::applyRac1NativeDbcInitialState(shortGuest),
                "out-of-range guest memory is rejected instead of partially written");
    test.expect(shortGuest.back() == 0x5au,
                "rejected DBC initialization performs no partial write");

    test.expect(Contract::kDbcWorkSnapshotBytes == 0x80u &&
                    Contract::kDbcEeStateBytes == 0x40u &&
                    Contract::kDbcInitialStateBytes == 0xc0u,
                "native DBC range exactly matches proved retail 0x80+0x40 state");
    test.expect(Contract::kDbcLinkCapacity == 16u,
                "native DBC link capacity matches retail allocator");

    ratchet::runtime::NativeReplacementRegistry registry;
    ratchet::game::declareNativePlatformBootstrapReplacements(registry);
    test.expect(registry.size(NativeReplacementStage::Runtime) == 7u,
                "platform bootstrap declares exactly seven native runtime boundaries");
    test.expect(registry.entries().size() == 7u &&
                    registry.entries()[0].address == Contract::kDbcInitFunction &&
                    registry.entries()[1].address == Contract::kDbcLinkAllocateFunction &&
                    registry.entries()[2].address == Contract::kMemoryCardBootstrapFunction &&
                    registry.entries()[3].address == Contract::kMemoryCardPreflightFunction &&
                    registry.entries()[4].address == Contract::kMoviePlaybackFunction &&
                    registry.entries()[5].address == Contract::kGsDxDyCapabilityProbeFunction &&
                    registry.entries()[6].address == Contract::kIsT10KProbeFunction,
                "platform replacement addresses match the proved EE boundaries");
    test.expect(registry.entries().size() == 7u &&
                    registry.entries()[3].fallbackStorage == nullptr &&
                    registry.entries()[4].fallbackStorage == nullptr &&
                    registry.entries()[5].fallbackStorage == nullptr &&
                    registry.entries()[6].fallbackStorage == nullptr,
                "deferred save/movie and ROMVER probes have no legacy SIF fallback storage");

    if (registry.entries().size() == 7u) {
        constexpr std::uint32_t kReturnAddress = 0x00abcdefu;

        // Exercise the actual replacement callback, not only its pure state helper.
        std::fill(guest.begin(), guest.end(), 0x5au);
        R5900Context dbcInitContext;
        setGuestReturnAddress(dbcInitContext, kReturnAddress);
        registry.entries()[0].function(guest.data(), &dbcInitContext, nullptr);
        test.expect(getRegU32(&dbcInitContext, 2) == 1u,
                    "native DBC init returns retail success value 1");
        test.expect(dbcInitContext.pc == kReturnAddress,
                    "native DBC init returns directly to the guest caller");
        test.expect(guest[begin - 1u] == 0x5au && guest[end] == 0x5au,
                    "native DBC callback preserves bytes outside the proved state range");
        for (std::size_t i = begin; i < end; ++i) {
            test.expect(guest[i] == 0u,
                        "native DBC callback publishes the complete proved initial state");
        }

        for (std::uint32_t expectedSlot = 0u;
             expectedSlot < Contract::kDbcLinkCapacity;
             ++expectedSlot) {
            R5900Context linkContext;
            setGuestReturnAddress(linkContext, kReturnAddress);
            registry.entries()[1].function(guest.data(), &linkContext, nullptr);
            test.expect(getRegU32(&linkContext, 2) == expectedSlot,
                        "native DBC allocator follows retail first-free slot order");
            test.expect(linkContext.pc == kReturnAddress,
                        "native DBC allocator returns directly to the guest caller");
        }

        R5900Context exhaustedContext;
        setGuestReturnAddress(exhaustedContext, kReturnAddress);
        registry.entries()[1].function(guest.data(), &exhaustedContext, nullptr);
        test.expect(getRegU32(&exhaustedContext, 2) == 0xffffffffu,
                    "native DBC allocator returns retail -1 after all 16 slots are occupied");

        // Reinitialization must reset platform allocation state exactly once per DBC init.
        R5900Context reinitContext;
        setGuestReturnAddress(reinitContext, kReturnAddress);
        registry.entries()[0].function(guest.data(), &reinitContext, nullptr);
        R5900Context firstSlotAgainContext;
        setGuestReturnAddress(firstSlotAgainContext, kReturnAddress);
        registry.entries()[1].function(guest.data(), &firstSlotAgainContext, nullptr);
        test.expect(getRegU32(&firstSlotAgainContext, 2) == 0u,
                    "native DBC init resets the 16-slot allocator");

        R5900Context memoryCardContext;
        setGuestReturnAddress(memoryCardContext, kReturnAddress);
        registry.entries()[2].function(guest.data(), &memoryCardContext, nullptr);
        test.expect(getRegU32(&memoryCardContext, 2) == 0u,
                    "native memory-card bootstrap returns the wrapper's retail success value");
        test.expect(memoryCardContext.pc == kReturnAddress,
                    "native memory-card bootstrap returns directly to the guest caller");

        R5900Context memoryCardPreflightContext;
        setGuestReturnAddress(memoryCardPreflightContext, kReturnAddress);
        registry.entries()[3].function(guest.data(), &memoryCardPreflightContext, nullptr);
        test.expect(getRegU32(&memoryCardPreflightContext, 2) == 0u,
                    "native memory-card preflight selects the proved non-blocking retail result");
        test.expect(memoryCardPreflightContext.pc == kReturnAddress,
                    "native memory-card preflight returns directly to the game wrapper caller");

        const std::vector<std::uint8_t> guestBeforeMovie = guest;
        R5900Context movieContext;
        setGuestReturnAddress(movieContext, kReturnAddress);
        registry.entries()[4].function(guest.data(), &movieContext, nullptr);
        test.expect(getRegU32(&movieContext, 2) == 0u,
                    "native movie boundary returns the wrapper's proved retail result 0");
        test.expect(movieContext.pc == kReturnAddress,
                    "native movie boundary returns directly to the game caller");
        test.expect(guest == guestBeforeMovie,
                    "deferred movie boundary has zero guest-memory side effects");

        R5900Context gsCapabilityContext;
        setGuestReturnAddress(gsCapabilityContext, kReturnAddress);
        registry.entries()[5].function(guest.data(), &gsCapabilityContext, nullptr);
        test.expect(getRegU32(&gsCapabilityContext, 2) == 0u,
                    "native GS capability probe truthfully reports syscall 0x80 unavailable");
        test.expect(gsCapabilityContext.pc == kReturnAddress,
                    "native GS capability probe returns directly to the libgraph caller");

        R5900Context t10kContext;
        setGuestReturnAddress(t10kContext, kReturnAddress);
        registry.entries()[6].function(guest.data(), &t10kContext, nullptr);
        test.expect(getRegU32(&t10kContext, 2) == 0u,
                    "native T10K probe selects the retail non-development-tool profile");
        test.expect(t10kContext.pc == kReturnAddress,
                    "native T10K probe returns directly to the libscf caller");
    }

    if (test.failures != 0) {
        return 1;
    }
    std::cout << "Native platform bootstrap contract tests passed\n";
    return 0;
}
