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
constexpr std::array<VerifiedCallBehavior, 7> kVerifiedCallBehaviors{{
    {0x80000592u, 0x00u, 0x10u, 0u, 0u, {1u, 0x21du, 0x21du, 0u}},
    {0x8000059au, 0x00u, 0x04u, 0u, 0u, {2u, 0u, 0u, 0u}},
    {0x80000593u, 0x22u, 0x04u, 0u, 0u, {1u, 0u, 0u, 0u}},
    {0x80000593u, 0x04u, 0x04u, 0u, 0u, {0u, 0u, 0u, 0u}},
    {0x80000595u, 0x0eu, 0x04u, 0u, 0u, {2u, 0u, 0u, 0u}},
    {0x80000003u, 0x01u, 0x04u, 0x04u, 0x1999u, {0x53300u, 0u, 0u, 0u}},
    {0x80000003u, 0x02u, 0x04u, 0x04u, 0x53300u, {0u, 0u, 0u, 0u}},
}};
}  // namespace

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
    uint32_t serviceId = 0u;
    for (const Binding& binding : bindings_) {
        if (binding.clientAddress == clientAddress) {
            serviceId = binding.serviceId;
            break;
        }
    }

    if (receiveBuffer == 0u) {
        return {};
    }
    for (const VerifiedCallBehavior& behavior : kVerifiedCallBehaviors) {
        const OutboundPayload* outboundPayload = nullptr;
        if (behavior.requestSize != 0u) {
            for (const OutboundPayload& payload : outboundPayloads_) {
                if (payload.remoteAddress == remoteSendBuffer && payload.size == sendSize) {
                    outboundPayload = &payload;
                    break;
                }
            }
        }
        if (behavior.serviceId == serviceId && behavior.function == function &&
            behavior.receiveSize == receiveSize &&
            (behavior.requestSize == 0u ||
             (sendSize == behavior.requestSize && outboundPayload != nullptr &&
              outboundPayload->payloadWords[0] == behavior.requestWord0))) {
            return {true, behavior.serviceId, behavior.receiveSize, behavior.payloadWords};
        }
    }
    return {};
}

void SifRpcTransport::reset() {
    bindings_ = {};
    outboundPayloads_ = {};
}

}  // namespace ratchet
