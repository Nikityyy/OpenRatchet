#include "assets/wad_decompressor.h"

#include <cstring>
#include <limits>

namespace ratchet::assets {
namespace {

constexpr std::size_t kHeaderBytes = 0x10u;
constexpr std::size_t kInputBlockBytes = 0x2000u;

std::uint32_t readLe32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[3]) << 24u);
}

struct Decoder {
    std::span<const std::uint8_t> encoded;
    std::span<std::uint8_t> output;
    std::size_t end = 0u;
    std::size_t input = kHeaderBytes;
    std::size_t written = 0u;

    WadDecompressResult result(WadDecompressStatus status,
                               std::uint32_t encodedSize) const noexcept {
        return {status, input, written, encodedSize};
    }

    bool canRead(std::size_t count) const noexcept {
        return input <= end && count <= end - input;
    }

    bool canWrite(std::size_t count) const noexcept {
        return written <= output.size() && count <= output.size() - written;
    }

    bool readByte(std::uint8_t& value) noexcept {
        if (!canRead(1u)) {
            return false;
        }
        value = encoded[input++];
        return true;
    }

    bool copyLiteral(std::size_t count) noexcept {
        if (!canRead(count) || !canWrite(count)) {
            return false;
        }
        if (count != 0u) {
            std::memcpy(output.data() + written, encoded.data() + input, count);
            input += count;
            written += count;
        }
        return true;
    }

    bool copyMatch(std::size_t distance, std::size_t count) noexcept {
        if (distance == 0u || distance > written || !canWrite(count)) {
            return false;
        }

        // Overlap is intentional. The guest routine copies one byte at a time,
        // so repeated patterns can refer to bytes produced by this same match.
        for (std::size_t i = 0u; i < count; ++i) {
            output[written] = output[written - distance];
            ++written;
        }
        return true;
    }

    bool alignToNextInputBlock() noexcept {
        if (input < kHeaderBytes) {
            return false;
        }
        const std::size_t relative = input - kHeaderBytes;
        if (relative > std::numeric_limits<std::size_t>::max() -
                           (kInputBlockBytes - 1u)) {
            return false;
        }
        const std::size_t aligned =
            (relative + (kInputBlockBytes - 1u)) & ~(kInputBlockBytes - 1u);
        const std::size_t next = kHeaderBytes + aligned;
        if (next < input || next > end) {
            return false;
        }
        input = next;
        return true;
    }
};

} // namespace

const char* wadDecompressStatusName(WadDecompressStatus status) noexcept {
    switch (status) {
    case WadDecompressStatus::Ok:
        return "ok";
    case WadDecompressStatus::InputTooSmall:
        return "input-too-small";
    case WadDecompressStatus::InvalidMagic:
        return "invalid-magic";
    case WadDecompressStatus::InvalidEncodedSize:
        return "invalid-encoded-size";
    case WadDecompressStatus::InputExhausted:
        return "input-exhausted";
    case WadDecompressStatus::OutputOverflow:
        return "output-overflow";
    case WadDecompressStatus::InvalidMatch:
        return "invalid-match";
    case WadDecompressStatus::InvalidBlockMarker:
        return "invalid-block-marker";
    }
    return "unknown";
}

WadDecompressResult decompressWad(std::span<const std::uint8_t> encoded,
                                  std::span<std::uint8_t> output) noexcept {
    if (encoded.size() < kHeaderBytes) {
        return {WadDecompressStatus::InputTooSmall, 0u, 0u, 0u};
    }
    if (encoded[0] != 'W' || encoded[1] != 'A' || encoded[2] != 'D') {
        return {WadDecompressStatus::InvalidMagic, 0u, 0u, 0u};
    }

    const std::uint32_t encodedSize = readLe32(encoded.data() + 3u);
    if (encodedSize < kHeaderBytes || encodedSize > encoded.size()) {
        return {WadDecompressStatus::InvalidEncodedSize,
                kHeaderBytes,
                0u,
                encodedSize};
    }

    Decoder decoder{encoded, output, encodedSize, kHeaderBytes, 0u};
    if (decoder.input == decoder.end) {
        return decoder.result(WadDecompressStatus::Ok, encodedSize);
    }

    // 0x20b690: the first token has a special literal-run interpretation when
    // it is greater than 17. If not, the main loop consumes that same token.
    const std::uint8_t first = encoded[decoder.input];
    if (first > 17u) {
        ++decoder.input;
        const std::size_t literalBytes = static_cast<std::size_t>(first - 17u);
        if (!decoder.canRead(literalBytes)) {
            return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
        }
        if (!decoder.canWrite(literalBytes)) {
            return decoder.result(WadDecompressStatus::OutputOverflow, encodedSize);
        }
        decoder.copyLiteral(literalBytes);
    }

    while (decoder.input < decoder.end) {
        std::uint8_t token = 0u;
        if (!decoder.readByte(token)) {
            return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
        }

        // Literal packet. The guest copies token+3 bytes, or next+18 bytes
        // for the zero-extension form.
        if (token < 0x10u) {
            std::size_t literalBytes = 0u;
            if (token != 0u) {
                literalBytes = static_cast<std::size_t>(token) + 3u;
            } else {
                std::uint8_t extension = 0u;
                if (!decoder.readByte(extension)) {
                    return decoder.result(WadDecompressStatus::InputExhausted,
                                          encodedSize);
                }
                literalBytes = static_cast<std::size_t>(extension) + 18u;
            }

            if (!decoder.canRead(literalBytes)) {
                return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
            }
            if (!decoder.canWrite(literalBytes)) {
                return decoder.result(WadDecompressStatus::OutputOverflow, encodedSize);
            }
            decoder.copyLiteral(literalBytes);
            continue;
        }

        std::size_t matchLength = 0u;
        std::size_t matchDistance = 0u;
        std::uint8_t lowOffsetByte = 0u;
        bool blockMarker = false;
        bool noOpEndMarker = false;

        if (token >= 0x40u) {
            // Short match: distance 1..2048, length 3..8.
            std::uint8_t highDistance = 0u;
            if (!decoder.readByte(highDistance)) {
                return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
            }
            lowOffsetByte = token;
            matchDistance = 1u + ((static_cast<std::size_t>(token) >> 2u) & 7u) +
                            (static_cast<std::size_t>(highDistance) << 3u);
            matchLength = (static_cast<std::size_t>(token) >> 5u) + 1u;
        } else if (token >= 0x20u) {
            // Medium match: 14-bit distance plus one, variable length.
            std::size_t lengthCode = static_cast<std::size_t>(token & 0x1fu);
            if (lengthCode == 0u) {
                std::uint8_t extension = 0u;
                if (!decoder.readByte(extension)) {
                    return decoder.result(WadDecompressStatus::InputExhausted,
                                          encodedSize);
                }
                lengthCode = static_cast<std::size_t>(extension) + 31u;
            }

            std::uint8_t low = 0u;
            std::uint8_t high = 0u;
            if (!decoder.readByte(low) || !decoder.readByte(high)) {
                return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
            }
            lowOffsetByte = low;
            const std::size_t encodedDistance =
                (static_cast<std::size_t>(low) >> 2u) +
                (static_cast<std::size_t>(high) << 6u);
            matchDistance = encodedDistance + 1u;
            matchLength = lengthCode + 2u;
        } else {
            // Far match. An encoded zero distance is control flow in the guest
            // decoder: length 1 is a no-op/end marker; other lengths advance to
            // the next 0x2000-byte source block instead of performing a match.
            const std::size_t highDistanceBase =
                static_cast<std::size_t>(token & 0x08u) << 11u;
            std::size_t lengthCode = static_cast<std::size_t>(token & 0x07u);
            if (lengthCode == 0u) {
                std::uint8_t extension = 0u;
                if (!decoder.readByte(extension)) {
                    return decoder.result(WadDecompressStatus::InputExhausted,
                                          encodedSize);
                }
                lengthCode = static_cast<std::size_t>(extension) + 7u;
            }

            std::uint8_t low = 0u;
            std::uint8_t high = 0u;
            if (!decoder.readByte(low) || !decoder.readByte(high)) {
                return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
            }
            lowOffsetByte = low;
            const std::size_t encodedDistance =
                (static_cast<std::size_t>(low) >> 2u) +
                (static_cast<std::size_t>(high) << 6u);

            if (highDistanceBase == 0u && encodedDistance == 0u) {
                noOpEndMarker = lengthCode == 1u;
                blockMarker = !noOpEndMarker;
            } else {
                matchDistance = highDistanceBase + encodedDistance + 0x4000u;
                matchLength = lengthCode + 2u;
            }
        }

        if (blockMarker) {
            if (!decoder.alignToNextInputBlock()) {
                return decoder.result(WadDecompressStatus::InvalidBlockMarker,
                                      encodedSize);
            }
            continue;
        }

        if (!noOpEndMarker) {
            if (matchDistance == 0u || matchDistance > decoder.written) {
                return decoder.result(WadDecompressStatus::InvalidMatch, encodedSize);
            }
            if (!decoder.canWrite(matchLength)) {
                return decoder.result(WadDecompressStatus::OutputOverflow, encodedSize);
            }
            decoder.copyMatch(matchDistance, matchLength);
        }

        const std::size_t trailingLiteralBytes =
            static_cast<std::size_t>(lowOffsetByte & 0x03u);
        if (trailingLiteralBytes != 0u) {
            if (!decoder.canRead(trailingLiteralBytes)) {
                return decoder.result(WadDecompressStatus::InputExhausted, encodedSize);
            }
            if (!decoder.canWrite(trailingLiteralBytes)) {
                return decoder.result(WadDecompressStatus::OutputOverflow, encodedSize);
            }
            decoder.copyLiteral(trailingLiteralBytes);
        }
    }

    return decoder.result(WadDecompressStatus::Ok, encodedSize);
}

} // namespace ratchet::assets
