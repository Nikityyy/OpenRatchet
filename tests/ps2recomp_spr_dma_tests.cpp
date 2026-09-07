#include "runtime/ps2_memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t kDctrl = 0x1000E000u;
constexpr uint32_t kDstat = 0x1000E010u;

constexpr uint32_t kFromSprChcr = 0x1000D000u;
constexpr uint32_t kFromSprMadr = 0x1000D010u;
constexpr uint32_t kFromSprQwc = 0x1000D020u;
constexpr uint32_t kFromSprSadr = 0x1000D080u;

constexpr uint32_t kToSprChcr = 0x1000D400u;
constexpr uint32_t kToSprMadr = 0x1000D410u;
constexpr uint32_t kToSprQwc = 0x1000D420u;
constexpr uint32_t kToSprSadr = 0x1000D480u;

bool hasCause(const std::vector<uint32_t> &causes, uint32_t cause)
{
    return std::find(causes.begin(), causes.end(), cause) != causes.end();
}

template <size_t N>
bool bytesEqual(const uint8_t *actual, const std::array<uint8_t, N> &expected)
{
    return std::memcmp(actual, expected.data(), expected.size()) == 0;
}

template <size_t N>
std::array<uint8_t, N> pattern(uint8_t seed)
{
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = static_cast<uint8_t>((static_cast<uint32_t>(seed) +
                                       static_cast<uint32_t>(i * 37u) +
                                       static_cast<uint32_t>((i >> 3u) * 11u)) &
                                      0xFFu);
    }
    return out;
}

int fail(const char *message)
{
    std::cerr << "PS2Recomp SPR DMA regression failed: " << message << '\n';
    return 1;
}
} // namespace

int main()
{
    PS2Memory memory;
    if (!memory.initialize())
    {
        return fail("PS2Memory initialization failed");
    }

    memory.writeIORegister(kDctrl, 1u);

    // Retail sub_00235BE8 uses channel 9 (TO_SPR), normal mode, to stage
    // 32-byte records from RDRAM into scratchpad offset 0x2000.
    constexpr uint32_t kToSprSource = 0x00120000u;
    constexpr uint32_t kToSprScratch = 0x2000u;
    constexpr uint32_t kToSprQwcValue = 0x100u;
    constexpr uint32_t kToSprBytes = kToSprQwcValue * 16u;
    const auto toSprPayload = pattern<kToSprBytes>(0x29u);
    std::memcpy(memory.getRDRAM() + kToSprSource, toSprPayload.data(), toSprPayload.size());

    memory.writeIORegister(kToSprSadr, kToSprScratch);
    memory.writeIORegister(kToSprMadr, kToSprSource);
    memory.writeIORegister(kToSprQwc, kToSprQwcValue);
    memory.writeIORegister(kToSprChcr, 0x100u);

    if (!bytesEqual(memory.getScratchpad() + kToSprScratch, toSprPayload))
        return fail("channel 9 did not copy RDRAM bytes into scratchpad");
    if (memory.readIORegister(kToSprQwc) != 0u)
        return fail("channel 9 QWC did not reach zero");
    if ((memory.readIORegister(kToSprChcr) & 0x100u) != 0u)
        return fail("channel 9 STR remained set after synchronous completion");
    if (memory.readIORegister(kToSprMadr) != kToSprSource + kToSprBytes)
        return fail("channel 9 MADR did not advance by the transferred byte count");
    if (memory.readIORegister(kToSprSadr) != kToSprScratch + kToSprBytes)
        return fail("channel 9 SADR did not advance by the transferred byte count");
    if ((memory.readIORegister(kDstat) & (1u << 9u)) == 0u)
        return fail("channel 9 completion did not raise D_STAT");
    if (!hasCause(memory.consumeCompletedDmacCauses(), 9u))
        return fail("channel 9 completion cause was not queued");

    // Retail FUN_001F9928 uses channel 8 (FROM_SPR), normal mode, to copy a
    // 0x40-qword result from scratchpad offset 0x3600 back to RDRAM 0x1E3200.
    constexpr uint32_t kFromSprDestination = 0x001E3200u;
    constexpr uint32_t kFromSprScratch = 0x3600u;
    constexpr uint32_t kFromSprQwcValue = 0x40u;
    constexpr uint32_t kFromSprBytes = kFromSprQwcValue * 16u;
    const auto fromSprPayload = pattern<kFromSprBytes>(0x91u);
    std::memcpy(memory.getScratchpad() + kFromSprScratch, fromSprPayload.data(), fromSprPayload.size());
    std::memset(memory.getRDRAM() + kFromSprDestination, 0, kFromSprBytes);
    memory.registerCodeRegion(kFromSprDestination, kFromSprDestination + kFromSprBytes);

    memory.writeIORegister(kFromSprSadr, kFromSprScratch);
    memory.writeIORegister(kFromSprMadr, kFromSprDestination);
    memory.writeIORegister(kFromSprQwc, kFromSprQwcValue);
    memory.writeIORegister(kFromSprChcr, 0x100u);

    if (!bytesEqual(memory.getRDRAM() + kFromSprDestination, fromSprPayload))
        return fail("channel 8 did not copy scratchpad bytes into RDRAM");
    if (memory.readIORegister(kFromSprQwc) != 0u)
        return fail("channel 8 QWC did not reach zero");
    if ((memory.readIORegister(kFromSprChcr) & 0x100u) != 0u)
        return fail("channel 8 STR remained set after synchronous completion");
    if (memory.readIORegister(kFromSprMadr) != kFromSprDestination + kFromSprBytes)
        return fail("channel 8 MADR did not advance by the transferred byte count");
    if (memory.readIORegister(kFromSprSadr) != kFromSprScratch + kFromSprBytes)
        return fail("channel 8 SADR did not advance by the transferred byte count");
    if ((memory.readIORegister(kDstat) & (1u << 8u)) == 0u)
        return fail("channel 8 completion did not raise D_STAT");
    if (!hasCause(memory.consumeCompletedDmacCauses(), 8u))
        return fail("channel 8 completion cause was not queued");
    if (!memory.isCodeModified(kFromSprDestination, kFromSprBytes))
        return fail("channel 8 direct RDRAM write was not marked modified");

    // SPR SADR is a 14-bit offset. Prove the data move itself wraps exactly at
    // the 16 KiB scratchpad boundary rather than overrunning or truncating.
    constexpr uint32_t kWrapSource = 0x00124000u;
    constexpr uint32_t kWrapScratch = 0x3FF0u;
    constexpr uint32_t kWrapQwc = 2u;
    const auto wrapPayload = pattern<32>(0xC3u);
    std::memcpy(memory.getRDRAM() + kWrapSource, wrapPayload.data(), wrapPayload.size());
    std::memset(memory.getScratchpad(), 0, PS2_SCRATCHPAD_SIZE);

    memory.writeIORegister(kToSprSadr, kWrapScratch);
    memory.writeIORegister(kToSprMadr, kWrapSource);
    memory.writeIORegister(kToSprQwc, kWrapQwc);
    memory.writeIORegister(kToSprChcr, 0x100u);

    if (std::memcmp(memory.getScratchpad() + 0x3FF0u, wrapPayload.data(), 16u) != 0)
        return fail("channel 9 wrap lost the tail at scratchpad 0x3FF0");
    if (std::memcmp(memory.getScratchpad(), wrapPayload.data() + 16u, 16u) != 0)
        return fail("channel 9 wrap lost the head at scratchpad 0x0000");
    if (memory.readIORegister(kToSprSadr) != 0x0010u)
        return fail("channel 9 wrapped SADR is not 0x0010");
    if (!hasCause(memory.consumeCompletedDmacCauses(), 9u))
        return fail("wrapped channel 9 completion cause was not queued");

    std::cout << "PS2Recomp synchronous normal SPR DMA regression passed\n";
    return 0;
}
