#include "sif_rpc_transport.h"

#include <array>

namespace ratchet {
namespace {
struct VerifiedCallBehavior {
    uint32_t serviceId;
    uint32_t function;
    uint32_t receiveSize;
    std::array<uint32_t, 4> payloadWords;
};

// Each row is service-level compatibility evidence, not a packet/address
// bypass. PCSX2 proved, in order: CDVD init at IOP 0x3b094; DiskReady at EE
// 0x121304; startup service calls at EE 0x1213f8 and 0x120be4. Native IOP
// execution should eventually replace this table by supplying the responses.
constexpr std::array<VerifiedCallBehavior, 4> kVerifiedCallBehaviors{{
    {0x80000592u, 0x00u, 0x10u, {1u, 0x21du, 0x21du, 0u}},
    {0x8000059au, 0x00u, 0x04u, {2u, 0u, 0u, 0u}},
    {0x80000593u, 0x22u, 0x04u, {1u, 0u, 0u, 0u}},
    {0x80000595u, 0x0eu, 0x04u, {2u, 0u, 0u, 0u}},
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

SifRpcCallResponse SifRpcTransport::resolveCall(uint32_t clientAddress,
                                                uint32_t function,
                                                uint32_t receiveBuffer,
                                                uint32_t receiveSize) const {
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
        if (behavior.serviceId == serviceId && behavior.function == function &&
            behavior.receiveSize == receiveSize) {
            return {true, behavior.serviceId, behavior.receiveSize, behavior.payloadWords};
        }
    }
    return {};
}

void SifRpcTransport::reset() {
    bindings_ = {};
}

}  // namespace ratchet
