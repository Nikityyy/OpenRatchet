#pragma once

#include <array>
#include <cstdint>

namespace ratchet {

enum class SifRpcCallDisposition {
    Completed,
    UnboundClient,
    NoResponsePayloadRequired,
    MissingReceiveBuffer,
    UnsupportedShape,
    RequestSizeMismatch,
    RequestPayloadMissing,
    RequestPayloadMismatch,
};

const char* sifRpcCallDispositionName(SifRpcCallDisposition disposition);

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
    SifRpcCallDisposition disposition = SifRpcCallDisposition::UnboundClient;
    bool requestPayloadAvailable = false;
    uint32_t requestPayloadSize = 0u;
    std::array<uint32_t, 4> requestPayloadWords{};
};

// Tracks the service bound to each EE RPC client and routes calls to a
// service-level payload provider. This keeps packet completion separate from
// service semantics and allows unsupported calls to remain pending.
class SifRpcTransport {
public:
    void recordBinding(uint32_t clientAddress, uint32_t serviceId);
    void recordOutboundPayload(uint32_t remoteAddress,
                               uint32_t size,
                               const std::array<uint32_t, 4>& payloadWords);
    SifRpcCallResponse resolveCall(uint32_t clientAddress,
                                  uint32_t function,
                                  uint32_t receiveBuffer,
                                  uint32_t receiveSize,
                                  uint32_t remoteSendBuffer = 0u,
                                  uint32_t sendSize = 0u) const;
    void reset();

private:
    struct Binding {
        uint32_t clientAddress = 0u;
        uint32_t serviceId = 0u;
    };

    struct OutboundPayload {
        uint32_t remoteAddress = 0u;
        uint32_t size = 0u;
        std::array<uint32_t, 4> payloadWords{};
    };

    std::array<Binding, 16> bindings_{};
    std::array<OutboundPayload, 32> outboundPayloads_{};
};

inline bool makeSifRpcCompletionSizeWord(uint32_t payloadSize, uint32_t& word) {
    if (payloadSize > 0x00ffffffu) {
        return false;
    }
    word = (payloadSize << 8u) | 0x40u;
    return true;
}

// `0x80000008` is the EE-side response descriptor consumed by the original
// SIF command receiver. PCSX2 captures at `0x11a948 -> 0x11ade0` show CALL
// descriptors carry a zero word 9; completion is signalled by the callback,
// which clears the client's active-packet word. Bind descriptors retain their
// separately observed completion/result fields.
inline std::array<uint32_t, 17> makeSifRpcResponsePacket(
    uint32_t payloadSize,
    uint32_t receiveBuffer,
    uint32_t packetCommand,
    uint32_t requestPacket,
    uint32_t clientAddress,
    bool bindCompleted,
    uint32_t bindResult0,
    uint32_t bindResult1) {
    uint32_t sizeWord = 0x40u;
    if (!makeSifRpcCompletionSizeWord(payloadSize, sizeWord)) {
        sizeWord = 0x40u;
    }

    const bool isBind = packetCommand == 0x80000009u;
    return {
        sizeWord,
        payloadSize != 0u ? receiveBuffer : 0u,
        0x80000008u,
        0u,
        0u,
        isBind ? requestPacket : 0u,
        0u,
        clientAddress,
        packetCommand,
        isBind && bindCompleted ? 1u : 0u,
        isBind ? bindResult0 : 0u,
        isBind ? bindResult1 : 0u,
        0u, 0u, 0u, 0u, 0u,
    };
}

}  // namespace ratchet
