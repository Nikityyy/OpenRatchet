#include "runtime/openratchet_runtime.h"

#include "game/native_replacements.h"
#include "game/native_services.h"
#include "game/rac1_live_animation.h"
#include "game/rac1_live_state.h"
#include "game/rac1_live_transform.h"
#include "guest_overrides.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>

#if defined(_M_X64) || defined(__SSE__)
#include <immintrin.h>
#endif

#include "ps2_runtime.h"

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

} // namespace

struct OpenRatchetRuntime::Impl {
    platform::NativeVfs vfs;
    PS2Runtime eeFallback;
    runtime::NativeReplacementRegistry replacements;
    std::optional<LiveMobySnapshotSignature> lastLiveMobySignature;
    game::Rac1LiveRatchetAnimationResult liveRatchetAnimation;
    std::optional<game::Rac1LiveRatchetAnimationStatus> lastLoggedAnimationStatus;
    game::Rac1LiveRatchetTransformResult liveRatchetTransform;
    std::optional<game::Rac1LiveRatchetTransformStatus> lastLoggedTransformStatus;
    std::uint64_t liveMobyPresentationCount = 0u;
    bool initialized = false;

    void inspectLiveMobyState(PS2Runtime& runtime) {
        // Steps 11.3/11.4 consume live animation and world-transform state on
        // every coherent host/guest handoff. Only diagnostics are throttled;
        // semantic bridge state never inherits the old Step-11.2 cadence.
        const std::uint64_t presentation = liveMobyPresentationCount++;
        const bool diagnosticTick =
            presentation == 0u || (presentation % 60u) == 0u;

        game::Rac1LiveMobyPoolSnapshot snapshot;
        game::Rac1LiveRatchetAnimationResult animation;
        game::Rac1LiveRatchetTransformResult transform;
        {
            // The fallback game thread mutates RDRAM while it executes. Use the
            // runtime's existing guest-execution handoff so both the pool and
            // its live sequence/frame IDs and consumed endpoint packets come
            // from one coherent read.
            // Logging occurs after this scope so stderr I/O never holds the
            // execution mutex.
            PS2Runtime::GuestExecutionScope guestExecution(&runtime);
            const std::span<const std::uint8_t> guestRdram(
                runtime.memory().getRDRAM(),
                static_cast<std::size_t>(PS2_RAM_SIZE));
            snapshot = game::inspectRac1LiveMobyPool(guestRdram);
            animation = game::inspectRac1LiveRatchetAnimation(guestRdram, snapshot);
            transform = game::inspectRac1LiveRatchetWorldTransform(snapshot);
        }

        const bool animationStatusChanged =
            !lastLoggedAnimationStatus ||
            *lastLoggedAnimationStatus != animation.status;
        liveRatchetAnimation = animation;
        const bool transformStatusChanged =
            !lastLoggedTransformStatus ||
            *lastLoggedTransformStatus != transform.status;
        liveRatchetTransform = transform;

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
    game::bindNativeGameServices({&impl_->vfs});

    game::declareNativeReplacements(impl_->replacements);

    // Preserve the verified legacy ordering exactly: the two bootstrap guest
    // PCs are installed before PS2Runtime initialization, then generated EE
    // code is loaded, then runtime replacements capture their fallbacks.
    installStage(impl_->eeFallback,
                 impl_->replacements,
                 runtime::NativeReplacementStage::Bootstrap);

    // PS2Runtime currently exposes its host presentation loop through this
    // callback boundary. OpenRatchet uses it only as a temporary read-only
    // sampling clock while PS2Runtime remains the EE fallback executor. The
    // no-op init/shutdown callbacks are required by PS2Runtime to enable draw
    // callbacks; no debug UI is created here.
    impl_->eeFallback.setDebugUiCallbacks(
        [](PS2Runtime&, void*) {},
        [](PS2Runtime& runtime, void* userData) {
            static_cast<Impl*>(userData)->inspectLiveMobyState(runtime);
        },
        [](PS2Runtime&, void*) {},
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
