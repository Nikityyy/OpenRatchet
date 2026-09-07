#include "render/rac1_render_bridge.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct TestState {
    int failures = 0;

    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

bool close(float lhs, float rhs, float epsilon = 1.0e-6f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

int main() {
    TestState test;

    ratchet::platform::NativeAssetLocation level0Gameplay{
        ratchet::platform::NativeAssetKind::Wad2,
        69u,
        0x38f6u,
        0x0834u,
        0u,
        {},
    };
    const auto level = ratchet::render::nativeLevelForRetailAssetRead(
        level0Gameplay, 0x38f6u, 0x0834u, 0x01654000u);
    test.expect(level && *level == 0u,
                "proved Retail wads2/69 request maps exactly to native level 0");

    auto wrongIndex = level0Gameplay;
    wrongIndex.index = 68u;
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     wrongIndex, 0x38f6u, 0x0834u, 0x01654000u),
                "adjacent WAD2 indices are not guessed to be levels");
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     level0Gameplay, 0x38f6u, 1u, 0x01654000u),
                "partial reads cannot claim the Level-0 identity bridge");
    auto wrongRange = level0Gameplay;
    wrongRange.startSector = 0x38f7u;
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     wrongRange, 0x38f7u, 0x0834u, 0x01654000u),
                "asset metadata must match the proved Retail Level-0 range");
    test.expect(!ratchet::render::nativeLevelForRetailAssetRead(
                     level0Gameplay, 0x38f6u, 0x0834u, 0x01655000u),
                "same WAD read at another destination cannot claim Level-0 init");


    ratchet::game::Rac1LiveMobyPoolSnapshot livePool;
    livePool.status = ratchet::game::Rac1LiveMobyPoolStatus::Ok;
    livePool.slotsBeforeTerminator = 6u;
    livePool.traversedMobyCount = 5u;
    livePool.skippedNegativeStateCount = 1u;
    auto appendLiveRecord = [&](std::int16_t oClass, bool active) {
        ratchet::game::Rac1LiveMobyRecord record;
        record.slotIndex = livePool.records.size();
        record.guestAddress = 0x00dc2ec0u +
                              static_cast<std::uint32_t>(record.slotIndex) *
                                  ratchet::game::Rac1LiveMobyLayout::kStride;
        record.traversalState = active ? 0 : -2;
        record.participatesInRetailTraversal = active;
        record.oClass = oClass;
        livePool.records.push_back(record);
    };
    appendLiveRecord(0, true);
    appendLiveRecord(42, true);
    appendLiveRecord(43, true);
    appendLiveRecord(777, true);
    appendLiveRecord(1905, true); // observed Windows runtime-only counterexample
    appendLiveRecord(9999, false); // stale bytes are not identity inputs

    ratchet::game::Rac1LiveMobyClassRegistrySnapshot retailRegistry;
    retailRegistry.status = ratchet::game::Rac1LiveMobyClassRegistryStatus::Ok;
    retailRegistry.records = 6u;
    retailRegistry.active = 5u;
    retailRegistry.inactive = 1u;
    for (std::size_t i = 0u; i < 5u; ++i) {
        const auto& record = livePool.records[i];
        ratchet::game::Rac1LiveMobyClassRegistryEntry entry;
        entry.status = ratchet::game::Rac1LiveMobyClassRegistryEntryStatus::Ok;
        entry.mobyGuestAddress = record.guestAddress;
        entry.oClass = record.oClass;
        entry.registrySlot = static_cast<std::uint8_t>(i);
        entry.mobyClassPointer = 0x00100000u + static_cast<std::uint32_t>(i) * 0x100u;
        entry.registryClassPointer = entry.mobyClassPointer;
        retailRegistry.activeEntries.push_back(entry);
    }

    const std::vector<ratchet::render::Rac1NativeMobyClassIdentity> nativeClasses{
        {0, ratchet::render::Rac1NativeMobyClassKind::RenderableTopology},
        {42, ratchet::render::Rac1NativeMobyClassKind::RenderableTopology},
        {43, ratchet::render::Rac1NativeMobyClassKind::RenderableTopology},
        {777, ratchet::render::Rac1NativeMobyClassKind::ClassOnly},
    };
    const auto classMap = ratchet::render::mapLiveMobyClassIdentity(
        livePool, retailRegistry, nativeClasses);
    test.expect(classMap.ok() && classMap.records == 6u && classMap.active == 5u &&
                    classMap.inactive == 1u && classMap.mapped == 5u &&
                    classMap.renderableTopology == 3u &&
                    classMap.intentionallyInvisible == 0u && classMap.classOnly == 1u &&
                    classMap.runtimeOnly == 1u && classMap.registryUnregistered == 0u &&
                    classMap.registryPointerMismatch == 0u && classMap.unaccounted == 0u,
                "registry-valid oClass 1905 maps as runtime-only while all five live identities account");

    auto levelSubset = nativeClasses;
    levelSubset.pop_back();
    const auto runtimeOnlyClass = ratchet::render::mapLiveMobyClassIdentity(
        livePool, retailRegistry, levelSubset);
    test.expect(runtimeOnlyClass.ok() && runtimeOnlyClass.mapped == 5u &&
                    runtimeOnlyClass.runtimeOnly == 2u &&
                    runtimeOnlyClass.renderableTopology == 3u &&
                    runtimeOnlyClass.intentionallyInvisible == 0u &&
                    runtimeOnlyClass.classOnly == 0u && runtimeOnlyClass.unaccounted == 0u,
                "registry-valid classes absent from the level core are runtime-only, not missing identity");

    auto unregisteredRegistry = retailRegistry;
    unregisteredRegistry.activeEntries[4].status =
        ratchet::game::Rac1LiveMobyClassRegistryEntryStatus::UnregisteredOClass;
    const auto unregisteredClass = ratchet::render::mapLiveMobyClassIdentity(
        livePool, unregisteredRegistry, nativeClasses);
    test.expect(unregisteredClass.status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::RetailRegistryIdentityMismatch &&
                    unregisteredClass.mapped == 4u &&
                    unregisteredClass.registryUnregistered == 1u &&
                    unregisteredClass.hasFirstRegistryIssueOClass &&
                    unregisteredClass.firstRegistryIssueOClass == 1905 &&
                    unregisteredClass.unaccounted == 1u,
                "unregistered active Retail oClass is a hard identity gap");

    auto pointerMismatchRegistry = retailRegistry;
    pointerMismatchRegistry.activeEntries[2].status =
        ratchet::game::Rac1LiveMobyClassRegistryEntryStatus::ClassPointerMismatch;
    const auto pointerMismatch = ratchet::render::mapLiveMobyClassIdentity(
        livePool, pointerMismatchRegistry, nativeClasses);
    test.expect(pointerMismatch.status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::RetailRegistryIdentityMismatch &&
                    pointerMismatch.registryPointerMismatch == 1u &&
                    pointerMismatch.unaccounted == 1u,
                "moby+0x24 must agree exactly with Retail 0x1B3200 class-data table");

    auto duplicateDomain = nativeClasses;
    duplicateDomain.push_back(nativeClasses[1]);
    test.expect(ratchet::render::mapLiveMobyClassIdentity(
                    livePool, retailRegistry, duplicateDomain).status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::DuplicateNativeClass,
                "duplicate native oClass identities are rejected");

    auto unavailablePool = livePool;
    unavailablePool.status = ratchet::game::Rac1LiveMobyPoolStatus::PoolNotInitialized;
    test.expect(ratchet::render::mapLiveMobyClassIdentity(
                    unavailablePool, retailRegistry, nativeClasses).status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::PoolUnavailable,
                "unmaterialized Retail pool does not publish class identity");

    auto unavailableRegistry = retailRegistry;
    unavailableRegistry.status = ratchet::game::Rac1LiveMobyClassRegistryStatus::GuestMemoryTooSmall;
    test.expect(ratchet::render::mapLiveMobyClassIdentity(
                    livePool, unavailableRegistry, nativeClasses).status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::RetailRegistryUnavailable,
                "unavailable Retail class registry cannot publish live class identity");

    auto inconsistentPool = livePool;
    inconsistentPool.traversedMobyCount = 4u;
    test.expect(ratchet::render::mapLiveMobyClassIdentity(
                    inconsistentPool, retailRegistry, nativeClasses).status ==
                    ratchet::render::Rac1LiveMobyClassMapStatus::AccountingMismatch,
                "pool traversal counters must exactly match mapped active records");

    ratchet::game::Rac1LiveRatchetAnimationResult liveAnimation;
    liveAnimation.status = ratchet::game::Rac1LiveRatchetAnimationStatus::EndpointsNotMaterialized;
    liveAnimation.ratchetCandidates = 1u;
    liveAnimation.selection.mobyGuestAddress = 0x00dc2ec0u;

    ratchet::game::Rac1LiveRatchetTransformResult liveTransform;
    liveTransform.status = ratchet::game::Rac1LiveRatchetTransformStatus::RotationBasisNotMaterialized;
    liveTransform.ratchetCandidates = 1u;
    liveTransform.transform.mobyGuestAddress = 0x00dc2ec0u;
    liveTransform.transform.oClass = 0;

    const auto deferredIdentity = ratchet::render::identifyLiveRatchetRenderIdentity(
        true, liveAnimation, liveTransform);
    test.expect(deferredIdentity.ok() && deferredIdentity.mobyGuestAddress == 0x00dc2ec0u,
                "proved oClass-0 identity maps Ratchet even before pose/basis materialization");

    const auto noTopologyIdentity = ratchet::render::identifyLiveRatchetRenderIdentity(
        false, liveAnimation, liveTransform);
    test.expect(noTopologyIdentity.status ==
                    ratchet::render::Rac1LiveRatchetRenderIdentityStatus::NativeTopologyUnavailable,
                "live Ratchet cannot map when native Ratchet topology is unavailable");

    auto wrongAddressTransform = liveTransform;
    wrongAddressTransform.transform.mobyGuestAddress += ratchet::game::Rac1LiveMobyLayout::kStride;
    test.expect(ratchet::render::identifyLiveRatchetRenderIdentity(
                    true, liveAnimation, wrongAddressTransform).status ==
                    ratchet::render::Rac1LiveRatchetRenderIdentityStatus::MobyAddressMismatch,
                "animation and transform bridges must identify the exact same guest Moby");

    auto wrongClassTransform = liveTransform;
    wrongClassTransform.transform.oClass = 1;
    test.expect(ratchet::render::identifyLiveRatchetRenderIdentity(
                    true, liveAnimation, wrongClassTransform).status ==
                    ratchet::render::Rac1LiveRatchetRenderIdentityStatus::OClassMismatch,
                "nonzero oClass cannot map onto native Ratchet topology");

    auto invalidAnimation = liveAnimation;
    invalidAnimation.status = ratchet::game::Rac1LiveRatchetAnimationStatus::FramePointerAOutOfRange;
    test.expect(ratchet::render::identifyLiveRatchetRenderIdentity(
                    true, invalidAnimation, liveTransform).status ==
                    ratchet::render::Rac1LiveRatchetRenderIdentityStatus::AnimationIdentityUnavailable,
                "malformed live animation state cannot publish Ratchet render identity");

    auto invalidTransform = liveTransform;
    invalidTransform.status = ratchet::game::Rac1LiveRatchetTransformStatus::NonFiniteScale;
    test.expect(ratchet::render::identifyLiveRatchetRenderIdentity(
                    true, liveAnimation, invalidTransform).status ==
                    ratchet::render::Rac1LiveRatchetRenderIdentityStatus::TransformIdentityUnavailable,
                "malformed live transform state cannot publish Ratchet render identity");

    const std::array<ratchet::render::Rac1NativeSkyShellIdentity, 4> nativeSky{{
        {0x49d00u, 38, 1},
        {0x4ce60u, 25, 0},
        {0x4ed70u, 3, 0},
        {0x4fa00u, 24, 0},
    }};
    ratchet::game::Rac1LiveSkyResult liveSky;
    liveSky.status = ratchet::game::Rac1LiveSkyStatus::Ok;
    liveSky.identityMaterialized = true;
    liveSky.sky.skyGuestAddress = 0x714640u;
    liveSky.sky.shellCount = 4u;
    liveSky.sky.shellGuestAddresses = {0x75e340u, 0x7614a0u, 0x7633b0u, 0x764040u, 0u, 0u};
    liveSky.sky.shellClusterCounts = {38, 25, 3, 24, 0, 0};
    liveSky.sky.shellFlags = {1, 0, 0, 0, 0, 0};
    const auto skyMapped = ratchet::render::mapLiveSkyRenderState(nativeSky, liveSky);
    test.expect(skyMapped.ok() && skyMapped.mapped == 1u &&
                    skyMapped.materialized == 1u && skyMapped.deferred == 0u &&
                    skyMapped.unaccounted == 0u,
                "native runtime-WAD sky plus matching Retail identity materializes exactly once");

    const std::span<const ratchet::render::Rac1NativeSkyShellIdentity> noNativeSkySpan;
    const auto noNativeSky = ratchet::render::mapLiveSkyRenderState(noNativeSkySpan, liveSky);
    test.expect(noNativeSky.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::NativeSkyUnavailable &&
                    noNativeSky.mapped == 0u && noNativeSky.unaccounted == 0u,
                "absent native runtime sky cannot claim a live transform mapping");

    auto deferredSky = liveSky;
    deferredSky.status = ratchet::game::Rac1LiveSkyStatus::TranslationNotMaterialized;
    const auto skyDeferred = ratchet::render::mapLiveSkyRenderState(nativeSky, deferredSky);
    test.expect(skyDeferred.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::LiveTransformDeferred &&
                    skyDeferred.mapped == 1u && skyDeferred.materialized == 0u &&
                    skyDeferred.deferred == 1u && skyDeferred.unaccounted == 0u,
                "matching runtime-WAD sky identity remains accounted while transform is deferred");

    auto wrongShellCountSky = deferredSky;
    wrongShellCountSky.sky.shellCount = 5u;
    const auto skyCountMismatch =
        ratchet::render::mapLiveSkyRenderState(nativeSky, wrongShellCountSky);
    test.expect(skyCountMismatch.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::ShellCountMismatch &&
                    skyCountMismatch.mapped == 1u && skyCountMismatch.unaccounted == 1u,
                "deferred transform cannot hide a native/live sky shell-count contradiction");

    auto wrongPointerSky = deferredSky;
    ++wrongPointerSky.sky.shellGuestAddresses[2];
    const auto skyPointerMismatch =
        ratchet::render::mapLiveSkyRenderState(nativeSky, wrongPointerSky);
    test.expect(skyPointerMismatch.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::ShellIdentityMismatch &&
                    skyPointerMismatch.mapped == 1u && skyPointerMismatch.unaccounted == 1u,
                "deferred transform cannot hide a different Retail shell relocation");

    auto wrongIdentitySky = deferredSky;
    wrongIdentitySky.sky.shellClusterCounts[2] = 7;
    const auto skyIdentityMismatch =
        ratchet::render::mapLiveSkyRenderState(nativeSky, wrongIdentitySky);
    test.expect(skyIdentityMismatch.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::ShellIdentityMismatch &&
                    skyIdentityMismatch.mapped == 1u && skyIdentityMismatch.unaccounted == 1u,
                "deferred transform cannot hide a different Retail sky resource");

    auto malformedSky = liveSky;
    malformedSky.status = ratchet::game::Rac1LiveSkyStatus::InvalidTranslationW;
    const auto skyInvalid = ratchet::render::mapLiveSkyRenderState(nativeSky, malformedSky);
    test.expect(skyInvalid.status ==
                    ratchet::render::Rac1LiveSkyMapStatus::LiveTransformInvalid &&
                    skyInvalid.mapped == 1u && skyInvalid.unaccounted == 1u,
                "malformed live sky transform cannot be silently deferred");

    ratchet::game::Rac1LiveCameraState camera;
    camera.clipX = {1.0f, 2.0f, 3.0f, 4.0f};
    camera.clipY = {5.0f, 6.0f, 7.0f, 8.0f};
    camera.clipZ = {9.0f, 10.0f, 11.0f, 12.0f};
    camera.clipW = {13.0f, 14.0f, 15.0f, 16.0f};
    const std::array<float, 4> point{2.0f, -3.0f, 0.5f, 1.0f};

    const auto retail = ratchet::game::transformRac1LiveCameraPointToClip(camera, point);
    const auto matrix = ratchet::render::rac1RetailClipMatrixColumnMajor(camera);
    const auto host = ratchet::render::transformColumnMajor(matrix, point);
    const std::array<float, 4> expected{4.5f, 5.0f, 5.5f, 6.0f};
    for (std::size_t i = 0u; i < retail.size(); ++i) {
        test.expect(close(retail[i], expected[i]),
                    "Retail clip oracle matches independently evaluated vector result");
        test.expect(close(host[i], expected[i]),
                    "column-major host matrix preserves Retail clip multiplication order");
    }

    test.expect(matrix[0] == camera.clipX[0] && matrix[3] == camera.clipX[3] &&
                    matrix[4] == camera.clipY[0] && matrix[15] == camera.clipW[3],
                "matrix storage is literal X/Y/Z/W column order for rlgl/OpenGL");

    if (test.failures != 0) {
        std::cerr << test.failures << " render bridge test(s) failed\n";
        return 1;
    }
    std::cout << "render bridge tests passed\n";
    return 0;
}
