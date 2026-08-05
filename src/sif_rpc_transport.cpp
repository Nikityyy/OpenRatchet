#include "sif_rpc_transport.h"

#include <array>

namespace ratchet {
namespace {
struct VerifiedCallBehavior {
    uint32_t serviceId;
    uint32_t function;
    uint32_t receiveSize;
    uint32_t requestSize;
    uint32_t requestWord0;
    std::array<uint32_t, 4> payloadWords;
};

// Each row is service-level compatibility evidence, not a packet/address
// bypass. PCSX2 proved, in order: CDVD init at IOP 0x3b094; DiskReady at EE
// 0x121304; startup service calls at EE 0x1213f8, 0x120be4, and 0x12167c.
// Native IOP execution should eventually replace this table by supplying the
// responses.
constexpr std::array<VerifiedCallBehavior, 9> kVerifiedCallBehaviors{{
    {0x80000592u, 0x00u, 0x10u, 0u, 0u, {1u, 0x21du, 0x21du, 0u}},
    {0x8000059au, 0x00u, 0x04u, 0u, 0u, {2u, 0u, 0u, 0u}},
    {0x80000593u, 0x22u, 0x04u, 0u, 0u, {1u, 0u, 0u, 0u}},
    {0x80000593u, 0x04u, 0x04u, 0u, 0u, {0u, 0u, 0u, 0u}},
    {0x80000595u, 0x0eu, 0x04u, 0u, 0u, {2u, 0u, 0u, 0u}},
    // PCSX2 startup capture: client 0x158400, function 0xff, receive 0x158200
    // (4 bytes). The IOP also leaves three service-private words beyond the
    // declared four-byte SIF transfer; the caller consumes only this first word.
    {0x80000006u, 0xffu, 0x04u, 0u, 0u, {0x30343532u, 0u, 0u, 0u}},
    // PCSX2 startup capture at EE 0x11cd2c: a 0x200-byte request transported
    // to the bound remote buffer begins with 0x53300 and returns {0x19, 0}.
    {0x80000006u, 0x06u, 0x08u, 0x200u, 0x53300u, {0x19u, 0u, 0u, 0u}},
    {0x80000003u, 0x01u, 0x04u, 0x04u, 0x1999u, {0x53300u, 0u, 0u, 0u}},
    {0x80000003u, 0x02u, 0x04u, 0x04u, 0x53300u, {0u, 0u, 0u, 0u}},
}};
}  // namespace

const char* sifRpcCallDispositionName(SifRpcCallDisposition disposition) {
    switch (disposition) {
    case SifRpcCallDisposition::Completed:
        return "matched";
    case SifRpcCallDisposition::UnboundClient:
        return "unbound-client";
    case SifRpcCallDisposition::NoResponsePayloadRequired:
        return "no-response-payload";
    case SifRpcCallDisposition::MissingReceiveBuffer:
        return "missing-receive-buffer";
    case SifRpcCallDisposition::UnsupportedShape:
        return "unsupported-shape";
    case SifRpcCallDisposition::RequestSizeMismatch:
        return "request-size-mismatch";
    case SifRpcCallDisposition::RequestPayloadMissing:
        return "request-payload-missing";
    case SifRpcCallDisposition::RequestPayloadMismatch:
        return "request-payload-mismatch";
    }
    return "unknown";
}

void SifRpcTransport::recordBinding(uint32_t clientAddress, uint32_t serviceId) {
    if (clientAddress == 0u || serviceId == 0u) {
        return;
    }

    Binding* freeBinding = nullptr;
    for (Binding& binding : bindings_) {
        if (binding.clientAddress == clientAddress) {
            binding.serviceId = serviceId;
            return;
        }
        if (freeBinding == nullptr && binding.clientAddress == 0u) {
            freeBinding = &binding;
        }
    }
    if (freeBinding != nullptr) {
        *freeBinding = {clientAddress, serviceId};
    }
}

void SifRpcTransport::recordOutboundPayload(
    uint32_t remoteAddress,
    uint32_t size,
    const std::array<uint32_t, 4>& payloadWords) {
    if (remoteAddress == 0u || size == 0u) {
        return;
    }

    OutboundPayload* freePayload = nullptr;
    for (OutboundPayload& payload : outboundPayloads_) {
        if (payload.remoteAddress == remoteAddress) {
            payload = {remoteAddress, size, payloadWords};
            return;
        }
        if (freePayload == nullptr && payload.remoteAddress == 0u) {
            freePayload = &payload;
        }
    }
    if (freePayload != nullptr) {
        *freePayload = {remoteAddress, size, payloadWords};
    }
}

SifRpcCallResponse SifRpcTransport::resolveCall(uint32_t clientAddress,
                                                uint32_t function,
                                                uint32_t receiveBuffer,
                                                uint32_t receiveSize,
                                                uint32_t remoteSendBuffer,
                                                uint32_t sendSize) const {
    SifRpcCallResponse response;
    for (const Binding& binding : bindings_) {
        if (binding.clientAddress == clientAddress) {
            response.serviceId = binding.serviceId;
            break;
        }
    }

    if (response.serviceId == 0u) {
        return response;
    }
    if (receiveSize == 0u) {
        response.disposition = SifRpcCallDisposition::NoResponsePayloadRequired;
        return response;
    }
    if (receiveBuffer == 0u) {
        response.disposition = SifRpcCallDisposition::MissingReceiveBuffer;
        return response;
    }

    response.disposition = SifRpcCallDisposition::UnsupportedShape;
    for (const VerifiedCallBehavior& behavior : kVerifiedCallBehaviors) {
        if (behavior.serviceId != response.serviceId || behavior.function != function ||
            behavior.receiveSize != receiveSize) {
            continue;
        }

        if (behavior.requestSize == 0u) {
            response.completed = true;
            response.payloadSize = behavior.receiveSize;
            response.payloadWords = behavior.payloadWords;
            response.disposition = SifRpcCallDisposition::Completed;
            return response;
        }

        if (sendSize != behavior.requestSize) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            continue;
        }

        const OutboundPayload* outboundPayload = nullptr;
        for (const OutboundPayload& payload : outboundPayloads_) {
            if (payload.remoteAddress == remoteSendBuffer) {
                outboundPayload = &payload;
                break;
            }
        }
        if (outboundPayload == nullptr) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMissing;
            continue;
        }

        response.requestPayloadAvailable = true;
        response.requestPayloadSize = outboundPayload->size;
        response.requestPayloadWords = outboundPayload->payloadWords;
        if (outboundPayload->size != sendSize) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            continue;
        }
        if (outboundPayload->payloadWords[0] != behavior.requestWord0) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMismatch;
            continue;
        }

        response.completed = true;
        response.payloadSize = behavior.receiveSize;
        response.payloadWords = behavior.payloadWords;
        response.disposition = SifRpcCallDisposition::Completed;
        return response;
    }
    return response;
}

void SifRpcTransport::reset() {
    bindings_ = {};
    outboundPayloads_ = {};
}

}  // namespace ratchet
