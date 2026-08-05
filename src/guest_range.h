#pragma once

#include <cstddef>
#include <cstdint>

namespace ratchet {

// Returns true only when [offset, offset + byteCount) is wholly within a
// host-owned buffer. The subtraction form avoids a guest-controlled addition
// overflowing before the comparison.
constexpr bool isRangeWithin(uint32_t offset, size_t byteCount, size_t capacity) {
    return offset <= capacity && byteCount <= capacity - offset;
}

}  // namespace ratchet
