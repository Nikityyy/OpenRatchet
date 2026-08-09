#include "sif_rpc_transport.h"

#include <array>

namespace ratchet {
namespace {
struct VerifiedCallBehavior {
    uint32_t serviceId;
    uint32_t function;
    uint32_t receiveSize;
    uint32_t requestSize;
    bool requireRequestWords;
    uint32_t requestWord0;
    std::array<uint32_t, 4> payloadWords;
    bool requireAllZeroRequest = false;
    bool zeroFillResponse = false;
};

// Each row is service-level compatibility evidence, not a packet/address
// bypass. PCSX2 proved, in order: CDVD init at IOP 0x3b094; DiskReady at EE
// 0x121304; startup service calls at EE 0x1213f8, 0x120be4, and 0x12167c.
// Native IOP execution should eventually replace this table by supplying the
// responses.
constexpr std::array<VerifiedCallBehavior, 10> kVerifiedCallBehaviors{{
    {0x80000592u, 0x00u, 0x10u, 0u, false, 0u, {1u, 0x21du, 0x21du, 0u}},
    {0x8000059au, 0x00u, 0x04u, 0u, false, 0u, {2u, 0u, 0u, 0u}},
    {0x80000593u, 0x22u, 0x04u, 0u, false, 0u, {1u, 0u, 0u, 0u}},
    {0x80000593u, 0x04u, 0x04u, 0u, false, 0u, {0u, 0u, 0u, 0u}},
    {0x80000595u, 0x0eu, 0x04u, 0u, false, 0u, {2u, 0u, 0u, 0u}},
    // PCSX2 callback capture: function 1 sends 24 bytes and returns no data;
    // the callback clears client 0x132490 only after this descriptor. Native
    // preserves the service/function/length shape but owns its request words.
    {0x80000595u, 0x01u, 0u, 0x18u, false, 0u, {0u, 0u, 0u, 0u}},
    // PCSX2 callback capture: service 0x80000400 function 1 sends 0x30
    // zeroed bytes and returns a four-byte zero result to client 0x159a00.
    {0x80000400u, 0x01u, 0x04u, 0x30u, true, 0u, {0u, 0u, 0u, 0u}},
    // PCSX2 startup capture: client 0x158400, function 0xff, receive 0x158200
    // (4 bytes). The IOP also leaves three service-private words beyond the
    // declared four-byte SIF transfer; the caller consumes only this first word.
    {0x80000006u, 0xffu, 0x04u, 0u, false, 0u, {0x30343532u, 0u, 0u, 0u}},
    // PCSX2 startup capture at EE 0x11cd2c: a 0x200-byte request transported
    // to the bound remote buffer begins with 0x53300 and returns {0x19, 0}.
    {0x80000006u, 0x06u, 0x08u, 0x200u, true, 0x53300u, {0x19u, 0u, 0u, 0u}},
    // One reset boot captured the complete version query at EE 0x1244f0:
    // service 0x80000900 function 0x80000963 sends 0x400 zero bytes and
    // receives 0x0202 followed by 0x3fc zero bytes. Generated 0x124600 then
    // requires the packed major version (bits 8..15) to equal 2.
    {0x80000900u, 0x80000963u, 0x400u, 0x400u, false, 0u,
     {0x00000202u, 0u, 0u, 0u}, true, true},
}};

// Generated FUN_0011c8c8 and FUN_0011c938 are the EE wrappers for the
// IOP Heap_lib allocation/free RPC service 0x80000003. PCSX2 proved the first
// allocation returns this base and that function 2 frees the returned pointer.
// The startup path has one live allocation at a time, so reuse the verified
// base only after its matching free. Replace this conservative model when
// native IOP/Heap_lib execution owns the service.
constexpr uint32_t kVerifiedIopHeapBase = 0x00053300u;
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
    const std::array<uint32_t, 4>& payloadWords,
    bool payloadAllZero) {
    if (remoteAddress == 0u || size == 0u) {
        return;
    }

    OutboundPayload* freePayload = nullptr;
    for (OutboundPayload& payload : outboundPayloads_) {
        if (payload.remoteAddress == remoteAddress) {
            payload = {remoteAddress, size, payloadWords, payloadAllZero};
            return;
        }
        if (freePayload == nullptr && payload.remoteAddress == 0u) {
            freePayload = &payload;
        }
    }
    if (freePayload != nullptr) {
        *freePayload = {remoteAddress, size, payloadWords, payloadAllZero};
    }
}

SifRpcCallResponse SifRpcTransport::resolveCall(uint32_t clientAddress,
                                                uint32_t function,
                                                uint32_t receiveBuffer,
                                                uint32_t receiveSize,
                                                uint32_t remoteSendBuffer,
                                                uint32_t sendSize) {
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
    if (receiveSize != 0u && receiveBuffer == 0u) {
        response.disposition = SifRpcCallDisposition::MissingReceiveBuffer;
        return response;
    }

    // Request capture is transport evidence, independent of whether service
    // semantics are implemented. Keep it available on unsupported calls so a
    // new RPC shape can be characterized without completing it speculatively.
    const OutboundPayload* outboundPayload = nullptr;
    if (remoteSendBuffer != 0u) {
        for (const OutboundPayload& payload : outboundPayloads_) {
            if (payload.remoteAddress == remoteSendBuffer) {
                outboundPayload = &payload;
                response.requestPayloadAvailable = true;
                response.requestPayloadSize = payload.size;
                response.requestPayloadWords = payload.payloadWords;
                response.requestPayloadAllZero = payload.allBytesZero;
                break;
            }
        }
    }

    response.disposition = SifRpcCallDisposition::UnsupportedShape;
    if (response.serviceId == 0x80000003u && (function == 1u || function == 2u)) {
        if (receiveSize != 4u) {
            return response;
        }
        if (sendSize != 4u) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            return response;
        }

        if (outboundPayload == nullptr) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMissing;
            return response;
        }

        if (outboundPayload->size != sendSize) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            return response;
        }

        const uint32_t requestWord = outboundPayload->payloadWords[0];
        if (function == 1u) {
            if (requestWord == 0u || iopHeapActiveAddress_ != 0u) {
                return response;
            }
            iopHeapActiveAddress_ = kVerifiedIopHeapBase;
            iopHeapActiveSize_ = requestWord;
            response.payloadWords[0] = iopHeapActiveAddress_;
        } else {
            if (requestWord == 0u || requestWord != iopHeapActiveAddress_) {
                response.disposition = SifRpcCallDisposition::RequestPayloadMismatch;
                return response;
            }
            iopHeapActiveAddress_ = 0u;
            iopHeapActiveSize_ = 0u;
            response.payloadWords[0] = 0u;
        }

        response.completed = true;
        response.payloadSize = 4u;
        response.disposition = SifRpcCallDisposition::Completed;
        return response;
    }

    for (const VerifiedCallBehavior& behavior : kVerifiedCallBehaviors) {
        if (behavior.serviceId != response.serviceId || behavior.function != function ||
            behavior.receiveSize != receiveSize) {
            continue;
        }

        if (behavior.requestSize == 0u) {
            response.completed = true;
            response.payloadSize = behavior.receiveSize;
            response.payloadWords = behavior.payloadWords;
            response.zeroFillPayload = behavior.zeroFillResponse;
            response.disposition = SifRpcCallDisposition::Completed;
            return response;
        }

        if (sendSize != behavior.requestSize) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            continue;
        }

        if (outboundPayload == nullptr) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMissing;
            continue;
        }

        if (outboundPayload->size != sendSize) {
            response.disposition = SifRpcCallDisposition::RequestSizeMismatch;
            continue;
        }
        if (behavior.requireRequestWords &&
            outboundPayload->payloadWords[0] != behavior.requestWord0) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMismatch;
            continue;
        }
        if (behavior.requireAllZeroRequest && !outboundPayload->allBytesZero) {
            response.disposition = SifRpcCallDisposition::RequestPayloadMismatch;
            continue;
        }

        response.completed = true;
        response.payloadSize = behavior.receiveSize;
        response.payloadWords = behavior.payloadWords;
        response.zeroFillPayload = behavior.zeroFillResponse;
        response.disposition = SifRpcCallDisposition::Completed;
        return response;
    }
    if (receiveSize == 0u) {
        response.disposition = SifRpcCallDisposition::NoResponsePayloadRequired;
    }
    return response;
}

void SifRpcTransport::reset() {
    bindings_ = {};
    outboundPayloads_ = {};
    iopHeapActiveAddress_ = 0u;
    iopHeapActiveSize_ = 0u;
}

}  // namespace ratchet
