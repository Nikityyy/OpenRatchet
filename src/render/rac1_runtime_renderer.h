#pragma once

#include "platform/native_vfs.h"
#include "render/native_mesh_renderer.h"

#include <cstddef>
#include <cstdint>
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
    GpuUploadFailed,
};

struct Rac1RuntimeRendererSummary {
    std::uint32_t levelIndex = 0u;
    std::size_t terrainBatches = 0u;
    std::size_t staticBatches = 0u;
    std::size_t terrainTriangles = 0u;
    std::size_t tieTriangles = 0u;
    std::size_t shrubTriangles = 0u;
};

// GPU owner for the already-proved Phase-6..9 static world path. Geometry is
// uploaded in Retail world coordinates; runtime camera conversion is performed
// separately from the exact Step-11.5 clip transform. Dynamic Mobys and sky are
// intentionally not guessed here and remain explicit Step-11.6 deferred work.
class Rac1RuntimeRenderer final {
public:
    Rac1RuntimeRenderer() = default;
    ~Rac1RuntimeRenderer();

    Rac1RuntimeRenderer(const Rac1RuntimeRenderer&) = delete;
    Rac1RuntimeRenderer& operator=(const Rac1RuntimeRenderer&) = delete;

    bool loadLevel(const platform::NativeAssetLocation& level);
    void unload() noexcept;
    void drawStaticWorld() const;

    [[nodiscard]] bool ready() const noexcept {
        return status_ == Rac1RuntimeRendererStatus::Ok;
    }
    [[nodiscard]] Rac1RuntimeRendererStatus status() const noexcept { return status_; }
    [[nodiscard]] const Rac1RuntimeRendererSummary& summary() const noexcept {
        return summary_;
    }

private:
    Rac1RuntimeRendererStatus status_ = Rac1RuntimeRendererStatus::Uninitialized;
    Rac1RuntimeRendererSummary summary_{};
    std::vector<Texture2D> tfragTextures_;
    std::vector<Texture2D> tieTextures_;
    std::vector<Texture2D> shrubTextures_;
    std::vector<NativeDrawBatch> terrainBatches_;
    std::vector<NativeDrawBatch> staticBatches_;
};

[[nodiscard]] const char* rac1RuntimeRendererStatusName(
    Rac1RuntimeRendererStatus status) noexcept;

} // namespace ratchet::render
