#include "assets/rac1_level.h"
#include "assets/rac1_sky.h"
#include "assets/rac1_static_scene.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"
#include "platform/native_vfs.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

Vector3 mapWorld(float x, float y, float z) {
    // R&C uses Z-up. Rotate -90 degrees around X for the Y-up PC viewer.
    return {x, z, -y};
}

Vector3 toViewerSpace(float x,
                      float y,
                      float z,
                      Vector3 center,
                      float scale) {
    const Vector3 mapped = mapWorld(x, y, z);
    return {(mapped.x - center.x) * scale,
            (mapped.y - center.y) * scale,
            (mapped.z - center.z) * scale};
}

Vector3 mappedCenter(const ratchet::assets::Rac1TfragBounds& b) {
    return {(b.minX + b.maxX) * 0.5f,
            (b.minZ + b.maxZ) * 0.5f,
            -(b.minY + b.maxY) * 0.5f};
}

float viewerScale(const ratchet::assets::Rac1TfragBounds& b) {
    const float x = b.maxX - b.minX;
    const float y = b.maxZ - b.minZ;
    const float z = b.maxY - b.minY;
    const float extent = std::max({x, y, z});
    if (!std::isfinite(extent) || extent <= 0.0001f) return 1.0f;
    return 80.0f / extent;
}

const ratchet::platform::NativeAssetLocation* selectLevel(
    const ratchet::platform::NativeVfs& vfs,
    int requestedIndex) {
    if (requestedIndex >= 0) {
        const auto* level = vfs.findLevel(static_cast<std::uint32_t>(requestedIndex));
        if (level != nullptr && std::filesystem::is_regular_file(level->path)) return level;
        return nullptr;
    }
    for (const auto& level : vfs.levels()) {
        if (std::filesystem::is_regular_file(level.path)) return &level;
    }
    return nullptr;
}

struct NativeDrawBatch {
    Model model{};
    bool transparent = false;
};

std::vector<Texture2D> uploadTextures(const std::vector<ratchet::assets::Rac1Texture>& source) {
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
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        result.push_back(texture);
    }
    return result;
}

void unloadTextures(std::vector<Texture2D>& textures) {
    for (Texture2D texture : textures) UnloadTexture(texture);
    textures.clear();
}

void unloadBatches(std::vector<NativeDrawBatch>& batches) {
    for (auto& batch : batches) UnloadModel(batch.model);
    batches.clear();
}

template <typename Vertex, typename PositionFn>
bool appendMeshBatch(const std::vector<Vertex>& vertices,
                     std::uint32_t materialIndex,
                     const std::vector<Texture2D>* gpuTextures,
                     const std::vector<ratchet::assets::Rac1Texture>* sourceTextures,
                     PositionFn&& positionFn,
                     std::vector<NativeDrawBatch>& output) {
    if (vertices.empty()) return true;
    if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    if ((gpuTextures == nullptr) != (sourceTextures == nullptr)) return false;
    if (gpuTextures != nullptr &&
        (materialIndex >= gpuTextures->size() || materialIndex >= sourceTextures->size())) {
        return false;
    }

    const std::size_t vertexCount = vertices.size();
    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(vertexCount / 3u);
    mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vertexCount * 3u * sizeof(float))));
    mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vertexCount * 2u * sizeof(float))));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(vertexCount * 4u)));
    if (mesh.vertices == nullptr || mesh.texcoords == nullptr || mesh.colors == nullptr) return false;

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
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = (*gpuTextures)[materialIndex];
        transparent = (*sourceTextures)[materialIndex].hasAlpha;
    } else {
        for (const auto& vertex : vertices) transparent |= vertex.a < 255u;
    }
    output.push_back({model, transparent});
    return true;
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: native_level_viewer <toc.json> <extracted-root> [level-index]\n";
        return 2;
    }

    int requestedIndex = -1;
    if (argc == 4) {
        try {
            requestedIndex = std::stoi(argv[3]);
        } catch (...) {
            std::cerr << "invalid level index: " << argv[3] << '\n';
            return 2;
        }
    }

    ratchet::platform::NativeVfs vfs;
    if (!vfs.initialize(argv[2], argv[1])) {
        std::cerr << "[OpenRatchet:viewer] VFS initialization failed\n";
        return 1;
    }
    const auto* level = selectLevel(vfs, requestedIndex);
    if (level == nullptr) {
        std::cerr << "[OpenRatchet:viewer] requested level is not extracted; "
                     "run tools/extract-native-levels.ps1 first\n";
        return 1;
    }

    auto loaded = ratchet::assets::loadRac1LevelCore(level->path,
                                                     level->index,
                                                     level->startSector,
                                                     level->sectorCount,
                                                     level->headerSector);
    if (!loaded.ok()) {
        std::cerr << "[OpenRatchet:viewer] level load failed status="
                  << ratchet::assets::rac1LevelInspectStatusName(loaded.status) << '\n';
        return 1;
    }

    auto tfragTextures = ratchet::assets::decodeRac1PaletteTextures(
        loaded.core, loaded.coreIndex, loaded.gsRam,
        loaded.summary.tfragTextures, loaded.summary.texturesBaseOffset);
    auto tieTextures = ratchet::assets::decodeRac1PaletteTextures(
        loaded.core, loaded.coreIndex, loaded.gsRam,
        loaded.summary.tieTextures, loaded.summary.texturesBaseOffset);
    auto shrubTextures = ratchet::assets::decodeRac1PaletteTextures(
        loaded.core, loaded.coreIndex, loaded.gsRam,
        loaded.summary.shrubTextures, loaded.summary.texturesBaseOffset);
    if (!tfragTextures.ok() || !tieTextures.ok() || !shrubTextures.ok()) {
        std::cerr << "[OpenRatchet:viewer] level texture decode failed"
                  << " tfrag=" << ratchet::assets::rac1TextureStatusName(tfragTextures.status)
                  << " tie=" << ratchet::assets::rac1TextureStatusName(tieTextures.status)
                  << " shrub=" << ratchet::assets::rac1TextureStatusName(shrubTextures.status)
                  << '\n';
        return 1;
    }

    auto terrain = ratchet::assets::decodeRac1TfragTerrain(
        loaded.core,
        loaded.summary.tfragsOffset,
        static_cast<std::uint32_t>(tfragTextures.textures.size()));
    if (!terrain.ok()) {
        std::cerr << "[OpenRatchet:viewer] tfrag decode failed offset=0x"
                  << std::hex << loaded.summary.tfragsOffset << std::dec
                  << " status=" << ratchet::assets::rac1TfragStatusName(terrain.status) << '\n';
        return 1;
    }

    auto staticScene = ratchet::assets::decodeRac1StaticScene(
        loaded.core,
        loaded.coreIndex,
        loaded.gameplay,
        loaded.summary.tieClasses,
        loaded.summary.shrubClasses,
        static_cast<std::uint32_t>(tieTextures.textures.size()),
        static_cast<std::uint32_t>(shrubTextures.textures.size()));
    if (!staticScene.ok()) {
        std::cerr << "[OpenRatchet:viewer] tie/shrub decode failed status="
                  << ratchet::assets::rac1StaticSceneStatusName(staticScene.status) << '\n';
        return 1;
    }

    auto sky = ratchet::assets::decodeRac1Sky(loaded.core, loaded.summary.skyOffset);
    if (!sky.ok()) {
        std::cerr << "[OpenRatchet:viewer] sky decode failed offset=0x"
                  << std::hex << loaded.summary.skyOffset << std::dec
                  << " status=" << ratchet::assets::rac1SkyStatusName(sky.status) << '\n';
        return 1;
    }

    const Vector3 center = mappedCenter(terrain.mesh.bounds);
    const float scale = viewerScale(terrain.mesh.bounds);

    std::cout << "[OpenRatchet:tfrag] level=" << loaded.summary.tocIndex
              << " id=" << loaded.summary.levelId
              << " core=0x" << std::hex << loaded.core.size()
              << " tfrags=0x" << loaded.summary.tfragsOffset << std::dec
              << " tfragCount=" << terrain.mesh.tfragCount
              << " strips=" << terrain.mesh.stripCount
              << " triangles=" << terrain.mesh.triangleCount
              << " batches=" << terrain.mesh.batches.size()
              << " textures=" << tfragTextures.textures.size()
              << " status=ok\n";
    std::cout << "[OpenRatchet:scene] gameplay=0x" << std::hex << loaded.gameplay.size()
              << std::dec
              << " tieClasses=" << staticScene.mesh.tieClassCount
              << " tieInstances=" << staticScene.mesh.tieInstanceCount
              << " tieTriangles=" << staticScene.mesh.tieTriangleCount
              << " tieTextures=" << tieTextures.textures.size()
              << " shrubClasses=" << staticScene.mesh.shrubClassCount
              << " shrubInstances=" << staticScene.mesh.shrubInstanceCount
              << " shrubTriangles=" << staticScene.mesh.shrubTriangleCount
              << " shrubTextures=" << shrubTextures.textures.size()
              << " skyShells=" << sky.mesh.shellCount
              << " skyClusters=" << sky.mesh.clusterCount
              << " skyTriangles=" << sky.mesh.triangleCount
              << " skyTextures=" << sky.mesh.textures.size()
              << " status=ok\n";
    std::cout << "[OpenRatchet:viewer] bounds raw=("
              << terrain.mesh.bounds.minX << ','
              << terrain.mesh.bounds.minY << ','
              << terrain.mesh.bounds.minZ << ")->("
              << terrain.mesh.bounds.maxX << ','
              << terrain.mesh.bounds.maxY << ','
              << terrain.mesh.bounds.maxZ << ") scale=" << scale << '\n';

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "OpenRatchet - Native R&C1 Scene");
    SetTargetFPS(60);

    std::vector<Texture2D> tfragGpu = uploadTextures(tfragTextures.textures);
    std::vector<Texture2D> tieGpu = uploadTextures(tieTextures.textures);
    std::vector<Texture2D> shrubGpu = uploadTextures(shrubTextures.textures);
    std::vector<Texture2D> skyGpu = uploadTextures(sky.mesh.textures);
    std::vector<NativeDrawBatch> terrainBatches;
    std::vector<NativeDrawBatch> staticBatches;
    std::vector<NativeDrawBatch> skyBatches;

    auto cleanup = [&]() {
        unloadBatches(terrainBatches);
        unloadBatches(staticBatches);
        unloadBatches(skyBatches);
        unloadTextures(tfragGpu);
        unloadTextures(tieGpu);
        unloadTextures(shrubGpu);
        unloadTextures(skyGpu);
        CloseWindow();
    };

    for (const auto& sourceBatch : terrain.mesh.batches) {
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                &tfragGpu,
                &tfragTextures.textures,
                [&](const ratchet::assets::Rac1TfragVertex& v) {
                    return toViewerSpace(v.x, v.y, v.z, center, scale);
                },
                terrainBatches)) {
            std::cerr << "[OpenRatchet:viewer] tfrag GPU mesh creation failed\n";
            cleanup();
            return 1;
        }
    }

    for (const auto& sourceBatch : staticScene.mesh.batches) {
        const bool tie = sourceBatch.kind == ratchet::assets::Rac1StaticMaterialKind::Tie;
        const auto& gpu = tie ? tieGpu : shrubGpu;
        const auto& source = tie ? tieTextures.textures : shrubTextures.textures;
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                &gpu,
                &source,
                [&](const ratchet::assets::Rac1StaticVertex& v) {
                    return toViewerSpace(v.x, v.y, v.z, center, scale);
                },
                staticBatches)) {
            std::cerr << "[OpenRatchet:viewer] tie/shrub GPU mesh creation failed\n";
            cleanup();
            return 1;
        }
    }

    for (const auto& sourceBatch : sky.mesh.batches) {
        const bool textured = sourceBatch.materialIndex != UINT32_MAX;
        const std::vector<Texture2D>* gpu = textured ? &skyGpu : nullptr;
        const std::vector<ratchet::assets::Rac1Texture>* source =
            textured ? &sky.mesh.textures : nullptr;
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                gpu,
                source,
                [&](const ratchet::assets::Rac1SkyVertex& v) {
                    const Vector3 p = mapWorld(v.x, v.y, v.z);
                    return Vector3{p.x * scale, p.y * scale, p.z * scale};
                },
                skyBatches)) {
            std::cerr << "[OpenRatchet:viewer] sky GPU mesh creation failed\n";
            cleanup();
            return 1;
        }
    }

    Camera3D camera{};
    camera.position = {70.0f, 50.0f, 70.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    const std::size_t totalTriangles = terrain.mesh.triangleCount +
                                       staticScene.mesh.tieTriangleCount +
                                       staticScene.mesh.shrubTriangleCount +
                                       sky.mesh.triangleCount;
    bool wireframe = false;
    while (!WindowShouldClose()) {
        // Match normal PC first-person/third-person camera behavior: once the
        // user clicks inside the native viewer, capture and hide the cursor so
        // mouse-look cannot escape onto another monitor/window. CloseWindow()
        // restores the cursor automatically when the viewer exits.
        if (IsWindowFocused() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            DisableCursor();
        }

        UpdateCamera(&camera, CAMERA_FREE);
        if (IsKeyPressed(KEY_TAB)) wireframe = !wireframe;

        BeginDrawing();
        ClearBackground({sky.mesh.clearColor[0],
                         sky.mesh.clearColor[1],
                         sky.mesh.clearColor[2],
                         255u});
        BeginMode3D(camera);
        rlDisableBackfaceCulling();
        if (wireframe) rlEnableWireMode();

        // Sky shells are authored camera-relative by the game. They are a
        // background layer, not world-space occluders. The retail renderer
        // effectively draws them with depth compare ALWAYS and depth writes
        // disabled; mirror that here so nearby shell geometry cannot hide
        // distant terrain/static scenery as the free camera moves around.
        rlDisableDepthTest();
        rlDisableDepthMask();
        drawBatches(skyBatches, camera.position);
        rlEnableDepthMask();
        rlEnableDepthTest();

        drawBatches(terrainBatches, {0.0f, 0.0f, 0.0f});
        drawBatches(staticBatches, {0.0f, 0.0f, 0.0f});

        if (wireframe) rlDisableWireMode();
        rlEnableBackfaceCulling();
        DrawGrid(20, 5.0f);
        EndMode3D();

        DrawRectangle(12, 12, 780, 110, {0, 0, 0, 170});
        DrawText("OpenRatchet native R&C1 scene: tfrags + ties + shrubs + sky",
                 24, 22, 22, RAYWHITE);
        DrawText(TextFormat("Level %u | %llu triangles | ties %llu | shrubs %llu | sky %llu | %s",
                            loaded.summary.tocIndex,
                            static_cast<unsigned long long>(totalTriangles),
                            static_cast<unsigned long long>(staticScene.mesh.tieInstanceCount),
                            static_cast<unsigned long long>(staticScene.mesh.shrubInstanceCount),
                            static_cast<unsigned long long>(sky.mesh.triangleCount),
                            wireframe ? "wireframe" : "textured"),
                 24, 50, 18, LIGHTGRAY);
        DrawText(TextFormat("Textures: tfrag %llu | tie %llu | shrub %llu | sky %llu",
                            static_cast<unsigned long long>(tfragTextures.textures.size()),
                            static_cast<unsigned long long>(tieTextures.textures.size()),
                            static_cast<unsigned long long>(shrubTextures.textures.size()),
                            static_cast<unsigned long long>(sky.mesh.textures.size())),
                 24, 74, 16, GRAY);
        DrawText("Click: lock mouse | WASD/mouse/wheel | TAB: wireframe | ESC: close",
                 24, 96, 16, GRAY);
        DrawFPS(GetScreenWidth() - 90, 16);
        EndDrawing();
    }

    cleanup();
    return 0;
}
