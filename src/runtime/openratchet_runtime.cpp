#include "runtime/openratchet_runtime.h"

#include "game/native_replacements.h"
#include "game/native_services.h"
#include "game/rac1_live_animation.h"
#include "game/rac1_live_camera.h"
#include "game/rac1_live_state.h"
#include "game/rac1_live_sky.h"
#include "game/rac1_live_transform.h"
#include "game/rac1_native_input.h"
#include "game/rac1_frontend_autopilot.h"
#include "game/rac1_overlay_coherence.h"
#include "game/rac1_overlay_aot_dispatch.h"
#include "guest_overrides.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"
#include "render/rac1_render_bridge.h"
#include "render/rac1_runtime_renderer.h"
#include "render/rac1_presentation_ownership.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(__SSE__)
#include <immintrin.h>
#endif

#include "ps2_runtime.h"

#include <raylib.h>
#include <rlgl.h>

namespace ratchet {
namespace {

constexpr std::uint32_t kLevel0GenerationEntry = 0x00245C28u;

void configureHostFloatingPoint() {
#if defined(_MSC_VER) && defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8000u | 0x0040u);
#elif defined(__SSE__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}


float nativeInputAxisWithDeadzone(float value) {
    constexpr float kDeadzone = 0.15f;
    if (value > -kDeadzone && value < kDeadzone) return 0.0f;
    return value;
}

float nativeKeyboardAxis(bool negative, bool positive) {
    if (negative == positive) return 0.0f;
    return negative ? -1.0f : 1.0f;
}

game::Rac1NativeInputSample sampleNativeHostInput() {
    game::Rac1NativeInputSample sample;
    if (!IsWindowFocused()) return sample;

    auto press = [&sample](game::Rac1PadButton button, bool down) {
        if (down) sample.pressedButtons |= game::rac1PadButtonMask(button);
    };

    sample.leftX = nativeKeyboardAxis(IsKeyDown(KEY_A), IsKeyDown(KEY_D));
    sample.leftY = nativeKeyboardAxis(IsKeyDown(KEY_W), IsKeyDown(KEY_S));
    sample.rightX = nativeKeyboardAxis(IsKeyDown(KEY_LEFT), IsKeyDown(KEY_RIGHT));
    sample.rightY = nativeKeyboardAxis(IsKeyDown(KEY_UP), IsKeyDown(KEY_DOWN));

    press(game::Rac1PadButton::Cross, IsKeyDown(KEY_SPACE));
    press(game::Rac1PadButton::Circle, IsKeyDown(KEY_E));
    press(game::Rac1PadButton::Square, IsKeyDown(KEY_F));
    press(game::Rac1PadButton::Triangle, IsKeyDown(KEY_R));
    press(game::Rac1PadButton::L1, IsKeyDown(KEY_LEFT_SHIFT));
    press(game::Rac1PadButton::R1, IsKeyDown(KEY_RIGHT_SHIFT));
    press(game::Rac1PadButton::L2, IsKeyDown(KEY_Z));
    press(game::Rac1PadButton::R2, IsKeyDown(KEY_C));
    press(game::Rac1PadButton::Start, IsKeyDown(KEY_ENTER));
    press(game::Rac1PadButton::Select, IsKeyDown(KEY_BACKSPACE));

    if (!IsGamepadAvailable(0)) return sample;

    const float gamepadLeftX = nativeInputAxisWithDeadzone(
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X));
    const float gamepadLeftY = nativeInputAxisWithDeadzone(
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y));
    const float gamepadRightX = nativeInputAxisWithDeadzone(
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X));
    const float gamepadRightY = nativeInputAxisWithDeadzone(
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y));
    if (sample.leftX == 0.0f) sample.leftX = gamepadLeftX;
    if (sample.leftY == 0.0f) sample.leftY = gamepadLeftY;
    if (sample.rightX == 0.0f) sample.rightX = gamepadRightX;
    if (sample.rightY == 0.0f) sample.rightY = gamepadRightY;

    press(game::Rac1PadButton::Up,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
    press(game::Rac1PadButton::Right,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));
    press(game::Rac1PadButton::Down,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
    press(game::Rac1PadButton::Left,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
    press(game::Rac1PadButton::Cross,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    press(game::Rac1PadButton::Circle,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));
    press(game::Rac1PadButton::Square,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
    press(game::Rac1PadButton::Triangle,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP));
    press(game::Rac1PadButton::L1,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1));
    press(game::Rac1PadButton::R1,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1));
    press(game::Rac1PadButton::L2,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_2));
    press(game::Rac1PadButton::R2,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2));
    press(game::Rac1PadButton::Select,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT));
    press(game::Rac1PadButton::Start,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT));
    press(game::Rac1PadButton::L3,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_THUMB));
    press(game::Rac1PadButton::R3,
          IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_THUMB));
    return sample;
}

bool sameParsedInputSnapshot(const game::Rac1RetailParsedInputSnapshot& lhs,
                             const game::Rac1RetailParsedInputSnapshot& rhs) noexcept {
    const auto sameFloatBits = [](float a, float b) noexcept {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    };
    return lhs.status == rhs.status &&
           lhs.currentButtons == rhs.currentButtons &&
           lhs.pressedEdges == rhs.pressedEdges &&
           lhs.releasedEdges == rhs.releasedEdges &&
           lhs.processedPressedEdges == rhs.processedPressedEdges &&
           sameFloatBits(lhs.rightX, rhs.rightX) &&
           sameFloatBits(lhs.rightY, rhs.rightY) &&
           sameFloatBits(lhs.leftX, rhs.leftX) &&
           sameFloatBits(lhs.leftY, rhs.leftY);
}

bool sameBootOverlayLifecycle(const game::Rac1BootOverlayProgress& lhs,
                              const game::Rac1BootOverlayProgress& rhs) noexcept {
    // Keep this comparison independent of generated Retail source timestamps;
    // only observed lifecycle values may trigger an immediate diagnostic record.
    // Raw loader words are still printed on the periodic diagnostic sample, but
    // they do not independently defeat throttling. Immediate records are reserved
    // for lifecycle/branch changes that can alter the Boot -> first-generation path.
    return lhs.status == rhs.status &&
           lhs.processedPressedEdges == rhs.processedPressedEdges &&
           lhs.bootExitFlag == rhs.bootExitFlag &&
           lhs.state13CAE4 == rhs.state13CAE4 &&
           lhs.branch2321A4Bypass == rhs.branch2321A4Bypass &&
           lhs.branch2321B8Wait == rhs.branch2321B8Wait &&
           lhs.branch2321C4EnterSetup == rhs.branch2321C4EnterSetup &&
           lhs.state15F604 == rhs.state15F604 &&
           lhs.callback2195OwnerPointer == rhs.callback2195OwnerPointer &&
           lhs.callback2195TablePointer == rhs.callback2195TablePointer &&
           lhs.callback2195TableReadable == rhs.callback2195TableReadable &&
           lhs.callback2195NonNullObjects == rhs.callback2195NonNullObjects &&
           lhs.callback2195ReadableObjects == rhs.callback2195ReadableObjects &&
           lhs.callback2195NonNullTargets == rhs.callback2195NonNullTargets &&
           lhs.callback2195Targets == rhs.callback2195Targets &&
           lhs.callback21E7C8Present == rhs.callback21E7C8Present &&
           lhs.callback21E7C8Slot == rhs.callback21E7C8Slot &&
           lhs.sharedLoaderPointer == rhs.sharedLoaderPointer &&
           lhs.sharedLoaderPrefixBytes == rhs.sharedLoaderPrefixBytes &&
           lhs.firstGenerationStreamSignatureObserved == rhs.firstGenerationStreamSignatureObserved &&
           lhs.wad158FirstRecordHeaderMatched == rhs.wad158FirstRecordHeaderMatched &&
           lhs.firstRecordAddress == rhs.firstRecordAddress &&
           lhs.firstGenerationEntry == rhs.firstGenerationEntry;
}

void logLiveParsedInput(const game::Rac1RetailParsedInputSnapshot& parsed) {
    using Layout = game::Rac1RetailParsedInputLayout;

    std::cerr << "[OpenRatchet:input] component=parsed-live"
              << " source=guest-rdram"
              << " parser=retail-FUN_00217328"
              << " state=0x" << std::hex << Layout::kControllerStateAddress << std::dec;
    if (parsed.ok()) {
        std::cerr << " buttons=0x" << std::hex << parsed.currentButtons
                  << " pressedEdges=0x" << parsed.pressedEdges
                  << " releasedEdges=0x" << parsed.releasedEdges
                  << " processedPressedEdges=0x" << parsed.processedPressedEdges << std::dec
                  << " rx=" << parsed.rightX
                  << " ry=" << parsed.rightY
                  << " lx=" << parsed.leftX
                  << " ly=" << parsed.leftY
                  << " ownership=retail-read-only";
    }
    std::cerr << " status=" << game::rac1RetailParsedInputStatusName(parsed.status)
              << '\n';
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

void logLiveRatchetControlState(
    const game::Rac1LiveRatchetControlStateSnapshot& snapshot) {
    // Build the gate evidence off-stream and publish it with one write. Runtime
    // GS/debug output can originate on another thread; composing this line via
    // many individual operator<< calls previously allowed evidence to be split.
    std::ostringstream line;
    line << "[OpenRatchet:live:ratchet-control-state]"
         << " source=guest-rdram"
         << " ratchetCandidates=" << snapshot.ratchetCandidates
         << " ratchetMoby=0x" << std::hex << snapshot.ratchetMoby
         << " ratchetPVar=0x" << snapshot.ratchetPVar
         << " state=0x" << snapshot.state
         << " stateRatchetMoby=0x" << snapshot.stateRatchetMoby
         << " companionMoby=0x" << snapshot.companionMoby
         << " companionOClass=0x"
         << static_cast<std::uint16_t>(snapshot.companionOClass)
         << std::dec
         << " companionLive=" << (snapshot.companionLive ? 1 : 0)
         << " updateCallback=0x" << std::hex << snapshot.ratchetUpdateCallback
         << std::dec
         << " status="
         << game::rac1LiveRatchetControlStateStatusName(snapshot.status)
         << '\n';
    const std::string text = line.str();
    std::cerr.write(text.data(), static_cast<std::streamsize>(text.size()));
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
    render::Rac1PresentationOwnership presentationOwnership;
    game::Rac1FrontendAutopilot frontendAutopilot;
    std::optional<game::Rac1FrontendAutopilotPulse> lastFrontendAutopilotPulse;
    bool frontendAutopilotStopLogged = false;
    std::optional<render::Rac1PresentationReadiness> lastPresentationReadiness;
    std::atomic<int> requestedNativeLevel{-1};
    std::atomic<int> requestedRuntimeWad2{-1};
    std::optional<int> rendererAttemptedLevel;
    std::optional<int> rendererAttemptedRuntimeWad2;
    std::optional<NativeRenderAccountingSignature> lastRenderAccounting;
    std::optional<LiveMobySnapshotSignature> lastLiveMobySignature;
    std::optional<game::Rac1RetailParsedInputSnapshot> lastLoggedParsedInput;
    game::Rac1StaticElfImage staticElfImage;
    std::vector<std::uint32_t> staticFallbackAddresses;
    std::vector<std::uint32_t> overlayFallbackAddresses;
    std::optional<game::Rac1OverlayCoherenceResult> overlayCoherence;
    std::optional<game::Rac1OverlayAotRuntimeState> lastOverlayAotState;
    std::optional<game::Rac1BootOverlayProgress> lastBootOverlayProgress;
    std::uint64_t overlayProgressPresentationCount = 0u;
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
    game::Rac1LiveRatchetControlStateSnapshot liveRatchetControlState;
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
        std::optional<game::Rac1LiveRatchetControlStateSnapshot> controlState;
        game::Rac1LiveRatchetAnimationResult animation;
        game::Rac1LiveRatchetTransformResult transform;
        game::Rac1LiveCameraResult camera;
        game::Rac1LiveSkyResult sky;
        game::Rac1RetailParsedInputSnapshot parsedInput;
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
            if (diagnosticTick) {
                controlState = game::inspectRac1LiveRatchetControlState(
                    guestRdram, snapshot);
            }
            animation = game::inspectRac1LiveRatchetAnimation(guestRdram, snapshot);
            transform = game::inspectRac1LiveRatchetWorldTransform(snapshot);
            camera = game::inspectRac1LiveCamera(guestRdram);
            sky = game::inspectRac1LiveSky(guestRdram);
            parsedInput = game::inspectRac1RetailParsedInput(guestRdram);
            ratchetFrame = nativeRenderer.prepareLiveRatchetFrame(
                guestRdram, animation, transform);
        }

        liveRatchetFrame = std::move(ratchetFrame);
        const bool parsedInputChanged =
            !lastLoggedParsedInput ||
            !sameParsedInputSnapshot(*lastLoggedParsedInput, parsedInput);
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

        if (diagnosticTick || parsedInputChanged) {
            lastLoggedParsedInput = parsedInput;
            logLiveParsedInput(parsedInput);
        }

        if (diagnosticTick) {
            const LiveMobySnapshotSignature signature = liveMobySignature(snapshot);
            if (!lastLiveMobySignature || *lastLiveMobySignature != signature) {
                lastLiveMobySignature = signature;
                logLiveMobySnapshot(snapshot);
            }
        }

        if (controlState) {
            liveRatchetControlState = *controlState;
            logLiveRatchetControlState(*controlState);
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
        presentationOwnership.reset();
        frontendAutopilot.reset();
        lastFrontendAutopilotPulse.reset();
        frontendAutopilotStopLogged = false;
        lastPresentationReadiness.reset();
        std::cerr << "[OpenRatchet:render:ownership]"
                  << " owner=frontend-dev"
                  << " bridge=phase12-temporary"
                  << " gsPresentation=suppressed-known-broken"
                  << " navigation=retail-parser-start-cross-autopilot"
                  << " nativeTakeover=level0-aot+level-map+renderer+camera"
                  << " status=ok\n";
    }

    void shutdownNativePresentation() {
        presentationOwnership.reset();
        frontendAutopilot.reset();
        lastFrontendAutopilotPulse.reset();
        frontendAutopilotStopLogged = false;
        lastPresentationReadiness.reset();
        nativeRenderer.unload();
        rendererAttemptedLevel.reset();
        rendererAttemptedRuntimeWad2.reset();
        liveRatchetFrame = {};
        liveMobySnapshot = {};
        liveMobyClassRegistry = {};
        liveRatchetControlState = {};
        liveSky = {};
        lastLoggedSkyStatus.reset();
        lastLoggedParsedInput.reset();
        overlayFallbackAddresses.clear();
        overlayCoherence.reset();
        lastOverlayAotState.reset();
        lastBootOverlayProgress.reset();
        overlayProgressPresentationCount = 0u;
        liveMobyRecordCount = 0u;
        liveMobyPoolUnaccounted = 0u;
        liveMobyPoolStatus = game::Rac1LiveMobyPoolStatus::GuestMemoryTooSmall;
        liveRatchetApplyStatus =
            render::Rac1RuntimeLiveRatchetApplyStatus::FrameNotMaterialized;
    }

    void snapshotStaticFallbackAddressSet() {
        staticFallbackAddresses.clear();
        staticFallbackAddresses.reserve(g_ps2RecompiledFunctionTableSlotCount);
        for (std::uint32_t slot = 0u; slot < g_ps2RecompiledFunctionTableSlotCount; ++slot) {
            if (g_ps2RecompiledFunctionTable[slot] == nullptr) continue;
            const std::uint64_t pc64 =
                static_cast<std::uint64_t>(g_ps2RecompiledFunctionTableBase) +
                static_cast<std::uint64_t>(slot) * 4u;
            if (pc64 >= g_ps2RecompiledFunctionTableEnd ||
                pc64 > std::numeric_limits<std::uint32_t>::max()) {
                continue;
            }
            staticFallbackAddresses.push_back(static_cast<std::uint32_t>(pc64));
        }
    }

    void rebuildOverlayFallbackAddressSet() {
        overlayFallbackAddresses.clear();
        const auto segments = nativeRenderer.levelOverlaySegments();
        for (const std::uint32_t pc : staticFallbackAddresses) {
            for (const auto& segment : segments) {
                const std::uint64_t begin = segment.destination;
                const std::uint64_t end = begin + segment.payloadSize;
                if (static_cast<std::uint64_t>(pc) >= begin &&
                    static_cast<std::uint64_t>(pc) < end) {
                    overlayFallbackAddresses.push_back(pc);
                    break;
                }
            }
        }
    }

    void inspectLevelOverlayCoherence(PS2Runtime& runtime) {
        const auto overlay = nativeRenderer.levelOverlayBytes();
        const auto segments = nativeRenderer.levelOverlaySegments();
        if (overlay.empty() || segments.empty()) return;

        const std::uint64_t presentation = overlayProgressPresentationCount++;
        const bool diagnosticTick = presentation == 0u || (presentation % 60u) == 0u;
        game::Rac1OverlayCoherenceResult coherence;
        game::Rac1BootOverlayProgress bootProgress;
        std::uint32_t guestPc = 0u;
        std::uint32_t guestRa = 0u;
        std::array<bool, game::Rac1BootOverlayProgress::kCallback2195SlotCount>
            callback2195Dispatchable{};
        std::size_t callback2195MissingSlots = 0u;
        std::uint32_t callback2195FirstMissingTarget = 0u;
        bool callback21E7C8Dispatchable = false;
        {
            PS2Runtime::GuestExecutionScope guestExecution(&runtime);
            const std::span<const std::uint8_t> guestRdram(
                runtime.memory().getRDRAM(),
                static_cast<std::size_t>(PS2_RAM_SIZE));
            coherence = game::inspectRac1OverlayCoherence(
                guestRdram, overlay, segments, staticElfImage, overlayFallbackAddresses);
            bootProgress = game::inspectRac1BootOverlayProgress(guestRdram);
            guestPc = runtime.cpu().pc;
            guestRa = getRegU32(&runtime.cpu(), 31);
            for (std::size_t slot = 0u;
                 slot < bootProgress.callback2195Targets.size(); ++slot) {
                const std::uint32_t target = bootProgress.callback2195Targets[slot];
                if (target == 0u) continue;
                callback2195Dispatchable[slot] = runtime.hasFunction(target);
                if (callback2195Dispatchable[slot]) continue;
                if (callback2195MissingSlots == 0u) {
                    callback2195FirstMissingTarget = target;
                }
                ++callback2195MissingSlots;
            }
            callback21E7C8Dispatchable = runtime.hasFunction(0x0021E7C8u);
        }
        const game::Rac1OverlayAotRuntimeState aotState =
            game::inspectRac1OverlayAotRuntimeState();

        const bool changed =
            !overlayCoherence || overlayCoherence->status != coherence.status ||
            overlayCoherence->materializedSegments != coherence.materializedSegments ||
            overlayCoherence->conflictingFallbackEntries != coherence.conflictingFallbackEntries ||
            !lastOverlayAotState || *lastOverlayAotState != aotState ||
            !lastBootOverlayProgress ||
            !sameBootOverlayLifecycle(*lastBootOverlayProgress, bootProgress);
        overlayCoherence = coherence;
        lastOverlayAotState = aotState;
        lastBootOverlayProgress = bootProgress;
        if (!changed && !diagnosticTick) return;

        // This is a verification gate. Compose the complete record before the
        // single stderr write so concurrent GS diagnostics cannot split fields.
        std::ostringstream line;
        line << "[OpenRatchet:gameplay-overlay]"
             << " source=retail-level-data"
             << " segments=" << coherence.segmentCount
             << " payloadBytes=" << coherence.payloadBytes
             << " materializedSegments=" << coherence.materializedSegments
             << " materializedBytes=" << coherence.materializedBytes
             << " fallbackEntries=" << coherence.registeredFallbackEntries
             << " comparableEntries=" << coherence.comparableFallbackEntries
             << " conflictingEntries=" << coherence.conflictingFallbackEntries
             << " firstConflictPc=";
        if (coherence.hasFirstConflictPc) {
            line << "0x" << std::hex << coherence.firstConflictPc << std::dec;
        } else {
            line << "none";
        }
        line << " guestPc=0x" << std::hex << guestPc
             << " guestRa=0x" << guestRa
             << " lastBoundaryGeneration=0x" << aotState.lastMaterializerGenerationEntry
             << " activeGeneration=0x" << aotState.activeGenerationEntry
             << std::dec
             << " materializerCalls=" << aotState.materializerCalls
             << " successfulActivations=" << aotState.successfulActivations
             << " activeFunctions=" << aotState.activeFunctionCount
             << " touchedSlots=" << aotState.touchedSlotCount
             << " processedPressedEdges=0x" << std::hex
             << bootProgress.processedPressedEdges << std::dec
             << " processedRightEdge=" << (bootProgress.processedRightEdge ? 1 : 0)
             << " bootExitFlag=" << bootProgress.bootExitFlag
             << " state13CAE4=0x" << std::hex << bootProgress.state13CAE4
             << " state15ED84=0x" << std::hex << bootProgress.state15ED84
             << " state15ED88=0x" << bootProgress.state15ED88
             << " state15F600=0x" << bootProgress.state15F600
             << " state15F604=0x" << bootProgress.state15F604
             << " state15F618=0x" << bootProgress.state15F618
             << " state13D364=0x" << bootProgress.state13D364
             << " state13D36C=0x" << bootProgress.state13D36C << std::dec
             << " branch2321A4Bypass=" << (bootProgress.branch2321A4Bypass ? 1 : 0)
             << " branch2321B8Wait=" << (bootProgress.branch2321B8Wait ? 1 : 0)
             << " branch2321C4EnterSetup=" << (bootProgress.branch2321C4EnterSetup ? 1 : 0)
             << " callback2195Owner=0x" << std::hex << bootProgress.callback2195OwnerPointer
             << " callback2195Table=0x" << bootProgress.callback2195TablePointer << std::dec
             << " callback2195TableReadable=" << (bootProgress.callback2195TableReadable ? 1 : 0)
             << " callback2195Objects=" << bootProgress.callback2195NonNullObjects
             << " callback2195ReadableObjects=" << bootProgress.callback2195ReadableObjects
             << " callback2195NonNullTargets=" << bootProgress.callback2195NonNullTargets
             << " callback2195MissingSlots=" << callback2195MissingSlots
             << " callback2195FirstMissing=";
        if (callback2195FirstMissingTarget != 0u) {
            line << "0x" << std::hex << callback2195FirstMissingTarget << std::dec;
        } else {
            line << "none";
        }
        line << " callback2195TargetList=[";
        bool firstCallback2195Target = true;
        for (std::size_t slot = 0u; slot < bootProgress.callback2195Targets.size(); ++slot) {
            const std::uint32_t target = bootProgress.callback2195Targets[slot];
            if (target == 0u) continue;
            if (!firstCallback2195Target) line << ',';
            firstCallback2195Target = false;
            line << slot << ":0x" << std::hex << target << std::dec
                 << ':' << (callback2195Dispatchable[slot] ? "dispatchable" : "missing");
        }
        line << "]"
             << " callback21E7C8Present=" << (bootProgress.callback21E7C8Present ? 1 : 0)
             << " callback21E7C8Slot=";
        if (bootProgress.callback21E7C8Present) {
            line << bootProgress.callback21E7C8Slot;
        } else {
            line << "none";
        }
        line << " callback21E7C8Dispatchable=" << (callback21E7C8Dispatchable ? 1 : 0)
             << " sharedLoader=0x" << std::hex << bootProgress.sharedLoaderPointer
             << " sharedPrefix=0x" << bootProgress.sharedLoaderPrefixBytes << std::dec
             << " firstGenerationStreamSignature="
             << (bootProgress.firstGenerationStreamSignatureObserved ? 1 : 0)
             << " wad158FirstRecordHeaderMatched=" << (bootProgress.wad158FirstRecordHeaderMatched ? 1 : 0)
             << " nextRecord=0x" << std::hex << bootProgress.firstRecordAddress
             << " nextDestination=0x" << bootProgress.firstDestination
             << " nextPayloadBytes=0x" << bootProgress.firstPayloadBytes
             << " nextField8=0x" << bootProgress.firstField8
             << " nextGeneration=0x" << bootProgress.firstGenerationEntry
             << std::dec
             << " bootProgress="
             << game::rac1BootOverlayProgressStatusName(bootProgress.status)
             << " ownership=retail-read-only"
             << " conflictReference=boot-elf-aot"
             << " status=" << game::rac1OverlayCoherenceStatusName(coherence.status)
             << '\n';
        const std::string text = line.str();
        std::cerr.write(text.data(), static_cast<std::streamsize>(text.size()));
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
        overlayCoherence.reset();
        lastOverlayAotState.reset();
        lastBootOverlayProgress.reset();
        overlayProgressPresentationCount = 0u;
        rebuildOverlayFallbackAddressSet();
        const auto& summary = nativeRenderer.summary();
        std::cerr << "[OpenRatchet:render:scene-load]"
                  << " nativeLevel=" << requested
                  << " mapped=1"
                  << " materialized=" << (loaded ? 1 : 0)
                  << " overlaySegments=" << nativeRenderer.levelOverlaySegments().size()
                  << " overlayBytes=" << nativeRenderer.levelOverlayBytes().size()
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

        const auto& parity = summary.staticWorldParity;
        std::cerr << "[OpenRatchet:render:parity] frontend=runtime scope=level-static"
                  << " batches=" << parity.batchCount
                  << " vertices=" << parity.vertexCount
                  << " indices=" << parity.indexCount
                  << " triangles=" << parity.triangleCount
                  << " vertexHash=0x" << std::hex << parity.vertexHash
                  << " indexHash=0x" << parity.indexHash
                  << " materialHash=0x" << parity.materialHash
                  << " textureHash=0x" << parity.textureHash
                  << " transformHash=0x" << parity.transformHash
                  << " topologyHash=0x" << parity.topologyHash
                  << " renderStateHash=0x" << parity.renderStateHash
                  << " drawOrderHash=0x" << parity.drawOrderHash
                  << " combinedHash=0x" << parity.combinedHash << std::dec
                  << " topology=triangle-list-unindexed"
                  << " transform=retail-world-identity"
                  << " status=" << render::rac1RenderParityStatusName(parity.status)
                  << '\n';
    }

    void drawNativeWorldWithRetailCamera(bool skyMaterialized) {
        if (!nativeRenderer.ready() || !liveCamera.ok()) return;

        // FUN_001F2260 materializes the Retail clip columns without camera-world
        // translation, while FUN_001F7D30 proves the corresponding world-space
        // convention is point.xyz -= state+0x140 before camera multiplication.
        // Native terrain/static/Ratchet vertices are absolute Retail world
        // coordinates, so compose exactly that T(-cameraPosition). FUN_0022BF94
        // then proves Retail's post-divide path maps NDC Y into the GS screen
        // coordinate system (Y down), while rlgl/OpenGL window Y is up. The
        // bridge therefore performs the single proved clip-Y convention change;
        // no Camera3D, FOV, target/up or guessed camera axis is reconstructed.
        const auto clip =
            render::rac1RetailWorldToOpenGlClipMatrixColumnMajor(liveCamera.camera);
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlLoadIdentity();
        rlMultMatrixf(clip.data());
        rlMatrixMode(RL_MODELVIEW);
        rlPushMatrix();
        rlLoadIdentity();

        // The frontend may inherit arbitrary compatibility renderer state.
        // Establish the exact same native raster contract as the standalone
        // viewer before each equivalent draw pass; do not rely on inherited
        // depth-write, cull or blend state from PS2Runtime.
        render::applyNativeRenderPassState(render::NativeRenderPass::Sky, false);
        if (skyMaterialized) {
            const std::span<const std::array<float, 16>> skyMatrices(
                liveSky.sky.shellObjectMatrices.data(), liveSky.sky.shellCount);
            nativeRenderer.drawSky(skyMatrices);
        }

        render::applyNativeRenderPassState(render::NativeRenderPass::World, false);
        nativeRenderer.drawStaticWorld();
        nativeRenderer.drawLiveRatchet();
        rlDrawRenderBatchActive();

        // Preserve the host's expected post-3D state for any later debug/UI work.
        rlDisableDepthTest();
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
        rlEnableColorBlend();
        rlSetBlendMode(RL_BLEND_ALPHA);
        rlDisableWireMode();
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

    void drawPhase12FrontendDevelopmentScreen(
        const game::Rac1OverlayAotRuntimeState& aotState,
        const render::Rac1PresentationReadiness& readiness,
        const game::Rac1FrontendAutopilotFrame& autopilotFrame) {
        rlDrawRenderBatchActive();
        ClearBackground(BLACK);

        constexpr int kLeft = 28;
        constexpr int kTop = 28;
        constexpr int kLine = 28;
        DrawText("OpenRatchet - Phase 12", kLeft, kTop, 26, RAYWHITE);
        DrawText("Retail frontend is running, but its PS2 GS fallback is intentionally hidden.",
                 kLeft, kTop + kLine * 2, 18, LIGHTGRAY);
        DrawText("Development bridge: navigating the unchanged Retail frontend input path.",
                 kLeft, kTop + kLine * 3, 18, LIGHTGRAY);
        DrawText("Your real controller remains merged and becomes fully manual on Veldin.",
                 kLeft, kTop + kLine * 4, 18, LIGHTGRAY);

        char generation[96]{};
        std::snprintf(generation,
                      sizeof(generation),
                      "Retail generation: 0x%08X   materializer calls: %llu",
                      static_cast<unsigned>(aotState.activeGenerationEntry),
                      static_cast<unsigned long long>(aotState.materializerCalls));
        DrawText(generation, kLeft, kTop + kLine * 6, 18, RAYWHITE);

        char readinessLine[160]{};
        std::snprintf(readinessLine,
                      sizeof(readinessLine),
                      "Level0 AOT:%d  map:%d  renderer:%d  camera:%d",
                      readiness.level0GenerationActive ? 1 : 0,
                      readiness.level0Mapped ? 1 : 0,
                      readiness.nativeRendererReady ? 1 : 0,
                      readiness.retailCameraReady ? 1 : 0);
        DrawText(readinessLine, kLeft, kTop + kLine * 7, 18, RAYWHITE);

        char inputLine[128]{};
        std::snprintf(inputLine,
                      sizeof(inputLine),
                      "Frontend navigation: %s%s%s",
                      autopilotFrame.active ? "automatic" : "complete",
                      autopilotFrame.pulse == game::Rac1FrontendAutopilotPulse::None
                          ? ""
                          : " / pulse=",
                      autopilotFrame.pulse == game::Rac1FrontendAutopilotPulse::None
                          ? ""
                          : game::rac1FrontendAutopilotPulseName(autopilotFrame.pulse));
        DrawText(inputLine, kLeft, kTop + kLine * 9, 18, LIGHTGRAY);
        DrawText("Waiting for authentic Level-0 activation (0x00245C28)...",
                 kLeft, kTop + kLine * 11, 18, GRAY);
    }

    void presentNativeFrame(PS2Runtime& runtime) {
        // Phase 12 must not resurrect the known-broken PS2Runtime GS fallback
        // merely to expose an unfinished frontend. Instead, contribute ordinary
        // Start/Cross host-pad pulses to the already proved native input path.
        // The unchanged Retail controller parser remains the sole game-state
        // authority; no guest memory, generation or menu state is injected.
        const auto aotStateBeforeInput = game::inspectRac1OverlayAotRuntimeState();
        const bool level0GenerationAlreadyActive =
            aotStateBeforeInput.activeGenerationEntry == kLevel0GenerationEntry;
        const auto autopilotFrame = frontendAutopilot.step(
            level0GenerationAlreadyActive, sampleNativeHostInput());
        game::publishRac1NativeInputSample(autopilotFrame.input);

        if (autopilotFrame.active &&
            autopilotFrame.pulse != game::Rac1FrontendAutopilotPulse::None &&
            (!lastFrontendAutopilotPulse ||
             *lastFrontendAutopilotPulse != autopilotFrame.pulse)) {
            std::cerr << "[OpenRatchet:frontend-dev]"
                      << " input=autopilot"
                      << " pulse="
                      << game::rac1FrontendAutopilotPulseName(autopilotFrame.pulse)
                      << " frame=" << autopilotFrame.frame
                      << " parser=retail-FUN_00217328"
                      << " guestStateMutation=0"
                      << " status=ok\n";
        }
        if (!autopilotFrame.active && !frontendAutopilotStopLogged) {
            frontendAutopilotStopLogged = true;
            std::cerr << "[OpenRatchet:frontend-dev]"
                      << " input=autopilot active=0"
                      << " reason=level0-generation-active"
                      << " generationEntry=0x" << std::hex
                      << aotStateBeforeInput.activeGenerationEntry << std::dec
                      << " liveController=retained"
                      << " status=ok\n";
        }
        lastFrontendAutopilotPulse = autopilotFrame.pulse;

        inspectLiveMobyState(runtime);
        ensureMappedLevelLoaded();
        inspectLevelOverlayCoherence(runtime);
        liveRatchetApplyStatus = nativeRenderer.applyLiveRatchetFrame(liveRatchetFrame);

        const auto aotState = game::inspectRac1OverlayAotRuntimeState();
        const int requested = requestedNativeLevel.load(std::memory_order_acquire);
        const render::Rac1PresentationReadiness readiness{
            aotState.activeGenerationEntry == kLevel0GenerationEntry,
            requested == 0,
            nativeRenderer.ready(),
            liveCamera.ok(),
        };
        const auto transition = presentationOwnership.observe(readiness);
        const bool readinessChanged =
            !lastPresentationReadiness || *lastPresentationReadiness != readiness;
        if (readinessChanged ||
            transition == render::Rac1PresentationTransition::AcquiredNativeGameplay) {
            lastPresentationReadiness = readiness;
            std::cerr << "[OpenRatchet:render:ownership]"
                      << " owner="
                      << render::rac1PresentationOwnerName(presentationOwnership.owner())
                      << " bridge=phase12-temporary"
                      << " generationEntry=0x" << std::hex << aotState.activeGenerationEntry
                      << std::dec
                      << " level0Mapped=" << (readiness.level0Mapped ? 1 : 0)
                      << " rendererReady=" << (readiness.nativeRendererReady ? 1 : 0)
                      << " cameraReady=" << (readiness.retailCameraReady ? 1 : 0);
            if (transition == render::Rac1PresentationTransition::AcquiredNativeGameplay) {
                std::cerr << " transition=frontend-dev->native-level0";
            }
            std::cerr << " status=ok\n";
        }

        if (presentationOwnership.developmentFrontendVisible()) {
            // PS2Runtime has already queued its compatibility framebuffer draw.
            // Flush it, then erase it: Step 11.6 proved this legacy image is the
            // fragmented/horizontal-line path that native presentation replaced.
            // A host-only development screen makes progress observable without
            // pretending to implement Phase-13/14 Retail UI.
            drawPhase12FrontendDevelopmentScreen(aotState, readiness, autopilotFrame);
            return;
        }

        // One-way ownership cut. Native gameplay can never fall back to GS.
        rlDrawRenderBatchActive();
        ClearBackground(BLACK);

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

    impl_->staticElfImage = game::loadRac1StaticElfImage(elf);
    if (!impl_->staticElfImage.ok()) {
        std::cerr << "[OpenRatchet:gameplay-overlay] static ELF parse failed status="
                  << game::rac1StaticElfStatusName(impl_->staticElfImage.status) << '\n';
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

    // Freeze the generated static function table before any OpenRatchet HLE
    // replacement mutates its slots. Overlay coherence must reason about stale
    // recompiled ELF entries, never about native replacements installed later.
    impl_->snapshotStaticFallbackAddressSet();

    game::declareNativeReplacements(impl_->replacements);

    // Preserve the verified legacy ordering exactly: the two bootstrap guest
    // PCs are installed before PS2Runtime initialization, then generated EE
    // code is loaded, then runtime replacements capture their fallbacks.
    installStage(impl_->eeFallback,
                 impl_->replacements,
                 runtime::NativeReplacementStage::Bootstrap);

    // PS2Runtime still owns the fallback EE executor and creates the host
    // window, but its known-broken compatibility GS image is never restored as
    // final presentation. Before Phase 13/14 provide native frontend/UI, this
    // callback draws a host-only development status screen and feeds ordinary
    // Start/Cross reports through the unchanged Retail controller parser. It
    // performs a one-way native gameplay ownership cut only after authentic
    // Level-0 AOT, mapping, renderer and Retail-camera readiness are all proved.
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
