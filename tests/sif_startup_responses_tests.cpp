#include "sif_startup_responses.h"
#include "guest_range.h"

#include <cassert>

int main() {
    assert(ratchet::isRangeWithin(0u, 0u, 0u));
    assert(ratchet::isRangeWithin(3u, 5u, 8u));
    assert(!ratchet::isRangeWithin(3u, 6u, 8u));
    assert(!ratchet::isRangeWithin(9u, 0u, 8u));

    ratchet::SifStartupResponseResolver resolver;

    const auto unknown = resolver.resolve(0xfeedfaceu);
    assert(!unknown.completed);

    const auto command6 = resolver.resolve(0x80000006u);
    assert(command6.completed);
    assert(command6.result0 == 0x220d0u);
    assert(command6.result1 == 0u);

    const auto firstCommand3 = resolver.resolve(0x80000003u);
    assert(!firstCommand3.completed);
    const auto secondCommand3 = resolver.resolve(0x80000003u);
    assert(secondCommand3.completed);
    assert(secondCommand3.result0 == 0x4f848u);
    assert(secondCommand3.result1 == 0x4f890u);

    resolver.reset();
    assert(!resolver.resolve(0x80000003u).completed);
}
