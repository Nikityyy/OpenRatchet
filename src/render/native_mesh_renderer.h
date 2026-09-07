#pragma once

#include "assets/rac1_texture.h"
#include "render/native_render_contract.h"

#include <raylib.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ratchet::render {

struct NativeDrawBatch {
    Model model{};
    bool transparent = false;
};

std::vector<Texture2D> uploadTextures(
    const std::vector<assets::Rac1Texture>& source);
void unloadTextures(std::vector<Texture2D>& textures) noexcept;
void unloadBatches(std::vector<NativeDrawBatch>& batches) noexcept;
void drawBatches(const std::vector<NativeDrawBatch>& batches, Vector3 position);
void applyNativeRenderPassState(NativeRenderPass pass, bool wireframe = false);

template <typename Vertex, typename PositionFn>
bool appendMeshBatch(const std::vector<Vertex>& vertices,
                     std::uint32_t materialIndex,
                     const std::vector<Texture2D>* gpuTextures,
                     const std::vector<assets::Rac1Texture>* sourceTextures,
                     PositionFn&& positionFn,
                     std::vector<NativeDrawBatch>& output) {
    if (vertices.empty()) return true;
    if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    if ((gpuTextures == nullptr) != (sourceTextures == nullptr)) return false;
    if (gpuTextures != nullptr &&
        (materialIndex >= gpuTextures->size() || materialIndex >= sourceTextures->size())) {
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
        if (mesh.vertices != nullptr) MemFree(mesh.vertices);
        if (mesh.texcoords != nullptr) MemFree(mesh.texcoords);
        if (mesh.colors != nullptr) MemFree(mesh.colors);
        return false;
    }

    for (std::size_t i = 0u; i < vertexCount; ++i) {
        const Vertex& source = vertices[i];
        const Vector3 p = positionFn(source);
        mesh.vertices[i * 3u + 0u] = p.x;
        mesh.vertices[i * 3u + 1u] = p.y;
        mesh.vertices[i * 3u + 2u] = p.z;
        mesh.texcoords[i * 2u + 0u] = source.u;
        mesh.texcoords[i * 2u + 1u] = source.v;
        mesh.colors[i * 4u + 0u] = source.r;
        mesh.colors[i * 4u + 1u] = source.g;
        mesh.colors[i * 4u + 2u] = source.b;
        mesh.colors[i * 4u + 3u] = source.a;
    }

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    if (model.materialCount <= 0 || model.materials == nullptr) {
        UnloadModel(model);
        return false;
    }

    bool transparent = false;
    if (gpuTextures != nullptr) {
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            (*gpuTextures)[materialIndex];
        transparent = (*sourceTextures)[materialIndex].hasAlpha;
    } else {
        for (const auto& vertex : vertices) transparent |= vertex.a < 255u;
    }
    output.push_back({model, transparent});
    return true;
}

} // namespace ratchet::render
