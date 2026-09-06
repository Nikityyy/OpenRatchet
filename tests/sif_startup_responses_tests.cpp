#include "guest_range.h"
#include "sif_startup_responses.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

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

struct ResponseCase {
    uint32_t command;
    uint32_t result0;
    uint32_t result1;
};

void expectResponse(TestContext& test,
                    const ratchet::SifStartupResponse& actual,
                    const ResponseCase& expected) {
    if (!actual.completed || actual.result0 != expected.result0 ||
        actual.result1 != expected.result1) {
        ++test.failures;
        std::cerr << "FAIL: command 0x" << std::hex << expected.command
                  << " expected completed=1 result0=0x" << expected.result0
                  << " result1=0x" << expected.result1
                  << " got completed=" << std::dec << actual.completed
                  << " result0=0x" << std::hex << actual.result0
                  << " result1=0x" << actual.result1 << std::dec << '\n';
    }
}

}  // namespace

int main() {
    TestContext test;

    // These cover empty ranges, exact-fit ranges, one-byte overrun, and values
    // near the 32-bit guest-address limit without relying on overflowing sums.
    test.expect(ratchet::isRangeWithin(0u, 0u, 0u), "empty range fits empty buffer");
    test.expect(ratchet::isRangeWithin(3u, 5u, 8u), "exact-fit range succeeds");
    test.expect(!ratchet::isRangeWithin(3u, 6u, 8u), "range ending past capacity fails");
    test.expect(!ratchet::isRangeWithin(9u, 0u, 8u), "offset past capacity fails");
    test.expect(ratchet::isRangeWithin(0xfffffff0u, 0x0fu, 0xffffffffull),
                "near-maximum exact-fit guest range succeeds");
    test.expect(!ratchet::isRangeWithin(0xfffffff0u, 0x10u, 0xffffffffull),
                "near-maximum overflowing guest range fails");
    test.expect(ratchet::isRangeWithin(std::numeric_limits<uint32_t>::max(), 0u,
                                       std::numeric_limits<size_t>::max()),
                "maximum offset with zero size succeeds in a larger host buffer");

    constexpr std::array<ResponseCase, 5> kMappedResponses{{
        {0x80000592u, 0x3f570u, 0x3fb20u},
        {0x8000059au, 0x3f648u, 0x3fc50u},
        {0x80000593u, 0x410f0u, 0x417c0u},
        {0x80000595u, 0x41060u, 0x41bd0u},
        {0x80000006u, 0x220d0u, 0u},
    }};

    ratchet::SifStartupResponseResolver resolver;
    for (const ResponseCase& response : kMappedResponses) {
        expectResponse(test, resolver.resolve(response.command), response);
    }

    for (uint32_t command : {0u, 0xfeedfaceu}) {
        const auto unknown = resolver.resolve(command);
        test.expect(!unknown.completed && unknown.result0 == 0u && unknown.result1 == 0u,
                    "unknown command remains incomplete with zero results");
    }

    for (uint32_t command : {0x80000900u, 0x8000091bu, 0x80000400u,
                             0x00123456u, 0x00123457u}) {
        const auto platformOwned = resolver.resolve(command);
        test.expect(!platformOwned.completed && platformOwned.result0 == 0u &&
                        platformOwned.result1 == 0u,
                    "native-owned controller/save/audio service is absent from legacy SIF startup mappings");
    }

    const auto firstCommand3 = resolver.resolve(0x80000003u);
    test.expect(!firstCommand3.completed && firstCommand3.result0 == 0u &&
                    firstCommand3.result1 == 0u,
                "first command 3 arms without completing");
    expectResponse(test, resolver.resolve(0x80000003u),
                   {0x80000003u, 0x4f848u, 0x4f890u});

    resolver.reset();
    const auto firstAfterReset = resolver.resolve(0x80000003u);
    test.expect(!firstAfterReset.completed && firstAfterReset.result0 == 0u &&
                    firstAfterReset.result1 == 0u,
                "reset restores command 3 first-pass behavior");
    expectResponse(test, resolver.resolve(0x80000003u),
                   {0x80000003u, 0x4f848u, 0x4f890u});

    if (test.failures != 0) {
        std::cerr << test.failures << " test check(s) failed\n";
        return 1;
    }
    return 0;
}
