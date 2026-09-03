#include "assets/rac1_collision.h"
#include "assets/rac1_level.h"
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

namespace {

Color collisionColor(std::int16_t type) {
    static constexpr Color kColors[16] = {
        {77, 77, 230, 255},   // water
        {230, 26, 26, 255},   // damage
        {230, 230, 51, 255},  // mag boots
        {153, 128, 77, 255},
        {26, 102, 26, 255},
        {230, 153, 77, 255},
        {102, 179, 102, 255},
        {179, 230, 255, 255},
        {51, 51, 77, 255},
        {204, 128, 128, 255},
        {204, 153, 230, 255},
        {102, 77, 77, 255},
        {51, 51, 51, 255},
        {77, 230, 153, 255},
        {77, 77, 230, 255},
        {220, 220, 220, 255}, // normal terrain
    };
    if (type < 0) {
        return {128, 255, 128, 255};
    }
    return kColors[static_cast<unsigned>(type) & 0x0fu];
}

Vector3 toViewerSpace(const ratchet::assets::Rac1CollisionVertex& v,
                      Vector3 center,
                      float scale) {
    // R&C uses Z-up. Rotate -90 degrees around X for the Y-up PC viewer,
    // matching the established noclip/Wrench convention.
    const Vector3 mapped{v.x, v.z, -v.y};
    return {(mapped.x - center.x) * scale,
            (mapped.y - center.y) * scale,
            (mapped.z - center.z) * scale};
}

Vector3 mappedCenter(const ratchet::assets::Rac1CollisionBounds& b) {
    return {(b.minX + b.maxX) * 0.5f,
            (b.minZ + b.maxZ) * 0.5f,
            -(b.minY + b.maxY) * 0.5f};
}

float viewerScale(const ratchet::assets::Rac1CollisionBounds& b) {
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

    auto collision = ratchet::assets::decodeRac1Collision(
        loaded.core, loaded.summary.collisionOffset);
    if (!collision.ok()) {
        std::cerr << "[OpenRatchet:viewer] collision decode failed offset=0x"
                  << std::hex << loaded.summary.collisionOffset << std::dec
                  << " status=" << ratchet::assets::rac1CollisionStatusName(collision.status)
                  << '\n';
        return 1;
    }

    const std::size_t vertexCount = collision.mesh.triangleVertices.size();
    const std::size_t triangleCount = vertexCount / 3u;
    if (vertexCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::cerr << "[OpenRatchet:viewer] collision mesh too large\n";
        return 1;
    }

    const Vector3 center = mappedCenter(collision.mesh.bounds);
    const float scale = viewerScale(collision.mesh.bounds);

    std::cout << "[OpenRatchet:viewer] level=" << loaded.summary.tocIndex
              << " id=" << loaded.summary.levelId
              << " core=0x" << std::hex << loaded.core.size()
              << " collision=0x" << loaded.summary.collisionOffset << std::dec
              << " octants=" << collision.mesh.octantCount
              << " faces=" << collision.mesh.faceCount
              << " quads=" << collision.mesh.quadCount
              << " heroGroups=" << collision.mesh.heroGroupCount
              << " heroFaces=" << collision.mesh.heroFaceCount
              << " triangles=" << triangleCount
              << " status=ok\n";
    std::cout << "[OpenRatchet:viewer] bounds raw=("
              << collision.mesh.bounds.minX << ','
              << collision.mesh.bounds.minY << ','
              << collision.mesh.bounds.minZ << ")->("
              << collision.mesh.bounds.maxX << ','
              << collision.mesh.bounds.maxY << ','
              << collision.mesh.bounds.maxZ << ") scale=" << scale << '\n';

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "OpenRatchet - Native R&C1 Level Viewer");
    SetTargetFPS(60);

    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(vertexCount);
    mesh.triangleCount = static_cast<int>(triangleCount);
    mesh.vertices = static_cast<float*>(MemAlloc(vertexCount * 3u * sizeof(float)));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(vertexCount * 4u));

    for (std::size_t i = 0u; i < vertexCount; ++i) {
        const auto& source = collision.mesh.triangleVertices[i];
        const Vector3 p = toViewerSpace(source, center, scale);
        mesh.vertices[i * 3u + 0u] = p.x;
        mesh.vertices[i * 3u + 1u] = p.y;
        mesh.vertices[i * 3u + 2u] = p.z;
        const Color color = collisionColor(source.type);
        mesh.colors[i * 4u + 0u] = color.r;
        mesh.colors[i * 4u + 1u] = color.g;
        mesh.colors[i * 4u + 2u] = color.b;
        mesh.colors[i * 4u + 3u] = color.a;
    }

    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);

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
        if (wireframe) {
            rlEnableWireMode();
        }
        DrawModel(model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
        if (wireframe) {
            rlDisableWireMode();
        }
        rlEnableBackfaceCulling();
        DrawGrid(20, 5.0f);
        EndMode3D();

        DrawRectangle(12, 12, 565, 88, {0, 0, 0, 170});
        DrawText("OpenRatchet native R&C1 collision geometry", 24, 22, 22, RAYWHITE);
        DrawText(TextFormat("Level %u | %llu triangles | %s",
                            loaded.summary.tocIndex,
                            static_cast<unsigned long long>(triangleCount),
                            wireframe ? "wireframe" : "collision types"),
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

    UnloadModel(model);
    CloseWindow();
    return 0;
}
