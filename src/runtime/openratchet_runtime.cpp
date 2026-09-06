#include "runtime/openratchet_runtime.h"

#include "game/native_replacements.h"
#include "game/native_services.h"
#include "game/rac1_live_state.h"
#include "guest_overrides.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"

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

} // namespace

struct OpenRatchetRuntime::Impl {
    platform::NativeVfs vfs;
    PS2Runtime eeFallback;
    runtime::NativeReplacementRegistry replacements;
    std::optional<LiveMobySnapshotSignature> lastLiveMobySignature;
    std::uint64_t liveMobyPresentationCount = 0u;
    bool initialized = false;

    void inspectLiveMobyState(PS2Runtime& runtime) {
        // Step 11.2 is observation-only. Sample immediately, then once per 60
        // host presentation callbacks to avoid perturbing fallback execution.
        const std::uint64_t presentation = liveMobyPresentationCount++;
        if (presentation != 0u && (presentation % 60u) != 0u) {
            return;
        }

        game::Rac1LiveMobyPoolSnapshot snapshot;
        {
            // The fallback game thread mutates RDRAM while it executes. Use the
            // runtime's existing guest-execution handoff so the snapshot is a
            // coherent read rather than a host/guest data race. Logging occurs
            // after this scope so stderr I/O never holds the execution mutex.
            PS2Runtime::GuestExecutionScope guestExecution(&runtime);
            const std::span<const std::uint8_t> guestRdram(
                runtime.memory().getRDRAM(),
                static_cast<std::size_t>(PS2_RAM_SIZE));
            snapshot = game::inspectRac1LiveMobyPool(guestRdram);
        }

        const LiveMobySnapshotSignature signature = liveMobySignature(snapshot);
        if (lastLiveMobySignature && *lastLiveMobySignature == signature) {
            return;
        }

        lastLiveMobySignature = signature;
        logLiveMobySnapshot(snapshot);
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
