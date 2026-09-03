#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ratchet::assets {

enum class WadDecompressStatus : std::uint8_t {
    Ok,
    InputTooSmall,
    InvalidMagic,
    InvalidEncodedSize,
    InputExhausted,
    OutputOverflow,
    InvalidMatch,
    InvalidBlockMarker,
};

struct WadDecompressResult {
    WadDecompressStatus status = WadDecompressStatus::InputTooSmall;
    std::size_t bytesRead = 0u;
    std::size_t bytesWritten = 0u;
    std::uint32_t encodedSize = 0u;

    [[nodiscard]] bool ok() const noexcept {
        return status == WadDecompressStatus::Ok;
    }
};

// Decodes the R&C1 WAD compression stream consumed by guest function 0x20b618.
// The first three bytes are "WAD" and the unaligned little-endian u32 at +3
// is the encoded stream size, including the 0x10-byte header. The compressed
// payload uses the game's LZO-family packet grammar and 0x2000-byte source
// blocks. The caller owns both buffers; this routine has no PS2/DMAC state.
WadDecompressResult decompressWad(std::span<const std::uint8_t> encoded,
                                  std::span<std::uint8_t> output) noexcept;

const char* wadDecompressStatusName(WadDecompressStatus status) noexcept;

} // namespace ratchet::assets
