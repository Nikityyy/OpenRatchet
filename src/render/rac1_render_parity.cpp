#include "render/rac1_render_parity.h"

#include "render/native_render_contract.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace ratchet::render {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

class StableHash final {
public:
    void byte(std::uint8_t value) noexcept {
        value_ ^= value;
        value_ *= kFnvPrime;
    }

    void bytes(std::span<const std::uint8_t> values) noexcept {
        for (std::uint8_t value : values) byte(value);
    }

    void text(std::string_view value) noexcept {
        for (char c : value) byte(static_cast<std::uint8_t>(c));
        byte(0u);
    }

    void u16(std::uint16_t value) noexcept {
        byte(static_cast<std::uint8_t>(value & 0xffu));
        byte(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    }

    void u32(std::uint32_t value) noexcept {
        for (unsigned shift = 0u; shift < 32u; shift += 8u) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void u64(std::uint64_t value) noexcept {
        for (unsigned shift = 0u; shift < 64u; shift += 8u) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void size(std::size_t value) noexcept {
        u64(static_cast<std::uint64_t>(value));
    }

    void boolean(bool value) noexcept {
        byte(value ? 1u : 0u);
    }

    void f32(float value) noexcept {
        u32(std::bit_cast<std::uint32_t>(value));
    }

    [[nodiscard]] std::uint64_t finish() const noexcept { return value_; }

private:
    std::uint64_t value_ = kFnvOffset;
};

template <typename Vertex>
bool finiteVertex(const Vertex& vertex) noexcept {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
           std::isfinite(vertex.z) && std::isfinite(vertex.u) &&
           std::isfinite(vertex.v);
}

template <typename Vertex>
void hashVertex(StableHash& hash, const Vertex& vertex) noexcept {
    hash.f32(vertex.x);
    hash.f32(vertex.y);
    hash.f32(vertex.z);
    hash.f32(vertex.u);
    hash.f32(vertex.v);
    hash.byte(vertex.r);
    hash.byte(vertex.g);
    hash.byte(vertex.b);
    hash.byte(vertex.a);
}

void hashTextureSet(StableHash& hash,
                    std::string_view semantic,
                    std::span<const assets::Rac1Texture> textures) noexcept {
    hash.text(semantic);
    hash.size(textures.size());
    for (std::size_t i = 0u; i < textures.size(); ++i) {
        const auto& texture = textures[i];
        hash.size(i);
        hash.u16(texture.width);
        hash.u16(texture.height);
        hash.boolean(texture.hasAlpha);
        hash.size(texture.rgba.size());
        hash.bytes(texture.rgba);
    }
}

void hashState(StableHash& hash,
               const NativeRenderStateContract& state) noexcept {
    hash.boolean(state.depthTest);
    hash.boolean(state.depthWrite);
    hash.boolean(state.backfaceCulling);
    hash.boolean(state.colorBlend);
    hash.boolean(state.wireframe);
    hash.byte(static_cast<std::uint8_t>(state.blend));
}

void hashIdentityTransform(StableHash& hash) noexcept {
    static constexpr float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    hash.text("retail-world-identity");
    for (float value : kIdentity) hash.f32(value);
}

} // namespace

Rac1StaticWorldParityDigest buildRac1StaticWorldParityDigest(
    const assets::Rac1TfragMesh& terrain,
    std::span<const assets::Rac1Texture> tfragTextures,
    const assets::Rac1StaticSceneMesh& staticScene,
    std::span<const assets::Rac1Texture> tieTextures,
    std::span<const assets::Rac1Texture> shrubTextures) noexcept {
    Rac1StaticWorldParityDigest result{};
    if (terrain.batches.empty() || staticScene.batches.empty()) {
        result.status = Rac1RenderParityStatus::EmptyWorld;
        return result;
    }

    StableHash vertexHash;
    StableHash indexHash;
    StableHash materialHash;
    StableHash textureHash;
    StableHash transformHash;
    StableHash topologyHash;
    StableHash renderStateHash;
    StableHash drawOrderHash;

    vertexHash.text("rac1-static-world-vertices-v1");
    indexHash.text("rac1-static-world-unindexed-v1");
    materialHash.text("rac1-static-world-materials-v1");
    textureHash.text("rac1-static-world-textures-v1");
    transformHash.text("rac1-static-world-transform-v1");
    topologyHash.text("rac1-static-world-topology-v1");
    renderStateHash.text("rac1-static-world-render-state-v1");
    drawOrderHash.text("rac1-static-world-draw-order-v1");

    hashTextureSet(textureHash, "tfrag", tfragTextures);
    hashTextureSet(textureHash, "tie", tieTextures);
    hashTextureSet(textureHash, "shrub", shrubTextures);
    hashIdentityTransform(transformHash);

    topologyHash.text("triangle-list");
    topologyHash.text("index-buffer-absent");
    const auto worldState = nativeRenderStateContract(NativeRenderPass::World, false);
    hashState(renderStateHash, worldState);

    result.batchCount = terrain.batches.size() + staticScene.batches.size();

    for (std::size_t batchIndex = 0u; batchIndex < terrain.batches.size(); ++batchIndex) {
        const auto& batch = terrain.batches[batchIndex];
        if (batch.triangleVertices.empty() || (batch.triangleVertices.size() % 3u) != 0u) {
            result.status = Rac1RenderParityStatus::InvalidTerrainBatch;
            return result;
        }
        if (batch.materialIndex >= tfragTextures.size()) {
            result.status = Rac1RenderParityStatus::InvalidMaterialIndex;
            return result;
        }

        vertexHash.text("terrain");
        vertexHash.size(batchIndex);
        vertexHash.size(batch.triangleVertices.size());
        materialHash.text("terrain");
        materialHash.size(batchIndex);
        materialHash.u32(batch.materialIndex);
        materialHash.boolean(tfragTextures[batch.materialIndex].hasAlpha);
        topologyHash.text("terrain");
        topologyHash.size(batch.triangleVertices.size());
        indexHash.text("terrain-no-index-buffer");
        indexHash.size(batchIndex);
        indexHash.size(0u);

        for (const auto& vertex : batch.triangleVertices) {
            if (!finiteVertex(vertex)) {
                result.status = Rac1RenderParityStatus::NonFiniteVertex;
                return result;
            }
            hashVertex(vertexHash, vertex);
        }
        result.vertexCount += batch.triangleVertices.size();
        result.triangleCount += batch.triangleVertices.size() / 3u;
    }

    for (std::size_t batchIndex = 0u; batchIndex < staticScene.batches.size(); ++batchIndex) {
        const auto& batch = staticScene.batches[batchIndex];
        if (batch.triangleVertices.empty() || (batch.triangleVertices.size() % 3u) != 0u) {
            result.status = Rac1RenderParityStatus::InvalidStaticBatch;
            return result;
        }
        const auto textures = batch.kind == assets::Rac1StaticMaterialKind::Tie
                                  ? tieTextures
                                  : shrubTextures;
        if (batch.materialIndex >= textures.size()) {
            result.status = Rac1RenderParityStatus::InvalidMaterialIndex;
            return result;
        }
        const char* kind = batch.kind == assets::Rac1StaticMaterialKind::Tie
                               ? "tie"
                               : "shrub";

        vertexHash.text(kind);
        vertexHash.size(batchIndex);
        vertexHash.size(batch.triangleVertices.size());
        materialHash.text(kind);
        materialHash.size(batchIndex);
        materialHash.u32(batch.materialIndex);
        materialHash.boolean(textures[batch.materialIndex].hasAlpha);
        topologyHash.text(kind);
        topologyHash.size(batch.triangleVertices.size());
        indexHash.text("static-no-index-buffer");
        indexHash.size(batchIndex);
        indexHash.size(0u);

        for (const auto& vertex : batch.triangleVertices) {
            if (!finiteVertex(vertex)) {
                result.status = Rac1RenderParityStatus::NonFiniteVertex;
                return result;
            }
            hashVertex(vertexHash, vertex);
        }
        result.vertexCount += batch.triangleVertices.size();
        result.triangleCount += batch.triangleVertices.size() / 3u;
    }

    // drawBatches() is deliberately two-pass. Terrain is submitted before the
    // static tie/shrub vector, and within each vector opaque batches precede
    // alpha-bearing batches while preserving source order.
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantTransparent = pass != 0;
        for (std::size_t batchIndex = 0u; batchIndex < terrain.batches.size(); ++batchIndex) {
            const auto& batch = terrain.batches[batchIndex];
            const bool transparent = tfragTextures[batch.materialIndex].hasAlpha;
            if (transparent != wantTransparent) continue;
            drawOrderHash.text("terrain");
            drawOrderHash.size(batchIndex);
            drawOrderHash.boolean(transparent);
            drawOrderHash.u32(batch.materialIndex);
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantTransparent = pass != 0;
        for (std::size_t batchIndex = 0u; batchIndex < staticScene.batches.size(); ++batchIndex) {
            const auto& batch = staticScene.batches[batchIndex];
            const auto textures = batch.kind == assets::Rac1StaticMaterialKind::Tie
                                      ? tieTextures
                                      : shrubTextures;
            const bool transparent = textures[batch.materialIndex].hasAlpha;
            if (transparent != wantTransparent) continue;
            drawOrderHash.text(batch.kind == assets::Rac1StaticMaterialKind::Tie
                                   ? "tie"
                                   : "shrub");
            drawOrderHash.size(batchIndex);
            drawOrderHash.boolean(transparent);
            drawOrderHash.u32(batch.materialIndex);
        }
    }

    result.indexCount = 0u;
    result.vertexHash = vertexHash.finish();
    result.indexHash = indexHash.finish();
    result.materialHash = materialHash.finish();
    result.textureHash = textureHash.finish();
    result.transformHash = transformHash.finish();
    result.topologyHash = topologyHash.finish();
    result.renderStateHash = renderStateHash.finish();
    result.drawOrderHash = drawOrderHash.finish();

    StableHash combined;
    combined.text("rac1-static-world-combined-v1");
    combined.size(result.batchCount);
    combined.size(result.vertexCount);
    combined.size(result.indexCount);
    combined.size(result.triangleCount);
    combined.u64(result.vertexHash);
    combined.u64(result.indexHash);
    combined.u64(result.materialHash);
    combined.u64(result.textureHash);
    combined.u64(result.transformHash);
    combined.u64(result.topologyHash);
    combined.u64(result.renderStateHash);
    combined.u64(result.drawOrderHash);
    result.combinedHash = combined.finish();
    result.status = Rac1RenderParityStatus::Ok;
    return result;
}

const char* rac1RenderParityStatusName(Rac1RenderParityStatus status) noexcept {
    switch (status) {
    case Rac1RenderParityStatus::Ok: return "ok";
    case Rac1RenderParityStatus::EmptyWorld: return "empty-world";
    case Rac1RenderParityStatus::InvalidTerrainBatch: return "invalid-terrain-batch";
    case Rac1RenderParityStatus::InvalidStaticBatch: return "invalid-static-batch";
    case Rac1RenderParityStatus::InvalidMaterialIndex: return "invalid-material-index";
    case Rac1RenderParityStatus::NonFiniteVertex: return "non-finite-vertex";
    }
    return "unknown";
}

} // namespace ratchet::render
