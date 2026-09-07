#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"
#include "Syscalls/System.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
constexpr std::uint32_t kArgsAddress = 0x00020000u;
constexpr std::uint32_t kStateAddress = 0x00154a50u;
constexpr std::uint32_t kSendBufferAddress = 0x00154a80u;
constexpr std::uint32_t kHandlerAddress = 0x00100100u;
constexpr std::uint32_t kCallerSp = 0x01ffecf0u;

std::vector<std::uint32_t> g_events;
std::uint32_t g_observedCallbackArg = 0u;
std::uint32_t g_observedCallbackSp = 0u;
std::uint32_t g_observedCallbackRa = 0u;
bool g_event4SawDrainedPayload = false;

void setGuestRegister(R5900Context& ctx, std::size_t reg, std::uint32_t value) {
    ctx.r[reg] = _mm_set_epi64x(0, static_cast<std::int64_t>(value));
}

void writeGuestU32(std::uint8_t* rdram, std::uint32_t address, std::uint32_t value) {
    std::memcpy(rdram + (address & PS2_RAM_MASK), &value, sizeof(value));
}

std::uint32_t readGuestU32(const std::uint8_t* rdram, std::uint32_t address) {
    std::uint32_t value = 0u;
    std::memcpy(&value, rdram + (address & PS2_RAM_MASK), sizeof(value));
    return value;
}

void writeArgs(std::uint8_t* rdram,
               std::uint32_t a0,
               std::uint32_t a1 = 0u,
               std::uint32_t a2 = 0u,
               std::uint32_t a3 = 0u) {
    writeGuestU32(rdram, kArgsAddress + 0u, a0);
    writeGuestU32(rdram, kArgsAddress + 4u, a1);
    writeGuestU32(rdram, kArgsAddress + 8u, a2);
    writeGuestU32(rdram, kArgsAddress + 12u, a3);
}

void invokeDeci2(std::uint8_t* rdram,
                 R5900Context& ctx,
                 PS2Runtime& runtime,
                 std::int32_t code) {
    setGuestRegister(ctx, 4u, static_cast<std::uint32_t>(code));
    setGuestRegister(ctx, 5u, kArgsAddress);
    ps2_syscalls::Deci2Call(rdram, &ctx, &runtime);
}

void syntheticRetailDeci2Handler(std::uint8_t* rdram,
                                 R5900Context* ctx,
                                 PS2Runtime* runtime) {
    const std::uint32_t event = getRegU32(ctx, 4u);
    const std::uint32_t state = getRegU32(ctx, 6u);
    g_events.push_back(event);
    g_observedCallbackArg = state;
    g_observedCallbackSp = getRegU32(ctx, 29u);
    g_observedCallbackRa = getRegU32(ctx, 31u);

    if (event == 3u) {
        // Mirror the proved R&C1 event-3 producer/consumer: FUN_00119610 reads
        // state+0/+0x10/+4, calls sceDeci2ExSend, advances the send pointer by
        // the accepted byte count and subtracts that count from state+4.
        const std::uint32_t socket = readGuestU32(rdram, state + 0u);
        const std::uint32_t sendPointer = readGuestU32(rdram, state + 0x10u);
        const std::uint32_t remaining = readGuestU32(rdram, state + 4u);
        writeArgs(rdram, socket, sendPointer, remaining);
        invokeDeci2(rdram, *ctx, *runtime, -6);

        const std::int32_t accepted = static_cast<std::int32_t>(getRegU32(ctx, 2u));
        if (accepted >= 0 && static_cast<std::uint32_t>(accepted) <= remaining) {
            const std::uint32_t acceptedBytes = static_cast<std::uint32_t>(accepted);
            writeGuestU32(rdram, state + 0x10u, sendPointer + acceptedBytes);
            writeGuestU32(rdram, state + 4u, remaining - acceptedBytes);
        }
    }
    else if (event == 4u) {
        g_event4SawDrainedPayload = readGuestU32(rdram, state + 4u) == 0u;
        // Exact Retail completion effect at FUN_00119610+0x178 (0x119788).
        writeGuestU32(rdram, state + 0x0cu, 0u);
    }

    ctx->pc = 0u;
}

int fail(const char* message) {
    std::cerr << "PS2Recomp DECI2 callback regression failed: " << message << '\n';
    return 1;
}
} // namespace

int main() {
    std::vector<std::uint8_t> rdram(PS2_RAM_SIZE, 0u);
    PS2Runtime runtime;
    if (!runtime.registerFunction(kHandlerAddress, &syntheticRetailDeci2Handler)) {
        return fail("could not register synthetic guest DECI2 handler");
    }

    R5900Context ctx{};
    setGuestRegister(ctx, 29u, kCallerSp);
    ctx.pc = 0x00118b90u;

    // R&C1 sub_001199C8 opens protocol 0x210 with a1=0x154A50 and handler
    // 0x119610. The Retail handler immediately copies callback a2 into s1 and
    // dereferences it as that state object, proving open argument 1 is callback a2.
    writeArgs(rdram.data(), 0x210u, kStateAddress, kHandlerAddress, 0x20154910u);
    setGuestRegister(ctx, 2u, 0x7f7f7f7fu);
    invokeDeci2(rdram.data(), ctx, runtime, 1);
    const std::int32_t socket = static_cast<std::int32_t>(getRegU32(&ctx, 2u));
    if (socket <= 0) return fail("open did not return a valid socket");

    constexpr char kPayload[] = "OR-DECI2";
    std::memcpy(rdram.data() + kSendBufferAddress, kPayload, sizeof(kPayload) - 1u);
    const std::uint32_t payloadBytes = static_cast<std::uint32_t>(sizeof(kPayload) - 1u);
    writeGuestU32(rdram.data(), kStateAddress + 0u, static_cast<std::uint32_t>(socket));
    writeGuestU32(rdram.data(), kStateAddress + 4u, payloadBytes);
    writeGuestU32(rdram.data(), kStateAddress + 0x0cu, 1u);
    writeGuestU32(rdram.data(), kStateAddress + 0x10u, kSendBufferAddress);

    // FUN_001197A8 sets state+0xC=1, requests send, then polls until Retail
    // event 4 clears exactly that word. A runtime that returns success without
    // dispatching the registered handler deadlocks at guest PC 0x1198B0.
    writeArgs(rdram.data(), static_cast<std::uint32_t>(socket), 0x48u);
    invokeDeci2(rdram.data(), ctx, runtime, 3);
    if (static_cast<std::int32_t>(getRegU32(&ctx, 2u)) < 0)
        return fail("sceDeci2ReqSend failed");
    if (g_events.size() != 1u || g_events[0] != 3u)
        return fail("request-send did not dispatch Retail event 3 exactly once");
    if (readGuestU32(rdram.data(), kStateAddress + 4u) != 0u)
        return fail("event 3 did not drain the Retail-shaped send payload through ExSend");
    if (readGuestU32(rdram.data(), kStateAddress + 0x10u) !=
        kSendBufferAddress + payloadBytes)
        return fail("event 3 did not advance the Retail send pointer by accepted bytes");

    writeArgs(rdram.data(), static_cast<std::uint32_t>(socket));
    invokeDeci2(rdram.data(), ctx, runtime, 4);
    if (static_cast<std::int32_t>(getRegU32(&ctx, 2u)) < 0)
        return fail("sceDeci2Poll failed");
    if (g_events.size() != 2u || g_events[1] != 4u)
        return fail("poll did not dispatch Retail completion event 4 exactly once");
    if (!g_event4SawDrainedPayload)
        return fail("event 4 ran before the send payload reached zero");
    if (readGuestU32(rdram.data(), kStateAddress + 0x0cu) != 0u)
        return fail("Retail completion event did not clear state+0xC");

    if (g_observedCallbackArg != kStateAddress)
        return fail("DECI2 callback a2 was not the open-time opt/state pointer");
    if (g_observedCallbackSp != kCallerSp)
        return fail("synchronous DECI2 callback did not inherit the interrupted guest SP");
    if (g_observedCallbackRa != 0u)
        return fail("DECI2 callback return sentinel was not zero");

    // A second poll after completion must not redispatch event 4.
    invokeDeci2(rdram.data(), ctx, runtime, 4);
    if (g_events.size() != 2u)
        return fail("completed send was redispatched on a later poll");

    writeArgs(rdram.data(), static_cast<std::uint32_t>(socket));
    invokeDeci2(rdram.data(), ctx, runtime, 2);
    if (static_cast<std::int32_t>(getRegU32(&ctx, 2u)) < 0)
        return fail("sceDeci2Close failed");

    std::cout << "PS2Recomp DECI2 registered-handler completion regression passed\n";
    return 0;
}
