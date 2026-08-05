#pragma once

#include <cstdint>

namespace ratchet {

struct SifStartupResponse {
    bool completed = false;
    uint32_t result0 = 0u;
    uint32_t result1 = 0u;
};

// Models only the verified startup command-to-result compatibility bridge.
// This is intentionally separate from SIF packet transport so it can be
// characterized now and removed when stateful SIF/RPC transport replaces it.
class SifStartupResponseResolver {
public:
    SifStartupResponse resolve(uint32_t requestCommand);
    void reset();

private:
    bool command3Seen_ = false;
};

}  // namespace ratchet
