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

// A synthetic RPC completion is valid only after the transport matched a
// verified call shape. Zero-size receives still need that proof; otherwise an
// unsupported no-output call must remain pending.
inline bool canCompleteSifRpcCallWithoutPayload(uint32_t receiveBuffer,
                                                uint32_t receiveSize,
                                                bool responseVerified) {
    if (receiveSize == 0u) {
        return responseVerified;
    }
    return responseVerified && receiveBuffer != 0u;
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
    bool requestPayloadAllZero = false;
    bool zeroFillPayload = false;
};

// Tracks the service bound to each EE RPC client and routes calls to a
// service-level payload provider. This keeps packet completion separate from
// service semantics and allows unsupported calls to remain pending.
class SifRpcTransport {
public:
    void recordBinding(uint32_t clientAddress, uint32_t serviceId);
    void recordOutboundPayload(uint32_t remoteAddress,
                               uint32_t size,
                               const std::array<uint32_t, 4>& payloadWords,
                               bool payloadAllZero = false);
    SifRpcCallResponse resolveCall(uint32_t clientAddress,
                                  uint32_t function,
                                  uint32_t receiveBuffer,
                                  uint32_t receiveSize,
                                  uint32_t remoteSendBuffer = 0u,
                                  uint32_t sendSize = 0u);
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
        bool allBytesZero = false;
    };

    std::array<Binding, 16> bindings_{};
    std::array<OutboundPayload, 32> outboundPayloads_{};
    uint32_t iopHeapActiveAddress_ = 0u;
    uint32_t iopHeapActiveSize_ = 0u;
};

inline bool makeSifRpcCompletionSizeWord(uint32_t payloadSize, uint32_t& word) {
    if (payloadSize > 0x00ffffffu) {
        return false;
    }
    // PCSX2 callback captures encode CALL payload length in bits 8..31 only:
    // 4 bytes -> 0x400 and 0x400 bytes -> 0x40000. The 0x40 control header
    // belongs to a zero-payload BIND descriptor, not to a CALL completion.
    word = payloadSize << 8u;
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
    const bool isBind = packetCommand == 0x80000009u;
    uint32_t sizeWord = isBind ? 0x40u : 0u;
    if (!isBind && !makeSifRpcCompletionSizeWord(payloadSize, sizeWord)) {
        sizeWord = 0u;
    }

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

// The root bridge writes directly into the queue consumed by generated
// FUN_0011a948. That queue's low byte is a 16-byte packet count, so the
// 64-byte ingress packet must retain 0x40 even though the later callback
// descriptor encodes a CALL payload as size << 8. Native IOP/DMA ownership
// should remove this representation boundary.
inline std::array<uint32_t, 17> makeSifRpcIngressPacket(
    uint32_t payloadSize,
    uint32_t receiveBuffer,
    uint32_t packetCommand,
    uint32_t requestPacket,
    uint32_t clientAddress,
    bool bindCompleted,
    uint32_t bindResult0,
    uint32_t bindResult1) {
    auto packet = makeSifRpcResponsePacket(
        payloadSize, receiveBuffer, packetCommand, requestPacket, clientAddress,
        bindCompleted, bindResult0, bindResult1);
    packet[0] = 0x40u;
    return packet;
}

}  // namespace ratchet
