#pragma once

#include <cstdint>

namespace ratchet {

// A synthetic RPC completion with no payload is only valid for calls that do
// not request receive data. A real transport can complete data-bearing calls
// once it has both the payload and a destination buffer.
inline bool canCompleteSifRpcCallWithoutPayload(uint32_t receiveBuffer,
                                                uint32_t receiveSize,
                                                bool payloadAvailable) {
    if (receiveSize == 0u) {
        return true;
    }
    return payloadAvailable && receiveBuffer != 0u;
}

}  // namespace ratchet
