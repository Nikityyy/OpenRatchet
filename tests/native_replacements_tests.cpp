#include "runtime/native_replacements.h"

#include <cstdint>
#include <iostream>

namespace {

void replacementA(std::uint8_t*, R5900Context*, PS2Runtime*) {}
void replacementB(std::uint8_t*, R5900Context*, PS2Runtime*) {}

int fail(const char* message) {
    std::cerr << "native_replacements_tests: " << message << '\n';
    return 1;
}

} // namespace

int main() {
    using ratchet::runtime::NativeReplacementRegistry;
    using ratchet::runtime::NativeReplacementStage;

    NativeReplacementRegistry registry;
    if (registry.size() != 0u) {
        return fail("new registry is not empty");
    }

    if (!registry.add(0x1000u, "bootstrap-a", NativeReplacementStage::Bootstrap,
                      replacementA)) {
        return fail("valid bootstrap replacement was rejected");
    }
    if (!registry.add(0x2000u, "runtime-b", NativeReplacementStage::Runtime,
                      replacementB)) {
        return fail("valid runtime replacement was rejected");
    }

    // Same address in a later stage is intentionally legal: bootstrap glue may
    // be superseded after the generated ELF function table is available.
    if (!registry.add(0x1000u, "runtime-a", NativeReplacementStage::Runtime,
                      replacementB)) {
        return fail("cross-stage address reuse was rejected");
    }

    if (registry.add(0x2000u, "duplicate-runtime", NativeReplacementStage::Runtime,
                     replacementA)) {
        return fail("duplicate address in one stage was accepted");
    }
    if (registry.add(0u, "bad-address", NativeReplacementStage::Runtime,
                     replacementA)) {
        return fail("zero address was accepted");
    }
    if (registry.add(0x3000u, "", NativeReplacementStage::Runtime,
                     replacementA)) {
        return fail("empty name was accepted");
    }
    if (registry.add(0x3000u, "null-function", NativeReplacementStage::Runtime,
                     nullptr)) {
        return fail("null function was accepted");
    }

    if (registry.size() != 3u ||
        registry.size(NativeReplacementStage::Bootstrap) != 1u ||
        registry.size(NativeReplacementStage::Runtime) != 2u) {
        return fail("stage accounting is incorrect");
    }

    const auto& entries = registry.entries();
    if (entries[0].address != 0x1000u || entries[0].name == nullptr ||
        entries[1].address != 0x2000u || entries[2].address != 0x1000u) {
        return fail("registry order/metadata changed unexpectedly");
    }

    return 0;
}
