#include "render/native_mesh_renderer.h"

#include <rlgl.h>

namespace ratchet::render {

std::vector<Texture2D> uploadTextures(
    const std::vector<assets::Rac1Texture>& source) {
    std::vector<Texture2D> result;
    result.reserve(source.size());
    for (const auto& textureSource : source) {
        Image image{};
        image.data = const_cast<std::uint8_t*>(textureSource.rgba.data());
        image.width = static_cast<int>(textureSource.width);
        image.height = static_cast<int>(textureSource.height);
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        Texture2D texture = LoadTextureFromImage(image);
        if (texture.id == 0u) {
            unloadTextures(result);
            return {};
        }
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        result.push_back(texture);
    }
    return result;
}

void unloadTextures(std::vector<Texture2D>& textures) noexcept {
    for (Texture2D texture : textures) UnloadTexture(texture);
    textures.clear();
}

void unloadBatches(std::vector<NativeDrawBatch>& batches) noexcept {
    for (auto& batch : batches) UnloadModel(batch.model);
    batches.clear();
}


void applyNativeRenderPassState(NativeRenderPass pass, bool wireframe) {
    const auto state = nativeRenderStateContract(pass, wireframe);

    if (state.depthTest) rlEnableDepthTest();
    else rlDisableDepthTest();

    if (state.depthWrite) rlEnableDepthMask();
    else rlDisableDepthMask();

    if (state.backfaceCulling) rlEnableBackfaceCulling();
    else rlDisableBackfaceCulling();

    if (state.colorBlend) rlEnableColorBlend();
    else rlDisableColorBlend();
    rlSetBlendMode(RL_BLEND_ALPHA);

    if (state.wireframe) rlEnableWireMode();
    else rlDisableWireMode();
}

void drawBatches(const std::vector<NativeDrawBatch>& batches, Vector3 position) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantTransparent = pass != 0;
        for (const auto& batch : batches) {
            if (batch.transparent == wantTransparent) {
                DrawModel(batch.model, position, 1.0f, WHITE);
            }
        }
    }
}

} // namespace ratchet::render
