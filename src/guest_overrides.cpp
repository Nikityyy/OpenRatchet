#include "guest_overrides.h"

#include <iostream>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace ratchet {
namespace {
PS2Runtime::RecompiledFunction g_guest11a948Original = nullptr;
bool g_sifInitResponseInjected = false;
bool g_sifCommandBridgeActive = false;

void guest_11a428(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t index = READ32(ADD32(GPR_U32(ctx, 4), 0x10u));
    const uint32_t table = READ32(ADD32(GPR_U32(ctx, 5), 0x1cu));
    const uint32_t address = ADD32(table, index << 2u);
    WRITE32(address, READ32(ADD32(GPR_U32(ctx, 4), 0x14u)));
    SET_GPR_U32(ctx, 2, address);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_11a448(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t value = READ32(ADD32(GPR_U32(ctx, 4), 0x10u));
    SET_GPR_U32(ctx, 2, value);
    WRITE32(ADD32(GPR_U32(ctx, 5), 8u), value);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_11cf10(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    // ponytail: bridge the absent async IOP-ready event; replace with real IOP status when available.
    g_sifInitResponseInjected = false;
    g_sifCommandBridgeActive = false;
    WRITE32(0x12fbf0u, 0u);
    SET_GPR_U32(ctx, 2, 1u);
    ctx->pc = GPR_U32(ctx, 31);
}

void guest_11a948(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (!g_sifInitResponseInjected &&
        READ32(0x154e48u) == 0x80000002u &&
        READ32(0x154e4cu) == 0u) {
        const uint32_t responseAddress = READ32(0x154e58u);
        if (responseAddress != 0u) {
            const uint32_t responseWords[] = {
                0x18u, 0x154e58u, 0x80000001u, 0u,
                0u, 1u, 0u, 0u,
            };
            for (uint32_t i = 0; i < sizeof(responseWords) / sizeof(responseWords[0]); ++i) {
                WRITE32(responseAddress + i * 4u, responseWords[i]);
            }
            g_sifInitResponseInjected = true;
            g_sifCommandBridgeActive = false;
            std::cerr << "[OpenRatchet:SIF] injected INIT response at 0x"
                      << std::hex << responseAddress << std::dec << "\n";
        }
    }

    const uint32_t responseAddress = READ32(0x154e58u);
    uint32_t packetAddress = 0u;
    uint32_t packetCommand = 0u;
    // ponytail: bounded 64-slot scan; use the SIF descriptor source if the guest pools change.
    for (const uint32_t pool : {0x20155000u, 0x20155800u}) {
        for (uint32_t slot = 0u; slot < 32u; ++slot) {
            const uint32_t candidate = pool + slot * 0x40u;
            const uint32_t command = READ32(candidate + 8u);
            if (command == 0x80000009u || command == 0x8000000au) {
                packetAddress = candidate;
                packetCommand = command;
                break;
            }
        }
        if (packetAddress != 0u) {
            break;
        }
    }

    if (responseAddress != 0u &&
        (packetCommand == 0x80000009u || packetCommand == 0x8000000au)) {
        if (packetCommand == 0x80000009u) {
            g_sifCommandBridgeActive = true;
        }
        if (!g_sifCommandBridgeActive) {
            packetCommand = 0u;
        }
    }

    if (responseAddress != 0u &&
        (packetCommand == 0x80000009u || packetCommand == 0x8000000au)) {
        const uint32_t requestAddress = READ32(packetAddress + 0x1cu);
        const uint32_t requestCommand = READ32(packetAddress + 0x20u);
        uint32_t result0 = 0u;
        uint32_t result1 = 0u;
        if (packetCommand == 0x80000009u) {
            if (requestCommand == 0x80000592u) {
                result0 = 0x3f570u;
                result1 = 0x3fb20u;
            } else if (requestCommand == 0x8000059au) {
                result0 = 0x3f648u;
                result1 = 0x3fc50u;
            }
        }
        const uint32_t responseWords[] = {
            packetCommand == 0x80000009u ? 0x40u : 0x1040u,
            packetCommand == 0x80000009u ? 0u : 0x1324c0u,
            0x80000008u, 0u,
            0u,
            packetCommand == 0x80000009u ? packetAddress : 0u,
            0u,
            requestAddress,
            packetCommand,
            result0,
            result1,
            0u, 0u, 0u, 0u, 0u,
        };
        for (uint32_t i = 0; i < sizeof(responseWords) / sizeof(responseWords[0]); ++i) {
            WRITE32(responseAddress + i * 4u, responseWords[i]);
        }
        std::cerr << "[OpenRatchet:SIF] injected completion for 0x"
                  << std::hex << packetCommand << " request=0x" << requestCommand
                  << " result0=0x" << result0 << " result1=0x" << result1
                  << std::dec << "\n";
    }

    if (g_guest11a948Original != nullptr) {
        g_guest11a948Original(rdram, ctx, runtime);
    } else {
        ctx->pc = GPR_U32(ctx, 31);
    }
}
}

void registerGuestBootstrapOverrides(PS2Runtime& runtime) {
    runtime.registerFunction(0x11a428u, guest_11a428);
    runtime.registerFunction(0x11a448u, guest_11a448);
}

void registerGuestDmacOverride(PS2Runtime& runtime) {
    g_guest11a948Original = runtime.lookupFunction(0x11a948u);
    runtime.registerFunction(0x11a948u, guest_11a948);
    runtime.registerFunction(0x11cf10u, guest_11cf10);
}
}
