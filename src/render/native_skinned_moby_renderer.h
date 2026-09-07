#pragma once

#include "assets/rac1_moby.h"
#include "assets/rac1_moby_animation.h"
#include "assets/rac1_texture.h"

#include <raylib.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ratchet::render {

struct NativeSkinnedMobyBatch {
    Model model{};
    bool transparent = false;
    std::vector<std::uint32_t> skinVertexIndices;
    std::vector<float> positions;
};

using NativeSkinnedMobyPositionTransform =
    std::function<Vector3(const std::array<float, 3>& rawPosition)>;

[[nodiscard]] bool nativeSkinnedMobyVertexMatches(
    const assets::Rac1MobyVertex& vertex,
    const assets::Rac1MobyRenderedInstance& instance) noexcept;

bool appendSkinnedMobyBatch(
    const std::vector<assets::Rac1MobyVertex>& vertices,
    std::uint32_t materialIndex,
    const std::vector<Texture2D>& gpuTextures,
    const std::vector<assets::Rac1Texture>& sourceTextures,
    const assets::Rac1MobyRenderedInstance& instance,
    const assets::Rac1MobySkinExecution& execution,
    const NativeSkinnedMobyPositionTransform& positionTransform,
    std::vector<NativeSkinnedMobyBatch>& output);

bool updateSkinnedMobyBatches(
    std::vector<NativeSkinnedMobyBatch>& batches,
    const assets::Rac1MobySkinExecution& execution,
    const NativeSkinnedMobyPositionTransform& positionTransform);

void drawSkinnedMobyBatches(const std::vector<NativeSkinnedMobyBatch>& batches);
void unloadSkinnedMobyBatches(std::vector<NativeSkinnedMobyBatch>& batches) noexcept;

} // namespace ratchet::render
