#pragma once

#include "game/rac1_live_animation.h"
#include "game/rac1_live_state.h"
#include "game/rac1_live_camera.h"
#include "game/rac1_live_sky.h"
#include "game/rac1_live_transform.h"
#include "platform/native_vfs.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace ratchet::render {

// The only currently proved retail level-identity bridge. Phase 11.2 proved
// that this exact WAD2 request is Level-0 initialization. No adjacent WAD2
// index is inferred to be another level.
[[nodiscard]] std::optional<std::uint32_t> nativeLevelForRetailAssetRead(
    const platform::NativeAssetLocation& asset,
    std::uint32_t sourceSector,
    std::uint32_t sectorCount,
    std::uint32_t destination) noexcept;


enum class Rac1NativeMobyClassKind : std::uint8_t {
    RenderableTopology,
    IntentionallyInvisible,
    ClassOnly,
};

struct Rac1NativeMobyClassIdentity {
    std::int32_t oClass = 0;
    Rac1NativeMobyClassKind kind = Rac1NativeMobyClassKind::ClassOnly;
};

enum class Rac1LiveMobyClassMapStatus : std::uint8_t {
    Ok,
    PoolUnavailable,
    RetailRegistryUnavailable,
    NativeClassCatalogUnavailable,
    DuplicateNativeClass,
    RetailRegistryIdentityMismatch,
    AccountingMismatch,
};

struct Rac1LiveMobyClassMapSummary {
    Rac1LiveMobyClassMapStatus status = Rac1LiveMobyClassMapStatus::PoolUnavailable;
    std::size_t records = 0u;
    std::size_t active = 0u;
    std::size_t inactive = 0u;
    std::size_t mapped = 0u;
    std::size_t renderableTopology = 0u;
    std::size_t intentionallyInvisible = 0u;
    std::size_t classOnly = 0u;
    std::size_t runtimeOnly = 0u;
    std::size_t registryOClassOutOfRange = 0u;
    std::size_t registryUnregistered = 0u;
    std::size_t registryPointerMismatch = 0u;
    bool hasFirstRegistryIssueOClass = false;
    std::int32_t firstRegistryIssueOClass = 0;
    std::size_t unaccounted = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveMobyClassMapStatus::Ok;
    }
};

// FUN_0020C5F0 resolves each requested oClass through Retail's runtime registry
// (0x1B3AC0 -> slot -> 0x1B3200) and stores that exact class-data pointer at moby+0x24.
// Therefore registry agreement is the authoritative live identity oracle. The
// loaded level-core class table is used only to classify whether that proved
// identity already has native topology; a registry-valid class absent from the
// level core is a legitimate runtime-only class, not an identity failure.
// Negative traversal states are accounted as inactive and never consult stale
// object bytes.
[[nodiscard]] Rac1LiveMobyClassMapSummary mapLiveMobyClassIdentity(
    const game::Rac1LiveMobyPoolSnapshot& livePool,
    const game::Rac1LiveMobyClassRegistrySnapshot& retailRegistry,
    std::span<const Rac1NativeMobyClassIdentity> nativeClasses) noexcept;

[[nodiscard]] const char* rac1LiveMobyClassMapStatusName(
    Rac1LiveMobyClassMapStatus status) noexcept;

[[nodiscard]] const char* rac1NativeMobyClassKindName(
    Rac1NativeMobyClassKind kind) noexcept;

struct Rac1NativeSkyShellIdentity {
    std::uint32_t sourceOffset = 0u;
    std::int32_t clusterCount = 0;
    std::int32_t flags = 0;
};

enum class Rac1LiveSkyMapStatus : std::uint8_t {
    Ok,
    NativeSkyUnavailable,
    LiveTransformDeferred,
    ShellCountMismatch,
    ShellIdentityMismatch,
    LiveTransformInvalid,
};

struct Rac1LiveSkyMapSummary {
    Rac1LiveSkyMapStatus status = Rac1LiveSkyMapStatus::NativeSkyUnavailable;
    std::size_t mapped = 0u;
    std::size_t materialized = 0u;
    std::size_t deferred = 0u;
    std::size_t unaccounted = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveSkyMapStatus::Ok;
    }
};

// Native runtime-sky geometry is sourced from the exact Retail WAD that
// produced the live resource. As soon as the live sky header is materialized,
// every shell's relative pointer plus clusterCount/flags provenance must agree
// with that native source even when the transform translation is still deferred.
// This prevents
// a deferred transform from hiding a topology/resource mismatch.
[[nodiscard]] Rac1LiveSkyMapSummary mapLiveSkyRenderState(
    std::span<const Rac1NativeSkyShellIdentity> nativeShells,
    const game::Rac1LiveSkyResult& liveSky) noexcept;

[[nodiscard]] const char* rac1LiveSkyMapStatusName(
    Rac1LiveSkyMapStatus status) noexcept;

enum class Rac1LiveRatchetRenderIdentityStatus : std::uint8_t {
    Ok,
    NativeTopologyUnavailable,
    AnimationIdentityUnavailable,
    TransformIdentityUnavailable,
    MobyAddressMismatch,
    OClassMismatch,
};

struct Rac1LiveRatchetRenderIdentity {
    Rac1LiveRatchetRenderIdentityStatus status =
        Rac1LiveRatchetRenderIdentityStatus::NativeTopologyUnavailable;
    std::uint32_t mobyGuestAddress = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1LiveRatchetRenderIdentityStatus::Ok;
    }
};

// Retail FUN_0020C5F0 writes its oClass argument unchanged to moby+0xa6.
// Steps 11.3/11.4 independently select the unique traversed oClass-0 Moby.
// This bridge therefore joins native Ratchet topology to live Ratchet solely by
// that proved class identity plus exact guest-Moby address agreement; the
// independently relocated moby+0x24 class pointer is deliberately not used.
[[nodiscard]] Rac1LiveRatchetRenderIdentity identifyLiveRatchetRenderIdentity(
    bool nativeRatchetTopologyAvailable,
    const game::Rac1LiveRatchetAnimationResult& animation,
    const game::Rac1LiveRatchetTransformResult& transform) noexcept;

[[nodiscard]] const char* rac1LiveRatchetRenderIdentityStatusName(
    Rac1LiveRatchetRenderIdentityStatus status) noexcept;

// Column-major storage form of Retail's already-materialized camera-relative
// clip transform. FUN_0022BF94 proves that the four qwords are columns:
// X*x + Y*y + Z*z + W*w. FUN_001F2260 proves that camera world position is
// not baked into those four columns. This preserves Retail clip semantics; it
// does not yet convert the post-divide GS screen-Y convention to OpenGL.
[[nodiscard]] std::array<float, 16> rac1RetailClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept;

// Full absolute-world -> clip transform for native geometry. Retail camera users
// such as FUN_001F7D30 first subtract state+0x140 from world xyz before applying
// a camera matrix built from the same rotation block. Algebraically this is the
// materialized clip matrix multiplied by T(-cameraWorldPosition).
[[nodiscard]] std::array<float, 16> rac1RetailWorldToClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept;

// Absolute-world -> OpenGL clip transform used by the native runtime. Retail
// FUN_0022BF94 performs perspective divide and then maps NDC Y through the GS
// screen transform, whose visible Y axis increases downward. OpenGL's viewport
// window Y increases upward. X and the canonical +/-W clip/depth convention are
// already compatible, so the exact host bridge is diag(1,-1,1,1) * Mretail.
[[nodiscard]] std::array<float, 16> rac1RetailWorldToOpenGlClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept;

[[nodiscard]] std::array<float, 4> transformColumnMajor(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) noexcept;

} // namespace ratchet::render
