#pragma once

#include "assets/rac1_moby.h"
#include "assets/rac1_moby_animation.h"
#include "assets/rac1_sky.h"
#include "game/rac1_live_animation.h"
#include "game/rac1_live_transform.h"
#include "platform/native_vfs.h"
#include "render/native_mesh_renderer.h"
#include "render/native_skinned_moby_renderer.h"
#include "render/rac1_render_bridge.h"
#include "render/rac1_render_parity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::render {

enum class Rac1RuntimeRendererStatus : std::uint8_t {
    Uninitialized,
    Ok,
    LevelFileMissing,
    LevelLoadFailed,
    TextureDecodeFailed,
    TerrainDecodeFailed,
    StaticSceneDecodeFailed,
    RuntimeSkyAssetMissing,
    RuntimeSkyAssetInvalid,
    RuntimeSkyDecompressionFailed,
    RuntimeSkySourceInvalid,
    SkyDecodeFailed,
    MobySceneDecodeFailed,
    MobyClassCatalogFailed,
    MobyClassAccountingInvalid,
    MobyAnimationMetadataFailed,
    RatchetAnimationBankFailed,
    RatchetTopologyInvalid,
    StaticWorldParityInvalid,
    GpuUploadFailed,
};

struct Rac1RuntimeRendererSummary {
    std::uint32_t levelIndex = 0u;
    std::size_t terrainBatches = 0u;
    std::size_t staticBatches = 0u;
    std::size_t terrainTriangles = 0u;
    std::size_t tieTriangles = 0u;
    std::size_t shrubTriangles = 0u;
    std::uint32_t skySourceWad2Index = 0u;
    std::uint32_t skySourceOffset = 0u;
    std::size_t skyBatches = 0u;
    std::size_t skyShells = 0u;
    std::size_t skyClusters = 0u;
    std::size_t skyTriangles = 0u;
    std::size_t skyTextures = 0u;
    std::size_t mobyNativeClasses = 0u;
    std::size_t mobyRenderableClasses = 0u;
    std::size_t mobyIntentionallyInvisibleClasses = 0u;
    std::size_t mobyClassOnlyClasses = 0u;
    std::size_t ratchetTopologyBatches = 0u;
    std::size_t ratchetTopologyTriangles = 0u;
    std::size_t ratchetSkinVertices = 0u;
    Rac1StaticWorldParityDigest staticWorldParity{};
};

enum class Rac1RuntimeLiveRatchetFrameStatus : std::uint8_t {
    RendererNotReady,
    IdentityNotMapped,
    AnimationNotMaterialized,
    TransformNotMaterialized,
    PoseDecodeFailed,
    SkinExecutionFailed,
    SkinVertexCountMismatch,
    Ok,
};

struct Rac1RuntimeLiveRatchetFrame {
    Rac1RuntimeLiveRatchetFrameStatus status =
        Rac1RuntimeLiveRatchetFrameStatus::RendererNotReady;
    std::uint32_t levelIndex = 0u;
    std::uint32_t mobyGuestAddress = 0u;
    assets::Rac1MobySkinExecution execution{};
    game::Rac1LiveMobyWorldTransform transform{};

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1RuntimeLiveRatchetFrameStatus::Ok;
    }
};

enum class Rac1RuntimeLiveRatchetApplyStatus : std::uint8_t {
    FrameNotMaterialized,
    LevelMismatch,
    GpuUploadFailed,
    Ok,
};

// GPU owner for the already-proved Phase-6..10 renderer path. Static world
// geometry stays in Retail world coordinates. Ratchet reuses the exact Phase-10
// oClass-0 topology/skinVertexIndex stream; live Step-11 pose packets and the
// Step-11.4 basis transform are consumed only after both report status=ok.
class Rac1RuntimeRenderer final {
public:
    Rac1RuntimeRenderer() = default;
    ~Rac1RuntimeRenderer();

    Rac1RuntimeRenderer(const Rac1RuntimeRenderer&) = delete;
    Rac1RuntimeRenderer& operator=(const Rac1RuntimeRenderer&) = delete;

    bool loadLevel(const platform::NativeAssetLocation& level,
                   const platform::NativeAssetLocation& runtimeLevelWad);
    void unload() noexcept;
    void drawSky(std::span<const std::array<float, 16>> shellObjectMatrices) const;
    void drawStaticWorld() const;
    void drawLiveRatchet() const;

    [[nodiscard]] Rac1RuntimeLiveRatchetFrame prepareLiveRatchetFrame(
        std::span<const std::uint8_t> guestRdram,
        const game::Rac1LiveRatchetAnimationResult& animation,
        const game::Rac1LiveRatchetTransformResult& transform) const;
    [[nodiscard]] Rac1RuntimeLiveRatchetApplyStatus applyLiveRatchetFrame(
        const Rac1RuntimeLiveRatchetFrame& frame);

    [[nodiscard]] bool ready() const noexcept {
        return status_ == Rac1RuntimeRendererStatus::Ok;
    }
    [[nodiscard]] bool hasSky() const noexcept {
        return ready() && summary_.skyShells > 0u &&
               skyShellBatches_.size() == summary_.skyShells;
    }
    [[nodiscard]] bool hasRatchetTopology() const noexcept {
        return ready() && !ratchetTopologyBatches_.empty() &&
               ratchetTopologyInstance_.oClass == 0 &&
               ratchetAnimationClass_.oClass == 0;
    }
    [[nodiscard]] bool liveRatchetGpuReady() const noexcept {
        return liveRatchetGpuReady_;
    }
    [[nodiscard]] std::span<const Rac1NativeMobyClassIdentity> nativeMobyClasses() const noexcept {
        return nativeMobyClasses_;
    }
    [[nodiscard]] std::span<const Rac1NativeSkyShellIdentity> nativeSkyShellIdentities() const noexcept {
        return nativeSkyShellIdentities_;
    }
    [[nodiscard]] Rac1RuntimeRendererStatus status() const noexcept { return status_; }
    [[nodiscard]] const Rac1RuntimeRendererSummary& summary() const noexcept {
        return summary_;
    }

private:
    Rac1RuntimeRendererStatus status_ = Rac1RuntimeRendererStatus::Uninitialized;
    Rac1RuntimeRendererSummary summary_{};
    std::vector<std::uint8_t> core_;
    std::vector<assets::Rac1Texture> mobySourceTextures_;
    assets::Rac1MobyRenderedInstance ratchetTopologyInstance_{};
    assets::Rac1MobyAnimationClass ratchetAnimationClass_{};
    std::vector<assets::Rac1MobyBatch> ratchetTopologyBatches_;
    std::vector<Rac1NativeMobyClassIdentity> nativeMobyClasses_;
    std::vector<Rac1NativeSkyShellIdentity> nativeSkyShellIdentities_;
    std::vector<Texture2D> tfragTextures_;
    std::vector<Texture2D> tieTextures_;
    std::vector<Texture2D> shrubTextures_;
    std::vector<Texture2D> skyTextures_;
    std::vector<Texture2D> mobyTextures_;
    std::vector<NativeDrawBatch> terrainBatches_;
    std::vector<std::vector<NativeDrawBatch>> skyShellBatches_;
    std::vector<NativeDrawBatch> staticBatches_;
    std::vector<NativeSkinnedMobyBatch> liveRatchetBatches_;
    bool liveRatchetGpuReady_ = false;
};

[[nodiscard]] const char* rac1RuntimeRendererStatusName(
    Rac1RuntimeRendererStatus status) noexcept;
[[nodiscard]] const char* rac1RuntimeLiveRatchetFrameStatusName(
    Rac1RuntimeLiveRatchetFrameStatus status) noexcept;
[[nodiscard]] const char* rac1RuntimeLiveRatchetApplyStatusName(
    Rac1RuntimeLiveRatchetApplyStatus status) noexcept;

} // namespace ratchet::render
