#include "game/native_assets.h"

#include "assets/wad_decompressor.h"
#include "guest_range.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace ratchet::game {
namespace {

constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
constexpr std::uint32_t kPhysicalAddressMask = 0x1fffffffu;
constexpr std::size_t kMaxShadowOutputBytes = 0x01000000u;

// Independent Phase-3 oracle for the target retail R&C1 boot WAD (WAD2/0).
// These values were established from the extracted retail asset and an
// independently implemented Ratchet WAD reference decoder, not from the legacy
// SPR/DMAC bridge.  The legacy bridge is known to overrun the authentic result
// and is therefore diagnostic-only during this gate.
constexpr std::uint32_t kBootEncodedSize = 0x00050e0fu;
constexpr std::uint32_t kBootEncodedHash = 0xb90bb3e3u;
constexpr std::size_t kBootOutputBytes = 0x000a346cu;
constexpr std::uint32_t kBootOutputHash = 0xd3cb9822u;

bool resolveGuestRamAddress(std::uint32_t address,
                            std::uint32_t& physical) noexcept {
    physical = address & kPhysicalAddressMask;
    return physical < kGuestRamBytes;
}

std::uint32_t fnv1a32(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

const char* oracleName(bool known, bool match) noexcept {
    if (!known) {
        return "unknown";
    }
    return match ? "match" : "mismatch";
}

} // namespace

void validateNativeWadDecompressorShadow(std::uint8_t* rdram,
                                         std::uint32_t inputAddress,
                                         std::uint32_t outputAddress,
                                         std::uint32_t legacyBytes) {
    static std::uint32_t comparisonCount = 0u;
    static std::uint32_t skippedCount = 0u;

    std::uint32_t inputPhysical = 0u;
    std::uint32_t outputPhysical = 0u;
    const bool inputInRam = resolveGuestRamAddress(inputAddress, inputPhysical);
    const bool legacyOutputInRam =
        resolveGuestRamAddress(outputAddress, outputPhysical) &&
        legacyBytes <= kMaxShadowOutputBytes &&
        isRangeWithin(outputPhysical, legacyBytes, kGuestRamBytes);

    if (!inputInRam) {
        ++skippedCount;
        if (skippedCount <= 4u) {
            std::cerr << "[OpenRatchet:WAD] shadow skipped=" << skippedCount
                      << " reason=input-range"
                      << " input=0x" << std::hex << inputAddress
                      << " output=0x" << outputAddress
                      << " legacyBytes=0x" << legacyBytes << std::dec << '\n';
        }
        return;
    }
    if (comparisonCount >= 8u) {
        return;
    }
    ++comparisonCount;

    const std::span<const std::uint8_t> encoded(
        rdram + inputPhysical,
        static_cast<std::size_t>(kGuestRamBytes - inputPhysical));
    std::vector<std::uint8_t> nativeOutput(kMaxShadowOutputBytes, 0u);
    const assets::WadDecompressResult result =
        assets::decompressWad(encoded, nativeOutput);

    const std::size_t encodedHashBytes =
        result.encodedSize <= encoded.size() ? result.encodedSize : 0u;
    const std::uint32_t encodedHash =
        encodedHashBytes != 0u
            ? fnv1a32(encoded.first(encodedHashBytes))
            : 0u;
    const std::uint32_t nativeHash =
        result.bytesWritten <= nativeOutput.size()
            ? fnv1a32(std::span<const std::uint8_t>(nativeOutput.data(),
                                                    result.bytesWritten))
            : 0u;

    const bool oracleKnown =
        result.encodedSize == kBootEncodedSize && encodedHash == kBootEncodedHash;
    const bool oracleMatch = oracleKnown && result.ok() &&
                             result.bytesWritten == kBootOutputBytes &&
                             nativeHash == kBootOutputHash;

    bool legacyMatch = false;
    std::size_t firstDifferenceOffset = 0u;
    std::uint32_t legacyHash = 0u;
    if (legacyOutputInRam && result.bytesWritten <= nativeOutput.size()) {
        const std::span<const std::uint8_t> legacyOutput(
            rdram + outputPhysical,
            static_cast<std::size_t>(legacyBytes));
        legacyHash = fnv1a32(legacyOutput);
        const std::size_t comparableBytes =
            std::min<std::size_t>(legacyBytes, result.bytesWritten);
        const auto firstDifference = std::mismatch(
            nativeOutput.begin(), nativeOutput.begin() + comparableBytes,
            legacyOutput.begin());
        firstDifferenceOffset =
            static_cast<std::size_t>(firstDifference.first - nativeOutput.begin());
        legacyMatch = result.ok() && result.bytesWritten == legacyBytes &&
                      firstDifferenceOffset == comparableBytes;
    }

    std::cerr << "[OpenRatchet:WAD] shadow count=" << comparisonCount
              << " input=0x" << std::hex << inputAddress
              << " output=0x" << outputAddress
              << " encodedSize=0x" << result.encodedSize
              << " encodedHash=0x" << encodedHash
              << " legacyBytes=0x" << legacyBytes
              << " nativeBytes=0x" << result.bytesWritten
              << " bytesRead=0x" << result.bytesRead
              << " firstDiff=0x" << firstDifferenceOffset
              << " legacyHash=0x" << legacyHash
              << " nativeHash=0x" << nativeHash
              << std::dec
              << " status=" << assets::wadDecompressStatusName(result.status)
              << " oracle=" << oracleName(oracleKnown, oracleMatch)
              << " legacyMatch=" << (legacyMatch ? 1 : 0) << '\n';
}

} // namespace ratchet::game
