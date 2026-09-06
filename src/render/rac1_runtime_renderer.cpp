#include "render/rac1_runtime_renderer.h"

#include "assets/rac1_level.h"
#include "assets/rac1_static_scene.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"

#include <filesystem>

namespace ratchet::render {

Rac1RuntimeRenderer::~Rac1RuntimeRenderer() {
    unload();
}

void Rac1RuntimeRenderer::unload() noexcept {
    unloadBatches(terrainBatches_);
    unloadBatches(staticBatches_);
    unloadTextures(tfragTextures_);
    unloadTextures(tieTextures_);
    unloadTextures(shrubTextures_);
    summary_ = {};
    status_ = Rac1RuntimeRendererStatus::Uninitialized;
}

bool Rac1RuntimeRenderer::loadLevel(const platform::NativeAssetLocation& level) {
    unload();
    summary_.levelIndex = level.index;

    if (level.kind != platform::NativeAssetKind::Level ||
        !std::filesystem::is_regular_file(level.path)) {
        status_ = Rac1RuntimeRendererStatus::LevelFileMissing;
        return false;
    }

    auto loaded = assets::loadRac1LevelCore(level.path,
                                            level.index,
                                            level.startSector,
                                            level.sectorCount,
                                            level.headerSector);
    if (!loaded.ok()) {
        status_ = Rac1RuntimeRendererStatus::LevelLoadFailed;
        return false;
    }

    auto tfragTextures = assets::decodeRac1PaletteTextures(
        loaded.core,
        loaded.coreIndex,
        loaded.gsRam,
        loaded.summary.tfragTextures,
        loaded.summary.texturesBaseOffset);
    auto tieTextures = assets::decodeRac1PaletteTextures(
        loaded.core,
        loaded.coreIndex,
        loaded.gsRam,
        loaded.summary.tieTextures,
        loaded.summary.texturesBaseOffset);
    auto shrubTextures = assets::decodeRac1PaletteTextures(
        loaded.core,
        loaded.coreIndex,
        loaded.gsRam,
        loaded.summary.shrubTextures,
        loaded.summary.texturesBaseOffset);
    if (!tfragTextures.ok() || !tieTextures.ok() || !shrubTextures.ok()) {
        status_ = Rac1RuntimeRendererStatus::TextureDecodeFailed;
        return false;
    }

    auto terrain = assets::decodeRac1TfragTerrain(
        loaded.core,
        loaded.summary.tfragsOffset,
        static_cast<std::uint32_t>(tfragTextures.textures.size()));
    if (!terrain.ok()) {
        status_ = Rac1RuntimeRendererStatus::TerrainDecodeFailed;
        return false;
    }

    auto staticScene = assets::decodeRac1StaticScene(
        loaded.core,
        loaded.coreIndex,
        loaded.gameplay,
        loaded.summary.tieClasses,
        loaded.summary.shrubClasses,
        static_cast<std::uint32_t>(tieTextures.textures.size()),
        static_cast<std::uint32_t>(shrubTextures.textures.size()));
    if (!staticScene.ok()) {
        status_ = Rac1RuntimeRendererStatus::StaticSceneDecodeFailed;
        return false;
    }

    tfragTextures_ = uploadTextures(tfragTextures.textures);
    tieTextures_ = uploadTextures(tieTextures.textures);
    shrubTextures_ = uploadTextures(shrubTextures.textures);
    if (tfragTextures_.size() != tfragTextures.textures.size() ||
        tieTextures_.size() != tieTextures.textures.size() ||
        shrubTextures_.size() != shrubTextures.textures.size()) {
        status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
        unload();
        status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
        return false;
    }

    for (const auto& sourceBatch : terrain.mesh.batches) {
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                &tfragTextures_,
                &tfragTextures.textures,
                [](const assets::Rac1TfragVertex& vertex) {
                    return Vector3{vertex.x, vertex.y, vertex.z};
                },
                terrainBatches_)) {
            status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
            unload();
            status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
            return false;
        }
    }

    for (const auto& sourceBatch : staticScene.mesh.batches) {
        const bool tie = sourceBatch.kind == assets::Rac1StaticMaterialKind::Tie;
        const auto& gpu = tie ? tieTextures_ : shrubTextures_;
        const auto& source = tie ? tieTextures.textures : shrubTextures.textures;
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                &gpu,
                &source,
                [](const assets::Rac1StaticVertex& vertex) {
                    return Vector3{vertex.x, vertex.y, vertex.z};
                },
                staticBatches_)) {
            status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
            unload();
            status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
            return false;
        }
    }

    summary_.terrainBatches = terrainBatches_.size();
    summary_.staticBatches = staticBatches_.size();
    summary_.terrainTriangles = terrain.mesh.triangleCount;
    summary_.tieTriangles = staticScene.mesh.tieTriangleCount;
    summary_.shrubTriangles = staticScene.mesh.shrubTriangleCount;
    if (summary_.terrainBatches == 0u || summary_.staticBatches == 0u ||
        summary_.terrainTriangles == 0u ||
        summary_.tieTriangles + summary_.shrubTriangles == 0u) {
        status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
        unload();
        status_ = Rac1RuntimeRendererStatus::GpuUploadFailed;
        return false;
    }

    status_ = Rac1RuntimeRendererStatus::Ok;
    return true;
}

void Rac1RuntimeRenderer::drawStaticWorld() const {
    if (!ready()) return;
    constexpr Vector3 kOrigin{0.0f, 0.0f, 0.0f};
    drawBatches(terrainBatches_, kOrigin);
    drawBatches(staticBatches_, kOrigin);
}

const char* rac1RuntimeRendererStatusName(Rac1RuntimeRendererStatus status) noexcept {
    switch (status) {
    case Rac1RuntimeRendererStatus::Uninitialized:
        return "uninitialized";
    case Rac1RuntimeRendererStatus::Ok:
        return "ok";
    case Rac1RuntimeRendererStatus::LevelFileMissing:
        return "level-file-missing";
    case Rac1RuntimeRendererStatus::LevelLoadFailed:
        return "level-load-failed";
    case Rac1RuntimeRendererStatus::TextureDecodeFailed:
        return "texture-decode-failed";
    case Rac1RuntimeRendererStatus::TerrainDecodeFailed:
        return "terrain-decode-failed";
    case Rac1RuntimeRendererStatus::StaticSceneDecodeFailed:
        return "static-scene-decode-failed";
    case Rac1RuntimeRendererStatus::GpuUploadFailed:
        return "gpu-upload-failed";
    }
    return "unknown";
}

} // namespace ratchet::render
