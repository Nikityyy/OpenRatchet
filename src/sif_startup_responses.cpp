#include "sif_startup_responses.h"

#include <array>

namespace ratchet {
namespace {
struct ResponseMapping {
    uint32_t requestCommand;
    uint32_t result0;
    uint32_t result1;
};

constexpr std::array<ResponseMapping, 5> kStartupResponseMappings{{
    {0x80000592u, 0x3f570u, 0x3fb20u},
    {0x8000059au, 0x3f648u, 0x3fc50u},
    {0x80000593u, 0x410f0u, 0x417c0u},
    {0x80000595u, 0x41060u, 0x41bd0u},
    {0x80000006u, 0x220d0u, 0u},
}};
}  // namespace

SifStartupResponse SifStartupResponseResolver::resolve(uint32_t requestCommand) {
    // Reference-backed startup sequence: the first command 3 only arms the
    // bridge; its next occurrence receives the recorded completion.
    if (requestCommand == 0x80000003u) {
        if (!command3Seen_) {
            command3Seen_ = true;
            return {};
        }
        return {true, 0x4f848u, 0x4f890u};
    }

    for (const ResponseMapping& mapping : kStartupResponseMappings) {
        if (mapping.requestCommand == requestCommand) {
            return {true, mapping.result0, mapping.result1};
        }
    }
    return {};
}

void SifStartupResponseResolver::reset() {
    command3Seen_ = false;
}

}  // namespace ratchet
