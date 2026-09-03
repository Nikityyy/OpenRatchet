#include "game/native_assets.h"

#include "assets/wad_decompressor.h"
#include "guest_range.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet::game {
namespace {

constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
constexpr std::uint32_t kPhysicalAddressMask = 0x1fffffffu;
constexpr std::size_t kWadHeaderBytes = 0x10u;

// Independent oracle for the target retail R&C1 boot WAD (WAD2/0). These
// values come from the extracted asset and an independent Ratchet WAD decoder,
// not from the removed SPR/DMAC compatibility bridge.
constexpr std::uint32_t kBootEncodedSize = 0x00050e0fu;
constexpr std::uint32_t kBootEncodedHash = 0xb90bb3e3u;
constexpr std::size_t kBootOutputBytes = 0x000a346cu;
constexpr std::uint32_t kBootOutputHash = 0xd3cb9822u;

bool resolveGuestRamAddress(std::uint32_t address,
                            std::uint32_t& physical) noexcept {
    physical = address & kPhysicalAddressMask;
    return physical < kGuestRamBytes;
}

std::uint32_t readLe32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

std::uint32_t fnv1a32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

void returnToGuestCaller(R5900Context* ctx, std::uint32_t result) noexcept {
    SET_GPR_U32(ctx, 2, result);
    ctx->pc = GPR_U32(ctx, 31);
}

void failNativeWad(R5900Context* ctx,
                   std::uint32_t inputAddress,
                   std::uint32_t outputAddress,
                   const char* reason) {
    static std::uint32_t failureCount = 0u;
    ++failureCount;
    std::cerr << "[OpenRatchet:WAD] native failure count=" << failureCount
              << " input=0x" << std::hex << inputAddress
              << " output=0x" << outputAddress << std::dec
              << " reason=" << reason << '\n';
    returnToGuestCaller(ctx, 0u);
}

void nativeDecompressWad(std::uint8_t* rdram,
                         R5900Context* ctx,
                         PS2Runtime* runtime) {
    (void)runtime;

    static std::uint32_t invocationCount = 0u;
    ++invocationCount;

    const std::uint32_t inputAddress = GPR_U32(ctx, 4);
    const std::uint32_t outputAddress = GPR_U32(ctx, 5);

    std::uint32_t inputPhysical = 0u;
    std::uint32_t outputPhysical = 0u;
    if (!resolveGuestRamAddress(inputAddress, inputPhysical) ||
        !isRangeWithin(inputPhysical, kWadHeaderBytes, kGuestRamBytes)) {
        failNativeWad(ctx, inputAddress, outputAddress, "input-range");
        return;
    }
    if (!resolveGuestRamAddress(outputAddress, outputPhysical)) {
        failNativeWad(ctx, inputAddress, outputAddress, "output-range");
        return;
    }

    if (rdram[inputPhysical + 0u] != 'W' ||
        rdram[inputPhysical + 1u] != 'A' ||
        rdram[inputPhysical + 2u] != 'D') {
        failNativeWad(ctx, inputAddress, outputAddress, "invalid-magic");
        return;
    }

    // The stream size is stored as an unaligned LE32 at +3. Copy the complete
    // encoded stream to host memory before decoding so the native replacement
    // remains correct even if a caller ever supplies overlapping input/output
    // guest ranges. The old SPR implementation implicitly had the same
    // decoupling by staging source blocks through scratchpad.
    const std::uint32_t encodedSize = readLe32(rdram + inputPhysical + 3u);
    if (encodedSize < kWadHeaderBytes ||
        !isRangeWithin(inputPhysical, encodedSize, kGuestRamBytes)) {
        failNativeWad(ctx, inputAddress, outputAddress, "encoded-range");
        return;
    }

    std::vector<std::uint8_t> encoded(encodedSize);
    std::copy_n(rdram + inputPhysical, encodedSize, encoded.data());

    std::span<std::uint8_t> output(
        rdram + outputPhysical,
        static_cast<std::size_t>(kGuestRamBytes - outputPhysical));
    const assets::WadDecompressResult result = assets::decompressWad(encoded, output);
    if (!result.ok() || result.bytesWritten > std::numeric_limits<std::uint32_t>::max()) {
        failNativeWad(ctx,
                      inputAddress,
                      outputAddress,
                      assets::wadDecompressStatusName(result.status));
        return;
    }

    const std::uint32_t encodedHash = fnv1a32(encoded);
    const std::uint32_t outputHash = fnv1a32(
        std::span<const std::uint8_t>(rdram + outputPhysical, result.bytesWritten));
    const bool bootOracleKnown =
        encodedSize == kBootEncodedSize && encodedHash == kBootEncodedHash;
    const bool bootOracleMatch =
        bootOracleKnown && result.bytesWritten == kBootOutputBytes &&
        outputHash == kBootOutputHash;

    if (invocationCount <= 12u) {
        std::cerr << "[OpenRatchet:WAD] native count=" << invocationCount
                  << " input=0x" << std::hex << inputAddress
                  << " output=0x" << outputAddress
                  << " encodedSize=0x" << encodedSize
                  << " encodedHash=0x" << encodedHash
                  << " bytes=0x" << result.bytesWritten
                  << " bytesRead=0x" << result.bytesRead
                  << " outputHash=0x" << outputHash << std::dec
                  << " status=" << assets::wadDecompressStatusName(result.status)
                  << " oracle="
                  << (bootOracleKnown ? (bootOracleMatch ? "match" : "mismatch")
                                      : "unknown")
                  << '\n';
    }

    // FUN_0020b618 returns the number of decompressed bytes in v0. No PS2 DMAC,
    // scratchpad state, or polling side effect is part of the game-visible
    // semantic contract now that OpenRatchet owns this function.
    returnToGuestCaller(ctx, static_cast<std::uint32_t>(result.bytesWritten));
}

} // namespace

void declareNativeAssetReplacements(runtime::NativeReplacementRegistry& registry) {
    registry.add(0x20b618u,
                 "native.assets.decompress-wad",
                 runtime::NativeReplacementStage::Runtime,
                 nativeDecompressWad);
}

} // namespace ratchet::game
