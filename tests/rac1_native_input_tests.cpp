#include "game/rac1_native_input.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
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

void writeGuestU32(std::vector<std::uint8_t>& guest,
                   std::uint32_t address,
                   std::uint32_t value) {
    std::memcpy(guest.data() + address, &value, sizeof(value));
}

void writeGuestFloat(std::vector<std::uint8_t>& guest,
                     std::uint32_t address,
                     float value) {
    std::memcpy(guest.data() + address, &value, sizeof(value));
}

} // namespace

int main() {
    using namespace ratchet::game;
    using ratchet::runtime::NativeReplacementStage;
    TestContext test;

    test.expect(quantizeRac1PadAxis(-1.0f) == 0u,
                "negative full-scale stick maps to retail byte 0");
    test.expect(quantizeRac1PadAxis(0.0f) == 0x7fu,
                "centered stick maps to the parser's exact 0x7F center");
    test.expect(quantizeRac1PadAxis(1.0f) == 0xffu,
                "positive full-scale stick maps to retail byte 255");
    test.expect(quantizeRac1PadAxis(-2.0f) == 0u &&
                    quantizeRac1PadAxis(2.0f) == 0xffu,
                "host stick input is clamped before quantization");

    const Rac1RetailPadReport neutral = encodeRac1RetailPadReport({});
    test.expect(neutral.bytes == Rac1RetailPadReport{}.bytes,
                "neutral native input yields active-low buttons and four centered sticks");

    Rac1NativeInputSample sample;
    sample.pressedButtons =
        rac1PadButtonMask(Rac1PadButton::Cross) |
        rac1PadButtonMask(Rac1PadButton::Start) |
        rac1PadButtonMask(Rac1PadButton::Left);
    sample.rightX = 1.0f;
    sample.rightY = -1.0f;
    sample.leftX = -0.5f;
    sample.leftY = 0.5f;
    const Rac1RetailPadReport report = encodeRac1RetailPadReport(sample);
    const std::uint16_t wire =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(report.bytes[0]) << 8u) |
                                   report.bytes[1]);
    test.expect(static_cast<std::uint16_t>(~wire) == sample.pressedButtons,
                "first two report bytes invert back to the exact Retail logical button mask");
    test.expect(report.bytes[2] == 0xffu && report.bytes[3] == 0u,
                "right-stick bytes occupy parser offsets 2 and 3");
    test.expect(report.bytes[4] < 0x7fu && report.bytes[5] > 0x7fu,
                "left-stick bytes occupy parser offsets 4 and 5");

    publishRac1NativeInputSample(sample);
    test.expect(currentRac1RetailPadReport() == report,
                "published host sample is consumed as one coherent report snapshot");

    ratchet::runtime::NativeReplacementRegistry registry;
    declareRac1NativeInputReplacements(registry);
    test.expect(registry.size(NativeReplacementStage::Runtime) == 4u,
                "Phase-12 input bridge declares exactly four narrow libdbc boundaries");
    test.expect(registry.entries().size() == 4u &&
                    registry.entries()[0].address == Rac1NativeInputContract::kReceiveDataFunction &&
                    registry.entries()[1].address == Rac1NativeInputContract::kConnectionInfoFunction &&
                    registry.entries()[2].address == Rac1NativeInputContract::kDeviceStatusFunction &&
                    registry.entries()[3].address == Rac1NativeInputContract::kAuxInfoFunction,
                "input replacement addresses match the Retail controller call chain");

    if (registry.entries().size() == 4u) {
        constexpr std::uint32_t kOutput = 0x00100000u;
        constexpr std::uint32_t kReturn = 0x00abcdefu;
        std::vector<std::uint8_t> guest(Rac1NativeInputContract::kGuestRamBytes, 0x5au);

        R5900Context statusContext;
        setGuestRegister(statusContext, 31u, kReturn);
        registry.entries()[2].function(guest.data(), &statusContext, nullptr);
        test.expect(getRegU32(&statusContext, 2) == 1u && statusContext.pc == kReturn,
                    "native status selects Retail connected/readable result 1");

        for (std::size_t entryIndex : {1u, 3u}) {
            std::fill_n(guest.begin() + kOutput,
                        Rac1NativeInputContract::kTransitionInfoBytes,
                        std::uint8_t{0x5a});
            R5900Context infoContext;
            setGuestRegister(infoContext, 5u, kOutput);
            setGuestRegister(infoContext, 31u, kReturn);
            registry.entries()[entryIndex].function(guest.data(), &infoContext, nullptr);
            test.expect(getRegU32(&infoContext, 2) == 0u && infoContext.pc == kReturn,
                        "native transition query reports no optional PC metadata bytes");
            test.expect(std::all_of(guest.begin() + kOutput,
                                    guest.begin() + kOutput + Rac1NativeInputContract::kTransitionInfoBytes,
                                    [](std::uint8_t byte) { return byte == 0u; }),
                        "native transition query clears the four-byte caller scratch deterministically");
        }

        std::fill_n(guest.begin() + kOutput, 8u, std::uint8_t{0x5a});
        R5900Context readContext;
        setGuestRegister(readContext, 5u, kOutput);
        setGuestRegister(readContext, 31u, kReturn);
        registry.entries()[0].function(guest.data(), &readContext, nullptr);
        test.expect(getRegU32(&readContext, 2) == Rac1RetailPadReport::kBytes &&
                        readContext.pc == kReturn,
                    "native DBC receive returns the exact six-byte Retail parser report length");
        test.expect(std::equal(report.bytes.begin(), report.bytes.end(), guest.begin() + kOutput),
                    "native DBC receive copies the coherent host report to guest memory");
        test.expect(guest[kOutput + Rac1RetailPadReport::kBytes] == 0x5au,
                    "native DBC receive does not write beyond the six-byte report");

        R5900Context badReadContext;
        setGuestRegister(badReadContext, 5u,
                         Rac1NativeInputContract::kGuestRamBytes - 2u);
        setGuestRegister(badReadContext, 31u, kReturn);
        registry.entries()[0].function(guest.data(), &badReadContext, nullptr);
        test.expect(getRegU32(&badReadContext, 2) == 0u && badReadContext.pc == kReturn,
                    "out-of-range guest report destination fails closed with zero bytes");

        using ParsedLayout = Rac1RetailParsedInputLayout;
        const std::uint32_t state = ParsedLayout::kControllerStateAddress;
        writeGuestFloat(guest, state + ParsedLayout::kRightXOffset, 0.25f);
        writeGuestFloat(guest, state + ParsedLayout::kRightYOffset, -0.5f);
        writeGuestFloat(guest, state + ParsedLayout::kLeftXOffset, 0.75f);
        writeGuestFloat(guest, state + ParsedLayout::kLeftYOffset, -1.0f);
        writeGuestU32(guest, state + ParsedLayout::kCurrentButtonsOffset, 0x00004808u);
        writeGuestU32(guest, state + ParsedLayout::kPressedEdgesOffset, 0x00004000u);
        writeGuestU32(guest, state + ParsedLayout::kReleasedEdgesOffset, 0x00000008u);
        writeGuestU32(guest, state + ParsedLayout::kProcessedPressedEdgesOffset, 0x00000020u);

        const auto parsedRegionBegin = guest.begin() + state;
        const auto parsedRegionEnd = parsedRegionBegin + ParsedLayout::kRequiredBytes;
        const std::vector<std::uint8_t> beforeParsedRead(parsedRegionBegin, parsedRegionEnd);
        const Rac1RetailParsedInputSnapshot parsed = inspectRac1RetailParsedInput(guest);
        test.expect(parsed.status == Rac1RetailParsedInputStatus::Ok && parsed.ok(),
                    "parsed-input inspector accepts the complete Retail controller state");
        test.expect(parsed.currentButtons == 0x00004808u &&
                        parsed.pressedEdges == 0x00004000u &&
                        parsed.releasedEdges == 0x00000008u &&
                        parsed.processedPressedEdges == 0x00000020u,
                    "parsed-input inspector reads Retail current/raw-edge/processed-edge fields exactly");
        test.expect(parsed.rightX == 0.25f && parsed.rightY == -0.5f &&
                        parsed.leftX == 0.75f && parsed.leftY == -1.0f,
                    "parsed-input inspector preserves Retail right-X/right-Y/left-X/left-Y order");
        test.expect(std::equal(beforeParsedRead.begin(), beforeParsedRead.end(), parsedRegionBegin),
                    "parsed-input inspector is strictly read-only");
        test.expect(std::string_view(rac1RetailParsedInputStatusName(parsed.status)) == "ok",
                    "parsed-input status names expose the successful gate explicitly");

        const std::span<const std::uint8_t> truncatedGuest(
            guest.data(),
            static_cast<std::size_t>(state + ParsedLayout::kRequiredBytes - 1u));
        const Rac1RetailParsedInputSnapshot truncated =
            inspectRac1RetailParsedInput(truncatedGuest);
        test.expect(truncated.status == Rac1RetailParsedInputStatus::GuestMemoryTooSmall &&
                        !truncated.ok(),
                    "parsed-input inspector fails closed when the final Retail field is unreadable");
    }

    if (test.failures != 0) return 1;
    std::cout << "R&C1 native input contract tests passed\n";
    return 0;
}
