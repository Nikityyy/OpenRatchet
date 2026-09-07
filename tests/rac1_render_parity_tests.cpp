#include "render/native_render_contract.h"
#include "render/rac1_render_parity.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

ratchet::assets::Rac1Texture texture(std::uint8_t r,
                                     std::uint8_t g,
                                     std::uint8_t b,
                                     std::uint8_t a) {
    ratchet::assets::Rac1Texture result{};
    result.width = 1u;
    result.height = 1u;
    result.hasAlpha = a != 255u;
    result.rgba = {r, g, b, a};
    return result;
}

ratchet::assets::Rac1TfragVertex tfragVertex(float x, float y, float z) {
    ratchet::assets::Rac1TfragVertex result{};
    result.x = x;
    result.y = y;
    result.z = z;
    result.u = x * 0.25f;
    result.v = y * 0.25f;
    return result;
}

ratchet::assets::Rac1StaticVertex staticVertex(float x, float y, float z) {
    ratchet::assets::Rac1StaticVertex result{};
    result.x = x;
    result.y = y;
    result.z = z;
    result.u = x * 0.5f;
    result.v = y * 0.5f;
    return result;
}

struct Fixture {
    ratchet::assets::Rac1TfragMesh terrain;
    std::vector<ratchet::assets::Rac1Texture> tfragTextures;
    ratchet::assets::Rac1StaticSceneMesh staticScene;
    std::vector<ratchet::assets::Rac1Texture> tieTextures;
    std::vector<ratchet::assets::Rac1Texture> shrubTextures;
};

Fixture makeFixture() {
    Fixture fixture{};

    ratchet::assets::Rac1TfragBatch terrainBatch{};
    terrainBatch.materialIndex = 0u;
    terrainBatch.triangleVertices = {
        tfragVertex(0.0f, 0.0f, 0.0f),
        tfragVertex(1.0f, 0.0f, 0.0f),
        tfragVertex(0.0f, 1.0f, 0.0f),
    };
    fixture.terrain.batches.push_back(terrainBatch);
    fixture.terrain.triangleCount = 1u;
    fixture.tfragTextures.push_back(texture(255u, 0u, 0u, 255u));

    ratchet::assets::Rac1StaticBatch tie{};
    tie.kind = ratchet::assets::Rac1StaticMaterialKind::Tie;
    tie.materialIndex = 0u;
    tie.triangleVertices = {
        staticVertex(2.0f, 0.0f, 0.0f),
        staticVertex(3.0f, 0.0f, 0.0f),
        staticVertex(2.0f, 1.0f, 0.0f),
    };
    fixture.staticScene.batches.push_back(tie);
    fixture.staticScene.tieTriangleCount = 1u;
    fixture.tieTextures.push_back(texture(0u, 255u, 0u, 255u));

    ratchet::assets::Rac1StaticBatch shrub{};
    shrub.kind = ratchet::assets::Rac1StaticMaterialKind::Shrub;
    shrub.materialIndex = 0u;
    shrub.triangleVertices = {
        staticVertex(4.0f, 0.0f, 0.0f),
        staticVertex(5.0f, 0.0f, 0.0f),
        staticVertex(4.0f, 1.0f, 0.0f),
    };
    fixture.staticScene.batches.push_back(shrub);
    fixture.staticScene.shrubTriangleCount = 1u;
    fixture.shrubTextures.push_back(texture(0u, 0u, 255u, 128u));

    return fixture;
}

ratchet::render::Rac1StaticWorldParityDigest digest(const Fixture& fixture) {
    return ratchet::render::buildRac1StaticWorldParityDigest(
        fixture.terrain,
        fixture.tfragTextures,
        fixture.staticScene,
        fixture.tieTextures,
        fixture.shrubTextures);
}

bool require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "rac1_render_parity_tests: " << message << '\n';
    return false;
}

} // namespace

int main() {
    const Fixture baselineFixture = makeFixture();
    const auto baseline = digest(baselineFixture);
    if (!require(baseline.ok(), "baseline digest failed") ||
        !require(baseline.batchCount == 3u, "unexpected batch count") ||
        !require(baseline.vertexCount == 9u, "unexpected vertex count") ||
        !require(baseline.indexCount == 0u, "native static world must remain unindexed") ||
        !require(baseline.triangleCount == 3u, "unexpected triangle count") ||
        !require(baseline.combinedHash == 0xbc4aaff79c96854dull,
                 "stable synthetic combined hash changed")) {
        return 1;
    }

    const auto repeat = digest(baselineFixture);
    if (!require(repeat.ok(), "repeat digest failed") ||
        !require(repeat.combinedHash == baseline.combinedHash,
                 "identical canonical input produced a different combined hash")) {
        return 1;
    }

    Fixture vertexMutation = baselineFixture;
    vertexMutation.staticScene.batches[1].triangleVertices[2].z = 7.0f;
    const auto changedVertex = digest(vertexMutation);
    if (!require(changedVertex.ok(), "vertex mutation digest failed") ||
        !require(changedVertex.vertexHash != baseline.vertexHash,
                 "vertex mutation did not change vertex hash") ||
        !require(changedVertex.combinedHash != baseline.combinedHash,
                 "vertex mutation did not change combined hash")) {
        return 1;
    }

    Fixture textureMutation = baselineFixture;
    textureMutation.tieTextures[0].rgba[0] ^= 0xffu;
    const auto changedTexture = digest(textureMutation);
    if (!require(changedTexture.ok(), "texture mutation digest failed") ||
        !require(changedTexture.textureHash != baseline.textureHash,
                 "texture mutation did not change texture hash") ||
        !require(changedTexture.combinedHash != baseline.combinedHash,
                 "texture mutation did not change combined hash")) {
        return 1;
    }

    Fixture orderMutation = baselineFixture;
    std::swap(orderMutation.staticScene.batches[0], orderMutation.staticScene.batches[1]);
    const auto changedOrder = digest(orderMutation);
    if (!require(changedOrder.ok(), "draw-order mutation digest failed") ||
        !require(changedOrder.drawOrderHash != baseline.drawOrderHash,
                 "draw-order mutation did not change draw-order hash") ||
        !require(changedOrder.combinedHash != baseline.combinedHash,
                 "draw-order mutation did not change combined hash")) {
        return 1;
    }

    Fixture invalidMaterial = baselineFixture;
    invalidMaterial.terrain.batches[0].materialIndex = 1u;
    const auto invalid = digest(invalidMaterial);
    if (!require(invalid.status == ratchet::render::Rac1RenderParityStatus::InvalidMaterialIndex,
                 "invalid material was not rejected")) {
        return 1;
    }

    const auto sky = ratchet::render::nativeRenderStateContract(
        ratchet::render::NativeRenderPass::Sky, false);
    const auto world = ratchet::render::nativeRenderStateContract(
        ratchet::render::NativeRenderPass::World, false);
    if (!require(!sky.depthTest && !sky.depthWrite && !sky.backfaceCulling && sky.colorBlend,
                 "sky render-state contract changed") ||
        !require(world.depthTest && world.depthWrite && !world.backfaceCulling && world.colorBlend,
                 "world render-state contract changed")) {
        return 1;
    }

    std::cout << "R&C1 native render parity tests passed combined=0x"
              << std::hex << baseline.combinedHash << std::dec << '\n';
    return 0;
}
