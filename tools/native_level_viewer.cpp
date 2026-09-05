#include "assets/rac1_level.h"
#include "assets/rac1_moby.h"
#include "assets/rac1_moby_animation.h"
#include "assets/rac1_sky.h"
#include "assets/rac1_static_scene.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"
#include "platform/native_vfs.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

struct NativeAnimatedMobyBatch {
    Model model{};
    bool transparent = false;
    std::vector<std::uint32_t> skinVertexIndices;
    std::vector<float> positions;
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

void unloadAnimatedMobyBatches(std::vector<NativeAnimatedMobyBatch>& batches) {
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

bool animatedMobyVertexMatches(const ratchet::assets::Rac1MobyVertex& vertex,
                               const ratchet::assets::Rac1MobyRenderedInstance& instance) noexcept {
    return vertex.oClass == instance.oClass && vertex.instanceIndex == instance.instanceIndex;
}

Vector3 animatedMobyViewerPosition(
    const ratchet::assets::Rac1MobyRenderedInstance& instance,
    const std::array<float, 3>& rawPosition,
    Vector3 center,
    float scale) {
    const auto world = ratchet::assets::transformRac1MobySkinnedPositionToWorld(
        instance, rawPosition);
    return toViewerSpace(world[0], world[1], world[2], center, scale);
}

bool appendAnimatedMobyBatch(
    const std::vector<ratchet::assets::Rac1MobyVertex>& vertices,
    std::uint32_t materialIndex,
    const std::vector<Texture2D>& gpuTextures,
    const std::vector<ratchet::assets::Rac1Texture>& sourceTextures,
    const ratchet::assets::Rac1MobyRenderedInstance& instance,
    const ratchet::assets::Rac1MobySkinExecution& execution,
    Vector3 center,
    float scale,
    std::vector<NativeAnimatedMobyBatch>& output) {
    if (vertices.empty()) return true;
    if (materialIndex >= gpuTextures.size() || materialIndex >= sourceTextures.size() ||
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
        return false;
    }

    NativeAnimatedMobyBatch batch{};
    batch.transparent = sourceTextures[materialIndex].hasAlpha;
    batch.skinVertexIndices.reserve(vertexCount);
    batch.positions.resize(vertexCount * 3u);
    for (std::size_t i = 0u; i < vertexCount; ++i) {
        const auto& source = vertices[i];
        if (!animatedMobyVertexMatches(source, instance) ||
            source.skinVertexIndex >= execution.vertices.size()) {
            return false;
        }
        const Vector3 p = animatedMobyViewerPosition(
            instance, execution.vertices[source.skinVertexIndex].position, center, scale);
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

bool updateAnimatedMobyBatches(
    std::vector<NativeAnimatedMobyBatch>& batches,
    const ratchet::assets::Rac1MobyRenderedInstance& instance,
    const ratchet::assets::Rac1MobySkinExecution& execution,
    Vector3 center,
    float scale) {
    for (auto& batch : batches) {
        if (batch.model.meshCount <= 0 || batch.model.meshes == nullptr ||
            batch.skinVertexIndices.size() * 3u != batch.positions.size()) {
            return false;
        }
        for (std::size_t i = 0u; i < batch.skinVertexIndices.size(); ++i) {
            const std::uint32_t source = batch.skinVertexIndices[i];
            if (source >= execution.vertices.size()) return false;
            const Vector3 p = animatedMobyViewerPosition(
                instance, execution.vertices[source].position, center, scale);
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

void drawAnimatedMobyBatches(const std::vector<NativeAnimatedMobyBatch>& batches) {
    for (int pass = 0; pass < 2; ++pass) {
        const bool wantTransparent = pass != 0;
        for (const auto& batch : batches) {
            if (batch.transparent == wantTransparent) {
                DrawModel(batch.model, {0.0f, 0.0f, 0.0f}, 1.0f, WHITE);
            }
        }
    }
}

template <std::size_t N>
std::string hexPrefix(const std::array<std::uint8_t, N>& bytes, std::size_t count) {
    static constexpr char kHex[] = "0123456789abcdef";
    count = std::min(count, N);
    std::string result;
    result.resize(count * 2u);
    for (std::size_t i = 0u; i < count; ++i) {
        result[i * 2u + 0u] = kHex[(bytes[i] >> 4u) & 0x0fu];
        result[i * 2u + 1u] = kHex[bytes[i] & 0x0fu];
    }
    return result;
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.resize(bytes.size() * 2u);
    for (std::size_t i = 0u; i < bytes.size(); ++i) {
        result[i * 2u + 0u] = kHex[(bytes[i] >> 4u) & 0x0fu];
        result[i * 2u + 1u] = kHex[bytes[i] & 0x0fu];
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const bool verboseAssetLogs = [] {
        const char* value = std::getenv("OPENRATCHET_VERBOSE_ASSET_LOGS");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();

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
    auto mobyTextures = ratchet::assets::decodeRac1PaletteTextures(
        loaded.core, loaded.coreIndex, loaded.gsRam,
        loaded.summary.mobyTextures, loaded.summary.texturesBaseOffset);
    auto shrubTextures = ratchet::assets::decodeRac1PaletteTextures(
        loaded.core, loaded.coreIndex, loaded.gsRam,
        loaded.summary.shrubTextures, loaded.summary.texturesBaseOffset);
    if (!tfragTextures.ok() || !tieTextures.ok() || !mobyTextures.ok() || !shrubTextures.ok()) {
        std::cerr << "[OpenRatchet:viewer] level texture decode failed"
                  << " tfrag=" << ratchet::assets::rac1TextureStatusName(tfragTextures.status)
                  << " tie=" << ratchet::assets::rac1TextureStatusName(tieTextures.status)
                  << " moby=" << ratchet::assets::rac1TextureStatusName(mobyTextures.status)
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

    auto mobys = ratchet::assets::decodeRac1MobyScene(
        loaded.core,
        loaded.coreIndex,
        loaded.gameplay,
        loaded.summary.mobyClasses,
        static_cast<std::uint32_t>(mobyTextures.textures.size()));
    if (!mobys.ok()) {
        std::cerr << "[OpenRatchet:viewer] moby decode failed status="
                  << ratchet::assets::rac1MobyStatusName(mobys.status) << '\n';
        return 1;
    }

    auto mobyAnimation = ratchet::assets::inspectRac1MobyAnimationMetadata(
        loaded.core, loaded.coreIndex, loaded.gameplay, loaded.summary.mobyClasses);
    if (!mobyAnimation.ok()) {
        std::cerr << "[OpenRatchet:viewer] moby animation metadata failed status="
                  << ratchet::assets::rac1MobyAnimationStatusName(mobyAnimation.status);
        if (mobyAnimation.failureOClassValid) {
            std::cerr << " oClass=" << mobyAnimation.failureOClass;
        }
        if (mobyAnimation.failureSequenceIndex >= 0) {
            std::cerr << " sequence=" << mobyAnimation.failureSequenceIndex
                      << " relative=" << mobyAnimation.failureSequenceRelative
                      << " tableOffset=0x" << std::hex
                      << mobyAnimation.failureSequenceTableOffset << std::dec;
        }
        if (mobyAnimation.failureSequenceReason !=
            ratchet::assets::Rac1MobySequenceFailure::None) {
            std::cerr << " sequenceReason="
                      << ratchet::assets::rac1MobySequenceFailureName(
                             mobyAnimation.failureSequenceReason);
            if (mobyAnimation.failureFrameIndex >= 0) {
                std::cerr << " frame=" << mobyAnimation.failureFrameIndex
                          << " frameOffset=0x" << std::hex
                          << mobyAnimation.failureFrameOffset << std::dec;
            }
        }
        if (mobyAnimation.failureRigReason != ratchet::assets::Rac1MobyRigFailure::None) {
            std::cerr << " rigReason="
                      << ratchet::assets::rac1MobyRigFailureName(mobyAnimation.failureRigReason)
                      << " expected=0x" << std::hex << mobyAnimation.failureRigExpectedOffset
                      << " actual=0x" << mobyAnimation.failureRigActualOffset << std::dec;
        }
        if (mobyAnimation.failureMatrixReason !=
            ratchet::assets::Rac1MobyMatrixTransferFailure::None) {
            std::cerr << " matrixReason="
                      << ratchet::assets::rac1MobyMatrixTransferFailureName(
                             mobyAnimation.failureMatrixReason);
            if (mobyAnimation.failurePacketIndex >= 0) {
                std::cerr << " packet=" << mobyAnimation.failurePacketIndex;
            }
            if (mobyAnimation.failureMatrixTransferIndex >= 0) {
                std::cerr << " transfer=" << mobyAnimation.failureMatrixTransferIndex;
            }
            if (mobyAnimation.failureMatrixScratchpadIndex >= 0) {
                std::cerr << " sprMatrix=" << mobyAnimation.failureMatrixScratchpadIndex;
            }
            if (mobyAnimation.failureMatrixVu0Destination >= 0) {
                std::cerr << " vu0Dest=0x" << std::hex
                          << mobyAnimation.failureMatrixVu0Destination << std::dec;
            }
        }
        if (mobyAnimation.failureSkinningReason !=
            ratchet::assets::Rac1MobySkinningFailure::None) {
            std::cerr << " skinningReason="
                      << ratchet::assets::rac1MobySkinningFailureName(
                             mobyAnimation.failureSkinningReason);
            if (mobyAnimation.failureSkinningPacketIndex >= 0) {
                std::cerr << " packet=" << mobyAnimation.failureSkinningPacketIndex;
            }
            if (mobyAnimation.failureSkinningVertexIndex >= 0) {
                std::cerr << " vertex=" << mobyAnimation.failureSkinningVertexIndex;
            }
            if (mobyAnimation.failureSkinningVertexKind !=
                ratchet::assets::Rac1MobySkinningVertexKind::None) {
                std::cerr << " kind="
                          << ratchet::assets::rac1MobySkinningVertexKindName(
                                 mobyAnimation.failureSkinningVertexKind);
            }
            if (mobyAnimation.failureSkinningAddress >= 0) {
                std::cerr << " address=0x" << std::hex
                          << mobyAnimation.failureSkinningAddress << std::dec;
            }
            if (mobyAnimation.metadata.threeWayBlendVertexCount != 0u) {
                std::cerr << " l3Encoding="
                          << ratchet::assets::rac1MobyPackedL3EncodingName(
                                 mobyAnimation.metadata.packedL3Encoding)
                          << " maxL3Raw=0x" << std::hex
                          << static_cast<unsigned>(mobyAnimation.metadata.maxPackedL3Raw)
                          << " maxL3Decoded=0x"
                          << static_cast<unsigned>(
                                 mobyAnimation.metadata.maxDecodedPackedL3Address)
                          << std::dec;
            }
        }
        std::cerr << '\n';
        return 1;
    }

    const ratchet::assets::Rac1MobyAnimationClass* ratchetBaseClass = nullptr;
    for (const auto& cls : mobyAnimation.metadata.classes) {
        if (cls.oClass == 0) {
            ratchetBaseClass = &cls;
            break;
        }
    }
    if (ratchetBaseClass == nullptr) {
        std::cerr << "[OpenRatchet:viewer] Ratchet animation class missing oClass=0\n";
        return 1;
    }
    auto ratchetAnimationBank = ratchet::assets::inspectRac1RatchetAnimationBank(
        loaded.core,
        loaded.coreIndex,
        loaded.summary.ratchetSequenceTableOffset,
        *ratchetBaseClass);
    if (!ratchetAnimationBank.ok()) {
        std::cerr << "[OpenRatchet:viewer] Ratchet animation bank failed status="
                  << ratchet::assets::rac1RatchetAnimationBankStatusName(
                         ratchetAnimationBank.status)
                  << " table=0x" << std::hex << loaded.summary.ratchetSequenceTableOffset
                  << std::dec;
        if (ratchetAnimationBank.failureSequenceIndex >= 0) {
            std::cerr << " seq=" << ratchetAnimationBank.failureSequenceIndex
                      << " pointer=0x" << std::hex
                      << ratchetAnimationBank.failureSequencePointer << std::dec;
        }
        if (ratchetAnimationBank.failureSequenceReason !=
            ratchet::assets::Rac1MobySequenceFailure::None) {
            std::cerr << " sequenceReason="
                      << ratchet::assets::rac1MobySequenceFailureName(
                             ratchetAnimationBank.failureSequenceReason);
        }
        if (ratchetAnimationBank.failureFrameIndex >= 0) {
            std::cerr << " frame=" << ratchetAnimationBank.failureFrameIndex
                      << " frameOffset=0x" << std::hex
                      << ratchetAnimationBank.failureFrameOffset << std::dec;
        }
        std::cerr << '\n';
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
    if (verboseAssetLogs) {
        for (const auto& skipped : mobys.mesh.skippedClasses) {
            std::cout << "[OpenRatchet:moby:skip] oClass=" << skipped.oClass
                      << " instances=" << skipped.instanceCount
                      << " reason=" << ratchet::assets::rac1MobySkipReasonName(skipped.reason)
                      << " sourceTriangles=" << skipped.sourceTriangleCount
                      << " specialDiscardTriangles=" << skipped.specialMaterialTriangleCount
                      << '\n';
        }
    }
    const std::size_t accountedMobyInstances =
        mobys.mesh.renderedInstanceCount + mobys.mesh.intentionallyNonVisibleInstanceCount;
    std::cout << "[OpenRatchet:moby] classes=" << mobys.mesh.classCount
              << " renderable=" << mobys.mesh.renderableClassCount
              << " instances=" << mobys.mesh.instanceCount
              << " rendered=" << mobys.mesh.renderedInstanceCount
              << " skipped=" << mobys.mesh.skippedInstanceCount
              << " intentionallyInvisible=" << mobys.mesh.intentionallyNonVisibleInstanceCount
              << " accounted=" << accountedMobyInstances
              << " missing=" << mobys.mesh.missingClassInstanceCount
              << " unaccounted=" << mobys.mesh.unaccountedInstanceCount
              << " sourceTriangles=" << mobys.mesh.sourceTriangleCount
              << " triangles=" << mobys.mesh.triangleCount
              << " specialDiscardTriangles=" << mobys.mesh.specialMaterialTriangleCount
              << " textures=" << mobyTextures.textures.size()
              << " status=ok\n";
    if (verboseAssetLogs) {
        for (const auto& cls : mobyAnimation.metadata.classes) {
            if (!cls.hasMesh() && !cls.hasSkeleton() && !cls.hasSequences()) continue;
            std::cout << "[OpenRatchet:moby:anim:class] oClass=" << cls.oClass
                      << " instances=" << cls.instanceCount
                      << " classOffset=0x" << std::hex << cls.classOffset
                      << " classEnd=0x" << cls.classEndOffset << std::dec
                      << " joints=" << static_cast<unsigned>(cls.jointCount)
                      << " sequenceSlots=" << static_cast<unsigned>(cls.sequenceCount)
                      << " sequencesPresent=" << cls.presentSequenceCount
                      << " sequenceHoles=" << cls.nullSequenceCount
                      << " decodedSequences=" << cls.sequenceLayouts.size()
                      << " frames=" << cls.totalFrameCount
                      << " nonFfByte11=" << cls.nonFfHeaderByte11Count
                      << " frameProbes=" << [&cls]() {
                             std::size_t count = 0u;
                             for (const auto& layout : cls.sequenceLayouts) count += layout.frameProbes.size();
                             return count;
                         }()
                      << " skeletonMatrices=" << cls.skeletonMatrices.size()
                      << " commonRecords=" << cls.commonTransformWords.size()
                      << " highLodPackets=" << static_cast<unsigned>(cls.highLodPacketCount)
                      << " lowLodPackets=" << static_cast<unsigned>(cls.lowLodPacketCount)
                      << " matrixTransfers=" << cls.matrixTransferCount
                      << " blend2=" << cls.twoWayBlendVertexCount
                      << " blend3=" << cls.threeWayBlendVertexCount
                      << " main=" << cls.mainVertexCount
                      << " skinVertices=" << cls.skinningVertexCount
                      << " documentedLayoutBad=" << cls.documentedLayoutInvalidAddresses
                      << " swappedLayoutBad=" << cls.swappedLayoutInvalidAddresses
                      << " maxMainSpr=" << static_cast<unsigned>(cls.maxMainScratchpadMatrixIndex)
                      << " maxPackedVu0=0x" << std::hex
                      << static_cast<unsigned>(cls.maxPackedVu0Address) << std::dec
                      << " maxSprMatrix=" << static_cast<unsigned>(cls.maxScratchpadMatrixIndex)
                      << " maxVu0Dest=0x" << std::hex << static_cast<unsigned>(cls.maxVu0Destination)
                      << " skeleton=0x" << cls.skeletonOffset
                      << " common=0x" << cls.commonTransformOffset
                      << " jointsData=0x" << cls.jointsOffset;
            if (cls.threeWayBlendVertexCount != 0u) {
                std::cout << " maxL3Raw=0x" << std::hex
                          << static_cast<unsigned>(cls.maxPackedL3Raw)
                          << " maxL3Decoded=0x"
                          << static_cast<unsigned>(cls.maxPackedL3Raw) * 2u;
            }
            const auto firstSequence = std::find_if(
                cls.sequenceOffsets.begin(), cls.sequenceOffsets.end(),
                [](std::uint32_t offset) { return offset != 0u; });
            if (firstSequence != cls.sequenceOffsets.end()) {
                std::cout << " firstSeq=0x" << *firstSequence;
            }
            std::cout << std::dec << '\n';

            for (const auto& rig : cls.rigProbes) {
                std::cout << "[OpenRatchet:moby:rig] oClass=" << cls.oClass
                          << " kind=" << ratchet::assets::rac1MobyRigProbeKindName(rig.kind)
                          << " offset=0x" << std::hex << rig.offset
                          << " absolute=0x" << (cls.classOffset + rig.offset)
                          << " next=0x" << rig.nextBoundaryOffset
                          << std::dec
                          << " prefixBytes=" << rig.prefixSize
                          << " prefixFnv=0x" << std::hex << rig.prefixFnv1a << std::dec
                          << " data=" << hexPrefix(rig.prefix, rig.prefixSize)
                          << '\n';
            }
            for (const auto& sequence : cls.sequenceProbes) {
                std::cout << "[OpenRatchet:moby:seq] oClass=" << cls.oClass
                          << " seq=" << static_cast<unsigned>(sequence.sequenceIndex)
                          << " offset=0x" << std::hex << sequence.offset
                          << " absolute=0x" << (cls.classOffset + sequence.offset)
                          << " next=0x" << sequence.nextBoundaryOffset
                          << std::dec
                          << " slotsAtPayload=" << sequence.aliasCount
                          << " prefixBytes=" << sequence.prefixSize
                          << " prefixFnv=0x" << std::hex << sequence.prefixFnv1a << std::dec
                          << " data=" << hexPrefix(sequence.prefix, sequence.prefixSize)
                          << '\n';
            }
            for (const auto& layout : cls.sequenceLayouts) {
                std::cout << "[OpenRatchet:moby:seq:layout] oClass=" << cls.oClass
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frames=" << static_cast<unsigned>(layout.frameCount)
                          << " byte11=";
                if (layout.headerByte11 == 0xffu) {
                    std::cout << "ff";
                } else {
                    std::cout << static_cast<unsigned>(layout.headerByte11);
                }
                std::cout << " control=" << static_cast<unsigned>(layout.controlByte)
                          << " reservedByte=" << static_cast<unsigned>(layout.reservedByte)
                          << " reservedWord=0x" << std::hex << layout.reservedWord
                          << std::dec << " scalar=" << layout.headerScalar
                          << " firstFrame=0x" << std::hex << layout.firstFrameOffset
                          << " lastFrame=0x" << layout.lastFrameOffset
                          << " minStride=0x" << layout.minFrameStride
                          << " maxStride=0x" << layout.maxFrameStride
                          << std::dec
                          << " strideKinds=" << layout.uniqueFrameStrides.size()
                          << '\n';
                for (const auto& frame : layout.frameProbes) {
                    std::cout << "[OpenRatchet:moby:frame] oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(frame.sequenceIndex)
                              << " frame=" << static_cast<unsigned>(frame.frameIndex)
                              << " offset=0x" << std::hex << frame.offset
                              << " stride=0x" << frame.stride
                              << std::dec
                              << " bytes=" << frame.payload.size()
                              << " fnv=0x" << std::hex << frame.payloadFnv1a << std::dec
                              << " data=" << hexBytes(frame.payload)
                              << '\n';
                }
            }
        }
    }
    std::size_t poseScannedFrames = 0u;
    std::size_t decodedPoseFrames = 0u;
    std::size_t densePoseFrames = 0u;
    std::size_t sparseDecodedPoseFrames = 0u;
    std::size_t sparseTranslationDecodedFrames = 0u;
    std::size_t unsupportedSparseQuaternionFrames = 0u;
    std::size_t sparseStream1Entries = 0u;
    std::size_t sparseStream1ActiveEntries = 0u;
    std::size_t sparseStream2Entries = 0u;
    std::size_t poseMatrices = 0u;
    std::size_t poseRoots = 0u;
    std::size_t poseParentLinks = 0u;
    float maxPoseQuaternionNormError = 0.0f;
    float maxPoseLocalTranslation = 0.0f;
    std::size_t skinExecutionCount = 0u;
    std::size_t skinExecutionPackets = 0u;
    std::size_t skinExecutionVertices = 0u;
    std::size_t skinExecutionMatrixTransfers = 0u;
    std::size_t skinExecutionTwoWay = 0u;
    std::size_t skinExecutionThreeWay = 0u;
    std::size_t skinExecutionMain = 0u;
    std::size_t skinExecutionSkeletonPostComposes = 0u;
    std::size_t skinExecutionVu0Writes = 0u;
    std::size_t skinExecutionCrossPacketReads = 0u;
    float skinExecutionMaxWeightSumError = 0.0f;
    float skinExecutionMaxAbsPosition = 0.0f;
    for (const auto& cls : mobyAnimation.metadata.classes) {
        if (!cls.hasSkeleton()) continue;
        for (const auto& layout : cls.sequenceLayouts) {
            for (std::size_t frameIndex = 0u; frameIndex < layout.frameOffsets.size(); ++frameIndex) {
                ++poseScannedFrames;
                const auto pose = ratchet::assets::decodeRac1MobyPoseFrame(
                    loaded.core,
                    cls,
                    layout.sequenceIndex,
                    static_cast<std::uint8_t>(frameIndex));
                sparseStream1Entries += pose.pose.stream1Count;
                sparseStream1ActiveEntries += pose.pose.stream1ActiveCount;
                sparseStream2Entries += pose.pose.stream2Count;
                if (pose.status == ratchet::assets::Rac1MobyPoseStatus::UnsupportedSparseFrame) {
                    ++unsupportedSparseQuaternionFrames;
                    continue;
                }
                if (!pose.ok()) {
                    std::cerr << "[OpenRatchet:viewer] moby pose failed status="
                              << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                              << " oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                              << " frame=" << frameIndex
                              << " joint=" << pose.failureJointIndex
                              << " parent=0x" << std::hex << pose.failureParentPointer
                              << " payloadQwords=0x" << pose.pose.payloadQwordCount
                              << " stream1Offset=0x" << pose.pose.stream1Offset
                              << " stream1Count=0x" << pose.pose.stream1Count
                              << " stream1Active=0x" << pose.pose.stream1ActiveCount
                              << " stream2Offset=0x" << pose.pose.stream2Offset
                              << " stream2Count=0x" << pose.pose.stream2Count
                              << std::dec
                              << " sparseEntry=" << pose.failureSparseEntryIndex
                              << " sparseJoint=" << pose.failureSparseJointIndex << '\n';
                    return 1;
                }
                ++decodedPoseFrames;
                const bool sparse = pose.pose.stream1Count != 0u || pose.pose.stream2Count != 0u;
                if (sparse) {
                    ++sparseDecodedPoseFrames;
                    if (pose.pose.stream2Count != 0u) ++sparseTranslationDecodedFrames;
                } else {
                    ++densePoseFrames;
                }
                poseMatrices += pose.pose.jointMatrices.size();
                poseRoots += pose.pose.rootJointCount;
                poseParentLinks += pose.pose.parentLinkCount;
                maxPoseQuaternionNormError =
                    std::max(maxPoseQuaternionNormError, pose.pose.maxQuaternionNormError);
                maxPoseLocalTranslation =
                    std::max(maxPoseLocalTranslation, pose.pose.maxAbsLocalTranslation);

                if (cls.hasMesh()) {
                    const auto skin = ratchet::assets::executeRac1MobySkinningProgram(cls, pose.pose);
                    if (!skin.ok()) {
                        std::cerr << "[OpenRatchet:viewer] moby skin execution failed status="
                                  << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                                  << " oClass=" << cls.oClass
                                  << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                                  << " frame=" << frameIndex
                                  << " packet=" << skin.failurePacketIndex
                                  << " vertex=" << skin.failureVertexIndex
                                  << " kind="
                                  << ratchet::assets::rac1MobySkinningVertexKindName(
                                         skin.failureVertexKind)
                                  << " joint=" << skin.failureJointIndex
                                  << " vu0=";
                        if (skin.failureVu0Address < 0) {
                            std::cerr << "n/a";
                        } else {
                            std::cerr << "0x" << std::hex << skin.failureVu0Address << std::dec;
                        }
                        std::cerr << '\n';
                        return 1;
                    }
                    ++skinExecutionCount;
                    skinExecutionPackets += skin.execution.packetCount;
                    skinExecutionVertices += skin.execution.vertices.size();
                    skinExecutionMatrixTransfers += skin.execution.matrixTransferCount;
                    skinExecutionTwoWay += skin.execution.twoWayVertexCount;
                    skinExecutionThreeWay += skin.execution.threeWayVertexCount;
                    skinExecutionMain += skin.execution.mainVertexCount;
                    skinExecutionSkeletonPostComposes +=
                        skin.execution.skeletonPostComposeCount;
                    skinExecutionVu0Writes += skin.execution.vu0MatrixWrites;
                    skinExecutionCrossPacketReads += skin.execution.vu0CrossPacketReads;
                    skinExecutionMaxWeightSumError = std::max(
                        skinExecutionMaxWeightSumError, skin.execution.maxWeightSumError);
                    skinExecutionMaxAbsPosition = std::max(
                        skinExecutionMaxAbsPosition, skin.execution.maxAbsPosition);
                }
            }
        }
    }
    if (poseScannedFrames != decodedPoseFrames + unsupportedSparseQuaternionFrames ||
        densePoseFrames == 0u || poseMatrices == 0u) {
        std::cerr << "[OpenRatchet:viewer] moby pose gate incomplete scanned="
                  << poseScannedFrames
                  << " decoded=" << decodedPoseFrames
                  << " dense=" << densePoseFrames
                  << " sparseDecoded=" << sparseDecodedPoseFrames
                  << " sparseQuaternionUnsupported=" << unsupportedSparseQuaternionFrames
                  << " matrices=" << poseMatrices << '\n';
        return 1;
    }
    std::cout << "[OpenRatchet:moby:pose] scannedFrames=" << poseScannedFrames
              << " decodedFrames=" << decodedPoseFrames
              << " denseFrames=" << densePoseFrames
              << " sparseDecodedFrames=" << sparseDecodedPoseFrames
              << " sparseQuaternionUnsupportedFrames=" << unsupportedSparseQuaternionFrames
              << " matrices=" << poseMatrices
              << " roots=" << poseRoots
              << " parentLinks=" << poseParentLinks
              << " maxQuaternionNormError=" << maxPoseQuaternionNormError
              << " maxAbsLocalTranslation=" << maxPoseLocalTranslation
              << " status=ok\n";
    std::cout << "[OpenRatchet:moby:sparse] framesWithStreams="
              << (sparseDecodedPoseFrames + unsupportedSparseQuaternionFrames)
              << " stream1Entries=" << sparseStream1Entries
              << " stream1Active=" << sparseStream1ActiveEntries
              << " stream2Entries=" << sparseStream2Entries
              << " decodedTranslationFrames=" << sparseTranslationDecodedFrames
              << " quaternionPolicy=retail-sign-gated-no-approximation"
              << " translationCodec=s16xyz-joint-byte6"
              << " status=ok\n";
    if (skinExecutionCount == 0u || skinExecutionPackets == 0u ||
        skinExecutionVertices == 0u || skinExecutionSkeletonPostComposes == 0u) {
        std::cerr << "[OpenRatchet:viewer] moby skin execution gate incomplete executions="
                  << skinExecutionCount << " packets=" << skinExecutionPackets
                  << " vertices=" << skinExecutionVertices
                  << " skeletonPostComposes=" << skinExecutionSkeletonPostComposes << '\n';
        return 1;
    }
    std::cout << "[OpenRatchet:moby:skinexec] executions=" << skinExecutionCount
              << " packets=" << skinExecutionPackets
              << " vertices=" << skinExecutionVertices
              << " matrixTransfers=" << skinExecutionMatrixTransfers
              << " blend2=" << skinExecutionTwoWay
              << " blend3=" << skinExecutionThreeWay
              << " main=" << skinExecutionMain
              << " skeletonPostComposes=" << skinExecutionSkeletonPostComposes
              << " vu0Writes=" << skinExecutionVu0Writes
              << " crossPacketReads=" << skinExecutionCrossPacketReads
              << " maxWeightByteSumError=" << skinExecutionMaxWeightSumError
              << " maxAbsRawPosition=" << skinExecutionMaxAbsPosition
              << " weights=byte-over-255"
              << " renderPalette=pose-times-class14-retail-postcompose"
              << " vu0State=persistent-between-packets"
              << " status=ok\n";
    // Phase 10 Step 12A: the oClass-0 sequence table selected by
    // LevelCoreHeader +0x78 is an external core-index table. Execute every one
    // of those frames through the already-proven pose + retail post-compose +
    // CPU skinning pipeline. This is deliberately a separate gate from the
    // ordinary moby aggregate so the established 847/847 baseline remains
    // directly comparable while Ratchet's exceptional storage contract is
    // validated independently.
    const auto& ratchetAnimatedClass = ratchetAnimationBank.bank.animationClass;
    std::size_t ratchetDecodedFrames = 0u;
    std::size_t ratchetMatrices = 0u;
    std::size_t ratchetRoots = 0u;
    std::size_t ratchetParentLinks = 0u;
    std::size_t ratchetStream1Entries = 0u;
    std::size_t ratchetStream1Active = 0u;
    std::size_t ratchetStream2Entries = 0u;
    std::size_t ratchetTranslationFrames = 0u;
    float ratchetMaxQuaternionNormError = 0.0f;
    float ratchetMaxLocalTranslation = 0.0f;

    std::size_t ratchetSkinExecutions = 0u;
    std::size_t ratchetSkinPackets = 0u;
    std::size_t ratchetSkinVertices = 0u;
    std::size_t ratchetSkinMatrixTransfers = 0u;
    std::size_t ratchetSkinTwoWay = 0u;
    std::size_t ratchetSkinThreeWay = 0u;
    std::size_t ratchetSkinMain = 0u;
    std::size_t ratchetSkinPostComposes = 0u;
    std::size_t ratchetSkinVu0Writes = 0u;
    std::size_t ratchetSkinCrossPacketReads = 0u;
    float ratchetSkinMaxWeightSumError = 0.0f;
    float ratchetSkinMaxAbsPosition = 0.0f;

    for (const auto& layout : ratchetAnimatedClass.sequenceLayouts) {
        if (layout.storage != ratchet::assets::Rac1MobySequenceStorage::RatchetExternal) {
            std::cerr << "[OpenRatchet:viewer] Ratchet sequence storage mismatch seq="
                      << static_cast<unsigned>(layout.sequenceIndex)
                      << " storage="
                      << ratchet::assets::rac1MobySequenceStorageName(layout.storage) << '\n';
            return 1;
        }
        for (std::size_t frameIndex = 0u; frameIndex < layout.frameOffsets.size(); ++frameIndex) {
            const auto pose = ratchet::assets::decodeRac1MobyPoseFrame(
                loaded.core,
                ratchetAnimatedClass,
                layout.sequenceIndex,
                static_cast<std::uint8_t>(frameIndex));
            ratchetStream1Entries += pose.pose.stream1Count;
            ratchetStream1Active += pose.pose.stream1ActiveCount;
            ratchetStream2Entries += pose.pose.stream2Count;
            if (!pose.ok()) {
                std::cerr << "[OpenRatchet:viewer] Ratchet pose failed status="
                          << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frame=" << frameIndex
                          << " payloadQwords=0x" << std::hex << pose.pose.payloadQwordCount
                          << " stream1Offset=0x" << pose.pose.stream1Offset
                          << " stream1Count=0x" << pose.pose.stream1Count
                          << " stream1Active=0x" << pose.pose.stream1ActiveCount
                          << " stream2Offset=0x" << pose.pose.stream2Offset
                          << " stream2Count=0x" << pose.pose.stream2Count
                          << std::dec
                          << " sparseEntry=" << pose.failureSparseEntryIndex
                          << " sparseJoint=" << pose.failureSparseJointIndex << '\n';
                return 1;
            }
            ++ratchetDecodedFrames;
            if (pose.pose.stream2Count != 0u) ++ratchetTranslationFrames;
            ratchetMatrices += pose.pose.jointMatrices.size();
            ratchetRoots += pose.pose.rootJointCount;
            ratchetParentLinks += pose.pose.parentLinkCount;
            ratchetMaxQuaternionNormError = std::max(
                ratchetMaxQuaternionNormError, pose.pose.maxQuaternionNormError);
            ratchetMaxLocalTranslation = std::max(
                ratchetMaxLocalTranslation, pose.pose.maxAbsLocalTranslation);

            const auto skin = ratchet::assets::executeRac1MobySkinningProgram(
                ratchetAnimatedClass, pose.pose);
            if (!skin.ok()) {
                std::cerr << "[OpenRatchet:viewer] Ratchet skin execution failed status="
                          << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frame=" << frameIndex
                          << " packet=" << skin.failurePacketIndex
                          << " vertex=" << skin.failureVertexIndex
                          << " kind="
                          << ratchet::assets::rac1MobySkinningVertexKindName(
                                 skin.failureVertexKind)
                          << " joint=" << skin.failureJointIndex
                          << " vu0=" << skin.failureVu0Address << '\n';
                return 1;
            }
            ++ratchetSkinExecutions;
            ratchetSkinPackets += skin.execution.packetCount;
            ratchetSkinVertices += skin.execution.vertices.size();
            ratchetSkinMatrixTransfers += skin.execution.matrixTransferCount;
            ratchetSkinTwoWay += skin.execution.twoWayVertexCount;
            ratchetSkinThreeWay += skin.execution.threeWayVertexCount;
            ratchetSkinMain += skin.execution.mainVertexCount;
            ratchetSkinPostComposes += skin.execution.skeletonPostComposeCount;
            ratchetSkinVu0Writes += skin.execution.vu0MatrixWrites;
            ratchetSkinCrossPacketReads += skin.execution.vu0CrossPacketReads;
            ratchetSkinMaxWeightSumError = std::max(
                ratchetSkinMaxWeightSumError, skin.execution.maxWeightSumError);
            ratchetSkinMaxAbsPosition = std::max(
                ratchetSkinMaxAbsPosition, skin.execution.maxAbsPosition);
        }
    }

    if (ratchetAnimationBank.bank.sequenceCount != ratchetAnimatedClass.sequenceCount ||
        ratchetDecodedFrames != ratchetAnimationBank.bank.totalFrameCount ||
        ratchetDecodedFrames == 0u || ratchetMatrices == 0u ||
        ratchetStream1Active != 0u ||
        ratchetSkinExecutions != ratchetDecodedFrames || ratchetSkinVertices == 0u ||
        ratchetSkinPostComposes == 0u) {
        std::cerr << "[OpenRatchet:viewer] Ratchet animation gate incomplete sequences="
                  << ratchetAnimationBank.bank.sequenceCount
                  << " expectedSlots=" << static_cast<unsigned>(ratchetAnimatedClass.sequenceCount)
                  << " frames=" << ratchetDecodedFrames
                  << " expectedFrames=" << ratchetAnimationBank.bank.totalFrameCount
                  << " matrices=" << ratchetMatrices
                  << " stream1Active=" << ratchetStream1Active
                  << " skinExecutions=" << ratchetSkinExecutions
                  << " skinVertices=" << ratchetSkinVertices << '\n';
        return 1;
    }
    std::cout << "[OpenRatchet:ratchet:anim] table=0x" << std::hex
              << ratchetAnimationBank.bank.sequenceTableOffset << std::dec
              << " sequences=" << ratchetAnimationBank.bank.sequenceCount
              << " frames=" << ratchetDecodedFrames
              << " joints=" << static_cast<unsigned>(ratchetAnimatedClass.jointCount)
              << " nonFfByte11=" << ratchetAnimationBank.bank.nonFfHeaderByte11Count
              << " minFrameStride=0x" << std::hex
              << ratchetAnimationBank.bank.minFrameStride
              << " maxFrameStride=0x" << ratchetAnimationBank.bank.maxFrameStride << std::dec
              << " uniqueFrameStrides=" << ratchetAnimationBank.bank.uniqueFrameStrideCount
              << " frameProbes=" << ratchetAnimationBank.bank.frameProbeCount
              << " oversizedFrameProbes=" << ratchetAnimationBank.bank.oversizedFrameProbeCount
              << " stream1Entries=" << ratchetStream1Entries
              << " stream1Active=" << ratchetStream1Active
              << " stream2Entries=" << ratchetStream2Entries
              << " translationFrames=" << ratchetTranslationFrames
              << " matrices=" << ratchetMatrices
              << " roots=" << ratchetRoots
              << " parentLinks=" << ratchetParentLinks
              << " maxQuaternionNormError=" << ratchetMaxQuaternionNormError
              << " maxAbsLocalTranslation=" << ratchetMaxLocalTranslation
              << " tableSource=core-header+0x78"
              << " sequencePointers=core-absolute"
              << " framePointers=sequence-relative"
              << " status=ok\n";
    std::cout << "[OpenRatchet:ratchet:skinexec] executions=" << ratchetSkinExecutions
              << " packets=" << ratchetSkinPackets
              << " vertices=" << ratchetSkinVertices
              << " matrixTransfers=" << ratchetSkinMatrixTransfers
              << " blend2=" << ratchetSkinTwoWay
              << " blend3=" << ratchetSkinThreeWay
              << " main=" << ratchetSkinMain
              << " skeletonPostComposes=" << ratchetSkinPostComposes
              << " vu0Writes=" << ratchetSkinVu0Writes
              << " crossPacketReads=" << ratchetSkinCrossPacketReads
              << " maxWeightByteSumError=" << ratchetSkinMaxWeightSumError
              << " maxAbsRawPosition=" << ratchetSkinMaxAbsPosition
              << " renderPalette=pose-times-class14-retail-postcompose"
              << " status=ok\n";

    std::size_t paletteClasses = 0u;
    std::size_t paletteJoints = 0u;
    std::size_t paletteDirectReferences = 0u;
    std::size_t paletteMatrixTransfers = 0u;
    std::size_t paletteTwoWayDirect = 0u;
    std::size_t paletteMainDirect = 0u;
    std::uint8_t paletteMaxJointIndex = 0u;
    for (const auto& cls : mobyAnimation.metadata.classes) {
        if (!cls.hasSkeleton() || !cls.hasMesh()) continue;
        const auto palette = ratchet::assets::inspectRac1MobyPosePaletteContract(cls);
        if (!palette.ok()) {
            std::cerr << "[OpenRatchet:viewer] moby pose palette failed status="
                      << ratchet::assets::rac1MobyPosePaletteStatusName(palette.status)
                      << " oClass=" << cls.oClass
                      << " source="
                      << ratchet::assets::rac1MobyPosePaletteSourceKindName(
                             palette.failureSourceKind)
                      << " joint=" << palette.failureJointIndex
                      << " jointCount=" << static_cast<unsigned>(cls.jointCount) << '\n';
            return 1;
        }
        ++paletteClasses;
        paletteJoints += palette.contract.jointCount;
        paletteDirectReferences += palette.contract.directSourceReferences;
        paletteMatrixTransfers += palette.contract.matrixTransferReferences;
        paletteTwoWayDirect += palette.contract.twoWayDirectReferences;
        paletteMainDirect += palette.contract.mainDirectReferences;
        paletteMaxJointIndex = std::max(
            paletteMaxJointIndex, palette.contract.maxReferencedJointIndex);
    }
    if (paletteClasses == 0u || paletteJoints == 0u || paletteDirectReferences == 0u) {
        std::cerr << "[OpenRatchet:viewer] moby pose palette gate incomplete classes="
                  << paletteClasses << " joints=" << paletteJoints
                  << " directReferences=" << paletteDirectReferences << '\n';
        return 1;
    }
    std::cout << "[OpenRatchet:moby:palette] classes=" << paletteClasses
              << " joints=" << paletteJoints
              << " directReferences=" << paletteDirectReferences
              << " matrixTransfers=" << paletteMatrixTransfers
              << " twoWayDirect=" << paletteTwoWayDirect
              << " mainDirect=" << paletteMainDirect
              << " maxJointIndex=" << static_cast<unsigned>(paletteMaxJointIndex)
              << " sourceDomain=pose-joint-index"
              << " boneExport=direct-0x40"
              << " bindPolicy=no-inferred-composition"
              << " status=ok\n";
    std::cout << "[OpenRatchet:moby:anim] referencedClasses="
              << mobyAnimation.metadata.referencedClassCount
              << " renderableClasses=" << mobyAnimation.metadata.renderableClassCount
              << " skeletalClasses=" << mobyAnimation.metadata.skeletalClassCount
              << " sequencedClasses=" << mobyAnimation.metadata.sequencedClassCount
              << " instances=" << mobyAnimation.metadata.instanceCount
              << " skeletalInstances=" << mobyAnimation.metadata.skeletalInstanceCount
              << " sequencedInstances=" << mobyAnimation.metadata.sequencedInstanceCount
              << " sequenceSlots=" << mobyAnimation.metadata.sequenceSlotCount
              << " sequencesPresent=" << mobyAnimation.metadata.presentSequenceCount
              << " sequenceHoles=" << mobyAnimation.metadata.nullSequenceCount
              << " uniqueSequencePayloads=" << mobyAnimation.metadata.uniqueSequencePayloadCount
              << " aliasedSequenceSlots=" << mobyAnimation.metadata.aliasedSequenceSlotCount
              << " packets=" << mobyAnimation.metadata.packetCount
              << " matrixTransfers=" << mobyAnimation.metadata.matrixTransferCount
              << " blend2=" << mobyAnimation.metadata.twoWayBlendVertexCount
              << " blend3=" << mobyAnimation.metadata.threeWayBlendVertexCount
              << " main=" << mobyAnimation.metadata.mainVertexCount
              << " status=ok\n";
    std::cout << "[OpenRatchet:moby:seqdecode] sequences="
              << mobyAnimation.metadata.decodedSequenceCount
              << " frames=" << mobyAnimation.metadata.totalFrameCount
              << " nonFfByte11=" << mobyAnimation.metadata.nonFfHeaderByte11Count
              << " skeletonMatrices=" << mobyAnimation.metadata.skeletonMatrixCount
              << " commonRecords=" << mobyAnimation.metadata.commonTransformRecordCount
              << " minFrameStride=0x" << std::hex << mobyAnimation.metadata.minFrameStride
              << " maxFrameStride=0x" << mobyAnimation.metadata.maxFrameStride
              << std::dec
              << " frameProbes=" << mobyAnimation.metadata.frameProbeCount
              << " uniqueFrameStrides=" << mobyAnimation.metadata.uniqueFrameStrideCount
              << " oversizedFrameProbes=" << mobyAnimation.metadata.oversizedFrameProbeCount
              << " maxProbedFrameBytes=" << mobyAnimation.metadata.maxProbedFrameBytes
              << " status=ok\n";
    std::cout << "[OpenRatchet:moby:skin] vertices="
              << mobyAnimation.metadata.skinningVertexCount
              << " blendLayout="
              << ratchet::assets::rac1MobyBlendLayoutName(mobyAnimation.metadata.blendLayout)
              << " documentedLayoutBad="
              << mobyAnimation.metadata.documentedLayoutInvalidAddresses
              << " swappedLayoutBad="
              << mobyAnimation.metadata.swappedLayoutInvalidAddresses
              << " l3Encoding="
              << ratchet::assets::rac1MobyPackedL3EncodingName(
                     mobyAnimation.metadata.packedL3Encoding)
              << " maxL3Raw=0x" << std::hex
              << static_cast<unsigned>(mobyAnimation.metadata.maxPackedL3Raw)
              << " maxL3Decoded=0x"
              << static_cast<unsigned>(mobyAnimation.metadata.maxDecodedPackedL3Address)
              << std::dec
              << " status=ok\n";
    std::size_t sequenceProbeCount = 0u;
    std::size_t rigProbeCount = 0u;
    for (const auto& cls : mobyAnimation.metadata.classes) {
        sequenceProbeCount += cls.sequenceProbes.size();
        rigProbeCount += cls.rigProbes.size();
    }
    std::cout << "[OpenRatchet:moby:seqprobe] sequenceProbes=" << sequenceProbeCount
              << " uniqueSequencePayloads=" << mobyAnimation.metadata.uniqueSequencePayloadCount
              << " aliasedSequenceSlots=" << mobyAnimation.metadata.aliasedSequenceSlotCount
              << " rigProbes=" << rigProbeCount
              << " prefixBytes=" << ratchet::assets::kRac1MobyProbePrefixBytes
              << " status=ok\n";

    // Phase 10 Step 9: choose the first rendered skeletal class with a genuinely
    // multi-frame dense sequence. On retail level 0 this resolves to the known
    // single-instance class 530 sequence 1, but the selection is structural
    // rather than hard-coded to that oClass. Precompute every frame through the
    // already-gated Step 6 + Step 8 pipeline so the render loop only updates a
    // dynamic vertex buffer.
    const ratchet::assets::Rac1MobyAnimationClass* visualClass = nullptr;
    const ratchet::assets::Rac1MobyRenderedInstance* visualInstance = nullptr;
    const ratchet::assets::Rac1MobySequenceLayout* visualLayout = nullptr;
    std::vector<ratchet::assets::Rac1MobySkinExecution> visualFrames;
    std::size_t visualTriangleVertexCount = 0u;
    std::size_t visualInterpolationProbeCount = 0u;
    float visualMaxRawFrameDelta = 0.0f;

    for (const auto& cls : mobyAnimation.metadata.classes) {
        if (!cls.hasMesh() || !cls.hasSkeleton() || !cls.hasSequences()) continue;
        const ratchet::assets::Rac1MobyRenderedInstance* rendered = nullptr;
        for (const auto& instance : mobys.mesh.renderedInstances) {
            if (instance.oClass == cls.oClass) {
                rendered = &instance;
                break;
            }
        }
        if (rendered == nullptr || rendered->skinVertexCount != cls.skinningVertexCount) continue;

        for (const auto& layout : cls.sequenceLayouts) {
            if (layout.frameOffsets.size() < 2u) continue;
            std::vector<ratchet::assets::Rac1MobySkinExecution> candidateFrames;
            candidateFrames.reserve(layout.frameOffsets.size());
            bool fullyDense = true;
            for (std::size_t frameIndex = 0u; frameIndex < layout.frameOffsets.size(); ++frameIndex) {
                const auto pose = ratchet::assets::decodeRac1MobyDensePoseFrame(
                    loaded.core, cls, layout.sequenceIndex,
                    static_cast<std::uint8_t>(frameIndex));
                if (pose.status == ratchet::assets::Rac1MobyPoseStatus::UnsupportedSparseFrame) {
                    fullyDense = false;
                    break;
                }
                if (!pose.ok()) {
                    std::cerr << "[OpenRatchet:viewer] animated moby pose selection failed status="
                              << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                              << " oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                              << " frame=" << frameIndex << '\n';
                    return 1;
                }
                const auto skin = ratchet::assets::executeRac1MobySkinningProgram(cls, pose.pose);
                if (!skin.ok() || skin.execution.vertices.size() != rendered->skinVertexCount) {
                    std::cerr << "[OpenRatchet:viewer] animated moby skin selection failed status="
                              << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                              << " oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                              << " frame=" << frameIndex
                              << " expectedVertices=" << rendered->skinVertexCount
                              << " actualVertices=" << skin.execution.vertices.size() << '\n';
                    return 1;
                }
                candidateFrames.push_back(skin.execution);
            }
            if (!fullyDense || candidateFrames.size() != layout.frameOffsets.size()) continue;

            std::vector<std::uint32_t> visibleSources;
            for (const auto& batch : mobys.mesh.batches) {
                for (const auto& vertex : batch.triangleVertices) {
                    if (!animatedMobyVertexMatches(vertex, *rendered)) continue;
                    if (vertex.skinVertexIndex >= candidateFrames.front().vertices.size()) {
                        std::cerr << "[OpenRatchet:viewer] animated moby topology source overflow"
                                  << " oClass=" << cls.oClass
                                  << " source=" << vertex.skinVertexIndex
                                  << " skinVertices=" << candidateFrames.front().vertices.size()
                                  << '\n';
                        return 1;
                    }
                    visibleSources.push_back(vertex.skinVertexIndex);
                }
            }
            if (visibleSources.empty() || (visibleSources.size() % 3u) != 0u) continue;

            float maxDelta = 0.0f;
            for (std::size_t frameIndex = 1u; frameIndex < candidateFrames.size(); ++frameIndex) {
                for (std::uint32_t source : visibleSources) {
                    const auto& a = candidateFrames.front().vertices[source].position;
                    const auto& b = candidateFrames[frameIndex].vertices[source].position;
                    maxDelta = std::max(maxDelta, std::max({
                        std::fabs(b[0] - a[0]),
                        std::fabs(b[1] - a[1]),
                        std::fabs(b[2] - a[2]),
                    }));
                }
            }
            if (!(maxDelta > 1.0e-4f) || !std::isfinite(maxDelta)) continue;

            // Step 10 gate: execute a real pose-space midpoint for every
            // adjacent dense keyframe in the selected visual sequence. This
            // proves the render path is consuming FUN_002109b8-style
            // hemisphere-corrected quaternion nlerp, not a final-vertex lerp.
            std::size_t interpolationProbes = 0u;
            for (std::size_t frameIndex = 0u;
                 frameIndex + 1u < layout.frameOffsets.size();
                 ++frameIndex) {
                const auto pose = ratchet::assets::decodeRac1MobyDensePoseInterpolated(
                    loaded.core,
                    cls,
                    layout.sequenceIndex,
                    static_cast<std::uint8_t>(frameIndex),
                    static_cast<std::uint8_t>(frameIndex + 1u),
                    0.5f);
                if (!pose.ok()) {
                    std::cerr << "[OpenRatchet:viewer] animated moby interpolation probe failed status="
                              << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                              << " oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                              << " frameA=" << frameIndex
                              << " frameB=" << (frameIndex + 1u) << '\n';
                    return 1;
                }
                const auto skin =
                    ratchet::assets::executeRac1MobySkinningProgram(cls, pose.pose);
                if (!skin.ok() ||
                    skin.execution.vertices.size() != rendered->skinVertexCount) {
                    std::cerr << "[OpenRatchet:viewer] animated moby interpolation skin probe failed status="
                              << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                              << " oClass=" << cls.oClass
                              << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                              << " frameA=" << frameIndex
                              << " frameB=" << (frameIndex + 1u)
                              << " expectedVertices=" << rendered->skinVertexCount
                              << " actualVertices=" << skin.execution.vertices.size() << '\n';
                    return 1;
                }
                ++interpolationProbes;
            }
            if (interpolationProbes + 1u != layout.frameOffsets.size()) continue;

            visualClass = &cls;
            visualInstance = rendered;
            visualLayout = &layout;
            visualFrames = std::move(candidateFrames);
            visualTriangleVertexCount = visibleSources.size();
            visualInterpolationProbeCount = interpolationProbes;
            visualMaxRawFrameDelta = maxDelta;
            break;
        }
        if (visualClass != nullptr) break;
    }

    if (visualClass == nullptr || visualInstance == nullptr || visualLayout == nullptr ||
        visualFrames.size() < 2u || visualTriangleVertexCount == 0u ||
        visualInterpolationProbeCount + 1u != visualFrames.size()) {
        std::cerr << "[OpenRatchet:viewer] no rendered multi-frame dense moby available for Step 10\n";
        return 1;
    }
    // The standalone viewer still owns only a deterministic demo clock; live
    // game animation speed/state arrives with Phase 11. What is no longer a
    // demo approximation is the pose interpolation itself: every displayed
    // in-between pose is built with the retail quaternion nlerp before
    // hierarchy evaluation and skinning. Keep the previous 8 keyframes/s so
    // this step isolates smoothness from animation-speed policy.
    constexpr double kVisualKeyframeSeconds = 0.125;
    std::cout << "[OpenRatchet:moby:visual] oClass=" << visualClass->oClass
              << " instance=" << visualInstance->instanceIndex
              << " seq=" << static_cast<unsigned>(visualLayout->sequenceIndex)
              << " frames=" << visualFrames.size()
              << " triangles=" << (visualTriangleVertexCount / 3u)
              << " skinVertices=" << visualInstance->skinVertexCount
              << " maxRawFrameDelta=" << visualMaxRawFrameDelta
              << " interpolationProbes=" << visualInterpolationProbeCount
              << " keyframeHz=" << (1.0 / kVisualKeyframeSeconds)
              << " interpolation=retail-shortest-nlerp"
              << " clock=viewer-demo"
              << " topology=phase9-cache-mapped"
              << " render=cpu-skinned-dynamic-vbo"
              << " status=ok\n";

    // Phase 10 Step 12B: drive Ratchet's rendered oClass-0 instance from the
    // external LevelCoreHeader +0x78 sequence bank. Selection is deliberately
    // structural rather than semantic: choose the first external multi-frame
    // sequence whose every keyframe and adjacent midpoint survives the already
    // proven pose/post-compose/skinning pipeline and whose visible vertices
    // actually move. No sequence name, gameplay state, or loop flag is guessed.
    const auto& ratchetVisualClass = ratchetAnimationBank.bank.animationClass;
    if (ratchetVisualClass.oClass != 0 || !ratchetVisualClass.hasMesh() ||
        !ratchetVisualClass.hasSkeleton() || !ratchetVisualClass.hasSequences() ||
        ratchetVisualClass.externalSequenceTableOffset == 0u ||
        ratchetVisualClass.externalSequenceTableOffset !=
            loaded.summary.ratchetSequenceTableOffset) {
        std::cerr << "[OpenRatchet:viewer] Ratchet visual class contract incomplete"
                  << " oClass=" << ratchetVisualClass.oClass
                  << " externalTable=0x" << std::hex
                  << ratchetVisualClass.externalSequenceTableOffset
                  << " expectedTable=0x" << loaded.summary.ratchetSequenceTableOffset
                  << std::dec << '\n';
        return 1;
    }

    const ratchet::assets::Rac1MobyRenderedInstance* ratchetVisualInstance = nullptr;
    for (const auto& instance : mobys.mesh.renderedInstances) {
        if (instance.oClass == 0) {
            if (ratchetVisualInstance != nullptr) {
                std::cerr << "[OpenRatchet:viewer] multiple rendered Ratchet instances found"
                          << " first=" << ratchetVisualInstance->instanceIndex
                          << " second=" << instance.instanceIndex << '\n';
                return 1;
            }
            ratchetVisualInstance = &instance;
        }
    }
    if (ratchetVisualInstance == nullptr ||
        ratchetVisualInstance->skinVertexCount != ratchetVisualClass.skinningVertexCount) {
        std::cerr << "[OpenRatchet:viewer] rendered Ratchet topology contract failed"
                  << " instance="
                  << (ratchetVisualInstance != nullptr
                          ? static_cast<long long>(ratchetVisualInstance->instanceIndex)
                          : -1ll)
                  << " renderedSkinVertices="
                  << (ratchetVisualInstance != nullptr
                          ? ratchetVisualInstance->skinVertexCount
                          : 0u)
                  << " classSkinVertices=" << ratchetVisualClass.skinningVertexCount
                  << '\n';
        return 1;
    }

    std::vector<std::uint32_t> ratchetVisibleSources;
    for (const auto& batch : mobys.mesh.batches) {
        for (const auto& vertex : batch.triangleVertices) {
            if (!animatedMobyVertexMatches(vertex, *ratchetVisualInstance)) continue;
            if (vertex.skinVertexIndex >= ratchetVisualInstance->skinVertexCount) {
                std::cerr << "[OpenRatchet:viewer] Ratchet visible topology source overflow"
                          << " source=" << vertex.skinVertexIndex
                          << " skinVertices=" << ratchetVisualInstance->skinVertexCount
                          << '\n';
                return 1;
            }
            ratchetVisibleSources.push_back(vertex.skinVertexIndex);
        }
    }
    if (ratchetVisibleSources.empty() || (ratchetVisibleSources.size() % 3u) != 0u) {
        std::cerr << "[OpenRatchet:viewer] Ratchet visible topology is empty/misaligned"
                  << " triangleVertices=" << ratchetVisibleSources.size() << '\n';
        return 1;
    }

    const ratchet::assets::Rac1MobySequenceLayout* ratchetVisualLayout = nullptr;
    std::vector<ratchet::assets::Rac1MobySkinExecution> ratchetVisualFrames;
    std::size_t ratchetVisualInterpolationProbeCount = 0u;
    float ratchetVisualMaxRawFrameDelta = 0.0f;

    for (const auto& layout : ratchetVisualClass.sequenceLayouts) {
        if (layout.storage != ratchet::assets::Rac1MobySequenceStorage::RatchetExternal ||
            layout.frameOffsets.size() < 2u) {
            continue;
        }

        std::vector<ratchet::assets::Rac1MobySkinExecution> candidateFrames;
        candidateFrames.reserve(layout.frameOffsets.size());
        for (std::size_t frameIndex = 0u; frameIndex < layout.frameOffsets.size(); ++frameIndex) {
            const auto pose = ratchet::assets::decodeRac1MobyPoseFrame(
                loaded.core,
                ratchetVisualClass,
                layout.sequenceIndex,
                static_cast<std::uint8_t>(frameIndex));
            if (!pose.ok()) {
                std::cerr << "[OpenRatchet:viewer] Ratchet visual keyframe pose failed status="
                          << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frame=" << frameIndex << '\n';
                return 1;
            }
            const auto skin = ratchet::assets::executeRac1MobySkinningProgram(
                ratchetVisualClass, pose.pose);
            if (!skin.ok() ||
                skin.execution.vertices.size() != ratchetVisualInstance->skinVertexCount) {
                std::cerr << "[OpenRatchet:viewer] Ratchet visual keyframe skin failed status="
                          << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frame=" << frameIndex
                          << " expectedVertices=" << ratchetVisualInstance->skinVertexCount
                          << " actualVertices=" << skin.execution.vertices.size() << '\n';
                return 1;
            }
            candidateFrames.push_back(skin.execution);
        }

        float maxDelta = 0.0f;
        for (std::size_t frameIndex = 1u; frameIndex < candidateFrames.size(); ++frameIndex) {
            for (std::uint32_t source : ratchetVisibleSources) {
                const auto& a = candidateFrames.front().vertices[source].position;
                const auto& b = candidateFrames[frameIndex].vertices[source].position;
                maxDelta = std::max(maxDelta, std::max({
                    std::fabs(b[0] - a[0]),
                    std::fabs(b[1] - a[1]),
                    std::fabs(b[2] - a[2]),
                }));
            }
        }
        if (!(maxDelta > 1.0e-4f) || !std::isfinite(maxDelta)) continue;

        // Retail FUN_0020d580 @ 0x20D658..0x20D6C4 advances frame A to
        // frame B, increments frame B, and wraps frame B to zero when it reaches
        // the sequence frame count. It then installs both frame pointers while
        // retaining the fractional animation time. Therefore the final->first
        // pair is a real interpolation segment, not a hard viewer reset.
        std::size_t interpolationProbes = 0u;
        for (std::size_t frameIndex = 0u; frameIndex < layout.frameOffsets.size(); ++frameIndex) {
            const std::size_t nextFrameIndex =
                (frameIndex + 1u) % layout.frameOffsets.size();
            const auto pose = ratchet::assets::decodeRac1MobyPoseInterpolated(
                loaded.core,
                ratchetVisualClass,
                layout.sequenceIndex,
                static_cast<std::uint8_t>(frameIndex),
                static_cast<std::uint8_t>(nextFrameIndex),
                0.5f);
            if (!pose.ok()) {
                std::cerr << "[OpenRatchet:viewer] Ratchet visual interpolation probe failed status="
                          << ratchet::assets::rac1MobyPoseStatusName(pose.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frameA=" << frameIndex
                          << " frameB=" << nextFrameIndex << '\n';
                return 1;
            }
            const auto skin = ratchet::assets::executeRac1MobySkinningProgram(
                ratchetVisualClass, pose.pose);
            if (!skin.ok() ||
                skin.execution.vertices.size() != ratchetVisualInstance->skinVertexCount) {
                std::cerr << "[OpenRatchet:viewer] Ratchet visual interpolation skin probe failed status="
                          << ratchet::assets::rac1MobySkinExecutionStatusName(skin.status)
                          << " seq=" << static_cast<unsigned>(layout.sequenceIndex)
                          << " frameA=" << frameIndex
                          << " frameB=" << nextFrameIndex
                          << " expectedVertices=" << ratchetVisualInstance->skinVertexCount
                          << " actualVertices=" << skin.execution.vertices.size() << '\n';
                return 1;
            }
            ++interpolationProbes;
        }
        if (interpolationProbes != candidateFrames.size()) continue;

        ratchetVisualLayout = &layout;
        ratchetVisualFrames = std::move(candidateFrames);
        ratchetVisualInterpolationProbeCount = interpolationProbes;
        ratchetVisualMaxRawFrameDelta = maxDelta;
        break;
    }

    if (ratchetVisualLayout == nullptr || ratchetVisualFrames.size() < 2u ||
        ratchetVisualInterpolationProbeCount != ratchetVisualFrames.size()) {
        std::cerr << "[OpenRatchet:viewer] no complete moving Ratchet external sequence available"
                  << " sequences=" << ratchetVisualClass.sequenceLayouts.size() << '\n';
        return 1;
    }

    const std::size_t ratchetVisualTriangleVertexCount = ratchetVisibleSources.size();
    std::cout << "[OpenRatchet:ratchet:visual] oClass=" << ratchetVisualClass.oClass
              << " instance=" << ratchetVisualInstance->instanceIndex
              << " seq=" << static_cast<unsigned>(ratchetVisualLayout->sequenceIndex)
              << " frames=" << ratchetVisualFrames.size()
              << " triangles=" << (ratchetVisualTriangleVertexCount / 3u)
              << " skinVertices=" << ratchetVisualInstance->skinVertexCount
              << " maxRawFrameDelta=" << ratchetVisualMaxRawFrameDelta
              << " interpolationProbes=" << ratchetVisualInterpolationProbeCount
              << " animationSource=external-core-bank"
              << " table=0x" << std::hex
              << ratchetVisualClass.externalSequenceTableOffset << std::dec
              << " selection=first-complete-moving-external-sequence"
              << " interpolation=retail-shortest-nlerp"
              << " clock=viewer-demo"
              << " loopPolicy=retail-frameB-wrap-to-zero"
              << " loopOracle=FUN_0020d580@0x20d658-0x20d6c4"
              << " topology=phase9-cache-mapped"
              << " render=cpu-skinned-dynamic-vbo"
              << " status=ok\n";

    std::cout << "[OpenRatchet:viewer] bounds raw=("
              << terrain.mesh.bounds.minX << ','
              << terrain.mesh.bounds.minY << ','
              << terrain.mesh.bounds.minZ << ")->("
              << terrain.mesh.bounds.maxX << ','
              << terrain.mesh.bounds.maxY << ','
              << terrain.mesh.bounds.maxZ << ") scale=" << scale << '\n';

    std::cout << "[OpenRatchet:viewer] logging="
              << (verboseAssetLogs ? "verbose" : "concise")
              << " detailEnv=OPENRATCHET_VERBOSE_ASSET_LOGS=1"
              << " raylib=" << (verboseAssetLogs ? "info" : "warning+") << '\n';
    SetTraceLogLevel(verboseAssetLogs ? LOG_INFO : LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "OpenRatchet - Native R&C1 Scene + Animated Ratchet");
    SetTargetFPS(60);

    std::vector<Texture2D> tfragGpu = uploadTextures(tfragTextures.textures);
    std::vector<Texture2D> tieGpu = uploadTextures(tieTextures.textures);
    std::vector<Texture2D> mobyGpu = uploadTextures(mobyTextures.textures);
    std::vector<Texture2D> shrubGpu = uploadTextures(shrubTextures.textures);
    std::vector<Texture2D> skyGpu = uploadTextures(sky.mesh.textures);
    std::vector<NativeDrawBatch> terrainBatches;
    std::vector<NativeDrawBatch> staticBatches;
    std::vector<NativeDrawBatch> mobyBatches;
    std::vector<NativeAnimatedMobyBatch> animatedMobyBatches;
    std::vector<NativeDrawBatch> skyBatches;

    auto cleanup = [&]() {
        unloadBatches(terrainBatches);
        unloadBatches(staticBatches);
        unloadBatches(mobyBatches);
        unloadAnimatedMobyBatches(animatedMobyBatches);
        unloadBatches(skyBatches);
        unloadTextures(tfragGpu);
        unloadTextures(tieGpu);
        unloadTextures(mobyGpu);
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

    Vector3 visualBoundsMin{
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    Vector3 visualBoundsMax{
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
    std::size_t uploadedAnimatedVertices = 0u;
    for (const auto& sourceBatch : mobys.mesh.batches) {
        std::vector<ratchet::assets::Rac1MobyVertex> staticVertices;
        std::vector<ratchet::assets::Rac1MobyVertex> animatedVertices;
        staticVertices.reserve(sourceBatch.triangleVertices.size());
        animatedVertices.reserve(sourceBatch.triangleVertices.size());
        if ((sourceBatch.triangleVertices.size() % 3u) != 0u) {
            std::cerr << "[OpenRatchet:viewer] moby triangle batch lost triangle alignment\n";
            cleanup();
            return 1;
        }
        for (std::size_t i = 0u; i < sourceBatch.triangleVertices.size(); i += 3u) {
            const bool t0 = animatedMobyVertexMatches(sourceBatch.triangleVertices[i + 0u], *ratchetVisualInstance);
            const bool t1 = animatedMobyVertexMatches(sourceBatch.triangleVertices[i + 1u], *ratchetVisualInstance);
            const bool t2 = animatedMobyVertexMatches(sourceBatch.triangleVertices[i + 2u], *ratchetVisualInstance);
            if ((t0 || t1 || t2) && !(t0 && t1 && t2)) {
                std::cerr << "[OpenRatchet:viewer] animated Ratchet triangle mixes instance ownership\n";
                cleanup();
                return 1;
            }
            auto& destination = t0 ? animatedVertices : staticVertices;
            for (std::size_t j = 0u; j < 3u; ++j) {
                const auto& vertex = sourceBatch.triangleVertices[i + j];
                destination.push_back(vertex);
                if (t0) {
                    const Vector3 p = toViewerSpace(vertex.x, vertex.y, vertex.z, center, scale);
                    visualBoundsMin.x = std::min(visualBoundsMin.x, p.x);
                    visualBoundsMin.y = std::min(visualBoundsMin.y, p.y);
                    visualBoundsMin.z = std::min(visualBoundsMin.z, p.z);
                    visualBoundsMax.x = std::max(visualBoundsMax.x, p.x);
                    visualBoundsMax.y = std::max(visualBoundsMax.y, p.y);
                    visualBoundsMax.z = std::max(visualBoundsMax.z, p.z);
                }
            }
        }

        if (!appendMeshBatch(
                staticVertices,
                sourceBatch.materialIndex,
                &mobyGpu,
                &mobyTextures.textures,
                [&](const ratchet::assets::Rac1MobyVertex& v) {
                    return toViewerSpace(v.x, v.y, v.z, center, scale);
                },
                mobyBatches) ||
            !appendAnimatedMobyBatch(
                animatedVertices,
                sourceBatch.materialIndex,
                mobyGpu,
                mobyTextures.textures,
                *ratchetVisualInstance,
                ratchetVisualFrames.front(),
                center,
                scale,
                animatedMobyBatches)) {
            std::cerr << "[OpenRatchet:viewer] moby/Ratchet GPU mesh creation failed\n";
            cleanup();
            return 1;
        }
        uploadedAnimatedVertices += animatedVertices.size();
    }
    if (uploadedAnimatedVertices != ratchetVisualTriangleVertexCount || animatedMobyBatches.empty() ||
        !std::isfinite(visualBoundsMin.x) || !std::isfinite(visualBoundsMax.x)) {
        std::cerr << "[OpenRatchet:viewer] animated Ratchet GPU topology incomplete expectedVertices="
                  << ratchetVisualTriangleVertexCount << " uploadedVertices=" << uploadedAnimatedVertices
                  << " batches=" << animatedMobyBatches.size() << '\n';
        cleanup();
        return 1;
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

    const Vector3 visualCenter{
        (visualBoundsMin.x + visualBoundsMax.x) * 0.5f,
        (visualBoundsMin.y + visualBoundsMax.y) * 0.5f,
        (visualBoundsMin.z + visualBoundsMax.z) * 0.5f,
    };
    const float visualExtent = std::max({
        visualBoundsMax.x - visualBoundsMin.x,
        visualBoundsMax.y - visualBoundsMin.y,
        visualBoundsMax.z - visualBoundsMin.z,
        2.0f,
    });
    const float visualCameraDistance = visualExtent * 2.5f;

    Camera3D camera{};
    camera.position = {visualCenter.x + visualCameraDistance,
                       visualCenter.y + visualCameraDistance * 0.55f,
                       visualCenter.z + visualCameraDistance};
    camera.target = visualCenter;
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    const std::size_t totalTriangles = terrain.mesh.triangleCount +
                                       staticScene.mesh.tieTriangleCount +
                                       staticScene.mesh.shrubTriangleCount +
                                       mobys.mesh.triangleCount +
                                       sky.mesh.triangleCount;
    bool wireframe = false;
    const double visualStartTime = GetTime();
    std::size_t visualFrameIndex = 0u;
    std::size_t visualNextFrameIndex = 1u;
    float visualInterpolationAlpha = 0.0f;
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

        const double visualPhase =
            (GetTime() - visualStartTime) / kVisualKeyframeSeconds;
        const double visualWhole = std::floor(visualPhase);
        // FUN_0020d580 wraps the retail next-frame index to zero at the end
        // of a same-sequence forward animation, so every keyframe contributes
        // one interpolation segment, including last -> first.
        const std::size_t ratchetVisualSegmentCount = ratchetVisualFrames.size();
        const std::size_t desiredVisualFrame =
            static_cast<std::size_t>(visualWhole) % ratchetVisualSegmentCount;
        const std::size_t desiredVisualNextFrame =
            (desiredVisualFrame + 1u) % ratchetVisualSegmentCount;
        const float desiredVisualAlpha =
            static_cast<float>(visualPhase - visualWhole);

        const auto interpolatedPose =
            ratchet::assets::decodeRac1MobyPoseInterpolated(
                loaded.core,
                ratchetVisualClass,
                ratchetVisualLayout->sequenceIndex,
                static_cast<std::uint8_t>(desiredVisualFrame),
                static_cast<std::uint8_t>(desiredVisualNextFrame),
                desiredVisualAlpha);
        if (!interpolatedPose.ok()) {
            std::cerr << "[OpenRatchet:viewer] animated Ratchet interpolated pose failed status="
                      << ratchet::assets::rac1MobyPoseStatusName(interpolatedPose.status)
                      << " frameA=" << desiredVisualFrame
                      << " frameB=" << desiredVisualNextFrame
                      << " alpha=" << desiredVisualAlpha << '\n';
            cleanup();
            return 1;
        }
        const auto interpolatedSkin =
            ratchet::assets::executeRac1MobySkinningProgram(
                ratchetVisualClass, interpolatedPose.pose);
        if (!interpolatedSkin.ok()) {
            std::cerr << "[OpenRatchet:viewer] animated Ratchet interpolated skin failed status="
                      << ratchet::assets::rac1MobySkinExecutionStatusName(
                             interpolatedSkin.status)
                      << " frameA=" << desiredVisualFrame
                      << " frameB=" << desiredVisualNextFrame
                      << " alpha=" << desiredVisualAlpha << '\n';
            cleanup();
            return 1;
        }
        if (!updateAnimatedMobyBatches(animatedMobyBatches,
                                       *ratchetVisualInstance,
                                       interpolatedSkin.execution,
                                       center,
                                       scale)) {
            std::cerr << "[OpenRatchet:viewer] animated Ratchet dynamic vertex update failed"
                      << " frameA=" << desiredVisualFrame
                      << " frameB=" << desiredVisualNextFrame
                      << " alpha=" << desiredVisualAlpha << '\n';
            cleanup();
            return 1;
        }
        visualFrameIndex = desiredVisualFrame;
        visualNextFrameIndex = desiredVisualNextFrame;
        visualInterpolationAlpha = desiredVisualAlpha;

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
        drawBatches(mobyBatches, {0.0f, 0.0f, 0.0f});
        drawAnimatedMobyBatches(animatedMobyBatches);

        if (wireframe) rlDisableWireMode();
        rlEnableBackfaceCulling();
        DrawGrid(20, 5.0f);
        EndMode3D();

        DrawRectangle(12, 12, 1010, 134, {0, 0, 0, 170});
        DrawText("OpenRatchet native R&C1 scene: tfrags + ties + shrubs + mobys + sky",
                 24, 22, 22, RAYWHITE);
        DrawText(TextFormat("Level %u | %llu triangles | ties %llu | shrubs %llu | mobys %llu/%llu | sky %llu | %s",
                            loaded.summary.tocIndex,
                            static_cast<unsigned long long>(totalTriangles),
                            static_cast<unsigned long long>(staticScene.mesh.tieInstanceCount),
                            static_cast<unsigned long long>(staticScene.mesh.shrubInstanceCount),
                            static_cast<unsigned long long>(mobys.mesh.renderedInstanceCount),
                            static_cast<unsigned long long>(mobys.mesh.instanceCount),
                            static_cast<unsigned long long>(sky.mesh.triangleCount),
                            wireframe ? "wireframe" : "textured"),
                 24, 50, 18, LIGHTGRAY);
        DrawText(TextFormat("ANIMATED RATCHET: external seq %u | %llu->%llu/%llu | alpha %.2f | retail pose nlerp",
                            static_cast<unsigned>(ratchetVisualLayout->sequenceIndex),
                            static_cast<unsigned long long>(visualFrameIndex + 1u),
                            static_cast<unsigned long long>(visualNextFrameIndex + 1u),
                            static_cast<unsigned long long>(ratchetVisualFrames.size()),
                            static_cast<double>(visualInterpolationAlpha)),
                 24, 74, 17, YELLOW);
        DrawText(TextFormat("Textures: tfrag %llu | tie %llu | shrub %llu | moby %llu | sky %llu",
                            static_cast<unsigned long long>(tfragTextures.textures.size()),
                            static_cast<unsigned long long>(tieTextures.textures.size()),
                            static_cast<unsigned long long>(shrubTextures.textures.size()),
                            static_cast<unsigned long long>(mobyTextures.textures.size()),
                            static_cast<unsigned long long>(sky.mesh.textures.size())),
                 24, 98, 16, GRAY);
        DrawText("Click: lock mouse | WASD/mouse/wheel | TAB: wireframe | ESC: close",
                 24, 120, 16, GRAY);
        DrawFPS(GetScreenWidth() - 90, 16);
        EndDrawing();
    }

    cleanup();
    return 0;
}
