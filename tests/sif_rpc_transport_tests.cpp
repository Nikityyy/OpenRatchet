#include "sif_rpc_transport.h"

#include <cstdint>
#include <iostream>
#include <string>

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

    test.expect(!ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0u, false),
                "unverified zero receive size remains pending with a buffer");
    test.expect(ratchet::canCompleteSifRpcCallWithoutPayload(0u, 0u, true),
                "verified zero receive size completes without a buffer");
    test.expect(!ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0x0cu, false),
                "nonzero receive size defers without payload");
    test.expect(!ratchet::canCompleteSifRpcCallWithoutPayload(0u, 0x0cu, true),
                "available payload still requires a receive buffer");
    test.expect(ratchet::canCompleteSifRpcCallWithoutPayload(0x133100u, 0x0cu, true),
                "available payload and valid receive buffer complete");

    uint32_t completionSizeWord = 0u;
    test.expect(ratchet::makeSifRpcCompletionSizeWord(0x10u, completionSizeWord) &&
                    completionSizeWord == 0x1000u,
                "16-byte CALL payload produces the reference size word without a BIND flag");
    test.expect(ratchet::makeSifRpcCompletionSizeWord(0x400u, completionSizeWord) &&
                    completionSizeWord == 0x40000u,
                "0x400-byte CALL payload matches the repeated IOP-loop descriptor");
    test.expect(!ratchet::makeSifRpcCompletionSizeWord(0x01000000u, completionSizeWord),
                "payload size that cannot fit the SIF header is rejected");

    const auto callResponsePacket = ratchet::makeSifRpcResponsePacket(
        0x4u, 0x131340u, 0x8000000au, 0x20155000u, 0x132490u,
        true, 0xdeadbeefu, 0xcafebabeu);
    test.expect(callResponsePacket[0] == 0x400u &&
                    callResponsePacket[1] == 0x131340u &&
                    callResponsePacket[2] == 0x80000008u &&
                    callResponsePacket[7] == 0x132490u &&
                    callResponsePacket[8] == 0x8000000au &&
                    callResponsePacket[9] == 0u &&
                    callResponsePacket[10] == 0u && callResponsePacket[11] == 0u,
                "CALL response matches the captured descriptor layout and callback-owned completion");

    const auto callIngressPacket = ratchet::makeSifRpcIngressPacket(
        0x4u, 0x131340u, 0x8000000au, 0x20155000u, 0x132490u,
        true, 0xdeadbeefu, 0xcafebabeu);
    test.expect(callIngressPacket[0] == 0x40u &&
                    callIngressPacket[1] == callResponsePacket[1] &&
                    callIngressPacket[2] == callResponsePacket[2] &&
                    callIngressPacket[7] == callResponsePacket[7] &&
                    callIngressPacket[8] == callResponsePacket[8],
                "native bridge ingress retains the command receiver's 64-byte envelope");

    const auto bindResponsePacket = ratchet::makeSifRpcResponsePacket(
        0u, 0u, 0x80000009u, 0x20155000u, 0x158040u,
        true, 0x4f848u, 0x4f890u);
    test.expect(bindResponsePacket[0] == 0x40u &&
                    bindResponsePacket[5] == 0x20155000u &&
                    bindResponsePacket[7] == 0x158040u &&
                    bindResponsePacket[8] == 0x80000009u &&
                    bindResponsePacket[9] == 1u &&
                    bindResponsePacket[10] == 0x4f848u &&
                    bindResponsePacket[11] == 0x4f890u,
                "BIND response retains its request pointer and observed startup result fields");

    ratchet::SifRpcTransport transport;
    auto call = transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u);
    test.expect(!call.completed &&
                    call.disposition == ratchet::SifRpcCallDisposition::UnboundClient,
                "unbound client remains pending with a diagnostic disposition");

    transport.recordBinding(0x159968u, 0x80000592u);
    call = transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u);
    test.expect(call.completed && call.serviceId == 0x80000592u &&
                    call.payloadSize == 0x10u &&
                    call.payloadWords[0] == 1u &&
                    call.payloadWords[1] == 0x21du &&
                    call.payloadWords[2] == 0x21du &&
                    call.payloadWords[3] == 0u,
                "bound CDVD init call returns the reference service payload");
    call = transport.resolveCall(0x159968u, 1u, 0x1324c0u, 0x10u);
    test.expect(!call.completed && call.serviceId == 0x80000592u &&
                    call.disposition == ratchet::SifRpcCallDisposition::UnsupportedShape,
                "unsupported CDVD function retains its bound service and reason");
    test.expect(!transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x0cu).completed,
                "mismatched CDVD receive size remains pending");
    test.expect(!transport.resolveCall(0x159968u, 0u, 0u, 0x10u).completed,
                "missing CDVD receive buffer remains pending");
    call = transport.resolveCall(0x159968u, 1u, 0u, 0u);
    test.expect(!call.completed &&
                    call.disposition ==
                        ratchet::SifRpcCallDisposition::NoResponsePayloadRequired,
                "zero-size response has a distinct transport-completion reason");

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

    call = transport.resolveCall(0x132d08u, 0x04u, 0x1324c0u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x80000593u &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 0u &&
                    call.payloadWords[1] == 0u && call.payloadWords[2] == 0u &&
                    call.payloadWords[3] == 0u,
                "bound startup service 0x80000593 function 0x04 returns the reference zero");
    test.expect(!transport.resolveCall(0x132d08u, 0x04u, 0x1324c0u, 0x10u).completed,
                "startup service function 0x04 with a mismatched receive size remains pending");
    test.expect(!transport.resolveCall(0x132d08u, 0x04u, 0u, 0x4u).completed,
                "startup service function 0x04 without a receive buffer remains pending");

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
    transport.recordOutboundPayload(0x41bd0u, 0x18u, {0u, 0u, 0u, 0u});
    call = transport.resolveCall(0x132490u, 0x01u, 0u, 0u, 0x41bd0u, 0x18u);
    test.expect(call.completed && call.serviceId == 0x80000595u &&
                    call.payloadSize == 0u && call.requestPayloadAvailable &&
                    call.requestPayloadSize == 0x18u &&
                    call.disposition == ratchet::SifRpcCallDisposition::Completed,
                "captured zero-receive service call completes only after its request matches");
    transport.recordOutboundPayload(0x41bd0u, 0x18u, {1u, 0u, 0u, 0u});
    test.expect(transport.resolveCall(0x132490u, 0x01u, 0u, 0u, 0x41bd0u, 0x18u).completed,
                "verified zero-receive service call accepts its runtime-owned request contents");
    test.expect(!transport.resolveCall(0x132490u, 0x01u, 0u, 0u, 0x41bd0u, 0x14u).completed,
                "zero-receive service call rejects a mismatched outbound length");

    transport.recordBinding(0x158400u, 0x80000006u);
    call = transport.resolveCall(0x158400u, 0xffu, 0x158200u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x80000006u &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 0x30343532u &&
                    call.disposition == ratchet::SifRpcCallDisposition::Completed,
                "service 0x80000006 function 0xff returns the captured four-byte result");
    test.expect(!transport.resolveCall(0x158400u, 0xffu, 0x158200u, 0x10u).completed,
                "service 0x80000006 function 0xff rejects a mismatched receive size");
    call = transport.resolveCall(0x158400u, 0x06u, 0x158200u, 0x8u,
                                 0x220d0u, 0x200u);
    test.expect(!call.completed &&
                    call.disposition == ratchet::SifRpcCallDisposition::RequestPayloadMissing,
                "service 0x80000006 function 6 waits for its captured outbound request");
    transport.recordOutboundPayload(0x220d0u, 0x200u,
                                    {0x53300u, 0u, 0x8001f150u, 0x10u});
    call = transport.resolveCall(0x158400u, 0x06u, 0x158200u, 0x8u,
                                 0x220d0u, 0x200u);
    test.expect(call.completed && call.serviceId == 0x80000006u &&
                    call.payloadSize == 0x8u && call.payloadWords[0] == 0x19u &&
                    call.payloadWords[1] == 0u && call.requestPayloadAvailable &&
                    call.requestPayloadSize == 0x200u &&
                    call.requestPayloadWords[0] == 0x53300u,
                "service 0x80000006 function 6 matches the captured 0x200-byte request");
    transport.recordOutboundPayload(0x220d0u, 0x200u,
                                    {0x53301u, 0u, 0x8001f150u, 0x10u});
    call = transport.resolveCall(0x158400u, 0x06u, 0x158200u, 0x8u,
                                 0x220d0u, 0x200u);
    test.expect(!call.completed &&
                    call.disposition == ratchet::SifRpcCallDisposition::RequestPayloadMismatch,
                "service 0x80000006 function 6 rejects an uncaptured request shape");

    transport.recordBinding(0x158040u, 0x80000003u);
    call = transport.resolveCall(0x158040u, 1u, 0x158080u, 0x4u,
                                 0x4f848u, 0x4u);
    test.expect(!call.completed && call.serviceId == 0x80000003u &&
                    call.disposition == ratchet::SifRpcCallDisposition::RequestPayloadMissing,
                "payload-dependent service call reports missing outbound data");
    transport.recordOutboundPayload(0x4f848u, 0x4u, {0x1999u, 0u, 0u, 0u});
    call = transport.resolveCall(0x158040u, 1u, 0x158080u, 0x4u,
                                 0x4f848u, 0x4u);
    test.expect(call.completed && call.serviceId == 0x80000003u &&
                    call.payloadSize == 0x4u && call.payloadWords[0] == 0x53300u &&
                    call.disposition == ratchet::SifRpcCallDisposition::Completed &&
                    call.requestPayloadAvailable && call.requestPayloadSize == 0x4u &&
                    call.requestPayloadWords[0] == 0x1999u,
                "service 0x80000003 function 1 matches the reference request word");
    call = transport.resolveCall(0x158040u, 1u, 0x158080u, 0x4u,
                                 0x4f848u, 0x8u);
    test.expect(!call.completed &&
                    call.disposition == ratchet::SifRpcCallDisposition::RequestSizeMismatch,
                "payload-dependent service call reports a mismatched send size");
    transport.recordOutboundPayload(0x4f848u, 0x4u, {0x1234u, 0u, 0u, 0u});
    call = transport.resolveCall(0x158040u, 1u, 0x158080u, 0x4u,
                                 0x4f848u, 0x4u);
    test.expect(!call.completed && call.requestPayloadAvailable &&
                    call.requestPayloadWords[0] == 0x1234u &&
                    call.disposition == ratchet::SifRpcCallDisposition::RequestPayloadMismatch,
                "payload-dependent service call reports the mismatched request word");
    transport.recordOutboundPayload(0x4f848u, 0x4u, {0x53300u, 0u, 0u, 0u});
    call = transport.resolveCall(0x158040u, 2u, 0x158080u, 0x4u,
                                 0x4f848u, 0x4u);
    test.expect(call.completed && call.payloadSize == 0x4u && call.payloadWords[0] == 0u,
                "service 0x80000003 function 2 matches the chained reference request word");

    transport.reset();
    test.expect(!transport.resolveCall(0x159968u, 0u, 0x1324c0u, 0x10u).completed,
                "reset removes recorded bindings");
    test.expect(!transport.resolveCall(0x132d08u, 0x22u, 0x1324c0u, 0x4u).completed,
                "reset removes startup service binding");
    test.expect(!transport.resolveCall(0x132490u, 0x0eu, 0x131340u, 0x4u).completed,
                "reset removes startup service 0x80000595 binding");
    test.expect(!transport.resolveCall(0x158040u, 1u, 0x158080u, 0x4u,
                                       0x4f848u, 0x4u).completed,
                "reset removes captured outbound payloads");

    test.expect(std::string(ratchet::sifRpcCallDispositionName(
                    ratchet::SifRpcCallDisposition::RequestPayloadMismatch)) ==
                    "request-payload-mismatch",
                "RPC dispositions have stable structured-log names");

    if (test.failures != 0) {
        std::cerr << test.failures << " test check(s) failed\n";
        return 1;
    }
    return 0;
}
