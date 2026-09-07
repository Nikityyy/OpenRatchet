#include "game/rac1_native_stash.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

void setGuestRegister(R5900Context& ctx, std::size_t reg, std::uint32_t value) {
    ctx.r[reg] = _mm_set_epi64x(0, static_cast<std::int64_t>(value));
}

const ratchet::runtime::NativeReplacement* findReplacement(
    const ratchet::runtime::NativeReplacementRegistry& registry,
    std::uint32_t address) {
    const auto& entries = registry.entries();
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [address](const auto& entry) {
                                     return entry.address == address;
                                 });
    return it == entries.end() ? nullptr : &*it;
}

} // namespace

int main() {
    using ratchet::game::Rac1NativeStashContract;
    using ratchet::runtime::NativeReplacementRegistry;
    using ratchet::runtime::NativeReplacementStage;

    TestContext test;
    NativeReplacementRegistry registry;
    ratchet::game::declareRac1NativeStashReplacements(registry);

    test.expect(registry.size(NativeReplacementStage::Runtime) == 4u,
                "native stash declares exactly four Retail resource-staging boundaries");

    const auto* init = findReplacement(registry, Rac1NativeStashContract::kInitializeFunction);
    const auto* store = findReplacement(registry, Rac1NativeStashContract::kStoreFunction);
    const auto* read = findReplacement(registry, Rac1NativeStashContract::kReadFunction);
    const auto* size = findReplacement(registry, Rac1NativeStashContract::kSizeFunction);
    test.expect(init != nullptr && store != nullptr && read != nullptr && size != nullptr,
                "all four exact stash addresses are registered");
    test.expect(init != nullptr && init->fallbackStorage == nullptr &&
                    store != nullptr && store->fallbackStorage == nullptr &&
                    read != nullptr && read->fallbackStorage == nullptr &&
                    size != nullptr && size->fallbackStorage == nullptr,
                "native stash cannot silently fall back to SID 0x11 SIF transport");

    if (init == nullptr || store == nullptr || read == nullptr || size == nullptr) {
        return 1;
    }

    constexpr std::uint32_t kGuestBytes = Rac1NativeStashContract::kGuestRamBytes;
    constexpr std::uint32_t kSource = 0x00100000u;
    constexpr std::uint32_t kDestination = 0x00200000u;
    constexpr std::uint32_t kReturn = 0x00abcdefu;
    constexpr std::uint32_t kUnits = 8u;
    constexpr std::size_t kBytes = kUnits * Rac1NativeStashContract::kUnitBytes;

    std::vector<std::uint8_t> guest(kGuestBytes, 0u);
    for (std::size_t i = 0u; i < kBytes; ++i) {
        guest[kSource + i] = static_cast<std::uint8_t>((i * 13u + 7u) & 0xffu);
    }

    R5900Context initContext;
    setGuestRegister(initContext, 2u, 0x12345678u);
    setGuestRegister(initContext, 31u, kReturn);
    init->function(guest.data(), &initContext, nullptr);
    test.expect(initContext.pc == kReturn && getRegU32(&initContext, 2) == 0x12345678u,
                "stash init returns to Retail without inventing a v0 result");

    R5900Context storeContext;
    setGuestRegister(storeContext, 4u, kSource);
    setGuestRegister(storeContext, 5u, kUnits);
    setGuestRegister(storeContext, 6u, kUnits);
    setGuestRegister(storeContext, 7u, 0x0015fbc0u);
    setGuestRegister(storeContext, 31u, kReturn);
    store->function(guest.data(), &storeContext, nullptr);
    test.expect(getRegU32(&storeContext, 2) == 0u && storeContext.pc == kReturn,
                "first native store returns Retail slot 0");

    // The native backend must own a real snapshot, not retain a guest pointer.
    std::fill_n(guest.begin() + kSource, kBytes, std::uint8_t{0xeeu});
    std::fill_n(guest.begin() + kDestination, kBytes, std::uint8_t{0x5au});

    R5900Context sizeContext;
    setGuestRegister(sizeContext, 4u, 0u);
    setGuestRegister(sizeContext, 31u, kReturn);
    size->function(guest.data(), &sizeContext, nullptr);
    test.expect(getRegU32(&sizeContext, 2) == kUnits && sizeContext.pc == kReturn,
                "slot-size query returns exact 16-byte-unit length");

    R5900Context readContext;
    setGuestRegister(readContext, 4u, kDestination);
    setGuestRegister(readContext, 5u, 0u);
    setGuestRegister(readContext, 6u, 0u);
    setGuestRegister(readContext, 7u, 0xffffffffu);
    setGuestRegister(readContext, 8u, 0u);
    setGuestRegister(readContext, 31u, kReturn);
    read->function(guest.data(), &readContext, nullptr);
    test.expect(getRegU32(&readContext, 2) == 0u && readContext.pc == kReturn,
                "full-slot native read matches Retail success result 0");

    bool payloadMatches = true;
    for (std::size_t i = 0u; i < kBytes; ++i) {
        const std::uint8_t expected = static_cast<std::uint8_t>((i * 13u + 7u) & 0xffu);
        if (guest[kDestination + i] != expected) {
            payloadMatches = false;
            break;
        }
    }
    test.expect(payloadMatches,
                "native stash round-trips the original staged bytes after source mutation");

    // Exercise the exact offset/count unit semantics consumed by 0x232F20.
    constexpr std::uint32_t kOffsetUnits = 2u;
    constexpr std::uint32_t kReadUnits = 3u;
    constexpr std::size_t kPartialBytes =
        kReadUnits * Rac1NativeStashContract::kUnitBytes;
    std::fill_n(guest.begin() + kDestination, kPartialBytes, std::uint8_t{0x5au});
    R5900Context partialContext;
    setGuestRegister(partialContext, 4u, kDestination);
    setGuestRegister(partialContext, 5u, 0u);
    setGuestRegister(partialContext, 6u, kOffsetUnits);
    setGuestRegister(partialContext, 7u, kReadUnits);
    setGuestRegister(partialContext, 8u, 0u);
    setGuestRegister(partialContext, 31u, kReturn);
    read->function(guest.data(), &partialContext, nullptr);
    test.expect(getRegU32(&partialContext, 2) == 0u,
                "bounded slot subrange read succeeds");
    bool partialMatches = true;
    for (std::size_t i = 0u; i < kPartialBytes; ++i) {
        const std::size_t sourceIndex =
            kOffsetUnits * Rac1NativeStashContract::kUnitBytes + i;
        const std::uint8_t expected =
            static_cast<std::uint8_t>((sourceIndex * 13u + 7u) & 0xffu);
        if (guest[kDestination + i] != expected) {
            partialMatches = false;
            break;
        }
    }
    test.expect(partialMatches,
                "partial native read uses the exact staged offset/count in 16-byte units");

    R5900Context invalidSlotContext;
    setGuestRegister(invalidSlotContext, 4u,
                     static_cast<std::uint32_t>(Rac1NativeStashContract::kSlotCount));
    setGuestRegister(invalidSlotContext, 31u, kReturn);
    size->function(guest.data(), &invalidSlotContext, nullptr);
    test.expect(static_cast<std::int32_t>(getRegU32(&invalidSlotContext, 2)) ==
                    Rac1NativeStashContract::kInvalidSlot,
                "slot-size query preserves Retail -3 for index >= 64");

    R5900Context outOfBoundsRead;
    setGuestRegister(outOfBoundsRead, 4u, kDestination);
    setGuestRegister(outOfBoundsRead, 5u, 0u);
    setGuestRegister(outOfBoundsRead, 6u, kUnits - 1u);
    setGuestRegister(outOfBoundsRead, 7u, 2u);
    setGuestRegister(outOfBoundsRead, 8u, 0u);
    setGuestRegister(outOfBoundsRead, 31u, kReturn);
    read->function(guest.data(), &outOfBoundsRead, nullptr);
    test.expect(static_cast<std::int32_t>(getRegU32(&outOfBoundsRead, 2)) ==
                    Rac1NativeStashContract::kStoreOutOfSpace,
                "read beyond the reserved slot length preserves Retail -1");

    R5900Context badSourceStore;
    setGuestRegister(badSourceStore, 4u, kGuestBytes - 8u);
    setGuestRegister(badSourceStore, 5u, 1u);
    setGuestRegister(badSourceStore, 6u, 1u);
    setGuestRegister(badSourceStore, 7u, 0u);
    setGuestRegister(badSourceStore, 31u, kReturn);
    store->function(guest.data(), &badSourceStore, nullptr);
    test.expect(static_cast<std::int32_t>(getRegU32(&badSourceStore, 2)) ==
                    Rac1NativeStashContract::kStoreOutOfSpace,
                "out-of-range source fails closed before staging any bytes");

    // Re-init is a real lifecycle reset: the old slot becomes empty again.
    R5900Context resetContext;
    setGuestRegister(resetContext, 31u, kReturn);
    init->function(guest.data(), &resetContext, nullptr);
    R5900Context emptySizeContext;
    setGuestRegister(emptySizeContext, 4u, 0u);
    setGuestRegister(emptySizeContext, 31u, kReturn);
    size->function(guest.data(), &emptySizeContext, nullptr);
    test.expect(getRegU32(&emptySizeContext, 2) == 0u,
                "stash init clears all 64 slot lengths exactly like Retail table initialization");

    if (test.failures != 0) return 1;
    std::cout << "R&C1 native stash contract tests passed\n";
    return 0;
}
