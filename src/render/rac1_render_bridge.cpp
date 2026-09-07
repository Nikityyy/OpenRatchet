#include "render/rac1_render_bridge.h"

namespace ratchet::render {
namespace {

constexpr std::uint32_t kLevel0Wad2Index = 69u;
constexpr std::uint32_t kLevel0Wad2StartSector = 0x38f6u;
constexpr std::uint32_t kLevel0Wad2SectorCount = 0x0834u;
constexpr std::uint32_t kLevel0Destination = 0x01654000u;
constexpr std::uint32_t kNativeLevel0Index = 0u;

} // namespace

std::optional<std::uint32_t> nativeLevelForRetailAssetRead(
    const platform::NativeAssetLocation& asset,
    std::uint32_t sourceSector,
    std::uint32_t sectorCount,
    std::uint32_t destination) noexcept {
    if (asset.kind != platform::NativeAssetKind::Wad2 ||
        asset.index != kLevel0Wad2Index ||
        asset.startSector != kLevel0Wad2StartSector ||
        asset.sectorCount != kLevel0Wad2SectorCount ||
        sourceSector != asset.startSector ||
        sectorCount != asset.sectorCount ||
        destination != kLevel0Destination) {
        return std::nullopt;
    }
    return kNativeLevel0Index;
}


Rac1LiveMobyClassMapSummary mapLiveMobyClassIdentity(
    const game::Rac1LiveMobyPoolSnapshot& livePool,
    const game::Rac1LiveMobyClassRegistrySnapshot& retailRegistry,
    std::span<const Rac1NativeMobyClassIdentity> nativeClasses) noexcept {
    Rac1LiveMobyClassMapSummary result{};
    if (livePool.status != game::Rac1LiveMobyPoolStatus::Ok) {
        result.status = Rac1LiveMobyClassMapStatus::PoolUnavailable;
        return result;
    }
    if (!retailRegistry.ok()) {
        result.status = Rac1LiveMobyClassMapStatus::RetailRegistryUnavailable;
        return result;
    }
    if (nativeClasses.empty()) {
        result.status = Rac1LiveMobyClassMapStatus::NativeClassCatalogUnavailable;
        return result;
    }

    for (std::size_t i = 0u; i < nativeClasses.size(); ++i) {
        for (std::size_t j = i + 1u; j < nativeClasses.size(); ++j) {
            if (nativeClasses[i].oClass == nativeClasses[j].oClass) {
                result.status = Rac1LiveMobyClassMapStatus::DuplicateNativeClass;
                return result;
            }
        }
    }

    result.records = retailRegistry.records;
    result.active = retailRegistry.active;
    result.inactive = retailRegistry.inactive;
    for (const auto& entry : retailRegistry.activeEntries) {
        if (!entry.ok()) {
            if (!result.hasFirstRegistryIssueOClass) {
                result.hasFirstRegistryIssueOClass = true;
                result.firstRegistryIssueOClass = entry.oClass;
            }
            switch (entry.status) {
            case game::Rac1LiveMobyClassRegistryEntryStatus::Ok:
                break;
            case game::Rac1LiveMobyClassRegistryEntryStatus::OClassOutOfRange:
                ++result.registryOClassOutOfRange;
                break;
            case game::Rac1LiveMobyClassRegistryEntryStatus::UnregisteredOClass:
                ++result.registryUnregistered;
                break;
            case game::Rac1LiveMobyClassRegistryEntryStatus::ClassPointerMismatch:
                ++result.registryPointerMismatch;
                break;
            }
            ++result.unaccounted;
            continue;
        }

        ++result.mapped;
        const Rac1NativeMobyClassIdentity* mapped = nullptr;
        for (const auto& candidate : nativeClasses) {
            if (candidate.oClass == entry.oClass) {
                mapped = &candidate;
                break;
            }
        }
        if (mapped == nullptr) {
            ++result.runtimeOnly;
            continue;
        }
        switch (mapped->kind) {
        case Rac1NativeMobyClassKind::RenderableTopology:
            ++result.renderableTopology;
            break;
        case Rac1NativeMobyClassKind::IntentionallyInvisible:
            ++result.intentionallyInvisible;
            break;
        case Rac1NativeMobyClassKind::ClassOnly:
            ++result.classOnly;
            break;
        }
    }

    const bool poolCountsMatch =
        result.records == livePool.slotsBeforeTerminator &&
        result.active == livePool.traversedMobyCount &&
        result.inactive == livePool.skippedNegativeStateCount &&
        result.active + result.inactive == result.records &&
        retailRegistry.records == livePool.records.size() &&
        retailRegistry.activeEntries.size() == result.active;
    const std::size_t registryIssues = result.registryOClassOutOfRange +
                                       result.registryUnregistered +
                                       result.registryPointerMismatch;
    const bool classCountsMatch =
        result.mapped + registryIssues == result.active &&
        result.renderableTopology + result.intentionallyInvisible + result.classOnly +
                result.runtimeOnly ==
            result.mapped;
    if (!poolCountsMatch || !classCountsMatch) {
        ++result.unaccounted;
        result.status = Rac1LiveMobyClassMapStatus::AccountingMismatch;
        return result;
    }
    if (registryIssues != 0u) {
        result.status = Rac1LiveMobyClassMapStatus::RetailRegistryIdentityMismatch;
        return result;
    }

    result.status = Rac1LiveMobyClassMapStatus::Ok;
    return result;
}

const char* rac1LiveMobyClassMapStatusName(Rac1LiveMobyClassMapStatus status) noexcept {
    switch (status) {
    case Rac1LiveMobyClassMapStatus::Ok: return "ok";
    case Rac1LiveMobyClassMapStatus::PoolUnavailable: return "pool-unavailable";
    case Rac1LiveMobyClassMapStatus::RetailRegistryUnavailable:
        return "retail-registry-unavailable";
    case Rac1LiveMobyClassMapStatus::NativeClassCatalogUnavailable:
        return "native-class-catalog-unavailable";
    case Rac1LiveMobyClassMapStatus::DuplicateNativeClass: return "duplicate-native-class";
    case Rac1LiveMobyClassMapStatus::RetailRegistryIdentityMismatch:
        return "retail-registry-identity-mismatch";
    case Rac1LiveMobyClassMapStatus::AccountingMismatch: return "accounting-mismatch";
    }
    return "unknown";
}

const char* rac1NativeMobyClassKindName(Rac1NativeMobyClassKind kind) noexcept {
    switch (kind) {
    case Rac1NativeMobyClassKind::RenderableTopology: return "renderable-topology";
    case Rac1NativeMobyClassKind::IntentionallyInvisible: return "intentionally-invisible";
    case Rac1NativeMobyClassKind::ClassOnly: return "class-only";
    }
    return "unknown";
}

Rac1LiveSkyMapSummary mapLiveSkyRenderState(
    std::span<const Rac1NativeSkyShellIdentity> nativeShells,
    const game::Rac1LiveSkyResult& liveSky) noexcept {
    Rac1LiveSkyMapSummary result{};
    if (nativeShells.empty()) {
        result.status = Rac1LiveSkyMapStatus::NativeSkyUnavailable;
        return result;
    }

    result.mapped = 1u;
    if (liveSky.identityMaterialized) {
        if (liveSky.sky.shellCount != nativeShells.size()) {
            result.unaccounted = 1u;
            result.status = Rac1LiveSkyMapStatus::ShellCountMismatch;
            return result;
        }
        for (std::size_t shell = 0u; shell < nativeShells.size(); ++shell) {
            const std::uint64_t expectedGuestAddress =
                static_cast<std::uint64_t>(liveSky.sky.skyGuestAddress) +
                nativeShells[shell].sourceOffset;
            if (expectedGuestAddress > std::numeric_limits<std::uint32_t>::max() ||
                liveSky.sky.shellGuestAddresses[shell] !=
                    static_cast<std::uint32_t>(expectedGuestAddress) ||
                liveSky.sky.shellClusterCounts[shell] !=
                    nativeShells[shell].clusterCount ||
                liveSky.sky.shellFlags[shell] != nativeShells[shell].flags) {
                result.unaccounted = 1u;
                result.status = Rac1LiveSkyMapStatus::ShellIdentityMismatch;
                return result;
            }
        }
    }

    if (game::rac1LiveSkyStatusIsDeferred(liveSky.status)) {
        result.deferred = 1u;
        result.status = Rac1LiveSkyMapStatus::LiveTransformDeferred;
        return result;
    }
    if (!liveSky.ok() || !liveSky.identityMaterialized) {
        result.unaccounted = 1u;
        result.status = Rac1LiveSkyMapStatus::LiveTransformInvalid;
        return result;
    }

    result.materialized = 1u;
    result.status = Rac1LiveSkyMapStatus::Ok;
    return result;
}

const char* rac1LiveSkyMapStatusName(Rac1LiveSkyMapStatus status) noexcept {
    switch (status) {
    case Rac1LiveSkyMapStatus::Ok: return "ok";
    case Rac1LiveSkyMapStatus::NativeSkyUnavailable: return "native-sky-unavailable";
    case Rac1LiveSkyMapStatus::LiveTransformDeferred: return "live-transform-deferred";
    case Rac1LiveSkyMapStatus::ShellCountMismatch: return "shell-count-mismatch";
    case Rac1LiveSkyMapStatus::ShellIdentityMismatch: return "shell-identity-mismatch";
    case Rac1LiveSkyMapStatus::LiveTransformInvalid: return "live-transform-invalid";
    }
    return "unknown";
}

Rac1LiveRatchetRenderIdentity identifyLiveRatchetRenderIdentity(
    bool nativeRatchetTopologyAvailable,
    const game::Rac1LiveRatchetAnimationResult& animation,
    const game::Rac1LiveRatchetTransformResult& transform) noexcept {
    Rac1LiveRatchetRenderIdentity result{};
    if (!nativeRatchetTopologyAvailable) {
        result.status = Rac1LiveRatchetRenderIdentityStatus::NativeTopologyUnavailable;
        return result;
    }

    const bool animationIdentityValid =
        (animation.status == game::Rac1LiveRatchetAnimationStatus::Ok ||
         animation.status == game::Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized) &&
        animation.ratchetCandidates == 1u &&
        animation.selection.mobyGuestAddress != 0u;
    if (!animationIdentityValid) {
        result.status = Rac1LiveRatchetRenderIdentityStatus::AnimationIdentityUnavailable;
        return result;
    }

    const bool transformIdentityValid =
        (transform.status == game::Rac1LiveRatchetTransformStatus::Ok ||
         transform.status == game::Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized) &&
        transform.ratchetCandidates == 1u &&
        transform.transform.mobyGuestAddress != 0u;
    if (!transformIdentityValid) {
        result.status = Rac1LiveRatchetRenderIdentityStatus::TransformIdentityUnavailable;
        return result;
    }
    if (transform.transform.oClass != 0) {
        result.status = Rac1LiveRatchetRenderIdentityStatus::OClassMismatch;
        return result;
    }
    if (animation.selection.mobyGuestAddress != transform.transform.mobyGuestAddress) {
        result.status = Rac1LiveRatchetRenderIdentityStatus::MobyAddressMismatch;
        return result;
    }

    result.status = Rac1LiveRatchetRenderIdentityStatus::Ok;
    result.mobyGuestAddress = animation.selection.mobyGuestAddress;
    return result;
}

const char* rac1LiveRatchetRenderIdentityStatusName(
    Rac1LiveRatchetRenderIdentityStatus status) noexcept {
    switch (status) {
    case Rac1LiveRatchetRenderIdentityStatus::Ok:
        return "ok";
    case Rac1LiveRatchetRenderIdentityStatus::NativeTopologyUnavailable:
        return "native-topology-unavailable";
    case Rac1LiveRatchetRenderIdentityStatus::AnimationIdentityUnavailable:
        return "animation-identity-unavailable";
    case Rac1LiveRatchetRenderIdentityStatus::TransformIdentityUnavailable:
        return "transform-identity-unavailable";
    case Rac1LiveRatchetRenderIdentityStatus::MobyAddressMismatch:
        return "moby-address-mismatch";
    case Rac1LiveRatchetRenderIdentityStatus::OClassMismatch:
        return "oclass-mismatch";
    }
    return "unknown";
}

std::array<float, 16> rac1RetailClipMatrixColumnMajor(
    const game::Rac1LiveCameraState& camera) noexcept {
    return {
        camera.clipX[0], camera.clipX[1], camera.clipX[2], camera.clipX[3],
        camera.clipY[0], camera.clipY[1], camera.clipY[2], camera.clipY[3],
        camera.clipZ[0], camera.clipZ[1], camera.clipZ[2], camera.clipZ[3],
        camera.clipW[0], camera.clipW[1], camera.clipW[2], camera.clipW[3],
    };
}

std::array<float, 4> transformColumnMajor(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) noexcept {
    std::array<float, 4> result{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        result[row] = matrix[0u * 4u + row] * point[0] +
                      matrix[1u * 4u + row] * point[1] +
                      matrix[2u * 4u + row] * point[2] +
                      matrix[3u * 4u + row] * point[3];
    }
    return result;
}

} // namespace ratchet::render
