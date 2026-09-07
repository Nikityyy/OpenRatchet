#include "render/rac1_runtime_renderer.h"

#include "assets/rac1_level.h"
#include "assets/rac1_static_scene.h"
#include "assets/rac1_sky.h"
#include "assets/rac1_texture.h"
#include "assets/rac1_tfrag.h"
#include "assets/wad_decompressor.h"
#include "render/rac1_render_bridge.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#include <rlgl.h>

namespace ratchet::render {
namespace {

constexpr std::size_t kMaxRuntimeWadOutputBytes = 64u * 1024u * 1024u;

bool readRuntimeWadAsset(const platform::NativeAssetLocation& asset,
                         std::vector<std::uint8_t>& bytes) {
    if (asset.kind != platform::NativeAssetKind::Wad2 ||
        !std::filesystem::is_regular_file(asset.path)) {
        return false;
    }

    const std::uint64_t expectedBytes64 =
        static_cast<std::uint64_t>(asset.sectorCount) * platform::NativeVfs::kSectorBytes;
    if (expectedBytes64 == 0u ||
        expectedBytes64 > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    std::error_code sizeError;
    const std::uint64_t fileBytes = std::filesystem::file_size(asset.path, sizeError);
    if (sizeError || fileBytes != expectedBytes64) {
        return false;
    }

    bytes.assign(static_cast<std::size_t>(expectedBytes64), 0u);
    std::ifstream input(asset.path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return false;
    }
    return true;
}

} // namespace

Rac1RuntimeRenderer::~Rac1RuntimeRenderer() {
    unload();
}

void Rac1RuntimeRenderer::unload() noexcept {
    unloadSkinnedMobyBatches(liveRatchetBatches_);
    unloadBatches(terrainBatches_);
    for (auto& shell : skyShellBatches_) unloadBatches(shell);
    skyShellBatches_.clear();
    unloadBatches(staticBatches_);
    unloadTextures(tfragTextures_);
    unloadTextures(tieTextures_);
    unloadTextures(shrubTextures_);
    unloadTextures(skyTextures_);
    unloadTextures(mobyTextures_);
    core_.clear();
    mobySourceTextures_.clear();
    ratchetTopologyInstance_ = {};
    ratchetAnimationClass_ = {};
    ratchetTopologyBatches_.clear();
    nativeMobyClasses_.clear();
    nativeSkyShellIdentities_.clear();
    liveRatchetGpuReady_ = false;
    summary_ = {};
    status_ = Rac1RuntimeRendererStatus::Uninitialized;
}

bool Rac1RuntimeRenderer::loadLevel(
    const platform::NativeAssetLocation& level,
    const platform::NativeAssetLocation& runtimeLevelWad) {
    unload();
    summary_.levelIndex = level.index;

    if (level.kind != platform::NativeAssetKind::Level ||
        !std::filesystem::is_regular_file(level.path)) {
        status_ = Rac1RuntimeRendererStatus::LevelFileMissing;
        return false;
    }
    summary_.skySourceWad2Index = runtimeLevelWad.index;

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
    auto mobyTextures = assets::decodeRac1PaletteTextures(
        loaded.core,
        loaded.coreIndex,
        loaded.gsRam,
        loaded.summary.mobyTextures,
        loaded.summary.texturesBaseOffset);
    if (!tfragTextures.ok() || !tieTextures.ok() || !shrubTextures.ok() ||
        !mobyTextures.ok()) {
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

    const auto staticWorldParity = buildRac1StaticWorldParityDigest(
        terrain.mesh,
        tfragTextures.textures,
        staticScene.mesh,
        tieTextures.textures,
        shrubTextures.textures);
    if (!staticWorldParity.ok()) {
        status_ = Rac1RuntimeRendererStatus::StaticWorldParityInvalid;
        return false;
    }
    summary_.staticWorldParity = staticWorldParity;

    std::vector<std::uint8_t> runtimeWadEncoded;
    if (runtimeLevelWad.kind != platform::NativeAssetKind::Wad2 ||
        !std::filesystem::is_regular_file(runtimeLevelWad.path)) {
        status_ = Rac1RuntimeRendererStatus::RuntimeSkyAssetMissing;
        return false;
    }
    if (!readRuntimeWadAsset(runtimeLevelWad, runtimeWadEncoded)) {
        status_ = Rac1RuntimeRendererStatus::RuntimeSkyAssetInvalid;
        return false;
    }

    std::vector<std::uint8_t> runtimeWadDecoded(kMaxRuntimeWadOutputBytes, 0u);
    const auto runtimeWadDecompress = assets::decompressWad(
        runtimeWadEncoded, runtimeWadDecoded);
    if (!runtimeWadDecompress.ok() || runtimeWadDecompress.bytesWritten == 0u ||
        runtimeWadDecompress.bytesWritten > runtimeWadDecoded.size()) {
        status_ = Rac1RuntimeRendererStatus::RuntimeSkyDecompressionFailed;
        return false;
    }
    runtimeWadDecoded.resize(runtimeWadDecompress.bytesWritten);

    const auto runtimeSkySource = assets::locateRac1RuntimeSky(runtimeWadDecoded);
    if (!runtimeSkySource.ok()) {
        status_ = Rac1RuntimeRendererStatus::RuntimeSkySourceInvalid;
        return false;
    }
    summary_.skySourceOffset = runtimeSkySource.skyOffset;
    auto sky = assets::decodeRac1Sky(runtimeWadDecoded, runtimeSkySource.skyOffset);
    if (!sky.ok()) {
        status_ = Rac1RuntimeRendererStatus::SkyDecodeFailed;
        return false;
    }

    auto mobys = assets::decodeRac1MobyScene(
        loaded.core,
        loaded.coreIndex,
        loaded.gameplay,
        loaded.summary.mobyClasses,
        static_cast<std::uint32_t>(mobyTextures.textures.size()));
    if (!mobys.ok()) {
        status_ = Rac1RuntimeRendererStatus::MobySceneDecodeFailed;
        return false;
    }

    auto classCatalog = assets::inspectRac1MobyClassCatalog(
        loaded.coreIndex, loaded.summary.mobyClasses);
    if (!classCatalog.ok()) {
        status_ = Rac1RuntimeRendererStatus::MobyClassCatalogFailed;
        return false;
    }

    std::vector<Rac1NativeMobyClassIdentity> nativeMobyClasses;
    nativeMobyClasses.reserve(classCatalog.classes.size());
    for (const auto& cls : classCatalog.classes) {
        const bool renderable = std::any_of(
            mobys.mesh.renderedInstances.begin(), mobys.mesh.renderedInstances.end(),
            [&](const assets::Rac1MobyRenderedInstance& instance) {
                return instance.oClass == cls.oClass;
            });
        const bool intentionallyInvisible = std::any_of(
            mobys.mesh.skippedClasses.begin(), mobys.mesh.skippedClasses.end(),
            [&](const assets::Rac1MobySkippedClass& skipped) {
                return skipped.oClass == cls.oClass;
            });
        if (renderable && intentionallyInvisible) {
            status_ = Rac1RuntimeRendererStatus::MobyClassAccountingInvalid;
            return false;
        }

        Rac1NativeMobyClassKind kind = Rac1NativeMobyClassKind::ClassOnly;
        if (renderable) kind = Rac1NativeMobyClassKind::RenderableTopology;
        else if (intentionallyInvisible) kind = Rac1NativeMobyClassKind::IntentionallyInvisible;
        nativeMobyClasses.push_back({cls.oClass, kind});
    }

    const auto catalogContains = [&](std::int32_t oClass) {
        return std::any_of(
            classCatalog.classes.begin(), classCatalog.classes.end(),
            [&](const assets::Rac1MobyClassCatalogEntry& cls) {
                return cls.oClass == oClass;
            });
    };
    for (const auto& instance : mobys.mesh.renderedInstances) {
        if (!catalogContains(instance.oClass)) {
            status_ = Rac1RuntimeRendererStatus::MobyClassAccountingInvalid;
            return false;
        }
    }
    for (const auto& skipped : mobys.mesh.skippedClasses) {
        if (!catalogContains(skipped.oClass)) {
            status_ = Rac1RuntimeRendererStatus::MobyClassAccountingInvalid;
            return false;
        }
    }

    auto mobyAnimation = assets::inspectRac1MobyAnimationMetadata(
        loaded.core,
        loaded.coreIndex,
        loaded.gameplay,
        loaded.summary.mobyClasses);
    if (!mobyAnimation.ok()) {
        status_ = Rac1RuntimeRendererStatus::MobyAnimationMetadataFailed;
        return false;
    }

    const assets::Rac1MobyAnimationClass* ratchetBaseClass = nullptr;
    for (const auto& cls : mobyAnimation.metadata.classes) {
        if (cls.oClass != 0) continue;
        if (ratchetBaseClass != nullptr) {
            status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
            return false;
        }
        ratchetBaseClass = &cls;
    }
    if (ratchetBaseClass == nullptr) {
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        return false;
    }

    auto ratchetAnimationBank = assets::inspectRac1RatchetAnimationBank(
        loaded.core,
        loaded.coreIndex,
        loaded.summary.ratchetSequenceTableOffset,
        *ratchetBaseClass);
    if (!ratchetAnimationBank.ok()) {
        status_ = Rac1RuntimeRendererStatus::RatchetAnimationBankFailed;
        return false;
    }
    const auto& ratchetClass = ratchetAnimationBank.bank.animationClass;
    if (ratchetClass.oClass != 0 || !ratchetClass.hasMesh() ||
        !ratchetClass.hasSkeleton() || !ratchetClass.hasSequences() ||
        ratchetClass.externalSequenceTableOffset == 0u ||
        ratchetClass.externalSequenceTableOffset != loaded.summary.ratchetSequenceTableOffset) {
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        return false;
    }

    const assets::Rac1MobyRenderedInstance* ratchetInstance = nullptr;
    for (const auto& instance : mobys.mesh.renderedInstances) {
        if (instance.oClass != 0) continue;
        if (ratchetInstance != nullptr) {
            status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
            return false;
        }
        ratchetInstance = &instance;
    }
    if (ratchetInstance == nullptr ||
        ratchetInstance->skinVertexCount != ratchetClass.skinningVertexCount) {
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        return false;
    }

    std::vector<assets::Rac1MobyBatch> ratchetTopology;
    std::size_t ratchetTopologyVertices = 0u;
    for (const auto& sourceBatch : mobys.mesh.batches) {
        if ((sourceBatch.triangleVertices.size() % 3u) != 0u) {
            status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
            return false;
        }

        assets::Rac1MobyBatch ratchetBatch;
        ratchetBatch.materialIndex = sourceBatch.materialIndex;
        ratchetBatch.triangleVertices.reserve(sourceBatch.triangleVertices.size());
        for (std::size_t i = 0u; i < sourceBatch.triangleVertices.size(); i += 3u) {
            const bool t0 = nativeSkinnedMobyVertexMatches(
                sourceBatch.triangleVertices[i + 0u], *ratchetInstance);
            const bool t1 = nativeSkinnedMobyVertexMatches(
                sourceBatch.triangleVertices[i + 1u], *ratchetInstance);
            const bool t2 = nativeSkinnedMobyVertexMatches(
                sourceBatch.triangleVertices[i + 2u], *ratchetInstance);
            if ((t0 || t1 || t2) && !(t0 && t1 && t2)) {
                status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
                return false;
            }
            if (!t0) continue;

            for (std::size_t j = 0u; j < 3u; ++j) {
                const auto& vertex = sourceBatch.triangleVertices[i + j];
                if (vertex.skinVertexIndex >= ratchetInstance->skinVertexCount) {
                    status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
                    return false;
                }
                ratchetBatch.triangleVertices.push_back(vertex);
            }
        }
        if (!ratchetBatch.triangleVertices.empty()) {
            ratchetTopologyVertices += ratchetBatch.triangleVertices.size();
            ratchetTopology.push_back(std::move(ratchetBatch));
        }
    }
    if (ratchetTopology.empty() || ratchetTopologyVertices == 0u ||
        (ratchetTopologyVertices % 3u) != 0u) {
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        return false;
    }

    tfragTextures_ = uploadTextures(tfragTextures.textures);
    tieTextures_ = uploadTextures(tieTextures.textures);
    shrubTextures_ = uploadTextures(shrubTextures.textures);
    skyTextures_ = uploadTextures(sky.mesh.textures);
    mobyTextures_ = uploadTextures(mobyTextures.textures);
    if (tfragTextures_.size() != tfragTextures.textures.size() ||
        tieTextures_.size() != tieTextures.textures.size() ||
        shrubTextures_.size() != shrubTextures.textures.size() ||
        skyTextures_.size() != sky.mesh.textures.size() ||
        mobyTextures_.size() != mobyTextures.textures.size()) {
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

    std::vector<Rac1NativeSkyShellIdentity> nativeSkyShellIdentities;
    nativeSkyShellIdentities.reserve(sky.mesh.shellIdentities.size());
    for (const auto& shell : sky.mesh.shellIdentities) {
        nativeSkyShellIdentities.push_back({shell.sourceOffset, shell.clusterCount, shell.flags});
    }
    if (nativeSkyShellIdentities.size() != sky.mesh.shellCount) {
        status_ = Rac1RuntimeRendererStatus::SkyDecodeFailed;
        return false;
    }

    skyShellBatches_.resize(sky.mesh.shellCount);
    for (const auto& sourceBatch : sky.mesh.batches) {
        if (sourceBatch.shellIndex >= skyShellBatches_.size()) {
            status_ = Rac1RuntimeRendererStatus::SkyDecodeFailed;
            unload();
            status_ = Rac1RuntimeRendererStatus::SkyDecodeFailed;
            return false;
        }
        const bool textured = sourceBatch.materialIndex != UINT32_MAX;
        const auto* gpuTextures = textured ? &skyTextures_ : nullptr;
        const auto* sourceTextures = textured ? &sky.mesh.textures : nullptr;
        if (!appendMeshBatch(
                sourceBatch.triangleVertices,
                sourceBatch.materialIndex,
                gpuTextures,
                sourceTextures,
                [](const assets::Rac1SkyVertex& vertex) {
                    // The shared sky decoder stores XYZ normalized by 1/1024. Retail
                    // FUN_0022BF94 consumes the original signed-int16 domain
                    // before FUN_0022B288's object matrix, so recover it
                    // exactly with the inverse power-of-two scale here.
                    return Vector3{vertex.x * 1024.0f,
                                   vertex.y * 1024.0f,
                                   vertex.z * 1024.0f};
                },
                skyShellBatches_[sourceBatch.shellIndex])) {
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
    summary_.skyShells = sky.mesh.shellCount;
    summary_.skyClusters = sky.mesh.clusterCount;
    summary_.skyTriangles = sky.mesh.triangleCount;
    summary_.skyTextures = sky.mesh.textures.size();
    for (const auto& shell : skyShellBatches_) summary_.skyBatches += shell.size();
    summary_.mobyNativeClasses = nativeMobyClasses.size();
    for (const auto& cls : nativeMobyClasses) {
        switch (cls.kind) {
        case Rac1NativeMobyClassKind::RenderableTopology:
            ++summary_.mobyRenderableClasses;
            break;
        case Rac1NativeMobyClassKind::IntentionallyInvisible:
            ++summary_.mobyIntentionallyInvisibleClasses;
            break;
        case Rac1NativeMobyClassKind::ClassOnly:
            ++summary_.mobyClassOnlyClasses;
            break;
        }
    }
    summary_.ratchetTopologyBatches = ratchetTopology.size();
    summary_.ratchetTopologyTriangles = ratchetTopologyVertices / 3u;
    summary_.ratchetSkinVertices = ratchetInstance->skinVertexCount;
    if (summary_.terrainBatches == 0u || summary_.staticBatches == 0u ||
        summary_.terrainTriangles == 0u ||
        summary_.tieTriangles + summary_.shrubTriangles == 0u ||
        summary_.skyBatches == 0u || summary_.skyShells == 0u ||
        summary_.skyClusters == 0u || summary_.skyTriangles == 0u ||
        summary_.skyShells != skyShellBatches_.size() ||
        summary_.skyShells != nativeSkyShellIdentities.size() ||
        summary_.mobyNativeClasses == 0u ||
        summary_.mobyNativeClasses != mobys.mesh.classCount ||
        summary_.mobyRenderableClasses != mobys.mesh.renderableClassCount ||
        summary_.mobyIntentionallyInvisibleClasses != mobys.mesh.skippedClasses.size() ||
        summary_.mobyRenderableClasses + summary_.mobyIntentionallyInvisibleClasses +
                summary_.mobyClassOnlyClasses != summary_.mobyNativeClasses ||
        summary_.ratchetTopologyBatches == 0u ||
        summary_.ratchetTopologyTriangles == 0u ||
        summary_.ratchetSkinVertices == 0u) {
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        unload();
        status_ = Rac1RuntimeRendererStatus::RatchetTopologyInvalid;
        return false;
    }

    const auto ratchetClassIdentity = std::find_if(
        nativeMobyClasses.begin(), nativeMobyClasses.end(),
        [](const Rac1NativeMobyClassIdentity& cls) { return cls.oClass == 0; });
    if (ratchetClassIdentity == nativeMobyClasses.end() ||
        ratchetClassIdentity->kind != Rac1NativeMobyClassKind::RenderableTopology) {
        status_ = Rac1RuntimeRendererStatus::MobyClassAccountingInvalid;
        unload();
        status_ = Rac1RuntimeRendererStatus::MobyClassAccountingInvalid;
        return false;
    }

    core_ = std::move(loaded.core);
    mobySourceTextures_ = std::move(mobyTextures.textures);
    ratchetTopologyInstance_ = *ratchetInstance;
    ratchetAnimationClass_ = std::move(ratchetAnimationBank.bank.animationClass);
    ratchetTopologyBatches_ = std::move(ratchetTopology);
    nativeMobyClasses_ = std::move(nativeMobyClasses);
    nativeSkyShellIdentities_ = std::move(nativeSkyShellIdentities);

    status_ = Rac1RuntimeRendererStatus::Ok;
    return true;
}

Rac1RuntimeLiveRatchetFrame Rac1RuntimeRenderer::prepareLiveRatchetFrame(
    std::span<const std::uint8_t> guestRdram,
    const game::Rac1LiveRatchetAnimationResult& animation,
    const game::Rac1LiveRatchetTransformResult& transform) const {
    Rac1RuntimeLiveRatchetFrame frame{};
    if (!hasRatchetTopology()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::RendererNotReady;
        return frame;
    }

    const auto identity = identifyLiveRatchetRenderIdentity(true, animation, transform);
    if (!identity.ok()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::IdentityNotMapped;
        return frame;
    }
    frame.levelIndex = summary_.levelIndex;
    frame.mobyGuestAddress = identity.mobyGuestAddress;
    frame.transform = transform.transform;

    if (!animation.ok()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::AnimationNotMaterialized;
        return frame;
    }
    if (!transform.ok()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::TransformNotMaterialized;
        return frame;
    }

    const auto pose = game::decodeRac1LiveRatchetPose(
        guestRdram, animation, core_, ratchetAnimationClass_);
    if (!pose.ok()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::PoseDecodeFailed;
        return frame;
    }

    auto skin = assets::executeRac1MobySkinningProgram(ratchetAnimationClass_, pose.pose.pose);
    if (!skin.ok()) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::SkinExecutionFailed;
        return frame;
    }
    if (skin.execution.oClass != 0 ||
        skin.execution.vertices.size() != ratchetTopologyInstance_.skinVertexCount) {
        frame.status = Rac1RuntimeLiveRatchetFrameStatus::SkinVertexCountMismatch;
        return frame;
    }

    frame.execution = std::move(skin.execution);
    frame.status = Rac1RuntimeLiveRatchetFrameStatus::Ok;
    return frame;
}

Rac1RuntimeLiveRatchetApplyStatus Rac1RuntimeRenderer::applyLiveRatchetFrame(
    const Rac1RuntimeLiveRatchetFrame& frame) {
    if (!frame.ok()) return Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized;
    if (!ready() || frame.levelIndex != summary_.levelIndex ||
        frame.mobyGuestAddress == 0u) {
        return Rac1RuntimeLiveRatchetApplyStatus::LevelMismatch;
    }

    const NativeSkinnedMobyPositionTransform positionTransform =
        [&](const std::array<float, 3>& rawPosition) {
            const auto world = game::transformRac1LiveMobyRawPositionToWorld(
                frame.transform, rawPosition);
            return Vector3{world[0], world[1], world[2]};
        };

    if (liveRatchetBatches_.empty()) {
        for (const auto& sourceBatch : ratchetTopologyBatches_) {
            if (!appendSkinnedMobyBatch(
                    sourceBatch.triangleVertices,
                    sourceBatch.materialIndex,
                    mobyTextures_,
                    mobySourceTextures_,
                    ratchetTopologyInstance_,
                    frame.execution,
                    positionTransform,
                    liveRatchetBatches_)) {
                unloadSkinnedMobyBatches(liveRatchetBatches_);
                liveRatchetGpuReady_ = false;
                return Rac1RuntimeLiveRatchetApplyStatus::GpuUploadFailed;
            }
        }
    } else if (!updateSkinnedMobyBatches(
                   liveRatchetBatches_, frame.execution, positionTransform)) {
        liveRatchetGpuReady_ = false;
        return Rac1RuntimeLiveRatchetApplyStatus::GpuUploadFailed;
    }

    if (liveRatchetBatches_.size() != ratchetTopologyBatches_.size()) {
        unloadSkinnedMobyBatches(liveRatchetBatches_);
        liveRatchetGpuReady_ = false;
        return Rac1RuntimeLiveRatchetApplyStatus::GpuUploadFailed;
    }

    liveRatchetGpuReady_ = true;
    return Rac1RuntimeLiveRatchetApplyStatus::Ok;
}

void Rac1RuntimeRenderer::drawSky(
    std::span<const std::array<float, 16>> shellObjectMatrices) const {
    if (!hasSky() || shellObjectMatrices.size() != skyShellBatches_.size()) return;
    constexpr Vector3 kOrigin{0.0f, 0.0f, 0.0f};
    for (std::size_t shell = 0u; shell < skyShellBatches_.size(); ++shell) {
        rlPushMatrix();
        rlMultMatrixf(shellObjectMatrices[shell].data());
        drawBatches(skyShellBatches_[shell], kOrigin);
        // DrawModel queues through rlgl; flush before restoring the next
        // shell's Retail object transform.
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
}

void Rac1RuntimeRenderer::drawStaticWorld() const {
    if (!ready()) return;
    constexpr Vector3 kOrigin{0.0f, 0.0f, 0.0f};
    drawBatches(terrainBatches_, kOrigin);
    drawBatches(staticBatches_, kOrigin);
}

void Rac1RuntimeRenderer::drawLiveRatchet() const {
    if (!ready() || !liveRatchetGpuReady_) return;
    drawSkinnedMobyBatches(liveRatchetBatches_);
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
    case Rac1RuntimeRendererStatus::RuntimeSkyAssetMissing:
        return "runtime-sky-asset-missing";
    case Rac1RuntimeRendererStatus::RuntimeSkyAssetInvalid:
        return "runtime-sky-asset-invalid";
    case Rac1RuntimeRendererStatus::RuntimeSkyDecompressionFailed:
        return "runtime-sky-decompression-failed";
    case Rac1RuntimeRendererStatus::RuntimeSkySourceInvalid:
        return "runtime-sky-source-invalid";
    case Rac1RuntimeRendererStatus::SkyDecodeFailed:
        return "sky-decode-failed";
    case Rac1RuntimeRendererStatus::MobySceneDecodeFailed:
        return "moby-scene-decode-failed";
    case Rac1RuntimeRendererStatus::MobyClassCatalogFailed:
        return "moby-class-catalog-failed";
    case Rac1RuntimeRendererStatus::MobyClassAccountingInvalid:
        return "moby-class-accounting-invalid";
    case Rac1RuntimeRendererStatus::MobyAnimationMetadataFailed:
        return "moby-animation-metadata-failed";
    case Rac1RuntimeRendererStatus::RatchetAnimationBankFailed:
        return "ratchet-animation-bank-failed";
    case Rac1RuntimeRendererStatus::RatchetTopologyInvalid:
        return "ratchet-topology-invalid";
    case Rac1RuntimeRendererStatus::StaticWorldParityInvalid:
        return "static-world-parity-invalid";
    case Rac1RuntimeRendererStatus::GpuUploadFailed:
        return "gpu-upload-failed";
    }
    return "unknown";
}

const char* rac1RuntimeLiveRatchetFrameStatusName(
    Rac1RuntimeLiveRatchetFrameStatus status) noexcept {
    switch (status) {
    case Rac1RuntimeLiveRatchetFrameStatus::RendererNotReady:
        return "renderer-not-ready";
    case Rac1RuntimeLiveRatchetFrameStatus::IdentityNotMapped:
        return "identity-not-mapped";
    case Rac1RuntimeLiveRatchetFrameStatus::AnimationNotMaterialized:
        return "animation-not-materialized";
    case Rac1RuntimeLiveRatchetFrameStatus::TransformNotMaterialized:
        return "transform-not-materialized";
    case Rac1RuntimeLiveRatchetFrameStatus::PoseDecodeFailed:
        return "pose-decode-failed";
    case Rac1RuntimeLiveRatchetFrameStatus::SkinExecutionFailed:
        return "skin-execution-failed";
    case Rac1RuntimeLiveRatchetFrameStatus::SkinVertexCountMismatch:
        return "skin-vertex-count-mismatch";
    case Rac1RuntimeLiveRatchetFrameStatus::Ok:
        return "ok";
    }
    return "unknown";
}

const char* rac1RuntimeLiveRatchetApplyStatusName(
    Rac1RuntimeLiveRatchetApplyStatus status) noexcept {
    switch (status) {
    case Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized:
        return "frame-not-materialized";
    case Rac1RuntimeLiveRatchetApplyStatus::LevelMismatch:
        return "level-mismatch";
    case Rac1RuntimeLiveRatchetApplyStatus::GpuUploadFailed:
        return "gpu-upload-failed";
    case Rac1RuntimeLiveRatchetApplyStatus::Ok:
        return "ok";
    }
    return "unknown";
}

} // namespace ratchet::render
