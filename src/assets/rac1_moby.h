#pragma once

#include "assets/rac1_level.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ratchet::assets {

enum class Rac1MobyStatus : std::uint8_t {
    Ok,
    InvalidIndexTable,
    InvalidGameplayHeader,
    InvalidInstanceBlock,
    InvalidClass,
    InvalidPacket,
    InvalidVifLayout,
    InvalidVertexTable,
    InvalidVertexCache,
    InvalidIndexStream,
    InvalidMaterial,
    MissingReferencedClass,
    UnsupportedLowLodOnlyClass,
    UnaccountedNonRenderableClass,
    UnaccountedInstance,
    EmptyScene,
};

enum class Rac1MobySkipReason : std::uint8_t {
    NoClassData,
    NoPacketTable,
    ZeroLodPacketCounts,
    SpecialMaterialDiscard,
};

struct Rac1MobySkippedClass {
    std::int32_t oClass = 0;
    Rac1MobySkipReason reason = Rac1MobySkipReason::NoClassData;
    std::size_t instanceCount = 0u;
    // Post-async-trim source triangles represented by these instances. For
    // SpecialMaterialDiscard this proves there was real mesh geometry which
    // the retail fragment path intentionally discards because TEX0.low < 0.
    std::size_t sourceTriangleCount = 0u;
    std::size_t specialMaterialTriangleCount = 0u;
};

struct Rac1MobyVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint8_t r = 255u;
    std::uint8_t g = 255u;
    std::uint8_t b = 255u;
    std::uint8_t a = 255u;
};

struct Rac1MobyBatch {
    std::uint32_t materialIndex = 0u;
    std::vector<Rac1MobyVertex> triangleVertices;
};

struct Rac1MobySceneMesh {
    std::vector<Rac1MobyBatch> batches;

    // Class table entries present in the level index.
    std::size_t classCount = 0u;
    // Gameplay-referenced classes that contain a decodable LOD0 render mesh.
    // Unreferenced class blobs are intentionally not decoded by this scene pass.
    std::size_t renderableClassCount = 0u;
    // Gameplay moby instances, including logic-only/non-renderable classes.
    std::size_t instanceCount = 0u;
    // Instances that map to a renderable class and were emitted.
    std::size_t renderedInstanceCount = 0u;
    // Instances which are intentionally non-visible in the retail render path:
    // either explicit no-mesh class metadata, or mesh geometry whose surviving
    // triangles all use the negative/special material that the retail fragment
    // shader discards. Every such oClass/reason is surfaced individually.
    std::size_t intentionallyNonVisibleInstanceCount = 0u;
    // Kept as the visible aggregate for diagnostics; on an Ok result this is
    // exactly intentionallyNonVisibleInstanceCount.
    std::size_t skippedInstanceCount = 0u;
    // These must both be zero on every successful decode.
    std::size_t missingClassInstanceCount = 0u;
    std::size_t unaccountedInstanceCount = 0u;
    std::vector<Rac1MobySkippedClass> skippedClasses;
    // Triangle accounting is performed after the retail async-tail trim.
    // sourceTriangleCount = visible triangleCount + specialMaterialTriangleCount.
    std::size_t sourceTriangleCount = 0u;
    std::size_t specialMaterialTriangleCount = 0u;
    std::size_t triangleCount = 0u;
};

struct Rac1MobyResult {
    Rac1MobyStatus status = Rac1MobyStatus::InvalidIndexTable;
    Rac1MobySceneMesh mesh{};

    [[nodiscard]] bool ok() const noexcept { return status == Rac1MobyStatus::Ok; }
};

// Decodes R&C1 moby class LOD0 meshes plus gameplay instance transforms into
// ordinary host-side world-space triangles. This is deliberately a bind-pose
// renderer boundary: the packet/vertex cache and material streams are decoded
// natively, while skeletal animation is left for the next phase.
//
// ClassEntry::textures remaps class-local TEX0 slots to the global moby texture
// table. Gameplay instances provide oClass, scale, Euler rotation, position and
// ambient colour. Logic-only classes (offset 0 / no packet table) are counted
// but skipped rather than treated as malformed render data.
Rac1MobyResult decodeRac1MobyScene(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange mobyClasses,
    std::uint32_t mobyTextureCount);

const char* rac1MobyStatusName(Rac1MobyStatus status) noexcept;
const char* rac1MobySkipReasonName(Rac1MobySkipReason reason) noexcept;

} // namespace ratchet::assets
