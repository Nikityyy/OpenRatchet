#include "sif_rpc_transport.h"

#include <cstdint>
#include <iostream>

namespace {

struct TestContext {
    int failures = 0;

    void expect(bool condition, const char* description) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << description << '\n';
        }
    }
};

}  // namespace

int main() {
    TestContext test;

    test.expect(ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0u, false),
                "zero receive size completes without payload and with a buffer");
    test.expect(ratchet::canCompleteSifRpcCallWithoutPayload(0u, 0u, false),
                "zero receive size completes without payload and without a buffer");
    test.expect(!ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0x0cu, false),
                "nonzero receive size defers without payload");
    test.expect(!ratchet::canCompleteSifRpcCallWithoutPayload(0u, 0x0cu, true),
                "available payload still requires a receive buffer");
    test.expect(ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0x0cu, true),
                "available payload and valid receive buffer complete");

    uint32_t completionSizeWord = 0u;
    test.expect(ratchet::makeSifRpcCompletionSizeWord(0x10u, completionSizeWord) &&
                    completionSizeWord == 0x1040u,
                "16-byte payload produces the observed SIF completion size word");
    test.expect(!ratchet::makeSifRpcCompletionSizeWord(0x01000000u, completionSizeWord),
                "payload size that cannot fit the SIF header is rejected");

    ratchet::SifRpcTransport transport;
    auto call = transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u);
    test.expect(!call.completed, "unbound client remains pending");

    transport.recordBinding(0x159968u, 0x80000592u);
    call = transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u);
    test.expect(call.completed && call.serviceId == 0x80000592u &&
                    call.payloadSize == 0x10u &&
                    call.payloadWords[0] == 1u &&
                    call.payloadWords[1] == 0x21du &&
                    call.payloadWords[2] == 0x21du &&
                    call.payloadWords[3] == 0u,
                "bound CDVD init call returns the reference service payload");
    test.expect(!transport.resolveCall(0x159968u, 1u, 0x1324c0u, 0x10u).completed,
                "unsupported CDVD function remains pending");
    test.expect(!transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x0cu).completed,
                "mismatched CDVD receive size remains pending");
    test.expect(!transport.resolveCall(0x159968u, 0u, 0u, 0x10u).completed,
                "missing CDVD receive buffer remains pending");

    transport.recordBinding(0x159990u, 0x8000059au);
    call = transport.resolveCall(0x159990u, 0u, 0x1324c0u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x8000059au &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 2u &&
                    call.payloadWords[1] == 0u && call.payloadWords[2] == 0u &&
                    call.payloadWords[3] == 0u,
                "bound CDVD DiskReady call returns the reference four-byte payload");
    test.expect(!transport.resolveCall(0x159990u, 1u, 0x1324c0u, 0x4u).completed,
                "unsupported CDVD DiskReady function remains pending");
    test.expect(!transport.resolveCall(0x159990u, 0u, 0x1324c0u, 0x10u).completed,
                "mismatched CDVD DiskReady receive size remains pending");
    test.expect(!transport.resolveCall(0x159990u, 0u, 0u, 0x4u).completed,
                "missing CDVD DiskReady receive buffer remains pending");

    transport.recordBinding(0x132d08u, 0x80000593u);
    call = transport.resolveCall(0x132d08u, 0x22u, 0x1324c0u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x80000593u &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 1u &&
                    call.payloadWords[1] == 0u && call.payloadWords[2] == 0u &&
                    call.payloadWords[3] == 0u,
                "bound startup service 0x80000593 function 0x22 returns reference payload");
    test.expect(!transport.resolveCall(0x132d08u, 0x16u, 0x1324c0u, 0x4u).completed,
                "unobserved startup service function remains pending");
    test.expect(!transport.resolveCall(0x132d08u, 0x22u, 0x1324c0u, 0x10u).completed,
                "mismatched startup service receive size remains pending");
    test.expect(!transport.resolveCall(0x132d08u, 0x22u, 0u, 0x4u).completed,
                "missing startup service receive buffer remains pending");

    transport.recordBinding(0x132490u, 0x80000595u);
    call = transport.resolveCall(0x132490u, 0x0eu, 0x131340u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x80000595u &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 2u &&
                    call.payloadWords[1] == 0u && call.payloadWords[2] == 0u &&
                    call.payloadWords[3] == 0u,
                "bound startup service 0x80000595 function 0x0e returns reference payload");
    test.expect(!transport.resolveCall(0x132490u, 0x0fu, 0x131340u, 0x4u).completed,
                "unobserved startup service 0x80000595 function remains pending");
    test.expect(!transport.resolveCall(0x132490u, 0x0eu, 0x131340u, 0x10u).completed,
                "mismatched startup service 0x80000595 receive size remains pending");
    test.expect(!transport.resolveCall(0x132490u, 0x0eu, 0u, 0x4u).completed,
                "missing startup service 0x80000595 receive buffer remains pending");

    transport.reset();
    test.expect(!transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u).completed,
                "reset removes recorded bindings");
    test.expect(!transport.resolveCall(0x132d08u, 0x22u, 0x1324c0u, 0x4u).completed,
                "reset removes startup service binding");
    test.expect(!transport.resolveCall(0x132490u, 0x0eu, 0x131340u, 0x4u).completed,
                "reset removes startup service 0x80000595 binding");

    if (test.failures != 0) {
        std::cerr << test.failures << " test check(s) failed\n";
        return 1;
    }
    return 0;
}
