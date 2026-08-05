#include "sif_rpc_transport.h"

namespace ratchet {
namespace {
constexpr uint32_t kCdvdInitService = 0x80000592u;
constexpr uint32_t kCdvdInitPayloadSize = 0x10u;
constexpr uint32_t kCdvdDiskReadyService = 0x8000059au;
constexpr uint32_t kCdvdDiskReadyPayloadSize = 0x4u;

SifRpcCallResponse resolveCdvdInit(uint32_t function,
                                   uint32_t receiveBuffer,
                                   uint32_t receiveSize) {
    if (function != 0u || receiveBuffer == 0u || receiveSize != kCdvdInitPayloadSize) {
        return {};
    }

    // PCSX2 IOP handler 0x0003b094 returns these four words. The two 0x21d
    // values are the versions reported by the loaded cdvd_ee_driver and
    // cdvd_driver modules; the final word is the handler's non-verbose mode.
    // Replace these target-BIOS compatibility values when native IOP module
    // execution supplies the service response directly.
    return {
        true,
        kCdvdInitService,
        kCdvdInitPayloadSize,
        {1u, 0x21du, 0x21du, 0u},
    };
}

SifRpcCallResponse resolveCdvdDiskReady(uint32_t function,
                                        uint32_t receiveBuffer,
                                        uint32_t receiveSize) {
    if (function != 0u || receiveBuffer == 0u || receiveSize != kCdvdDiskReadyPayloadSize) {
        return {};
    }

    // PCSX2 PINE/DebugServer capture at generated call 0x121304: the bound
    // service 0x8000059a receives function 0 with a four-byte buffer at
    // 0x1324c0 and changes its first word from 1 to 2. This is CDVD service
    // semantics, not a packet-specific completion; native IOP execution will
    // replace the compatibility provider when it owns this response.
    return {
        true,
        kCdvdDiskReadyService,
        kCdvdDiskReadyPayloadSize,
        {2u, 0u, 0u, 0u},
    };
}
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

    if (serviceId == kCdvdInitService) {
        return resolveCdvdInit(function, receiveBuffer, receiveSize);
    }
    if (serviceId == kCdvdDiskReadyService) {
        return resolveCdvdDiskReady(function, receiveBuffer, receiveSize);
    }
    return {};
}

void SifRpcTransport::reset() {
    bindings_ = {};
}

}  // namespace ratchet
