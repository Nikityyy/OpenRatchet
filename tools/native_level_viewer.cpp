#include "assets/rac1_level.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"
#include "platform/native_vfs.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

Vector3 toViewerSpace(const ratchet::assets::Rac1TfragVertex& v,
                      Vector3 center,
                      float scale) {
    // R&C uses Z-up. Rotate -90 degrees around X for the Y-up PC viewer.
    const Vector3 mapped{v.x, v.z, -v.y};
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
    if (!std::isfinite(extent) || extent <= 0.0001f) {
        return 1.0f;
    }
    return 80.0f / extent;
}

const ratchet::platform::NativeAssetLocation* selectLevel(
    const ratchet::platform::NativeVfs& vfs,
    int requestedIndex) {
    if (requestedIndex >= 0) {
        const auto* level = vfs.findLevel(static_cast<std::uint32_t>(requestedIndex));
        if (level != nullptr && std::filesystem::is_regular_file(level->path)) {
            return level;
        }
        return nullptr;
    }
    for (const auto& level : vfs.levels()) {
        if (std::filesystem::is_regular_file(level.path)) {
            return &level;
        }
    }
    return nullptr;
}

struct NativeDrawBatch {
    Model model{};
    std::uint32_t materialIndex = 0u;
    bool transparent = false;
};

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

    auto textures = ratchet::assets::decodeRac1TfragTextures(
        loaded.core,
        loaded.coreIndex,
        loaded.gsRam,
        loaded.summary.tfragTextures,
        loaded.summary.texturesBaseOffset);
    if (!textures.ok()) {
        std::cerr << "[OpenRatchet:viewer] tfrag texture decode failed status="
                  << ratchet::assets::rac1TextureStatusName(textures.status) << '\n';
        return 1;
    }
    if (textures.textures.empty()) {
        std::cerr << "[OpenRatchet:viewer] tfrag texture table is empty\n";
        return 1;
    }

    auto terrain = ratchet::assets::decodeRac1TfragTerrain(
        loaded.core,
        loaded.summary.tfragsOffset,
        static_cast<std::uint32_t>(textures.textures.size()));
    if (!terrain.ok()) {
        std::cerr << "[OpenRatchet:viewer] tfrag decode failed offset=0x"
                  << std::hex << loaded.summary.tfragsOffset << std::dec
                  << " status=" << ratchet::assets::rac1TfragStatusName(terrain.status)
                  << '\n';
        return 1;
    }

    std::size_t vertexCount = 0u;
    for (const auto& batch : terrain.mesh.batches) {
        if (batch.triangleVertices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            std::cerr << "[OpenRatchet:viewer] tfrag batch too large\n";
            return 1;
        }
        vertexCount += batch.triangleVertices.size();
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
              << " textures=" << textures.textures.size()
              << " status=ok\n";
    std::cout << "[OpenRatchet:viewer] bounds raw=("
              << terrain.mesh.bounds.minX << ','
              << terrain.mesh.bounds.minY << ','
              << terrain.mesh.bounds.minZ << ")->("
              << terrain.mesh.bounds.maxX << ','
              << terrain.mesh.bounds.maxY << ','
              << terrain.mesh.bounds.maxZ << ") scale=" << scale << '\n';

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "OpenRatchet - Native R&C1 Textured Terrain");
    SetTargetFPS(60);

    std::vector<Texture2D> gpuTextures;
    gpuTextures.reserve(textures.textures.size());
    for (const auto& source : textures.textures) {
        Image image{};
        image.data = const_cast<std::uint8_t*>(source.rgba.data());
        image.width = static_cast<int>(source.width);
        image.height = static_cast<int>(source.height);
        image.mipmaps = 1;
        image.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        Texture2D texture = LoadTextureFromImage(image);
        SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
        gpuTextures.push_back(texture);
    }

    std::vector<NativeDrawBatch> drawBatches;
    drawBatches.reserve(terrain.mesh.batches.size());
    for (const auto& sourceBatch : terrain.mesh.batches) {
        if (sourceBatch.triangleVertices.empty()) {
            continue;
        }
        if (sourceBatch.materialIndex >= gpuTextures.size()) {
            std::cerr << "[OpenRatchet:viewer] material index escaped decoded texture table\n";
            for (Texture2D texture : gpuTextures) UnloadTexture(texture);
            CloseWindow();
            return 1;
        }

        const std::size_t batchVertexCount = sourceBatch.triangleVertices.size();
        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(batchVertexCount);
        mesh.triangleCount = static_cast<int>(batchVertexCount / 3u);
        mesh.vertices = static_cast<float*>(MemAlloc(batchVertexCount * 3u * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(batchVertexCount * 2u * sizeof(float)));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(batchVertexCount * 4u));
        if (mesh.vertices == nullptr || mesh.texcoords == nullptr || mesh.colors == nullptr) {
            std::cerr << "[OpenRatchet:viewer] GPU mesh allocation failed\n";
            CloseWindow();
            return 1;
        }

        for (std::size_t i = 0u; i < batchVertexCount; ++i) {
            const auto& source = sourceBatch.triangleVertices[i];
            const Vector3 p = toViewerSpace(source, center, scale);
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
            std::cerr << "[OpenRatchet:viewer] model material creation failed\n";
            UnloadModel(model);
            for (Texture2D texture : gpuTextures) UnloadTexture(texture);
            CloseWindow();
            return 1;
        }
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = gpuTextures[sourceBatch.materialIndex];
        drawBatches.push_back({model,
                               sourceBatch.materialIndex,
                               textures.textures[sourceBatch.materialIndex].hasAlpha});
    }

    Camera3D camera{};
    camera.position = {70.0f, 50.0f, 70.0f};
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    bool wireframe = false;
    while (!WindowShouldClose()) {
        UpdateCamera(&camera, CAMERA_FREE);
        if (IsKeyPressed(KEY_TAB)) {
            wireframe = !wireframe;
        }

        BeginDrawing();
        ClearBackground({18, 20, 24, 255});
        BeginMode3D(camera);
        rlDisableBackfaceCulling();
        if (wireframe) rlEnableWireMode();

        // Draw opaque batches first, then alpha-containing materials. This is
        // intentionally still a simple viewer; per-triangle depth sorting and
        // exact GS CLAMP state belong to renderer refinement, not asset decode.
        for (int transparentPass = 0; transparentPass < 2; ++transparentPass) {
            const bool wantTransparent = transparentPass != 0;
            for (const auto& batch : drawBatches) {
                if (batch.transparent == wantTransparent) {
                    DrawModel(batch.model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
                }
            }
        }

        if (wireframe) rlDisableWireMode();
        rlEnableBackfaceCulling();
        DrawGrid(20, 5.0f);
        EndMode3D();

        DrawRectangle(12, 12, 650, 88, {0, 0, 0, 170});
        DrawText("OpenRatchet native R&C1 textured tfrag terrain", 24, 22, 22, RAYWHITE);
        DrawText(TextFormat("Level %u | %llu triangles | %llu textures | %s",
                            loaded.summary.tocIndex,
                            static_cast<unsigned long long>(terrain.mesh.triangleCount),
                            static_cast<unsigned long long>(textures.textures.size()),
                            wireframe ? "wireframe" : "textured"),
                 24,
                 50,
                 18,
                 LIGHTGRAY);
        DrawText("Free camera: WASD/mouse/wheel | TAB: wireframe | ESC: close",
                 24,
                 74,
                 16,
                 GRAY);
        DrawFPS(GetScreenWidth() - 90, 16);
        EndDrawing();
    }

    for (auto& batch : drawBatches) {
        UnloadModel(batch.model);
    }
    for (Texture2D texture : gpuTextures) {
        UnloadTexture(texture);
    }
    CloseWindow();
    return 0;
}
