#include "render/native_skinned_moby_renderer.h"

#include <limits>
#include <utility>

namespace ratchet::render {
namespace {

void releaseMeshCpuBuffers(Mesh& mesh) noexcept {
    if (mesh.vertices != nullptr) MemFree(mesh.vertices);
    if (mesh.texcoords != nullptr) MemFree(mesh.texcoords);
    if (mesh.colors != nullptr) MemFree(mesh.colors);
    mesh.vertices = nullptr;
    mesh.texcoords = nullptr;
    mesh.colors = nullptr;
}

} // namespace

bool nativeSkinnedMobyVertexMatches(
    const assets::Rac1MobyVertex& vertex,
    const assets::Rac1MobyRenderedInstance& instance) noexcept {
    return vertex.oClass == instance.oClass &&
           vertex.instanceIndex == instance.instanceIndex;
}

bool appendSkinnedMobyBatch(
    const std::vector<assets::Rac1MobyVertex>& vertices,
    std::uint32_t materialIndex,
    const std::vector<Texture2D>& gpuTextures,
    const std::vector<assets::Rac1Texture>& sourceTextures,
    const assets::Rac1MobyRenderedInstance& instance,
    const assets::Rac1MobySkinExecution& execution,
    const NativeSkinnedMobyPositionTransform& positionTransform,
    std::vector<NativeSkinnedMobyBatch>& output) {
    if (vertices.empty()) return true;
    if (!positionTransform || materialIndex >= gpuTextures.size() ||
        materialIndex >= sourceTextures.size() ||
        vertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const std::size_t vertexCount = vertices.size();
    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(vertexCount / 3u);
    mesh.vertices = static_cast<float*>(MemAlloc(
        static_cast<unsigned int>(vertexCount * 3u * sizeof(float))));
    mesh.texcoords = static_cast<float*>(MemAlloc(
        static_cast<unsigned int>(vertexCount * 2u * sizeof(float))));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(
        static_cast<unsigned int>(vertexCount * 4u)));
    if (mesh.vertices == nullptr || mesh.texcoords == nullptr || mesh.colors == nullptr) {
        releaseMeshCpuBuffers(mesh);
        return false;
    }

    NativeSkinnedMobyBatch batch{};
    batch.transparent = sourceTextures[materialIndex].hasAlpha;
    batch.skinVertexIndices.reserve(vertexCount);
    batch.positions.resize(vertexCount * 3u);
    for (std::size_t i = 0u; i < vertexCount; ++i) {
        const auto& source = vertices[i];
        if (!nativeSkinnedMobyVertexMatches(source, instance) ||
            source.skinVertexIndex >= execution.vertices.size()) {
            releaseMeshCpuBuffers(mesh);
            return false;
        }

        const Vector3 p = positionTransform(execution.vertices[source.skinVertexIndex].position);
        mesh.vertices[i * 3u + 0u] = p.x;
        mesh.vertices[i * 3u + 1u] = p.y;
        mesh.vertices[i * 3u + 2u] = p.z;
        mesh.texcoords[i * 2u + 0u] = source.u;
        mesh.texcoords[i * 2u + 1u] = source.v;
        mesh.colors[i * 4u + 0u] = source.r;
        mesh.colors[i * 4u + 1u] = source.g;
        mesh.colors[i * 4u + 2u] = source.b;
        mesh.colors[i * 4u + 3u] = source.a;
        batch.skinVertexIndices.push_back(source.skinVertexIndex);
        batch.positions[i * 3u + 0u] = p.x;
        batch.positions[i * 3u + 1u] = p.y;
        batch.positions[i * 3u + 2u] = p.z;
    }

    UploadMesh(&mesh, true);
    Model model = LoadModelFromMesh(mesh);
    if (model.materialCount <= 0 || model.materials == nullptr ||
        model.meshCount <= 0 || model.meshes == nullptr) {
        UnloadModel(model);
        return false;
    }

    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = gpuTextures[materialIndex];
    batch.model = model;
    output.push_back(std::move(batch));
    return true;
}

bool updateSkinnedMobyBatches(
    std::vector<NativeSkinnedMobyBatch>& batches,
    const assets::Rac1MobySkinExecution& execution,
    const NativeSkinnedMobyPositionTransform& positionTransform) {
    if (!positionTransform) return false;

    for (auto& batch : batches) {
        if (batch.model.meshCount <= 0 || batch.model.meshes == nullptr ||
            batch.skinVertexIndices.size() * 3u != batch.positions.size()) {
            return false;
        }
        for (std::size_t i = 0u; i < batch.skinVertexIndices.size(); ++i) {
            const std::uint32_t source = batch.skinVertexIndices[i];
            if (source >= execution.vertices.size()) return false;
            const Vector3 p = positionTransform(execution.vertices[source].position);
            batch.positions[i * 3u + 0u] = p.x;
            batch.positions[i * 3u + 1u] = p.y;
            batch.positions[i * 3u + 2u] = p.z;
        }
        UpdateMeshBuffer(batch.model.meshes[0],
                         0,
                         batch.positions.data(),
                         static_cast<int>(batch.positions.size() * sizeof(float)),
                         0);
    }
    return true;
}

void drawSkinnedMobyBatches(const std::vector<NativeSkinnedMobyBatch>& batches) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantTransparent = pass != 0;
        for (const auto& batch : batches) {
            if (batch.transparent == wantTransparent) {
                DrawModel(batch.model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
        }
    }
}

void unloadSkinnedMobyBatches(std::vector<NativeSkinnedMobyBatch>& batches) noexcept {
    for (auto& batch : batches) UnloadModel(batch.model);
    batches.clear();
}

} // namespace ratchet::render
