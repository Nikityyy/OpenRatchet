#include "game/rac1_live_sky.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

struct TestState {
    int failures = 0;
    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void writeU16(std::vector<std::uint8_t>& bytes, std::uint32_t address, std::uint16_t value) {
    std::memcpy(bytes.data() + address, &value, sizeof(value));
}

void writeU32(std::vector<std::uint8_t>& bytes, std::uint32_t address, std::uint32_t value) {
    std::memcpy(bytes.data() + address, &value, sizeof(value));
}

void writeFloat(std::vector<std::uint8_t>& bytes, std::uint32_t address, float value) {
    std::memcpy(bytes.data() + address, &value, sizeof(value));
}

void seedMaterializedSky(std::vector<std::uint8_t>& guest) {
    using Layout = ratchet::game::Rac1LiveSkyLayout;
    constexpr std::uint32_t kSky = 0x00500000u;
    writeU32(guest, Layout::kSkyPointerAddress, kSky);
    writeU16(guest, kSky + Layout::kShellCountOffset, 5u);
    constexpr std::array<std::int32_t, 5> kClusterCounts{{38, 25, 7, 24, 24}};
    constexpr std::array<std::int32_t, 5> kFlags{{1, 0, 0, 0, 0}};
    for (std::uint32_t shell = 0u; shell < 5u; ++shell) {
        const std::uint32_t shellAddress = kSky + 0x100u + shell * 0x40u;
        writeU32(guest,
                 kSky + Layout::kShellPointerTableOffset + shell * sizeof(std::uint32_t),
                 shellAddress);
        writeU32(guest, shellAddress + 0x00u,
                 static_cast<std::uint32_t>(kClusterCounts[shell]));
        writeU32(guest, shellAddress + 0x04u,
                 static_cast<std::uint32_t>(kFlags[shell]));
    }
    writeFloat(guest, Layout::kBaseAngleAddress, 0.25f);
    writeFloat(guest, Layout::kTranslationAddress + 0u, 10.0f);
    writeFloat(guest, Layout::kTranslationAddress + 4u, 20.0f);
    writeFloat(guest, Layout::kTranslationAddress + 8u, 30.0f);
    writeFloat(guest, Layout::kTranslationAddress + 12u, 1.0f);

    // The transient 0x1D96E0 matrix is intentionally garbage. The inspector
    // must reconstruct FUN_0022B288 from its live inputs, not sample scratch.
    for (std::uint32_t i = 0u; i < 64u; ++i) {
        guest[Layout::kObjectMatrixScratchAddress + i] =
            static_cast<std::uint8_t>(0xa5u ^ i);
    }
}

bool matrixBitsEqual(const std::array<float, 16>& actual,
                     const std::array<std::uint32_t, 16>& expected) {
    for (std::size_t i = 0u; i < actual.size(); ++i) {
        if (std::bit_cast<std::uint32_t>(actual[i]) != expected[i]) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace ratchet::game;
    TestState test;

    static_assert(Rac1LiveSkyLayout::kBaseAngleAddress == 0x00160404u);
    static_assert(Rac1LiveSkyLayout::kSkyPointerAddress == 0x0016045cu);
    static_assert(Rac1LiveSkyLayout::kTranslationAddress == 0x00160460u);
    static_assert(Rac1LiveSkyLayout::kObjectMatrixScratchAddress == 0x001d96e0u);

    std::vector<std::uint8_t> guest(32u * 1024u * 1024u, 0u);
    seedMaterializedSky(guest);
    const auto live = inspectRac1LiveSky(guest);
    test.expect(live.ok() && live.identityMaterialized && live.sky.shellCount == 5u,
                "materialized Retail sky identity decodes five shells");
    test.expect(live.sky.baseAngle == 0.25f && live.sky.translation[0] == 10.0f &&
                    live.sky.translation[1] == 20.0f && live.sky.translation[2] == 30.0f &&
                    live.sky.translation[3] == 1.0f,
                "live sky angle and translation come from proved Retail globals");
    constexpr std::array<std::int32_t, 5> kExpectedClusters{{38, 25, 7, 24, 24}};
    constexpr std::array<std::int32_t, 5> kExpectedFlags{{1, 0, 0, 0, 0}};
    for (std::size_t shell = 0u; shell < kExpectedClusters.size(); ++shell) {
        test.expect(live.sky.shellClusterCounts[shell] == kExpectedClusters[shell] &&
                        live.sky.shellFlags[shell] == kExpectedFlags[shell],
                    "live shell provenance preserves Retail cluster-count/flags header");
    }

    constexpr std::array<std::array<std::uint32_t, 16>, 5> kExpected{{
        {{0x3f780aa6u,0x3e7d5776u,0x00000000u,0x00000000u,
          0xbe7d5776u,0x3f780aa6u,0x00000000u,0x00000000u,
          0x00000000u,0x00000000u,0x3f800000u,0x00000000u,
          0x41200000u,0x41a00000u,0x41f00000u,0x3f800000u}},
        {{0x3f780aa6u,0x3e7d5776u,0x00000000u,0x00000000u,
          0xbe7d5776u,0x3f780aa6u,0x00000000u,0x00000000u,
          0x00000000u,0x00000000u,0x3f800000u,0x00000000u,
          0x41200000u,0x41a00000u,0x41f00000u,0x3f800000u}},
        {{0x3f9ec0cdu,0x3dff92d2u,0x3dbedc9eu,0x00000000u,
          0xbdfedae5u,0x3f9f335fu,0xbc19334eu,0x00000000u,
          0xbdbfd1f0u,0x00000000u,0x3f9f8cdau,0x00000000u,
          0x41200000u,0x41a00000u,0x41f00000u,0x3f800000u}},
        {{0x3fb26f28u,0x3f0ca60eu,0xbd8eddcfu,0x00000000u,
          0xbf0c790fu,0x3fb2a850u,0x3ce0f1b1u,0x00000000u,
          0x3d998938u,0x00000000u,0x3fbfc294u,0x00000000u,
          0x41200000u,0x41a00000u,0x41f00000u,0x3f800000u}},
        {{0x3fda702cu,0x3eb201fbu,0xbe2f55dau,0x00000000u,
          0xbeb11e52u,0x3fdb88f0u,0x3d0e2b41u,0x00000000u,
          0x3e32e6c7u,0x00000000u,0x3fdee185u,0x00000000u,
          0x41200000u,0x41a00000u,0x41f00000u,0x3f800000u}},
    }};
    for (std::size_t shell = 0u; shell < kExpected.size(); ++shell) {
        test.expect(matrixBitsEqual(live.sky.shellObjectMatrices[shell], kExpected[shell]),
                    "FUN_0022B288/FUN_001FA070 shell matrix matches independent bit oracle");
    }
    test.expect(live.sky.shellObjectMatrices[0] == live.sky.shellObjectMatrices[1],
                "Retail shell-0 jump-table fallthrough makes shell 0 and 1 transforms identical");

    auto noPointer = guest;
    writeU32(noPointer, Rac1LiveSkyLayout::kSkyPointerAddress, 0u);
    const auto deferredPointer = inspectRac1LiveSky(noPointer);
    test.expect(deferredPointer.status == Rac1LiveSkyStatus::SkyPointerNotMaterialized &&
                    rac1LiveSkyStatusIsDeferred(deferredPointer.status),
                "zero live sky pointer remains explicit deferred state");

    auto zeroTranslation = guest;
    constexpr std::uint32_t kSky = 0x00500000u;
    writeU16(zeroTranslation, kSky + Rac1LiveSkyLayout::kShellCountOffset, 4u);
    for (std::uint32_t byte = 0u; byte < 16u; ++byte) {
        zeroTranslation[Rac1LiveSkyLayout::kTranslationAddress + byte] = 0u;
    }
    const auto deferredTranslation = inspectRac1LiveSky(zeroTranslation);
    test.expect(deferredTranslation.status == Rac1LiveSkyStatus::TranslationNotMaterialized &&
                    deferredTranslation.identityMaterialized &&
                    deferredTranslation.sky.shellCount == 4u &&
                    deferredTranslation.sky.shellClusterCounts[0] == 38 &&
                    deferredTranslation.sky.shellFlags[0] == 1 &&
                    deferredTranslation.sky.shellClusterCounts[3] == 24 &&
                    deferredTranslation.sky.shellFlags[3] == 0 &&
                    rac1LiveSkyStatusIsDeferred(deferredTranslation.status),
                "four-shell deferred sky preserves all available shell provenance");

    auto wrongW = guest;
    writeFloat(wrongW, Rac1LiveSkyLayout::kTranslationAddress + 12u, 0.5f);
    test.expect(inspectRac1LiveSky(wrongW).status == Rac1LiveSkyStatus::InvalidTranslationW,
                "Retail sky translation W must remain exactly one");

    auto badAngle = guest;
    writeFloat(badAngle, Rac1LiveSkyLayout::kBaseAngleAddress,
               std::numeric_limits<float>::quiet_NaN());
    test.expect(inspectRac1LiveSky(badAngle).status == Rac1LiveSkyStatus::NonFiniteBaseAngle,
                "nonfinite Retail sky angle is rejected");

    auto badTranslation = guest;
    writeFloat(badTranslation, Rac1LiveSkyLayout::kTranslationAddress,
               std::numeric_limits<float>::infinity());
    test.expect(inspectRac1LiveSky(badTranslation).status == Rac1LiveSkyStatus::NonFiniteTranslation,
                "nonfinite Retail sky translation is rejected");

    auto tooManyShells = guest;
    writeU16(tooManyShells, kSky + Rac1LiveSkyLayout::kShellCountOffset, 7u);
    test.expect(inspectRac1LiveSky(tooManyShells).status == Rac1LiveSkyStatus::UnsupportedShellCount,
                "shell counts outside the six proved transform cases are rejected");

    auto badShellPointer = guest;
    writeU32(badShellPointer,
             kSky + Rac1LiveSkyLayout::kShellPointerTableOffset + 2u * sizeof(std::uint32_t),
             0u);
    test.expect(inspectRac1LiveSky(badShellPointer).status == Rac1LiveSkyStatus::ShellPointerOutOfRange,
                "unrelocated live shell pointer is rejected");

    if (test.failures != 0) {
        std::cerr << test.failures << " live sky test(s) failed\n";
        return 1;
    }
    std::cout << "rac1_live_sky_tests: ok\n";
    return 0;
}
