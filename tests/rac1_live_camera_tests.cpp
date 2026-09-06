#include "game/rac1_live_camera.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using Layout = ratchet::game::Rac1LiveCameraLayout;

static_assert(Layout::kStateBase == 0x00186f40u);
static_assert(Layout::kStateBytes == 0x03a0u);
static_assert(Layout::kClipXOffset == 0x0100u);
static_assert(Layout::kClipYOffset == 0x0110u);
static_assert(Layout::kClipZOffset == 0x0120u);
static_assert(Layout::kClipWOffset == 0x0130u);
static_assert(Layout::kWorldPositionOffset == 0x0140u);
static_assert(Layout::kStateBase + Layout::kWorldPositionOffset == 0x00187080u);
static_assert(Layout::kOrientationXOffset == 0x0350u);
static_assert(Layout::kOrientationYOffset == 0x0360u);
static_assert(Layout::kOrientationZOffset == 0x0370u);
static_assert(Layout::kStateBase + Layout::kOrientationXOffset == 0x00187290u);
static_assert(Layout::kStateBase + Layout::kOrientationYOffset == 0x001872a0u);
static_assert(Layout::kStateBase + Layout::kOrientationZOffset == 0x001872b0u);

void writeFloat(std::vector<std::uint8_t>& ram, std::uint32_t address, float value) {
    std::memcpy(ram.data() + address, &value, sizeof(value));
}

template <std::size_t N>
void writeVector(std::vector<std::uint8_t>& ram,
                 std::uint32_t address,
                 const std::array<float, N>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        writeFloat(ram,
                   address + static_cast<std::uint32_t>(index * sizeof(float)),
                   values[index]);
    }
}

struct Fixture {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(32u * 1024u * 1024u);

    Fixture() {
        const std::uint32_t base = Layout::kStateBase;
        writeVector(ram, base + Layout::kWorldPositionOffset,
                    std::array<float, 3>{100.0f, 200.0f, 300.0f});
        writeVector(ram, base + Layout::kOrientationXOffset,
                    std::array<float, 3>{1.0f, 0.0f, 0.0f});
        writeVector(ram, base + Layout::kOrientationYOffset,
                    std::array<float, 3>{0.0f, 1.0f, 0.0f});
        writeVector(ram, base + Layout::kOrientationZOffset,
                    std::array<float, 3>{0.0f, 0.0f, 1.0f});

        // Deliberately non-symmetric vectors. FUN_0022BF94 proves the exact
        // combination clipX*x + clipY*y + clipZ*z + clipW*w.
        writeVector(ram, base + Layout::kClipXOffset,
                    std::array<float, 4>{1.0f, 2.0f, 3.0f, 4.0f});
        writeVector(ram, base + Layout::kClipYOffset,
                    std::array<float, 4>{5.0f, 6.0f, 7.0f, 8.0f});
        writeVector(ram, base + Layout::kClipZOffset,
                    std::array<float, 4>{9.0f, 10.0f, 11.0f, 12.0f});
        writeVector(ram, base + Layout::kClipWOffset,
                    std::array<float, 4>{13.0f, 14.0f, 15.0f, 16.0f});
    }
};

void testRetailAddressesAndExactClipCombination() {
    const Fixture fixture;
    const auto result = ratchet::game::inspectRac1LiveCamera(fixture.ram);
    assert(result.ok());
    assert((result.camera.worldPosition ==
            std::array<float, 3>{100.0f, 200.0f, 300.0f}));
    assert((result.camera.orientationX == std::array<float, 3>{1.0f, 0.0f, 0.0f}));
    assert((result.camera.orientationY == std::array<float, 3>{0.0f, 1.0f, 0.0f}));
    assert((result.camera.orientationZ == std::array<float, 3>{0.0f, 0.0f, 1.0f}));

    const auto clip = ratchet::game::transformRac1LiveCameraPointToClip(
        result.camera, {2.0f, 3.0f, 5.0f, 1.0f});
    assert((clip == std::array<float, 4>{75.0f, 86.0f, 97.0f, 108.0f}));
}

void testRetailInitStateHasOrientationBeforeClipTransform() {
    std::vector<std::uint8_t> ram(32u * 1024u * 1024u);
    const std::uint32_t base = Layout::kStateBase;

    // FUN_00218D10 initializes position to (256,256,64) and the proved xyz
    // orientation to identity while the later render builder owns clip output.
    writeVector(ram, base + Layout::kWorldPositionOffset,
                std::array<float, 3>{256.0f, 256.0f, 64.0f});
    writeVector(ram, base + Layout::kOrientationXOffset,
                std::array<float, 3>{1.0f, 0.0f, 0.0f});
    writeVector(ram, base + Layout::kOrientationYOffset,
                std::array<float, 3>{0.0f, 1.0f, 0.0f});
    writeVector(ram, base + Layout::kOrientationZOffset,
                std::array<float, 3>{0.0f, 0.0f, 1.0f});

    const auto result = ratchet::game::inspectRac1LiveCamera(ram);
    assert(result.status ==
           ratchet::game::Rac1LiveCameraStatus::ClipTransformNotMaterialized);
}

void testZeroedRetailStateDoesNotSynthesizeOrientation() {
    const std::vector<std::uint8_t> ram(32u * 1024u * 1024u);
    const auto result = ratchet::game::inspectRac1LiveCamera(ram);
    assert(result.status ==
           ratchet::game::Rac1LiveCameraStatus::OrientationNotMaterialized);
}

void testNonFiniteFieldsFailClosed() {
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Layout::kStateBase + Layout::kWorldPositionOffset,
                   std::numeric_limits<float>::quiet_NaN());
        assert(ratchet::game::inspectRac1LiveCamera(fixture.ram).status ==
               ratchet::game::Rac1LiveCameraStatus::NonFinitePosition);
    }
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Layout::kStateBase + Layout::kOrientationYOffset + 4u,
                   std::numeric_limits<float>::infinity());
        assert(ratchet::game::inspectRac1LiveCamera(fixture.ram).status ==
               ratchet::game::Rac1LiveCameraStatus::NonFiniteOrientation);
    }
    {
        Fixture fixture;
        writeFloat(fixture.ram,
                   Layout::kStateBase + Layout::kClipWOffset + 12u,
                   std::numeric_limits<float>::quiet_NaN());
        assert(ratchet::game::inspectRac1LiveCamera(fixture.ram).status ==
               ratchet::game::Rac1LiveCameraStatus::NonFiniteClipTransform);
    }
}

void testGuestMemoryBoundsFailClosed() {
    const std::size_t required =
        static_cast<std::size_t>(Layout::kStateBase) + Layout::kStateBytes;
    const std::vector<std::uint8_t> shortRam(required - 1u);
    assert(ratchet::game::inspectRac1LiveCamera(shortRam).status ==
           ratchet::game::Rac1LiveCameraStatus::GuestMemoryTooSmall);
}

} // namespace

int main() {
    testRetailAddressesAndExactClipCombination();
    testRetailInitStateHasOrientationBeforeClipTransform();
    testZeroedRetailStateDoesNotSynthesizeOrientation();
    testNonFiniteFieldsFailClosed();
    testGuestMemoryBoundsFailClosed();

    assert(std::string_view(ratchet::game::rac1LiveCameraStatusName(
               ratchet::game::Rac1LiveCameraStatus::Ok)) == "ok");
    assert(std::string_view(ratchet::game::rac1LiveCameraStatusName(
               ratchet::game::Rac1LiveCameraStatus::OrientationNotMaterialized)) ==
           "orientation-not-materialized");
    assert(std::string_view(ratchet::game::rac1LiveCameraStatusName(
               ratchet::game::Rac1LiveCameraStatus::ClipTransformNotMaterialized)) ==
           "clip-transform-not-materialized");

    std::cout << "rac1_live_camera_tests: PASS\n";
    return 0;
}
