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

    if (test.failures != 0) {
        std::cerr << test.failures << " test check(s) failed\n";
        return 1;
    }
    return 0;
}
