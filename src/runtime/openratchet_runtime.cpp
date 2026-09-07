#include "runtime/openratchet_runtime.h"

#include "game/native_replacements.h"
#include "game/native_services.h"
#include "game/rac1_live_animation.h"
#include "game/rac1_live_camera.h"
#include "game/rac1_live_state.h"
#include "game/rac1_live_sky.h"
#include "game/rac1_live_transform.h"
#include "guest_overrides.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"
#include "render/rac1_render_bridge.h"
#include "render/rac1_runtime_renderer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <utility>

#if defined(_M_X64) || defined(__SSE__)
#include <immintrin.h>
#endif

#include "ps2_runtime.h"

#include <raylib.h>
#include <rlgl.h>

namespace ratchet {
namespace {

void configureHostFloatingPoint() {
#if defined(_MSC_VER) && defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8000u | 0x0040u);
#elif defined(__SSE__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

const char* stageName(runtime::NativeReplacementStage stage) {
    switch (stage) {
    case runtime::NativeReplacementStage::Bootstrap:
        return "bootstrap";
    case runtime::NativeReplacementStage::Runtime:
        return "runtime";
    }
    return "unknown";
}

void installStage(PS2Runtime& fallback,
                  const runtime::NativeReplacementRegistry& replacements,
                  runtime::NativeReplacementStage stage) {
    const runtime::NativeReplacementInstallSummary summary =
        replacements.install(fallback, stage);
    std::cerr << "[OpenRatchet:native] replacements stage=" << stageName(stage)
              << " declared=" << summary.declared
              << " installed=" << summary.installed
              << " install_errors=" << summary.failed << '\n';
}

struct LiveMobySnapshotSignature {
    game::Rac1LiveMobyPoolStatus status =
        game::Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
    std::uint32_t poolBase = 0u;
    std::uint32_t poolLastSlot = 0u;
    std::size_t terminatorSlot = 0u;
    std::size_t slotsBeforeTerminator = 0u;
    std::size_t traversedMobyCount = 0u;
    std::size_t skippedNegativeStateCount = 0u;
    std::size_t ratchetCandidateCount = 0u;

    bool operator==(const LiveMobySnapshotSignature&) const = default;
};

LiveMobySnapshotSignature liveMobySignature(
    const game::Rac1LiveMobyPoolSnapshot& snapshot) {
    return {
        snapshot.status,
        snapshot.poolBase,
        snapshot.poolLastSlot,
        snapshot.terminatorSlot,
        snapshot.slotsBeforeTerminator,
        snapshot.traversedMobyCount,
        snapshot.skippedNegativeStateCount,
        snapshot.ratchetCandidateCount,
    };
}

void logLiveMobySnapshot(const game::Rac1LiveMobyPoolSnapshot& snapshot) {
    using Layout = game::Rac1LiveMobyLayout;

    std::cerr << "[OpenRatchet:live:moby]"
              << " source=guest-rdram"
              << " capacity=" << Layout::kCapacity;

    if (snapshot.status != game::Rac1LiveMobyPoolStatus::PoolNotInitialized) {
        std::cerr << " pool=0x" << std::hex << snapshot.poolBase
                  << " last=0x" << snapshot.poolLastSlot << std::dec;
    }

    if (snapshot.status == game::Rac1LiveMobyPoolStatus::Ok) {
        const std::size_t accounted =
            snapshot.traversedMobyCount + snapshot.skippedNegativeStateCount;
        const std::size_t unaccounted =
            snapshot.slotsBeforeTerminator >= accounted
                ? snapshot.slotsBeforeTerminator - accounted
                : accounted - snapshot.slotsBeforeTerminator;

        std::cerr << " terminatorSlot=" << snapshot.terminatorSlot
                  << " slotsBeforeTerminator=" << snapshot.slotsBeforeTerminator
                  << " traversed=" << snapshot.traversedMobyCount
                  << " skipped=" << snapshot.skippedNegativeStateCount
                  << " ratchetCandidates=" << snapshot.ratchetCandidateCount
                  << " records=" << snapshot.records.size()
                  << " accounted=" << accounted
                  << " unaccounted=" << unaccounted;
    }

    std::cerr << " status=" << game::rac1LiveMobyPoolStatusName(snapshot.status)
              << '\n';
}

void logLiveRatchetAnimation(
    const game::Rac1LiveRatchetAnimationResult& animation) {
    std::cerr << "[OpenRatchet:live:ratchet-animation]"
              << " source=guest-rdram"
              << " ratchetCandidates=" << animation.ratchetCandidates;

    // Keep permanent failure diagnostics at the semantic boundary. A partially
    // resolved selection is still valuable evidence when a packet fails closed,
    // and avoids reintroducing temporary probes for the next Retail edge case.
    const auto& selection = animation.selection;
    if (selection.mobyGuestAddress != 0u) {
        std::cerr << " moby=0x" << std::hex << selection.mobyGuestAddress
                  << " class=0x" << selection.classPointer << std::dec
                  << " sequenceCount=" << static_cast<unsigned>(selection.sequenceCount)
                  << " externalSequenceCount="
                  << static_cast<unsigned>(selection.externalSequenceCount)
                  << " runtimeLocalSequenceCount="
                  << static_cast<unsigned>(selection.runtimeLocalSequenceCount)
                  << " endpointA="
                  << game::rac1LiveAnimationEndpointKindName(selection.endpointA.kind)
                  << " sequenceA=" << static_cast<unsigned>(selection.endpointA.sequenceIndex)
                  << " frameA=" << static_cast<unsigned>(selection.endpointA.frameIndex)
                  << " sequencePointerA=0x" << std::hex
                  << selection.endpointA.sequencePointer
                  << " framePointerA=0x"
                  << selection.endpointA.observedFramePointer
                  << " resolvedFramePointerA=0x"
                  << selection.endpointA.expectedFramePointer << std::dec
                  << " packetBytesA=0x" << std::hex
                  << selection.endpointA.packetBytes << std::dec
                  << " endpointB="
                  << game::rac1LiveAnimationEndpointKindName(selection.endpointB.kind)
                  << " sequenceB=" << static_cast<unsigned>(selection.endpointB.sequenceIndex)
                  << " frameB=" << static_cast<unsigned>(selection.endpointB.frameIndex)
                  << " sequencePointerB=0x" << std::hex
                  << selection.endpointB.sequencePointer
                  << " framePointerB=0x"
                  << selection.endpointB.observedFramePointer
                  << " resolvedFramePointerB=0x"
                  << selection.endpointB.expectedFramePointer << std::dec
                  << " packetBytesB=0x" << std::hex
                  << selection.endpointB.packetBytes << std::dec
                  << " alpha=" << selection.interpolation;
    }

    std::cerr << " status="
              << game::rac1LiveRatchetAnimationStatusName(animation.status)
              << '\n';
}

void logLiveRatchetTransform(
    const game::Rac1LiveRatchetTransformResult& transformResult) {
    std::cerr << "[OpenRatchet:live:ratchet-transform]"
              << " source=guest-rdram"
              << " ratchetCandidates=" << transformResult.ratchetCandidates;

    const auto& transform = transformResult.transform;
    if (transform.mobyGuestAddress != 0u) {
        const auto printVector = [](const std::array<float, 3>& value) {
            std::cerr << '(' << value[0] << ',' << value[1] << ',' << value[2] << ')';
        };

        std::cerr << " moby=0x" << std::hex << transform.mobyGuestAddress << std::dec
                  << " oClass=" << transform.oClass
                  << " position=";
        printVector(transform.position);
        std::cerr << " rawScale=" << transform.rawModelScale
                  << " worldScale=" << transform.worldModelScale
                  << " rotationInput=";
        printVector(transform.rotationInput);
        std::cerr << " basisX=";
        printVector(transform.basisX);
        std::cerr << " basisY=";
        printVector(transform.basisY);
        std::cerr << " basisZ=";
        printVector(transform.basisZ);
    }

    std::cerr << " status="
              << game::rac1LiveRatchetTransformStatusName(transformResult.status)
              << '\n';
}


void logLiveCamera(const game::Rac1LiveCameraResult& cameraResult) {
    using Layout = game::Rac1LiveCameraLayout;

    const auto printVec3 = [](const std::array<float, 3>& value) {
        std::cerr << '(' << value[0] << ',' << value[1] << ',' << value[2] << ')';
    };
    const auto printVec4 = [](const std::array<float, 4>& value) {
        std::cerr << '(' << value[0] << ',' << value[1] << ',' << value[2] << ','
                  << value[3] << ')';
    };

    std::cerr << "[OpenRatchet:live:camera]"
              << " source=guest-rdram"
              << " state=0x" << std::hex << Layout::kStateBase << std::dec
              << " position=";
    printVec3(cameraResult.camera.worldPosition);
    std::cerr << " orientationX=";
    printVec3(cameraResult.camera.orientationX);
    std::cerr << " orientationY=";
    printVec3(cameraResult.camera.orientationY);
    std::cerr << " orientationZ=";
    printVec3(cameraResult.camera.orientationZ);
    std::cerr << " clipX=";
    printVec4(cameraResult.camera.clipX);
    std::cerr << " clipY=";
    printVec4(cameraResult.camera.clipY);
    std::cerr << " clipZ=";
    printVec4(cameraResult.camera.clipZ);
    std::cerr << " clipW=";
    printVec4(cameraResult.camera.clipW);
    std::cerr << " status=" << game::rac1LiveCameraStatusName(cameraResult.status)
              << '\n';
}

void logLiveSky(const game::Rac1LiveSkyResult& skyResult) {
    const auto printVec4 = [](const std::array<float, 4>& value) {
        std::cerr << '(' << value[0] << ',' << value[1] << ',' << value[2] << ','
                  << value[3] << ')';
    };

    std::cerr << "[OpenRatchet:live:sky]"
              << " source=guest-rdram"
              << " sky=0x" << std::hex << skyResult.sky.skyGuestAddress << std::dec
              << " shells=" << skyResult.sky.shellCount
              << " identityMaterialized=" << (skyResult.identityMaterialized ? 1 : 0);
    if (skyResult.identityMaterialized) {
        std::cerr << " baseAngle=" << skyResult.sky.baseAngle
                  << " translation=";
        printVec4(skyResult.sky.translation);
        const std::size_t shellCount = std::min(
            skyResult.sky.shellCount,
            game::Rac1LiveSkyLayout::kProvedShellTransformCount);
        for (std::size_t shell = 0u; shell < shellCount; ++shell) {
            std::cerr << " shell" << shell
                      << "=0x" << std::hex << skyResult.sky.shellGuestAddresses[shell]
                      << std::dec << ":clusters=" << skyResult.sky.shellClusterCounts[shell]
                      << ":flags=0x" << std::hex
                      << static_cast<std::uint32_t>(skyResult.sky.shellFlags[shell])
                      << std::dec;
        }
    }
    std::cerr << " status=" << game::rac1LiveSkyStatusName(skyResult.status) << '\n';
}

struct NativeRenderAccountingSignature {
    int requestedLevel = -1;
    bool sceneMaterialized = false;
    bool sceneRendered = false;
    std::size_t liveMobyRecords = 0u;
    std::size_t liveMobyActive = 0u;
    std::size_t liveMobyInactive = 0u;
    std::size_t liveMobyMapped = 0u;
    std::size_t liveMobyRuntimeOnlyClass = 0u;
    std::size_t liveMobyRegistryOClassOutOfRange = 0u;
    std::size_t liveMobyRegistryUnregistered = 0u;
    std::size_t liveMobyRegistryPointerMismatch = 0u;
    bool liveMobyHasFirstRegistryIssueOClass = false;
    std::int32_t liveMobyFirstRegistryIssueOClass = 0;
    std::size_t liveMobyClassUnaccounted = 0u;
    render::Rac1LiveMobyClassMapStatus liveMobyClassMapStatus =
        render::Rac1LiveMobyClassMapStatus::PoolUnavailable;
    std::size_t mobyPoolUnaccounted = 0u;
    game::Rac1LiveMobyPoolStatus mobyPoolStatus =
        game::Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
    game::Rac1LiveCameraStatus cameraStatus =
        game::Rac1LiveCameraStatus::GuestMemoryTooSmall;
    game::Rac1LiveSkyStatus skyStatus =
        game::Rac1LiveSkyStatus::GuestMemoryTooSmall;
    render::Rac1LiveSkyMapStatus skyMapStatus =
        render::Rac1LiveSkyMapStatus::NativeSkyUnavailable;
    std::size_t skyMapped = 0u;
    std::size_t skyMaterialized = 0u;
    std::size_t skyRendered = 0u;
    std::size_t skyDeferred = 0u;
    std::size_t skyUnaccounted = 0u;
    game::Rac1LiveRatchetAnimationStatus animationStatus =
        game::Rac1LiveRatchetAnimationStatus::PoolNotReady;
    game::Rac1LiveRatchetTransformStatus transformStatus =
        game::Rac1LiveRatchetTransformStatus::PoolNotReady;
    render::Rac1RuntimeRendererStatus rendererStatus =
        render::Rac1RuntimeRendererStatus::Uninitialized;
    render::Rac1LiveRatchetRenderIdentityStatus ratchetIdentityStatus =
        render::Rac1LiveRatchetRenderIdentityStatus::NativeTopologyUnavailable;
    render::Rac1RuntimeLiveRatchetFrameStatus ratchetFrameStatus =
        render::Rac1RuntimeLiveRatchetFrameStatus::RendererNotReady;
    render::Rac1RuntimeLiveRatchetApplyStatus ratchetApplyStatus =
        render::Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized;

    bool operator==(const NativeRenderAccountingSignature&) const = default;
};

} // namespace

struct OpenRatchetRuntime::Impl {
    platform::NativeVfs vfs;
    PS2Runtime eeFallback;
    runtime::NativeReplacementRegistry replacements;
    render::Rac1RuntimeRenderer nativeRenderer;
    std::atomic<int> requestedNativeLevel{-1};
    std::atomic<int> requestedRuntimeWad2{-1};
    std::optional<int> rendererAttemptedLevel;
    std::optional<int> rendererAttemptedRuntimeWad2;
    std::optional<NativeRenderAccountingSignature> lastRenderAccounting;
    std::optional<LiveMobySnapshotSignature> lastLiveMobySignature;
    game::Rac1LiveRatchetAnimationResult liveRatchetAnimation;
    std::optional<game::Rac1LiveRatchetAnimationStatus> lastLoggedAnimationStatus;
    game::Rac1LiveRatchetTransformResult liveRatchetTransform;
    std::optional<game::Rac1LiveRatchetTransformStatus> lastLoggedTransformStatus;
    game::Rac1LiveCameraResult liveCamera;
    std::optional<game::Rac1LiveCameraStatus> lastLoggedCameraStatus;
    game::Rac1LiveSkyResult liveSky;
    std::optional<game::Rac1LiveSkyStatus> lastLoggedSkyStatus;
    render::Rac1RuntimeLiveRatchetFrame liveRatchetFrame;
    render::Rac1RuntimeLiveRatchetApplyStatus liveRatchetApplyStatus =
        render::Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized;
    std::uint64_t liveMobyPresentationCount = 0u;
    game::Rac1LiveMobyPoolSnapshot liveMobySnapshot;
    game::Rac1LiveMobyClassRegistrySnapshot liveMobyClassRegistry;
    std::size_t liveMobyRecordCount = 0u;
    std::size_t liveMobyPoolUnaccounted = 0u;
    game::Rac1LiveMobyPoolStatus liveMobyPoolStatus =
        game::Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
    bool initialized = false;

    static void observeIndexedAssetRead(
        void* userData,
        const platform::NativeAssetLocation& asset,
        std::uint32_t sourceSector,
        std::uint32_t sectorCount,
        std::uint32_t destination) {
        auto& self = *static_cast<Impl*>(userData);
        const auto mapped = render::nativeLevelForRetailAssetRead(
            asset, sourceSector, sectorCount, destination);
        if (!mapped) return;

        const int next = static_cast<int>(*mapped);
        const int wad2Index = static_cast<int>(asset.index);
        self.requestedRuntimeWad2.store(wad2Index, std::memory_order_release);
        const int previous = self.requestedNativeLevel.exchange(
            next, std::memory_order_release);
        if (previous != next) {
            std::cerr << "[OpenRatchet:render:level-map]"
                      << " asset=" << platform::nativeAssetName(asset)
                      << " source=0x" << std::hex << sourceSector
                      << " sectors=0x" << sectorCount
                      << " destination=0x" << destination << std::dec
                      << " nativeLevel=" << next
                      << " runtimeWad2=" << wad2Index
                      << " mapping=retail-proved"
                      << " status=ok\n";
        }
    }

    void inspectLiveMobyState(PS2Runtime& runtime) {
        // Steps 11.3-11.5 consume live animation, world-transform and camera
        // state on every coherent host/guest handoff. Only diagnostics are
        // throttled; semantic bridge state never inherits the old Step-11.2 cadence.
        const std::uint64_t presentation = liveMobyPresentationCount++;
        const bool diagnosticTick =
            presentation == 0u || (presentation % 60u) == 0u;

        game::Rac1LiveMobyPoolSnapshot snapshot;
        game::Rac1LiveMobyClassRegistrySnapshot classRegistry;
        game::Rac1LiveRatchetAnimationResult animation;
        game::Rac1LiveRatchetTransformResult transform;
        game::Rac1LiveCameraResult camera;
        game::Rac1LiveSkyResult sky;
        render::Rac1RuntimeLiveRatchetFrame ratchetFrame;
        {
            // The fallback game thread mutates RDRAM while it executes. Use the
            // runtime's existing guest-execution handoff so the pool, live
            // object state and independent global camera state come from one
            // coherent read.
            // Logging occurs after this scope so stderr I/O never holds the
            // execution mutex.
            PS2Runtime::GuestExecutionScope guestExecution(&runtime);
            const std::span<const std::uint8_t> guestRdram(
                runtime.memory().getRDRAM(),
                static_cast<std::size_t>(PS2_RAM_SIZE));
            snapshot = game::inspectRac1LiveMobyPool(guestRdram);
            classRegistry = game::inspectRac1LiveMobyClassRegistry(guestRdram, snapshot);
            animation = game::inspectRac1LiveRatchetAnimation(guestRdram, snapshot);
            transform = game::inspectRac1LiveRatchetWorldTransform(snapshot);
            camera = game::inspectRac1LiveCamera(guestRdram);
            sky = game::inspectRac1LiveSky(guestRdram);
            ratchetFrame = nativeRenderer.prepareLiveRatchetFrame(
                guestRdram, animation, transform);
        }

        liveRatchetFrame = std::move(ratchetFrame);
        const bool animationStatusChanged =
            !lastLoggedAnimationStatus ||
            *lastLoggedAnimationStatus != animation.status;
        liveRatchetAnimation = animation;
        const bool transformStatusChanged =
            !lastLoggedTransformStatus ||
            *lastLoggedTransformStatus != transform.status;
        liveRatchetTransform = transform;
        const bool cameraStatusChanged =
            !lastLoggedCameraStatus || *lastLoggedCameraStatus != camera.status;
        liveCamera = camera;
        const bool skyStatusChanged =
            !lastLoggedSkyStatus || *lastLoggedSkyStatus != sky.status;
        liveSky = sky;
        liveMobyRecordCount = snapshot.records.size();
        liveMobyPoolStatus = snapshot.status;
        liveMobyPoolUnaccounted = 0u;
        if (snapshot.status == game::Rac1LiveMobyPoolStatus::Ok) {
            const std::size_t accounted =
                snapshot.traversedMobyCount + snapshot.skippedNegativeStateCount;
            liveMobyPoolUnaccounted =
                snapshot.slotsBeforeTerminator >= accounted
                    ? snapshot.slotsBeforeTerminator - accounted
                    : accounted - snapshot.slotsBeforeTerminator;
        }

        if (diagnosticTick) {
            const LiveMobySnapshotSignature signature = liveMobySignature(snapshot);
            if (!lastLiveMobySignature || *lastLiveMobySignature != signature) {
                lastLiveMobySignature = signature;
                logLiveMobySnapshot(snapshot);
            }
        }

        if (diagnosticTick || animationStatusChanged) {
            lastLoggedAnimationStatus = animation.status;
            logLiveRatchetAnimation(animation);
        }
        if (diagnosticTick || transformStatusChanged) {
            lastLoggedTransformStatus = transform.status;
            logLiveRatchetTransform(transform);
        }
        if (diagnosticTick || cameraStatusChanged) {
            lastLoggedCameraStatus = camera.status;
            logLiveCamera(camera);
        }
        if (diagnosticTick || skyStatusChanged) {
            lastLoggedSkyStatus = sky.status;
            logLiveSky(sky);
        }
        liveMobySnapshot = std::move(snapshot);
        liveMobyClassRegistry = std::move(classRegistry);
    }

    void initializeNativePresentation() {
        std::cerr << "[OpenRatchet:render:ownership]"
                  << " owner=native"
                  << " boundary=ps2runtime-post-gs-pre-enddrawing"
                  << " gsFinalPresentation=0"
                  << " status=ok\n";
    }

    void shutdownNativePresentation() {
        nativeRenderer.unload();
        rendererAttemptedLevel.reset();
        rendererAttemptedRuntimeWad2.reset();
        liveRatchetFrame = {};
        liveMobySnapshot = {};
        liveMobyClassRegistry = {};
        liveSky = {};
        lastLoggedSkyStatus.reset();
        liveMobyRecordCount = 0u;
        liveMobyPoolUnaccounted = 0u;
        liveMobyPoolStatus = game::Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
        liveRatchetApplyStatus =
            render::Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized;
    }

    void ensureMappedLevelLoaded() {
        const int requested = requestedNativeLevel.load(std::memory_order_acquire);
        const int runtimeWad2 = requestedRuntimeWad2.load(std::memory_order_acquire);
        if (requested < 0 || runtimeWad2 < 0 ||
            (rendererAttemptedLevel && *rendererAttemptedLevel == requested &&
             rendererAttemptedRuntimeWad2 && *rendererAttemptedRuntimeWad2 == runtimeWad2)) {
            return;
        }

        nativeRenderer.unload();
        rendererAttemptedLevel = requested;
        rendererAttemptedRuntimeWad2 = runtimeWad2;
        const auto* level = vfs.findLevel(static_cast<std::uint32_t>(requested));
        const auto* runtimeWad = vfs.findAsset(
            platform::NativeAssetKind::Wad2, static_cast<std::uint32_t>(runtimeWad2));
        if (level == nullptr || runtimeWad == nullptr) {
            std::cerr << "[OpenRatchet:render:scene-load]"
                      << " nativeLevel=" << requested
                      << " runtimeWad2=" << runtimeWad2
                      << " mapped=1 materialized=0 rendered=0 deferred=1 unaccounted=0"
                      << " status="
                      << (level == nullptr ? "level-not-indexed" : "runtime-wad2-not-indexed")
                      << '\n';
            return;
        }

        const bool loaded = nativeRenderer.loadLevel(*level, *runtimeWad);
        const auto& summary = nativeRenderer.summary();
        std::cerr << "[OpenRatchet:render:scene-load]"
                  << " nativeLevel=" << requested
                  << " mapped=1"
                  << " materialized=" << (loaded ? 1 : 0)
                  << " terrainBatches=" << summary.terrainBatches
                  << " staticBatches=" << summary.staticBatches
                  << " terrainTriangles=" << summary.terrainTriangles
                  << " tieTriangles=" << summary.tieTriangles
                  << " shrubTriangles=" << summary.shrubTriangles
                  << " skySource=wads2/" << summary.skySourceWad2Index
                  << " skyOffset=0x" << std::hex << summary.skySourceOffset << std::dec
                  << " skyBatches=" << summary.skyBatches
                  << " skyShells=" << summary.skyShells
                  << " skyClusters=" << summary.skyClusters
                  << " skyTriangles=" << summary.skyTriangles
                  << " skyTextures=" << summary.skyTextures
                  << " nativeMobyClasses=" << summary.mobyNativeClasses
                  << " renderableMobyClasses=" << summary.mobyRenderableClasses
                  << " invisibleMobyClasses=" << summary.mobyIntentionallyInvisibleClasses
                  << " classOnlyMobyClasses=" << summary.mobyClassOnlyClasses
                  << " ratchetTopologyBatches=" << summary.ratchetTopologyBatches
                  << " ratchetTopologyTriangles=" << summary.ratchetTopologyTriangles
                  << " ratchetSkinVertices=" << summary.ratchetSkinVertices
                  << " rendered=0"
                  << " deferred=1"
                  << " unaccounted=0"
                  << " sky=runtime-wad-retail-shell-transform-bridged"
                  << " mobys=retail-oclass-catalog-mapped-dynamic-render-deferred"
                  << " status="
                  << render::rac1RuntimeRendererStatusName(nativeRenderer.status())
                  << '\n';
    }

    void drawNativeWorldWithRetailCamera(bool skyMaterialized) {
        if (!nativeRenderer.ready() || !liveCamera.ok()) return;

        // FUN_0022BF94 uses vclipw.xyz against +/-w, matching OpenGL clip
        // semantics exactly. Feed its four proved qword columns directly to
        // rlgl; no Camera3D, FOV, target/up or axis conversion is reconstructed.
        const auto clip = render::rac1RetailClipMatrixColumnMajor(liveCamera.camera);
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        rlMultMatrixf(clip.data());
        rlMatrixMode(RL_MODELVIEW);
        rlPushMatrix();
        rlLoadIdentity();

        if (skyMaterialized) {
            // Retail draws sky shells before ordinary world geometry, each with
            // its own FUN_0022B288 object matrix. Depth stays disabled here so
            // sky geometry does not own the later world depth buffer.
            const std::span<const std::array<float, 16>> skyMatrices(
                liveSky.sky.shellObjectMatrices.data(), liveSky.sky.shellCount);
            nativeRenderer.drawSky(skyMatrices);
        }

        rlEnableDepthTest();
        nativeRenderer.drawStaticWorld();
        nativeRenderer.drawLiveRatchet();
        rlDrawRenderBatchActive();

        rlDisableDepthTest();
        rlPopMatrix();
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();
        rlMatrixMode(RL_MODELVIEW);
    }

    void logNativeRenderAccounting(bool sceneRendered) {
        const int requested = requestedNativeLevel.load(std::memory_order_acquire);
        const bool mapped = requested >= 0;
        const bool materialized = nativeRenderer.ready();
        const std::size_t mappedCount = mapped ? 1u : 0u;
        const std::size_t renderedCount = sceneRendered ? 1u : 0u;
        const bool sceneAccountingOrdered = renderedCount <= mappedCount;
        const std::size_t deferredCount =
            sceneAccountingOrdered ? mappedCount - renderedCount : 0u;
        const std::size_t sceneUnaccounted =
            sceneAccountingOrdered ? 0u : renderedCount - mappedCount;

        const auto skyMap = render::mapLiveSkyRenderState(
            nativeRenderer.nativeSkyShellIdentities(), liveSky);
        const std::size_t skyMapped = skyMap.mapped;
        const std::size_t skyMaterialized = skyMap.materialized;
        const std::size_t skyRendered =
            sceneRendered && skyMaterialized == 1u ? 1u : 0u;
        const bool skyHierarchyOrdered =
            skyMaterialized <= skyMapped && skyRendered <= skyMaterialized;
        const std::size_t skyDeferred =
            skyHierarchyOrdered ? skyMapped - skyRendered : 0u;
        const std::size_t skyUnaccounted =
            (materialized ? skyMap.unaccounted : 0u) +
            (skyHierarchyOrdered ? 0u : 1u);

        const auto liveClassMap = render::mapLiveMobyClassIdentity(
            liveMobySnapshot, liveMobyClassRegistry, nativeRenderer.nativeMobyClasses());
        const bool liveClassMapRequired =
            materialized && liveMobyPoolStatus == game::Rac1LiveMobyPoolStatus::Ok;
        const auto ratchetIdentity = render::identifyLiveRatchetRenderIdentity(
            nativeRenderer.hasRatchetTopology(), liveRatchetAnimation, liveRatchetTransform);
        const std::size_t liveMobyMapped = liveClassMap.mapped;
        const std::size_t liveMobyMaterialized =
            liveRatchetApplyStatus == render::Rac1RuntimeLiveRatchetApplyStatus::Ok &&
                    liveRatchetFrame.ok()
                ? 1u
                : 0u;
        const std::size_t liveMobyRendered =
            sceneRendered && liveMobyMaterialized == 1u &&
                    nativeRenderer.liveRatchetGpuReady()
                ? 1u
                : 0u;
        const bool liveHierarchyOrdered =
            liveMobyMaterialized <= liveMobyMapped &&
            liveMobyRendered <= liveMobyMaterialized;
        const std::size_t liveMobyDeferred =
            liveHierarchyOrdered ? liveMobyMapped - liveMobyRendered : 0u;
        const std::size_t liveClassUnaccounted =
            liveClassMapRequired ? liveClassMap.unaccounted : 0u;
        const std::size_t liveMobyUnaccounted =
            liveClassUnaccounted + (liveHierarchyOrdered ? 0u : 1u);

        const bool liveClassMapHealthy = !liveClassMapRequired || liveClassMap.ok();
        const bool ratchetIdentityHealthy =
            ratchetIdentity.status != render::Rac1LiveRatchetRenderIdentityStatus::MobyAddressMismatch &&
            ratchetIdentity.status != render::Rac1LiveRatchetRenderIdentityStatus::OClassMismatch;
        const bool ratchetFrameHealthy =
            liveRatchetFrame.status == render::Rac1RuntimeLiveRatchetFrameStatus::RendererNotReady ||
            liveRatchetFrame.status == render::Rac1RuntimeLiveRatchetFrameStatus::IdentityNotMapped ||
            liveRatchetFrame.status == render::Rac1RuntimeLiveRatchetFrameStatus::AnimationNotMaterialized ||
            liveRatchetFrame.status == render::Rac1RuntimeLiveRatchetFrameStatus::TransformNotMaterialized ||
            liveRatchetFrame.status == render::Rac1RuntimeLiveRatchetFrameStatus::Ok;
        const bool ratchetApplyHealthy =
            liveRatchetApplyStatus == render::Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized ||
            liveRatchetApplyStatus == render::Rac1RuntimeLiveRatchetApplyStatus::Ok;
        const bool accountingOk = sceneUnaccounted == 0u &&
                                  skyUnaccounted == 0u &&
                                  liveMobyUnaccounted == 0u &&
                                  liveMobyPoolUnaccounted == 0u &&
                                  liveClassMapHealthy &&
                                  ratchetIdentityHealthy &&
                                  ratchetFrameHealthy &&
                                  ratchetApplyHealthy;

        NativeRenderAccountingSignature signature{
            requested,
            materialized,
            sceneRendered,
            liveMobyRecordCount,
            liveClassMap.active,
            liveClassMap.inactive,
            liveMobyMapped,
            liveClassMap.runtimeOnly,
            liveClassMap.registryOClassOutOfRange,
            liveClassMap.registryUnregistered,
            liveClassMap.registryPointerMismatch,
            liveClassMap.hasFirstRegistryIssueOClass,
            liveClassMap.firstRegistryIssueOClass,
            liveClassUnaccounted,
            liveClassMap.status,
            liveMobyPoolUnaccounted,
            liveMobyPoolStatus,
            liveCamera.status,
            liveSky.status,
            skyMap.status,
            skyMapped,
            skyMaterialized,
            skyRendered,
            skyDeferred,
            skyUnaccounted,
            liveRatchetAnimation.status,
            liveRatchetTransform.status,
            nativeRenderer.status(),
            ratchetIdentity.status,
            liveRatchetFrame.status,
            liveRatchetApplyStatus,
        };
        const bool periodic =
            liveMobyPresentationCount <= 1u || (liveMobyPresentationCount % 60u) == 0u;
        if (!periodic && lastRenderAccounting && *lastRenderAccounting == signature) return;
        lastRenderAccounting = signature;

        std::cerr << "[OpenRatchet:render:frame]"
                  << " owner=native"
                  << " nativeLevel=";
        if (mapped) std::cerr << requested;
        else std::cerr << "unmapped";
        std::cerr << " mapped=" << mappedCount
                  << " materialized=" << (materialized ? 1 : 0)
                  << " rendered=" << renderedCount
                  << " deferred=" << deferredCount
                  << " unaccounted=" << sceneUnaccounted
                  << " renderer="
                  << render::rac1RuntimeRendererStatusName(nativeRenderer.status())
                  << " camera=" << game::rac1LiveCameraStatusName(liveCamera.status)
                  << " sky=" << game::rac1LiveSkyStatusName(liveSky.status)
                  << " skyMap=" << render::rac1LiveSkyMapStatusName(skyMap.status)
                  << " skyMapped=" << skyMapped
                  << " skyMaterialized=" << skyMaterialized
                  << " skyRendered=" << skyRendered
                  << " skyDeferred=" << skyDeferred
                  << " skyUnaccounted=" << skyUnaccounted
                  << " mobyPool=" << game::rac1LiveMobyPoolStatusName(liveMobyPoolStatus)
                  << " liveMobyActive=" << liveClassMap.active
                  << " liveMobyInactive=" << liveClassMap.inactive
                  << " liveMobyMapped=" << liveMobyMapped
                  << " liveMobyRenderableTopology=" << liveClassMap.renderableTopology
                  << " liveMobyIntentionallyInvisible=" << liveClassMap.intentionallyInvisible
                  << " liveMobyClassOnly=" << liveClassMap.classOnly
                  << " liveMobyRuntimeOnlyClass=" << liveClassMap.runtimeOnly
                  << " liveMobyRegistryOClassOutOfRange="
                  << liveClassMap.registryOClassOutOfRange
                  << " liveMobyRegistryUnregistered=" << liveClassMap.registryUnregistered
                  << " liveMobyRegistryPointerMismatch="
                  << liveClassMap.registryPointerMismatch
                  << " liveMobyFirstRegistryIssueOClass=";
        if (liveClassMap.hasFirstRegistryIssueOClass) {
            std::cerr << liveClassMap.firstRegistryIssueOClass;
        }
        else std::cerr << "none";
        std::cerr << " liveMobyClassMap="
                  << render::rac1LiveMobyClassMapStatusName(liveClassMap.status)
                  << " liveMobyMaterialized=" << liveMobyMaterialized
                  << " liveMobyRendered=" << liveMobyRendered
                  << " liveMobyDeferred=" << liveMobyDeferred
                  << " liveMobyUnaccounted=" << liveMobyUnaccounted
                  << " poolUnaccounted=" << liveMobyPoolUnaccounted
                  << " ratchetIdentity="
                  << render::rac1LiveRatchetRenderIdentityStatusName(ratchetIdentity.status)
                  << " ratchetFrame="
                  << render::rac1RuntimeLiveRatchetFrameStatusName(liveRatchetFrame.status)
                  << " ratchetGpu="
                  << render::rac1RuntimeLiveRatchetApplyStatusName(liveRatchetApplyStatus)
                  << " animation="
                  << game::rac1LiveRatchetAnimationStatusName(liveRatchetAnimation.status)
                  << " transform="
                  << game::rac1LiveRatchetTransformStatusName(liveRatchetTransform.status)
                  << " status=" << (accountingOk ? "ok" : "accounting-error") << '\n';
    }

    void presentNativeFrame(PS2Runtime& runtime) {
        inspectLiveMobyState(runtime);

        // PS2Runtime queued its compatibility DrawTexturePro before this callback.
        // Flush it first, then clear it away so no delayed GS batch can regain
        // final-frame ownership after OpenRatchet starts drawing.
        rlDrawRenderBatchActive();
        ClearBackground(BLACK);

        ensureMappedLevelLoaded();
        liveRatchetApplyStatus = nativeRenderer.applyLiveRatchetFrame(liveRatchetFrame);
        const auto skyMap = render::mapLiveSkyRenderState(
            nativeRenderer.nativeSkyShellIdentities(), liveSky);
        const bool sceneRendered = nativeRenderer.ready() && liveCamera.ok();
        if (sceneRendered) drawNativeWorldWithRetailCamera(skyMap.materialized == 1u);
        logNativeRenderAccounting(sceneRendered);
    }
};

OpenRatchetRuntime::OpenRatchetRuntime()
    : impl_(std::make_unique<Impl>()) {}

OpenRatchetRuntime::~OpenRatchetRuntime() {
    game::unbindNativeGameServices();
}

bool OpenRatchetRuntime::initialize(const std::filesystem::path& elf) {
    configureHostFloatingPoint();

    if (!std::filesystem::is_regular_file(elf)) {
        std::cerr << "Missing guest ELF: " << elf << "\n"
                  << "Run tools/bootstrap.ps1 -Stage Extract first.\n";
        return false;
    }

    const std::filesystem::path extractedRoot = elf.parent_path();
    const std::filesystem::path tocPath = extractedRoot.parent_path() / "toc.json";
    if (!impl_->vfs.initialize(extractedRoot, tocPath)) {
        std::cerr << "[OpenRatchet:native] native VFS initialization failed\n";
        return false;
    }
    game::bindNativeGameServices({
        &impl_->vfs,
        &Impl::observeIndexedAssetRead,
        impl_.get(),
    });

    game::declareNativeReplacements(impl_->replacements);

    // Preserve the verified legacy ordering exactly: the two bootstrap guest
    // PCs are installed before PS2Runtime initialization, then generated EE
    // code is loaded, then runtime replacements capture their fallbacks.
    installStage(impl_->eeFallback,
                 impl_->replacements,
                 runtime::NativeReplacementStage::Bootstrap);

    // PS2Runtime still owns the fallback EE executor and creates the host
    // window, but its post-GS/pre-EndDrawing callback is now the deliberate
    // final presentation ownership cut. OpenRatchet flushes and supersedes the
    // compatibility framebuffer there, using only proved native scene/live state.
    impl_->eeFallback.setDebugUiCallbacks(
        [](PS2Runtime&, void* userData) {
            static_cast<Impl*>(userData)->initializeNativePresentation();
        },
        [](PS2Runtime& runtime, void* userData) {
            static_cast<Impl*>(userData)->presentNativeFrame(runtime);
        },
        [](PS2Runtime&, void* userData) {
            static_cast<Impl*>(userData)->shutdownNativePresentation();
        },
        impl_.get());

    if (!impl_->eeFallback.initialize("OpenRatchet")) {
        std::cerr << "PS2 fallback runtime initialization failed\n";
        return false;
    }
    if (!impl_->eeFallback.loadELF(elf.string())) {
        std::cerr << "Could not load guest ELF: " << elf << "\n";
        return false;
    }

    installStage(impl_->eeFallback,
                 impl_->replacements,
                 runtime::NativeReplacementStage::Runtime);

    installLegacyGuestDeviceBridges(impl_->eeFallback);
    impl_->initialized = true;

    std::cerr << "[OpenRatchet:native] host owns application runtime; "
                 "PS2Recomp retained as EE fallback backend\n";
    return true;
}

void OpenRatchetRuntime::run() {
    if (!impl_->initialized) {
        std::cerr << "[OpenRatchet:native] run requested before initialization\n";
        return;
    }
    impl_->eeFallback.run();
}

} // namespace ratchet
