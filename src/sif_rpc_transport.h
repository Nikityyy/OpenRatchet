#pragma once

#include <array>
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

struct SifRpcCallResponse {
    bool completed = false;
    uint32_t serviceId = 0u;
    uint32_t payloadSize = 0u;
    std::array<uint32_t, 4> payloadWords{};
};

// Tracks the service bound to each EE RPC client and routes calls to a
// service-level payload provider. This keeps packet completion separate from
// service semantics and allows unsupported calls to remain pending.
class SifRpcTransport {
public:
    void recordBinding(uint32_t clientAddress, uint32_t serviceId);
    SifRpcCallResponse resolveCall(uint32_t clientAddress,
                                  uint32_t function,
                                  uint32_t receiveBuffer,
                                  uint32_t receiveSize) const;
    void reset();

private:
    struct Binding {
        uint32_t clientAddress = 0u;
        uint32_t serviceId = 0u;
    };

    std::array<Binding, 16> bindings_{};
};

inline bool makeSifRpcCompletionSizeWord(uint32_t payloadSize, uint32_t& word) {
    if (payloadSize > 0x00ffffffu) {
        return false;
    }
    word = (payloadSize << 8u) | 0x40u;
    return true;
}

}  // namespace ratchet
