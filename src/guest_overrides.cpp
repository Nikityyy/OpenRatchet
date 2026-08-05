#include "guest_overrides.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet {
namespace {
PS2Runtime::RecompiledFunction g_guest11a948Original = nullptr;
PS2Runtime::RecompiledFunction g_guest12f208Original = nullptr;
PS2Runtime::RecompiledFunction g_guest121e40Original = nullptr;
PS2Runtime::RecompiledFunction g_guest1f97e8Original = nullptr;
PS2Runtime::RecompiledFunction g_guest20b618Original = nullptr;
bool g_sifInitResponseInjected = false;
bool g_sifCommandBridgeActive = false;
bool g_sifCommand3Completed = false;
bool g_guest121e40DiagnosticsLogged = false;
bool g_imagePayloadDiagnosticsLogged = false;
uint32_t g_imagePayloadDiagnosticsCount = 0u;
uint32_t g_guest120788DiagnosticsCount = 0u;

const std::vector<uint8_t>& getBootWad() {
    static const std::vector<uint8_t> data = [] {
        const std::filesystem::path path =
            std::filesystem::current_path() / "build" / "extracted" / "wads2" / "wad2_0.wad";
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            std::cerr << "[OpenRatchet:CDVD] missing boot WAD: " << path.string() << std::endl;
            return std::vector<uint8_t>{};
        }

        const std::streamoff size = input.tellg();
        if (size <= 0 || (size % 0x800) != 0) {
            std::cerr << "[OpenRatchet:CDVD] invalid boot WAD size=0x"
                      << std::hex << size << std::dec << " path=" << path.string() << std::endl;
            return std::vector<uint8_t>{};
        }

        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0, std::ios::beg);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size) ||
            bytes.size() < 4u ||
            bytes[0] != 0x57u || bytes[1] != 0x41u ||
            bytes[2] != 0x44u || bytes[3] != 0x0fu) {
            std::cerr << "[OpenRatchet:CDVD] invalid boot WAD header path="
                      << path.string() << std::endl;
            return std::vector<uint8_t>{};
        }

        std::cerr << "[OpenRatchet:CDVD] loaded boot WAD sectors=0x"
                  << std::hex << (bytes.size() / 0x800u)
                  << " bytes=0x" << bytes.size() << std::dec
                  << " path=" << path.string() << std::endl;
        return bytes;
    }();
    return data;
}

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

void completeGuestSprTransfer(PS2Runtime& runtime,
                              uint32_t inputAddress,
                              const std::vector<uint8_t>& bootWad) {
    constexpr uint32_t kSprChannelBase = 0x1000d400u;
    constexpr uint32_t kScratchpadSize = 0x4000u;
    const uint32_t chcr = runtime.memory().readIORegister(kSprChannelBase);
    const uint32_t madr = runtime.memory().readIORegister(kSprChannelBase + 0x10u);
    const uint32_t qwc = runtime.memory().readIORegister(kSprChannelBase + 0x20u);
    const uint32_t sprAddress = runtime.memory().readIORegister(kSprChannelBase + 0x80u);
    const uint32_t sourceBase = inputAddress + 0x10u;
    const uint32_t sourceOffset = madr - sourceBase;
    const uint32_t byteCount = qwc * 16u;
    const bool isBootWadRange =
        sourceOffset < bootWad.size() && byteCount <= bootWad.size() - sourceOffset;

    if ((chcr & 0x100u) != 0u && isBootWadRange && byteCount != 0u) {
        uint8_t* scratchpad = runtime.memory().getScratchpad();
        const uint32_t scratchOffset = sprAddress & (kScratchpadSize - 1u);
        const uint32_t firstBytes = std::min(byteCount, kScratchpadSize - scratchOffset);
        std::memcpy(scratchpad + scratchOffset,
                    bootWad.data() + sourceOffset,
                    firstBytes);
        if (firstBytes < byteCount) {
            std::memcpy(scratchpad,
                        bootWad.data() + sourceOffset + firstBytes,
                        byteCount - firstBytes);
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
                  << " copied=" << std::dec << static_cast<uint32_t>(isBootWadRange ? 1u : 0u)
                  << std::endl;
    }
    runtime.memory().writeIORegister(kSprChannelBase, 0u);
}

void guest_12f208(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t sourceSector = GPR_U32(ctx, 4);
    const uint32_t sectorCount = GPR_U32(ctx, 5);
    const uint32_t destination = GPR_U32(ctx, 6);
    static uint32_t diagnosticsCount = 0u;
    ++diagnosticsCount;
    if (diagnosticsCount <= 8u) {
        std::cerr << "[OpenRatchet:CDVD] 12f208 enter count=" << diagnosticsCount
                  << " source=0x" << std::hex << sourceSector
                  << " sectors=0x" << sectorCount
                  << " destination=0x" << destination
                  << " globalSource=0x" << READ32(0x138e40u)
                  << " globalSectors=0x" << READ32(0x138e44u)
                  << std::dec << std::endl;
    }
    if (g_guest12f208Original != nullptr) {
        g_guest12f208Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }

    if (diagnosticsCount <= 8u) {
        std::cerr << "[OpenRatchet:CDVD] 12f208 after original count=" << diagnosticsCount
                  << " pc=0x" << std::hex << ctx->pc
                  << " globalSource=0x" << READ32(0x138e40u)
                  << " globalSectors=0x" << READ32(0x138e44u)
                  << std::dec << std::endl;
    }

    const auto& bootWad = getBootWad();
    if (bootWad.empty()) {
        return;
    }

    // The first startup query supplies the boot WAD's sector count to the
    // following load. The extracted WAD is the authentic CD/DVD payload,
    // not a generated framebuffer or decompressor input.
    if (sourceSector == 0x121u && sectorCount == 1u && READ32(0x138e44u) == 0u) {
        WRITE32(0x138e40u, 0x3809u);
        WRITE32(0x138e44u, static_cast<uint32_t>(bootWad.size() / 0x800u));
        std::cerr << "[OpenRatchet:CDVD] boot metadata sectors=0x"
                  << std::hex << READ32(0x138e44u) << std::dec << std::endl;
    }

    const bool nativeBootLoad =
        sourceSector == 0u && sectorCount == 0u && destination == 0x1ff8000u;
    const bool referenceBootLoad =
        sourceSector == 0x3809u && sectorCount == bootWad.size() / 0x800u;
    const uint32_t loadDestination = destination;
    const size_t availableBytes = 0x02000000u - loadDestination;
    const size_t copyBytes = std::min(bootWad.size(), availableBytes);
    if ((!nativeBootLoad && !referenceBootLoad) ||
        loadDestination >= 0x02000000u || copyBytes < 0x2000u) {
        return;
    }

    std::memcpy(runtime->memory().getRDRAM() + loadDestination,
                bootWad.data(), copyBytes);
    SET_GPR_U32(ctx, 2, static_cast<uint32_t>(copyBytes));
    std::cerr << "[OpenRatchet:CDVD] loaded boot WAD destination=0x"
              << std::hex << loadDestination << " bytes=0x" << copyBytes
              << (nativeBootLoad ? " native-fallback" : " reference-sector")
              << std::dec << std::endl;
}

void guest_20b618(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static uint32_t count = 0u;
    ++count;
    const __m128i savedReturnAddress = GPR_VEC(ctx, 31);
    if (count <= 4u) {
        std::cerr << "[OpenRatchet:guest] 20b618 enter count=" << count
                  << " a0=0x" << std::hex << GPR_U32(ctx, 4)
                  << " a1=0x" << GPR_U32(ctx, 5)
                  << " ra=0x" << GPR_U32(ctx, 31)
                  << " pc=0x" << ctx->pc << std::dec << std::endl;
    }
    copyGuestDecompressorInputToScratchpad(*runtime, GPR_U32(ctx, 4));
    const auto& bootWad = getBootWad();
    if (g_guest20b618Original != nullptr) {
        for (uint32_t resumeCount = 0u; resumeCount < 200000u; ++resumeCount) {
            g_guest20b618Original(rdram, ctx, runtime);
            const uint32_t waitPc = ctx->pc;
            if (waitPc == 0x20b660u || waitPc == 0x20b670u || waitPc == 0x20b908u) {
                // The guest has submitted an SPR transfer and is polling
                // its CHCR start bit. The root-owned SPR copy above
                // completed the transfer; clear the emulated completion bit
                // before resuming the generated wait loop.
                completeGuestSprTransfer(*runtime, GPR_U32(ctx, 4), bootWad);
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
    g_sifCommand3Completed = false;
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
                const uint32_t requestAddress = READ32(candidate + 0x1cu);
                // The original response callback clears the request object's first
                // word after consuming it. Do not re-bridge the stale outbound
                // packet when the runtime dispatches the response DMA locally.
                if (requestAddress == 0u || READ32(requestAddress) == 0u) {
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
        const uint32_t requestAddress = READ32(packetAddress + 0x1cu);
        const uint32_t requestCommand = READ32(packetAddress + 0x20u);
        uint32_t completion = 0u;
        uint32_t result0 = 0u;
        uint32_t result1 = 0u;
        if (packetCommand == 0x80000009u) {
            if (requestCommand == 0x80000592u) {
                completion = 1u;
                result0 = 0x3f570u;
                result1 = 0x3fb20u;
            } else if (requestCommand == 0x8000059au) {
                completion = 1u;
                result0 = 0x3f648u;
                result1 = 0x3fc50u;
            } else if (requestCommand == 0x80000593u) {
                completion = 1u;
                result0 = 0x410f0u;
                result1 = 0x417c0u;
            } else if (requestCommand == 0x80000595u) {
                completion = 1u;
                result0 = 0x41060u;
                result1 = 0x41bd0u;
            } else if (requestCommand == 0x80000003u) {
                if (g_sifCommand3Completed) {
                    completion = 1u;
                    result0 = 0x4f848u;
                    result1 = 0x4f890u;
                } else {
                    g_sifCommand3Completed = true;
                }
            } else if (requestCommand == 0x80000006u) {
                completion = 1u;
                result0 = 0x220d0u;
            } else if (requestCommand == 0x80000900u) {
                completion = 1u;
                result0 = 0x60f38u;
            } else if (requestCommand == 0x8000091bu) {
                completion = 1u;
                result0 = 0x61338u;
            } else if (requestCommand == 0x80000400u) {
                completion = 1u;
                result0 = 0x5ad00u;
            } else if (requestCommand == 0x123456u) {
                completion = 1u;
                result0 = 0x56500u;
            } else if (requestCommand == 0x123457u) {
                completion = 1u;
                result0 = 0xd6e30u;
            }
        }
        const uint32_t responseWords[] = {
            packetCommand == 0x80000009u ? 0x40u : 0x1040u,
            packetCommand == 0x80000009u ? 0u : 0x1324c0u,
            0x80000008u, 0u,
            0u,
            packetCommand == 0x80000009u ? packetAddress : 0u,
            0u,
            requestAddress,
            packetCommand,
            completion,
            result0,
            result1,
            0u, 0u, 0u, 0u, 0u,
        };
        for (uint32_t i = 0; i < sizeof(responseWords) / sizeof(responseWords[0]); ++i) {
            WRITE32(responseAddress + i * 4u, responseWords[i]);
        }
        std::cerr << "[OpenRatchet:SIF] injected completion for 0x"
                  << std::hex << packetCommand << " request=0x" << requestCommand
                  << " result0=0x" << result0 << " result1=0x" << result1
                  << std::dec << "\n";
    }

    if (g_guest11a948Original != nullptr) {
        g_guest11a948Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
}
}

void registerGuestBootstrapOverrides(PS2Runtime& runtime) {
    runtime.registerFunction(0x11a428u, guest_11a428);
    runtime.registerFunction(0x11a448u, guest_11a448);
}

void registerGuestDmacOverride(PS2Runtime& runtime) {
    g_guest11a948Original = runtime.lookupFunction(0x11a948u);
    g_guest12f208Original = runtime.lookupFunction(0x12f208u);
    g_guest121e40Original = runtime.lookupFunction(0x121e40u);
    g_guest1f97e8Original = runtime.lookupFunction(0x1f97e8u);
    g_guest20b618Original = runtime.lookupFunction(0x20b618u);
    runtime.registerFunction(0x11a948u, guest_11a948);
    runtime.registerFunction(0x12f208u, guest_12f208);
    runtime.registerFunction(0x11cf10u, guest_11cf10);
    runtime.registerFunction(0x12f1c8u, guest_12f1c8);
    runtime.registerFunction(0x120788u, guest_120788);
    runtime.registerFunction(0x121e40u, guest_121e40);
    runtime.registerFunction(0x1f97e8u, guest_1f97e8);
    runtime.registerFunction(0x20b618u, guest_20b618);
    installGuestGraphicsBridge(runtime);
}
}
