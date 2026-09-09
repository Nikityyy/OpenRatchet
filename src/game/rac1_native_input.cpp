#include "game/rac1_native_input.h"

#include "runtime/native_replacements.h"

#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

using Contract = Rac1NativeInputContract;

constexpr std::uint64_t packReport(const Rac1RetailPadReport& report) noexcept {
    std::uint64_t packed = 0u;
    for (std::size_t i = 0u; i < report.bytes.size(); ++i) {
        packed |= static_cast<std::uint64_t>(report.bytes[i]) << (i * 8u);
    }
    return packed;
}

constexpr Rac1RetailPadReport unpackReport(std::uint64_t packed) noexcept {
    Rac1RetailPadReport report;
    for (std::size_t i = 0u; i < report.bytes.size(); ++i) {
        report.bytes[i] = static_cast<std::uint8_t>((packed >> (i * 8u)) & 0xffu);
    }
    return report;
}

constexpr Rac1RetailPadReport kNeutralReport{};
std::atomic<std::uint64_t> g_report{packReport(kNeutralReport)};
std::atomic<std::uint64_t> g_lastLoggedReport{~packReport(kNeutralReport)};
std::atomic<bool> g_statusLogged{false};
std::atomic<bool> g_transitionLogged{false};

void returnToGuestCaller(R5900Context* ctx, std::uint32_t result) {
    SET_GPR_U32(ctx, 2, result);
    ctx->pc = GPR_U32(ctx, 31);
}

bool guestRangeValid(std::uint32_t address, std::size_t size) noexcept {
    return address <= Contract::kGuestRamBytes &&
           size <= static_cast<std::size_t>(Contract::kGuestRamBytes - address);
}

std::uint32_t readGuestLe32(std::span<const std::uint8_t> guestRdram,
                            std::uint32_t address) noexcept {
    return static_cast<std::uint32_t>(guestRdram[address + 0u]) |
           (static_cast<std::uint32_t>(guestRdram[address + 1u]) << 8u) |
           (static_cast<std::uint32_t>(guestRdram[address + 2u]) << 16u) |
           (static_cast<std::uint32_t>(guestRdram[address + 3u]) << 24u);
}

float readGuestLeFloat(std::span<const std::uint8_t> guestRdram,
                       std::uint32_t address) noexcept {
    return std::bit_cast<float>(readGuestLe32(guestRdram, address));
}

void nativeControllerStatus(std::uint8_t*, R5900Context* ctx, PS2Runtime*) {
    if (!g_statusLogged.exchange(true, std::memory_order_relaxed)) {
        std::cerr << "[OpenRatchet:input] component=device-status"
                  << " source=native-host connected=1"
                  << " retailResult=1 sifBypass=1 status=ok\n";
    }

    // sub_002170C8 accepts exactly status 1 as the connected/readable path.
    // A PC keyboard is always present; gamepad input is merged when available.
    returnToGuestCaller(ctx, 1u);
}

void nativeControllerTransitionInfo(std::uint8_t* rdram,
                                    R5900Context* ctx,
                                    PS2Runtime*,
                                    const char* component) {
    const std::uint32_t output = GPR_U32(ctx, 5);
    if (!guestRangeValid(output, Contract::kTransitionInfoBytes)) {
        std::cerr << "[OpenRatchet:input] component=" << component
                  << " source=native-host output=0x" << std::hex << output << std::dec
                  << " status=guest-range-error\n";
        returnToGuestCaller(ctx, 0u);
        return;
    }

    // The original controller state machine copies a four-byte transition
    // scratch area even when the transport reports no optional metadata. Clear
    // that caller-provided scratch deterministically, but report zero logical
    // metadata bytes. This is host capability absence, not fabricated pad input.
    std::fill_n(rdram + output, Contract::kTransitionInfoBytes, std::uint8_t{0});
    if (!g_transitionLogged.exchange(true, std::memory_order_relaxed)) {
        std::cerr << "[OpenRatchet:input] component=connection-transition"
                  << " source=native-host optionalMetadataBytes=0"
                  << " scratchBytes=" << Contract::kTransitionInfoBytes
                  << " sifBypass=1 status=ok\n";
    }
    returnToGuestCaller(ctx, 0u);
}

void nativeControllerConnectionInfo(std::uint8_t* rdram,
                                    R5900Context* ctx,
                                    PS2Runtime* runtime) {
    nativeControllerTransitionInfo(rdram, ctx, runtime, "connection-info");
}

void nativeControllerAuxInfo(std::uint8_t* rdram,
                             R5900Context* ctx,
                             PS2Runtime* runtime) {
    nativeControllerTransitionInfo(rdram, ctx, runtime, "aux-info");
}

void nativeControllerReceiveData(std::uint8_t* rdram,
                                 R5900Context* ctx,
                                 PS2Runtime*) {
    const std::uint32_t output = GPR_U32(ctx, 5);
    constexpr std::size_t size = Rac1RetailPadReport::kBytes;
    if (!guestRangeValid(output, size)) {
        std::cerr << "[OpenRatchet:input] component=read"
                  << " source=native-host output=0x" << std::hex << output << std::dec
                  << " reportBytes=" << size
                  << " status=guest-range-error\n";
        returnToGuestCaller(ctx, 0u);
        return;
    }

    const std::uint64_t packed = g_report.load(std::memory_order_acquire);
    const Rac1RetailPadReport report = unpackReport(packed);
    std::memcpy(rdram + output, report.bytes.data(), report.bytes.size());

    const std::uint64_t previous = g_lastLoggedReport.exchange(
        packed, std::memory_order_relaxed);
    if (previous != packed) {
        const std::uint16_t wireButtons =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(report.bytes[0]) << 8u) |
                                       report.bytes[1]);
        const std::uint16_t pressed = static_cast<std::uint16_t>(~wireButtons);
        std::cerr << "[OpenRatchet:input] component=read"
                  << " source=native-host reportBytes=" << report.bytes.size()
                  << " pressed=0x" << std::hex << pressed << std::dec
                  << " rx=" << static_cast<unsigned>(report.bytes[2])
                  << " ry=" << static_cast<unsigned>(report.bytes[3])
                  << " lx=" << static_cast<unsigned>(report.bytes[4])
                  << " ly=" << static_cast<unsigned>(report.bytes[5])
                  << " pressure=deferred"
                  << " parser=retail-FUN_00217328"
                  << " sifBypass=1 status=ok\n";
    }

    returnToGuestCaller(ctx, static_cast<std::uint32_t>(report.bytes.size()));
}

} // namespace

std::uint8_t quantizeRac1PadAxis(float value) noexcept {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    const float scaled = clamped >= 0.0f
        ? 127.0f + clamped * 128.0f
        : 127.0f + clamped * 127.0f;
    const long rounded = std::lround(scaled);
    return static_cast<std::uint8_t>(std::clamp(rounded, 0l, 255l));
}

Rac1RetailPadReport encodeRac1RetailPadReport(
    const Rac1NativeInputSample& sample) noexcept {
    Rac1RetailPadReport report;
    const std::uint16_t activeLowButtons =
        static_cast<std::uint16_t>(~sample.pressedButtons);
    report.bytes[0] = static_cast<std::uint8_t>(activeLowButtons >> 8u);
    report.bytes[1] = static_cast<std::uint8_t>(activeLowButtons & 0xffu);
    report.bytes[2] = quantizeRac1PadAxis(sample.rightX);
    report.bytes[3] = quantizeRac1PadAxis(sample.rightY);
    report.bytes[4] = quantizeRac1PadAxis(sample.leftX);
    report.bytes[5] = quantizeRac1PadAxis(sample.leftY);
    return report;
}

void publishRac1NativeInputSample(const Rac1NativeInputSample& sample) noexcept {
    g_report.store(packReport(encodeRac1RetailPadReport(sample)),
                   std::memory_order_release);
}

Rac1RetailPadReport currentRac1RetailPadReport() noexcept {
    return unpackReport(g_report.load(std::memory_order_acquire));
}

Rac1RetailParsedInputSnapshot inspectRac1RetailParsedInput(
    std::span<const std::uint8_t> guestRdram) noexcept {
    using Layout = Rac1RetailParsedInputLayout;

    Rac1RetailParsedInputSnapshot result;
    constexpr std::uint64_t requiredEnd =
        static_cast<std::uint64_t>(Layout::kControllerStateAddress) +
        static_cast<std::uint64_t>(Layout::kRequiredBytes);
    if (requiredEnd > guestRdram.size()) {
        return result;
    }

    const std::uint32_t state = Layout::kControllerStateAddress;
    result.rightX = readGuestLeFloat(guestRdram, state + Layout::kRightXOffset);
    result.rightY = readGuestLeFloat(guestRdram, state + Layout::kRightYOffset);
    result.leftX = readGuestLeFloat(guestRdram, state + Layout::kLeftXOffset);
    result.leftY = readGuestLeFloat(guestRdram, state + Layout::kLeftYOffset);
    result.currentButtons = readGuestLe32(guestRdram, state + Layout::kCurrentButtonsOffset);
    result.pressedEdges = readGuestLe32(guestRdram, state + Layout::kPressedEdgesOffset);
    result.releasedEdges = readGuestLe32(guestRdram, state + Layout::kReleasedEdgesOffset);
    result.processedPressedEdges =
        readGuestLe32(guestRdram, state + Layout::kProcessedPressedEdgesOffset);
    result.status = Rac1RetailParsedInputStatus::Ok;
    return result;
}

const char* rac1RetailParsedInputStatusName(
    Rac1RetailParsedInputStatus status) noexcept {
    switch (status) {
    case Rac1RetailParsedInputStatus::Ok: return "ok";
    case Rac1RetailParsedInputStatus::GuestMemoryTooSmall: return "guest-memory-too-small";
    }
    return "unknown";
}

void declareRac1NativeInputReplacements(
    runtime::NativeReplacementRegistry& registry) {
    using runtime::NativeReplacementStage;

    registry.add(Contract::kReceiveDataFunction,
                 "native.input.dbc-receive-data",
                 NativeReplacementStage::Runtime,
                 nativeControllerReceiveData);
    registry.add(Contract::kConnectionInfoFunction,
                 "native.input.dbc-connection-info",
                 NativeReplacementStage::Runtime,
                 nativeControllerConnectionInfo);
    registry.add(Contract::kDeviceStatusFunction,
                 "native.input.dbc-device-status",
                 NativeReplacementStage::Runtime,
                 nativeControllerStatus);
    registry.add(Contract::kAuxInfoFunction,
                 "native.input.dbc-aux-info",
                 NativeReplacementStage::Runtime,
                 nativeControllerAuxInfo);
}

} // namespace ratchet::game
