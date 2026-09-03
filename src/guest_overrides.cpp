#include "guest_overrides.h"

#include "guest_range.h"
#include "game/native_assets.h"
#include "sif_rpc_transport.h"
#include "sif_startup_responses.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet {
namespace {
PS2Runtime::RecompiledFunction g_guest11a948Original = nullptr;
PS2Runtime::RecompiledFunction g_guest118b20Original = nullptr;
PS2Runtime::RecompiledFunction g_guest121e40Original = nullptr;
PS2Runtime::RecompiledFunction g_guest1f97e8Original = nullptr;
PS2Runtime::RecompiledFunction g_guest20b618Original = nullptr;
bool g_sifInitResponseInjected = false;
bool g_sifCommandBridgeActive = false;
SifStartupResponseResolver g_sifStartupResponseResolver;
SifRpcTransport g_sifRpcTransport;
bool g_guest121e40DiagnosticsLogged = false;
bool g_imagePayloadDiagnosticsLogged = false;
uint32_t g_imagePayloadDiagnosticsCount = 0u;
uint32_t g_guest120788DiagnosticsCount = 0u;
uint32_t g_sifDeferredPayloadDiagnosticsCount = 0u;
uint32_t g_lastSifRpcTracePacket = 0u;
uint32_t g_lastSifRpcTraceSequence = 0u;
uint32_t g_lastSifRpcTraceFunction = 0u;
SifRpcCallDisposition g_lastSifRpcTraceDisposition =
    SifRpcCallDisposition::UnboundClient;
bool g_hasSifRpcTrace = false;
constexpr size_t kGuestMemorySize = 0x02000000u;

void copyGuestDecompressorInputToScratchpad(PS2Runtime& runtime, uint32_t inputAddress) {
    constexpr uint32_t kScratchpadAddress = 0x70000000u;
    constexpr uint32_t kTransferBytes = 0x200u * 16u;
    const uint32_t sourceAddress = inputAddress + 0x10u;
    for (uint32_t offset = 0u; offset < kTransferBytes; offset += 16u) {
        runtime.memory().write128(
            kScratchpadAddress + offset,
            runtime.memory().read128(sourceAddress + offset));
    }

    static uint32_t diagnosticsCount = 0u;
    if (diagnosticsCount < 4u) {
        ++diagnosticsCount;
        uint32_t nonZeroBytes = 0u;
        for (uint32_t offset = 0u; offset < kTransferBytes; ++offset) {
            nonZeroBytes += runtime.memory().read8(kScratchpadAddress + offset) != 0u ? 1u : 0u;
        }
        std::cerr << "[OpenRatchet:DMAC] SPR_FROM source=0x"
                  << std::hex << sourceAddress
                  << " destination=0x" << kScratchpadAddress
                  << " bytes=0x" << kTransferBytes
                  << " nonZeroBytes=" << std::dec << nonZeroBytes << std::endl;
    }
}

void completeGuestSprTransfer(PS2Runtime& runtime, uint32_t inputAddress) {
    constexpr uint32_t kSprChannelBase = 0x1000d400u;
    constexpr uint32_t kScratchpadSize = 0x4000u;
    const uint32_t chcr = runtime.memory().readIORegister(kSprChannelBase);
    const uint32_t madr = runtime.memory().readIORegister(kSprChannelBase + 0x10u);
    const uint32_t qwc = runtime.memory().readIORegister(kSprChannelBase + 0x20u);
    const uint32_t sprAddress = runtime.memory().readIORegister(kSprChannelBase + 0x80u);
    const uint32_t sourceBase = inputAddress + 0x10u;
    const uint32_t sourceOffset = madr - sourceBase;
    const bool hasValidTransferSize = qwc <= UINT32_MAX / 16u;
    const uint32_t byteCount = hasValidTransferSize ? qwc * 16u : 0u;
    const uint32_t physicalMadr = madr & PS2_RAM_MASK;
    const bool isGuestInputRange =
        madr >= sourceBase && hasValidTransferSize &&
        isRangeWithin(physicalMadr, byteCount, PS2_RAM_SIZE);

    if ((chcr & 0x100u) != 0u && isGuestInputRange && byteCount != 0u) {
        uint8_t* scratchpad = runtime.memory().getScratchpad();
        const uint32_t scratchOffset = sprAddress & (kScratchpadSize - 1u);
        for (uint32_t offset = 0u; offset < byteCount; offset += 16u) {
            const __m128i quadword = runtime.memory().read128(madr + offset);
            alignas(16) uint8_t bytes[16];
            std::memcpy(bytes, &quadword, sizeof(bytes));
            for (uint32_t i = 0u; i < 16u; ++i) {
                scratchpad[(scratchOffset + offset + i) & (kScratchpadSize - 1u)] = bytes[i];
            }
        }
    }

    static uint32_t diagnosticsCount = 0u;
    if (diagnosticsCount < 8u) {
        ++diagnosticsCount;
        std::cerr << "[OpenRatchet:DMAC] SPR completion madr=0x"
                  << std::hex << madr
                  << " spr=0x" << sprAddress
                  << " qwc=0x" << qwc
                  << " sourceOffset=0x" << sourceOffset
                  << " copied=" << std::dec
                  << static_cast<uint32_t>(isGuestInputRange ? 1u : 0u)
                  << std::endl;
    }
    // This bridge retains the observed whole-register clear for now. Replacing
    // it with a bit-level completion update requires a DMAC reference capture.
    runtime.memory().writeIORegister(kSprChannelBase, 0u);
}

void guest_20b618(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static uint32_t count = 0u;
    ++count;
    const __m128i savedReturnAddress = GPR_VEC(ctx, 31);
    const uint32_t inputAddress = GPR_U32(ctx, 4);
    const uint32_t outputAddress = GPR_U32(ctx, 5);
    if (count <= 4u) {
        std::cerr << "[OpenRatchet:guest] 20b618 enter count=" << count
                  << " a0=0x" << std::hex << GPR_U32(ctx, 4)
                  << " a1=0x" << GPR_U32(ctx, 5)
                  << " ra=0x" << GPR_U32(ctx, 31)
                  << " pc=0x" << ctx->pc << std::dec << std::endl;
    }
    copyGuestDecompressorInputToScratchpad(*runtime, inputAddress);
    if (g_guest20b618Original != nullptr) {
        for (uint32_t resumeCount = 0u; resumeCount < 200000u; ++resumeCount) {
            g_guest20b618Original(rdram, ctx, runtime);
            const uint32_t waitPc = ctx->pc;
            if (waitPc == 0x20b660u || waitPc == 0x20b670u || waitPc == 0x20b908u) {
                // The guest has submitted an SPR transfer and is polling
                // its CHCR start bit. The root-owned SPR copy above
                // completed the transfer; clear the emulated completion bit
                // before resuming the generated wait loop.
                completeGuestSprTransfer(*runtime, inputAddress);
            }
            if (waitPc < 0x20b660u || waitPc > 0x20b8e0u) {
                break;
            }
        }
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
    const uint32_t savedReturnPc = _mm_extract_epi32(savedReturnAddress, 0);
    if (ctx->pc == 0u && savedReturnPc != 0u) {
        ctx->pc = savedReturnPc;
        SET_GPR_VEC(ctx, 31, savedReturnAddress);
        std::cerr << "[OpenRatchet:guest] 20b618 repaired return pc=0x"
                  << std::hex << savedReturnPc << std::dec << std::endl;
    }
    game::validateNativeWadDecompressorShadow(
        rdram, inputAddress, outputAddress, GPR_U32(ctx, 2));

    if (count <= 4u) {
        uint32_t nonZeroOutput = 0u;
        for (uint32_t i = 0u; i < 0x100u; ++i) {
            nonZeroOutput += READ8(ADD32(GPR_U32(ctx, 5), i)) != 0u ? 1u : 0u;
        }
        std::cerr << "[OpenRatchet:guest] 20b618 exit count=" << count
                  << " pc=0x" << std::hex << ctx->pc
                  << " v0=0x" << GPR_U32(ctx, 2)
                  << " output=0x" << GPR_U32(ctx, 5)
                  << " outputNonZero=" << std::dec << nonZeroOutput << std::endl;
    }
}

void guest_1f97e8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t originalDestination = GPR_U32(ctx, 4);
    const uint32_t value = GPR_U32(ctx, 5);
    const uint32_t size = GPR_U32(ctx, 6);
    const bool correctsGeneratedCall =
        originalDestination == GPR_U32(ctx, 29) &&
        value == 0x80808080u &&
        size == 0x100u;
    const uint32_t destination = correctsGeneratedCall ? 0x1941c0u : originalDestination;
    const bool tracksImageStaging = destination == 0x1941c0u;
    if (tracksImageStaging) {
        std::cerr << "[OpenRatchet:GS] staging fill before dst=0x" << std::hex
                  << originalDestination << " effectiveDst=0x" << destination
                  << " value=0x" << value << " size=0x" << size
                  << (correctsGeneratedCall ? " corrected-generated-destination" : "")
                  << std::dec << std::endl;
    }

    if (correctsGeneratedCall) {
        SET_GPR_U32(ctx, 4, destination);
    }
    if (g_guest1f97e8Original != nullptr) {
        g_guest1f97e8Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
    if (correctsGeneratedCall) {
        SET_GPR_U32(ctx, 4, originalDestination);
    }

    if (tracksImageStaging) {
        uint32_t firstWord = 0u;
        std::memcpy(&firstWord, rdram + destination, sizeof(firstWord));
        std::cerr << "[OpenRatchet:GS] staging fill after firstWord=0x"
                  << std::hex << firstWord << std::dec << std::endl;
    }
}

void guest_11a428(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t index = READ32(ADD32(GPR_U32(ctx, 4), 0x10u));
    const uint32_t table = READ32(ADD32(GPR_U32(ctx, 5), 0x1cu));
    const uint32_t address = ADD32(table, index << 2u);
    WRITE32(address, READ32(ADD32(GPR_U32(ctx, 4), 0x14u)));
    SET_GPR_U32(ctx, 2, address);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_11a448(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t value = READ32(ADD32(GPR_U32(ctx, 4), 0x10u));
    SET_GPR_U32(ctx, 2, value);
    WRITE32(ADD32(GPR_U32(ctx, 5), 8u), value);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_11cf10(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    // ponytail: bridge the absent async IOP-ready event; replace with real IOP status when available.
    g_sifInitResponseInjected = false;
    g_sifCommandBridgeActive = false;
    g_sifDeferredPayloadDiagnosticsCount = 0u;
    g_hasSifRpcTrace = false;
    g_sifStartupResponseResolver.reset();
    WRITE32(0x12fbf0u, 0u);
    SET_GPR_U32(ctx, 2, 1u);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_12f1c8(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    // The original VBlank-start callback enters at this interior label of
    // sub_0012F1A0, which the generated function table cannot address directly.
    const uint64_t nextVblank = READ64(0x15ed48u) + 1u;
    const uint64_t frameCounterBase = READ64(0x15ed40u);
    WRITE64(0x15ed48u, nextVblank);
    SET_GPR_U64(ctx, 2, 0u);
    SET_GPR_U64(ctx, 6, frameCounterBase + READ32(0x1000800u));
    WRITE64(ADD32(GPR_U32(ctx, 28), 0xffff8150u), GPR_U64(ctx, 6));
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_120788(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t buffer = GPR_U32(ctx, 4) | 0x20000000u;
    const uint32_t firstSize = READ32(buffer);
    const uint32_t secondSize = READ32(ADD32(buffer, 4u));
    ++g_guest120788DiagnosticsCount;
    if (g_guest120788DiagnosticsCount <= 12u) {
        uint32_t firstNonZero = 0u;
        uint32_t secondNonZero = 0u;
        for (uint32_t i = 0u; i < firstSize && i < 0x4000u; ++i) {
            firstNonZero += READ8(ADD32(buffer, 0x10u + i)) != 0u ? 1u : 0u;
        }
        for (uint32_t i = 0u; i < secondSize && i < 0x4000u; ++i) {
            secondNonZero += READ8(ADD32(buffer, 0x50u + i)) != 0u ? 1u : 0u;
        }
        std::cerr << "[OpenRatchet:SIF] 120788 count=" << g_guest120788DiagnosticsCount
                  << " buffer=0x" << std::hex << buffer
                  << " firstSize=0x" << firstSize
                  << " firstDst=0x" << READ32(ADD32(buffer, 8u))
                  << " firstNonZero=" << std::dec << firstNonZero
                  << " secondSize=0x" << std::hex << secondSize
                  << " secondDst=0x" << READ32(ADD32(buffer, 0xcu))
                  << " secondNonZero=" << std::dec << secondNonZero
                  << std::endl;
    }
    if (firstSize > 0u) {
        const uint32_t destination = READ32(ADD32(buffer, 8u));
        for (uint32_t i = 0u; i < firstSize; ++i) {
            WRITE8(ADD32(destination, i), READ8(ADD32(buffer, 0x10u + i)));
        }
    }

    if (secondSize > 0u) {
        const uint32_t destination = READ32(ADD32(buffer, 0xcu));
        for (uint32_t i = 0u; i < secondSize; ++i) {
            WRITE8(ADD32(destination, i), READ8(ADD32(buffer, 0x50u + i)));
        }
    }

    ctx->pc = 0x1206d8u;
}

void guest_121e40(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (g_guest121e40Original != nullptr) {
        g_guest121e40Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }

    const auto &gs = runtime->memory().gs();
    std::cerr << "[OpenRatchet:GS] presentation setup pmode=0x"
              << std::hex << gs.pmode
              << " smode2=0x" << gs.smode2
              << " dispfb1=0x" << gs.dispfb1
              << " display1=0x" << gs.display1
              << " dispfb2=0x" << gs.dispfb2
              << " display2=0x" << gs.display2
              << " bgcolor=0x" << gs.bgcolor
              << std::dec << "\n";

    if (!g_guest121e40DiagnosticsLogged) {
        g_guest121e40DiagnosticsLogged = true;
        runtime->gs().latchHostPresentationFrame();
        std::vector<uint8_t> pixels;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t displayFbp = 0u;
        uint32_t sourceFbp = 0u;
        bool preferred = false;
        const bool copied = runtime->gs().copyLatchedHostPresentationFrame(
            pixels, width, height, &displayFbp, &sourceFbp, &preferred);
        const GSDebugSnapshot snapshot = runtime->gs().getDebugSnapshot();
        uint32_t nonBlackPixels = 0u;
        uint64_t pixelHash = 1469598103934665603ull;
        for (size_t i = 0; i + 3u < pixels.size(); i += 4u) {
            if (pixels[i] != 0u || pixels[i + 1u] != 0u || pixels[i + 2u] != 0u) {
                ++nonBlackPixels;
            }
            for (size_t channel = 0u; channel < 4u; ++channel) {
                pixelHash ^= pixels[i + channel];
                pixelHash *= 1099511628211ull;
            }
        }
        runtime->gs().refreshDisplaySnapshot();
        uint32_t vramSize = 0u;
        const uint8_t *vram = runtime->gs().lockDisplaySnapshot(vramSize);
        uint32_t nonZeroVramBytes = 0u;
        uint32_t firstNonZeroVramOffset = 0xFFFFFFFFu;
        if (vram != nullptr) {
            for (uint32_t i = 0u; i < vramSize; ++i) {
                if (vram[i] != 0u) {
                    ++nonZeroVramBytes;
                    if (firstNonZeroVramOffset == 0xFFFFFFFFu) {
                        firstNonZeroVramOffset = i;
                    }
                }
            }
            runtime->gs().unlockDisplaySnapshot();
        }
        std::cerr << "[OpenRatchet:GS] presentation probe copied="
                  << static_cast<uint32_t>(copied ? 1u : 0u)
                  << " pixels=" << pixels.size()
                  << " size=" << width << "x" << height
                  << " displayFbp=" << displayFbp
                  << " sourceFbp=" << sourceFbp
                  << " preferred=" << static_cast<uint32_t>(preferred ? 1u : 0u)
                  << " hasFrame=" << static_cast<uint32_t>(snapshot.hasHostPresentationFrame ? 1u : 0u)
                  << " nonBlackPixels=" << nonBlackPixels
                  << " pixelHash=0x" << std::hex << pixelHash << std::dec
                  << " nonZeroVramBytes=" << nonZeroVramBytes
                  << " firstNonZeroVramOffset=0x" << std::hex << firstNonZeroVramOffset << std::dec
                  << std::endl;
    }
}

void installGuestGraphicsBridge(PS2Runtime& runtime) {
    runtime.gifArbiter().setProcessPacketFn([&runtime](const uint8_t* data, uint32_t sizeBytes) {
        const GSDebugSnapshot snapshot = runtime.gs().getDebugSnapshot();
        uint64_t firstWord = 0u;
        if (data != nullptr && sizeBytes >= 16u) {
            std::memcpy(&firstWord, data, sizeof(firstWord));
        }

        const bool isActiveImagePayload =
            data != nullptr &&
            sizeBytes >= 16u &&
            snapshot.trxdir == 0u &&
            snapshot.transferTotalPixels > snapshot.transferCopiedPixels &&
            (firstWord & 0x7FFFu) == 0u;
        if (!isActiveImagePayload) {
            runtime.gs().processGIFPacket(data, sizeBytes);
            return;
        }

        ++g_imagePayloadDiagnosticsCount;
        if (!g_imagePayloadDiagnosticsLogged ||
            (g_imagePayloadDiagnosticsCount <= 8u) ||
            ((g_imagePayloadDiagnosticsCount % 128u) == 0u)) {
            g_imagePayloadDiagnosticsLogged = true;
            uint32_t nonZeroBytes = 0u;
            uint64_t payloadHash = 1469598103934665603ull;
            for (uint32_t i = 0u; i < sizeBytes; ++i) {
                if (data[i] != 0u) {
                    ++nonZeroBytes;
                }
                payloadHash ^= data[i];
                payloadHash *= 1099511628211ull;
            }
            std::cerr << "[OpenRatchet:GS] image payload size=" << sizeBytes
                      << " count=" << g_imagePayloadDiagnosticsCount
                      << " nonZeroBytes=" << nonZeroBytes
                      << " hash=0x" << std::hex << payloadHash << std::dec
                      << " transferPixels=" << snapshot.transferTotalPixels
                      << " copiedPixels=" << snapshot.transferCopiedPixels
                      << " bitblt=0x" << std::hex << snapshot.bitbltbuf.dbp
                      << ":" << static_cast<uint32_t>(snapshot.bitbltbuf.dbw)
                      << ":" << static_cast<uint32_t>(snapshot.bitbltbuf.dpsm)
                      << " trxpos=0x" << snapshot.trxpos.dsax << ":" << snapshot.trxpos.dsay
                      << " trxreg=0x" << snapshot.trxreg.rrw << ":" << snapshot.trxreg.rrh
                      << std::dec
                      << std::endl;
        }

        const uint32_t imageQwc = sizeBytes / 16u;
        if (imageQwc == 0u || imageQwc > 0x7FFFu || imageQwc * 16u != sizeBytes) {
            runtime.gs().processGIFPacket(data, sizeBytes);
            return;
        }

        std::vector<uint8_t> imagePacket(sizeBytes + 16u, 0u);
        const uint64_t imageTag = static_cast<uint64_t>(imageQwc) | (2ull << 58u);
        std::memcpy(imagePacket.data(), &imageTag, sizeof(imageTag));
        std::memcpy(imagePacket.data() + 16u, data, sizeBytes);
        runtime.gs().processGIFPacket(imagePacket.data(), static_cast<uint32_t>(imagePacket.size()));
    });
}

void guest_118b20(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    constexpr uint32_t kDescriptorSize = 0x10u;
    constexpr uint32_t kMaximumDescriptors = 32u;
    constexpr uint32_t kCapturedPayloadBytes = 16u;

    const uint32_t descriptorList = GPR_U32(ctx, 4);
    const uint32_t descriptorCount = GPR_U32(ctx, 5);
    const uint32_t boundedCount = std::min(descriptorCount, kMaximumDescriptors);
    if (isRangeWithin(descriptorList,
                      static_cast<size_t>(boundedCount) * kDescriptorSize,
                      kGuestMemorySize)) {
        for (uint32_t index = 0u; index < boundedCount; ++index) {
            const uint32_t descriptor = descriptorList + index * kDescriptorSize;
            const uint32_t source = READ32(descriptor);
            const uint32_t destination = READ32(descriptor + 4u);
            const uint32_t size = READ32(descriptor + 8u);
            const uint32_t attributes = READ32(descriptor + 0xcu);
            if (destination == 0u || size == 0u || (attributes & 0x44u) != 0u ||
                !isRangeWithin(source, std::min(size, kCapturedPayloadBytes), kGuestMemorySize)) {
                continue;
            }
            std::array<uint32_t, 4> payloadWords{};
            for (uint32_t offset = 0u; offset + sizeof(uint32_t) <= size &&
                 offset < kCapturedPayloadBytes; offset += sizeof(uint32_t)) {
                payloadWords[offset / sizeof(uint32_t)] = READ32(source + offset);
            }
            bool payloadAllZero = isRangeWithin(source, size, kGuestMemorySize);
            for (uint32_t offset = 0u; payloadAllZero && offset < size; ++offset) {
                payloadAllZero = READ8(source + offset) == 0u;
            }
            g_sifRpcTransport.recordOutboundPayload(
                destination, size, payloadWords, payloadAllZero);
        }
    }

    if (g_guest118b20Original != nullptr) {
        g_guest118b20Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
}

void guest_11a948(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (!g_sifInitResponseInjected &&
        READ32(0x154e48u) == 0x80000002u &&
        READ32(0x154e4cu) == 0u) {
        const uint32_t responseAddress = READ32(0x154e58u);
        if (responseAddress != 0u) {
            const uint32_t responseWords[] = {
                0x18u, 0x154e58u, 0x80000001u, 0u,
                0u, 1u, 0u, 0u,
            };
            for (uint32_t i = 0; i < sizeof(responseWords) / sizeof(responseWords[0]); ++i) {
                WRITE32(responseAddress + i * 4u, responseWords[i]);
            }
            g_sifInitResponseInjected = true;
            g_sifCommandBridgeActive = false;
            std::cerr << "[OpenRatchet:SIF] injected INIT response at 0x"
                      << std::hex << responseAddress << std::dec << "\n";
        }
    }

    const uint32_t responseAddress = READ32(0x154e58u);
    uint32_t packetAddress = 0u;
    uint32_t packetCommand = 0u;
    // ponytail: bounded 64-slot scan; use the SIF descriptor source if the guest pools change.
    for (const uint32_t pool : {0x20155000u, 0x20155800u}) {
        for (uint32_t slot = 0u; slot < 32u; ++slot) {
            const uint32_t candidate = pool + slot * 0x40u;
            const uint32_t command = READ32(candidate + 8u);
            if (command == 0x80000009u || command == 0x8000000au) {
                const uint32_t clientAddress = READ32(candidate + 0x1cu);
                // The original response callback clears the request object's first
                // word after consuming it. Do not re-bridge the stale outbound
                // packet when the runtime dispatches the response DMA locally.
                if (clientAddress == 0u || READ32(clientAddress) == 0u) {
                    continue;
                }
                packetAddress = candidate;
                packetCommand = command;
                break;
            }
        }
        if (packetAddress != 0u) {
            break;
        }
    }

    if (responseAddress != 0u &&
        (packetCommand == 0x80000009u || packetCommand == 0x8000000au)) {
        if (packetCommand == 0x80000009u) {
            g_sifCommandBridgeActive = true;
        }
        if (!g_sifCommandBridgeActive) {
            packetCommand = 0u;
        }
    }

    if (responseAddress != 0u &&
        (packetCommand == 0x80000009u || packetCommand == 0x8000000au)) {
        const uint32_t clientAddress = READ32(packetAddress + 0x1cu);
        const uint32_t requestCommand = READ32(packetAddress + 0x20u);
        const uint32_t remoteSendBuffer = READ32(packetAddress + 0x04u);
        const uint32_t sendSize = READ32(packetAddress + 0x24u);
        const uint32_t receiveBuffer = READ32(packetAddress + 0x28u);
        const uint32_t receiveSize = READ32(packetAddress + 0x2cu);
        const uint32_t packetStatus = READ32(packetAddress + 0x10u);
        const uint32_t packetSequence = READ32(packetAddress + 0x18u);
        const uint32_t clientPacket = clientAddress != 0u ? READ32(clientAddress) : 0u;
        const uint32_t clientSequence = clientAddress != 0u ? READ32(clientAddress + 4u) : 0u;

        SifRpcCallResponse callResponse;
        if (packetCommand == 0x8000000au) {
            callResponse = g_sifRpcTransport.resolveCall(
                clientAddress, requestCommand, receiveBuffer, receiveSize,
                remoteSendBuffer, sendSize);

            const bool canCompleteCall = canCompleteSifRpcCallWithoutPayload(
                receiveBuffer, receiveSize, callResponse.completed);

            const bool traceChanged = !g_hasSifRpcTrace ||
                packetAddress != g_lastSifRpcTracePacket ||
                packetSequence != g_lastSifRpcTraceSequence ||
                requestCommand != g_lastSifRpcTraceFunction ||
                callResponse.disposition != g_lastSifRpcTraceDisposition;
            if (traceChanged) {
                g_hasSifRpcTrace = true;
                g_lastSifRpcTracePacket = packetAddress;
                g_lastSifRpcTraceSequence = packetSequence;
                g_lastSifRpcTraceFunction = requestCommand;
                g_lastSifRpcTraceDisposition = callResponse.disposition;
                std::cerr << "[OpenRatchet:SIF:RPC] disposition="
                          << (canCompleteCall ? "completed" : "pending")
                          << " reason=" << sifRpcCallDispositionName(callResponse.disposition)
                          << " packet=0x" << std::hex << packetAddress
                          << " client=0x" << clientAddress
                          << " service=0x" << callResponse.serviceId
                          << " function=0x" << requestCommand
                          << " send=0x" << remoteSendBuffer
                          << " sendSize=0x" << sendSize
                          << " requestPayloadAvailable="
                          << (callResponse.requestPayloadAvailable ? 1u : 0u)
                          << " requestPayloadSize=0x" << callResponse.requestPayloadSize
                          << " requestPayloadAllZero="
                          << (callResponse.requestPayloadAllZero ? 1u : 0u)
                          << " requestWords=0x" << callResponse.requestPayloadWords[0]
                          << ",0x" << callResponse.requestPayloadWords[1]
                          << ",0x" << callResponse.requestPayloadWords[2]
                          << ",0x" << callResponse.requestPayloadWords[3]
                          << " receive=0x" << receiveBuffer
                          << " receiveSize=0x" << receiveSize
                          << " status=0x" << packetStatus
                          << " sequence=0x" << packetSequence
                          << " clientPacket=0x" << clientPacket
                          << " clientSequence=0x" << clientSequence
                          << " busy=" << ((packetStatus & 1u) != 0u ? 1u : 0u)
                          << " responseSize=0x" << callResponse.payloadSize
                          << std::dec << "\n";
            }
        }
        const bool canCompleteCall = packetCommand != 0x8000000au ||
            canCompleteSifRpcCallWithoutPayload(
                receiveBuffer, receiveSize, callResponse.completed);
        if (!canCompleteCall) {
            if (g_sifDeferredPayloadDiagnosticsCount < 8u) {
                ++g_sifDeferredPayloadDiagnosticsCount;
                std::cerr << "[OpenRatchet:SIF] deferred data-bearing CALL packet=0x"
                          << std::hex << packetAddress
                          << " client=0x" << clientAddress
                          << " request=0x" << requestCommand
                          << " receive=0x" << receiveBuffer
                          << " size=0x" << receiveSize
                          << " status=0x" << packetStatus
                          << " sequence=0x" << packetSequence
                          << " clientPacket=0x" << clientPacket
                          << " clientSequence=0x" << clientSequence
                          << " busy=" << ((packetStatus & 1u) != 0u ? 1u : 0u)
                          << std::dec << "\n";
            }
        } else {
            SifStartupResponse response;
            if (packetCommand == 0x80000009u) {
                response = g_sifStartupResponseResolver.resolve(requestCommand);
                if (response.completed) {
                    g_sifRpcTransport.recordBinding(clientAddress, requestCommand);
                }
            }
            if (callResponse.completed) {
                if (callResponse.zeroFillPayload &&
                    isRangeWithin(receiveBuffer, callResponse.payloadSize, kGuestMemorySize)) {
                    std::memset(rdram + receiveBuffer, 0, callResponse.payloadSize);
                }
                const uint32_t payloadWordCount = std::min<uint32_t>(
                    static_cast<uint32_t>(callResponse.payloadWords.size()),
                    callResponse.payloadSize / sizeof(uint32_t));
                for (uint32_t i = 0u; i < payloadWordCount; ++i) {
                    WRITE32(receiveBuffer + i * 4u, callResponse.payloadWords[i]);
                }
            }
            const uint32_t completionPayloadSize =
                packetCommand == 0x8000000au ? callResponse.payloadSize : 0u;
            const auto responseWords = makeSifRpcIngressPacket(
                completionPayloadSize, receiveBuffer, packetCommand, packetAddress,
                clientAddress, response.completed, response.result0, response.result1);
            for (uint32_t i = 0; i < responseWords.size(); ++i) {
                WRITE32(responseAddress + i * 4u, responseWords[i]);
            }
            std::cerr << "[OpenRatchet:SIF] injected completion for 0x"
                      << std::hex << packetCommand << " request=0x" << requestCommand
                      << " service=0x" << callResponse.serviceId
                      << " payload=0x" << callResponse.payloadSize
                      << " result0=0x" << response.result0 << " result1=0x" << response.result1
                      << std::dec << "\n";
        }
    }

    if (g_guest11a948Original != nullptr) {
        g_guest11a948Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
}
}

void declareLegacyGuestCompatibilityReplacements(
    runtime::NativeReplacementRegistry& registry) {
    using runtime::NativeReplacementStage;

    // These two addresses are interior startup PCs installed before the ELF
    // function table is available. They remain legacy compatibility bridges.
    registry.add(0x11a428u, "legacy.sif.bootstrap.11a428",
                 NativeReplacementStage::Bootstrap, guest_11a428);
    registry.add(0x11a448u, "legacy.sif.bootstrap.11a448",
                 NativeReplacementStage::Bootstrap, guest_11a448);

    // Runtime wrappers preserve their original generated fallback explicitly.
    // The registry captures the fallback before installing each wrapper.
    registry.add(0x118b20u, "legacy.sif.set-dma",
                 NativeReplacementStage::Runtime, guest_118b20,
                 &g_guest118b20Original);
    registry.add(0x11a948u, "legacy.sif.response-dispatch",
                 NativeReplacementStage::Runtime, guest_11a948,
                 &g_guest11a948Original);
    registry.add(0x11cf10u, "legacy.callback.vblank",
                 NativeReplacementStage::Runtime, guest_11cf10);
    registry.add(0x12f1c8u, "legacy.cdvd.read-interior",
                 NativeReplacementStage::Runtime, guest_12f1c8);
    registry.add(0x120788u, "legacy.sif.call-target-repair",
                 NativeReplacementStage::Runtime, guest_120788);
    registry.add(0x121e40u, "legacy.graphics.image-diagnostics",
                 NativeReplacementStage::Runtime, guest_121e40,
                 &g_guest121e40Original);
    registry.add(0x1f97e8u, "legacy.graphics.init-diagnostics",
                 NativeReplacementStage::Runtime, guest_1f97e8,
                 &g_guest1f97e8Original);
    registry.add(0x20b618u, "legacy.dmac.decompressor-bridge",
                 NativeReplacementStage::Runtime, guest_20b618,
                 &g_guest20b618Original);
}

void installLegacyGuestDeviceBridges(PS2Runtime& fallbackRuntime) {
    installGuestGraphicsBridge(fallbackRuntime);
}
}
