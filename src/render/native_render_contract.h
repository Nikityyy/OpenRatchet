#pragma once

#include <cstdint>

namespace ratchet::render {

enum class NativeRenderPass : std::uint8_t {
    Sky,
    World,
};

enum class NativeBlendContract : std::uint8_t {
    Alpha,
};

struct NativeRenderStateContract {
    bool depthTest = false;
    bool depthWrite = false;
    bool backfaceCulling = false;
    bool colorBlend = true;
    bool wireframe = false;
    NativeBlendContract blend = NativeBlendContract::Alpha;
};

[[nodiscard]] constexpr NativeRenderStateContract nativeRenderStateContract(
    NativeRenderPass pass,
    bool wireframe = false) noexcept {
    NativeRenderStateContract state{};
    state.depthTest = pass == NativeRenderPass::World;
    state.depthWrite = pass == NativeRenderPass::World;
    state.backfaceCulling = false;
    state.colorBlend = true;
    state.wireframe = wireframe;
    state.blend = NativeBlendContract::Alpha;
    return state;
}

} // namespace ratchet::render
