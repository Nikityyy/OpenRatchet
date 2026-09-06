#include "render/rac1_render_bridge.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

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

bool close(float lhs, float rhs, float epsilon = 1.0e-6f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
    TestState test;

    ratchet::platform::NativeAssetLocation level0Gameplay{
        ratchet::platform::NativeAssetKind::Wad2,
        69u,
        0x38f6u,
        0x0834u,
        0u,
        {},
    };
    const auto level = ratchet::render::nativeLevelForRetailAssetRead(
        level0Gameplay, 0x38f6u, 0x0834u, 0x01654000u);
    test.expect(level && *level == 0u,
                "proved Retail wads2/69 request maps exactly to native level 0");

    auto wrongIndex = level0Gameplay;
    wrongIndex.index = 68u;
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     wrongIndex, 0x38f6u, 0x0834u, 0x01654000u),
                "adjacent WAD2 indices are not guessed to be levels");
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     level0Gameplay, 0x38f6u, 1u, 0x01654000u),
                "partial reads cannot claim the Level-0 identity bridge");
    auto wrongRange = level0Gameplay;
    wrongRange.startSector = 0x38f7u;
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     wrongRange, 0x38f7u, 0x0834u, 0x01654000u),
                "asset metadata must match the proved Retail Level-0 range");
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     level0Gameplay, 0x38f6u, 0x0834u, 0x01655000u),
                "same WAD read at another destination cannot claim Level-0 init");

    ratchet::game::Rac1LiveCameraState camera;
    camera.clipX = {1.0f, 2.0f, 3.0f, 4.0f};
    camera.clipY = {5.0f, 6.0f, 7.0f, 8.0f};
    camera.clipZ = {9.0f, 10.0f, 11.0f, 12.0f};
    camera.clipW = {13.0f, 14.0f, 15.0f, 16.0f};
    const std::array<float, 4> point{2.0f, -3.0f, 0.5f, 1.0f};

    const auto retail = ratchet::game::transformRac1LiveCameraPointToClip(camera, point);
    const auto matrix = ratchet::render::rac1RetailClipMatrixColumnMajor(camera);
    const auto host = ratchet::render::transformColumnMajor(matrix, point);
    const std::array<float, 4> expected{4.5f, 5.0f, 5.5f, 6.0f};
    for (std::size_t i = 0u; i < retail.size(); ++i) {
        test.expect(close(retail[i], expected[i]),
                    "Retail clip oracle matches independently evaluated vector result");
        test.expect(close(host[i], expected[i]),
                    "column-major host matrix preserves Retail clip multiplication order");
    }

    test.expect(matrix[0] == camera.clipX[0] && matrix[3] == camera.clipX[3] &&
                    matrix[4] == camera.clipY[0] && matrix[15] == camera.clipW[3],
                "matrix storage is literal X/Y/Z/W column order for rlgl/OpenGL");

    if (test.failures != 0) {
        std::cerr << test.failures << " render bridge test(s) failed\n";
        return 1;
    }
    std::cout << "render bridge tests passed\n";
    return 0;
}
