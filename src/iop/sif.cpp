#include "openratchet/iop.h"
#include <iostream>

namespace OpenRatchet {
namespace IOP {

static std::unordered_map<uint32_t, IOP_Module*> g_modules;

void InitSif() {
    g_modules.clear();
}

void RegisterModule(uint32_t server_id, IOP_Module* module) {
    g_modules[server_id] = module;
}

void sceSifInitRpc(MIPS_EE_Context* ctx, EE_Memory* mem) {
    // No-op
}

int32_t sceSifBindRpc(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t client_addr, uint32_t server_id, uint32_t mode) {
    // Find if we have an HLE module for this server_id
    if (g_modules.find(server_id) != g_modules.end()) {
        std::cout << "[SIF] Bound RPC server 0x" << std::hex << server_id << std::dec << "\n";
        return 0; // Success
    }
    std::cerr << "[SIF] Warning: Bind to unknown RPC server 0x" << std::hex << server_id << std::dec << "\n";
    return -1;
}

int32_t sceSifCallRpc(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t client_addr, uint32_t func, uint32_t mode, uint32_t send_addr, uint32_t ssize, uint32_t recv_addr, uint32_t rsize, uint32_t end_func, uint32_t end_param) {
    // Client addr in guest memory probably has the bound server_id, but here we can't easily extract it without knowing the struct.
    // For a real implementation, we'd read the server_id from the client struct.
    // We'll log it for now.
    std::cout << "[SIF] sceSifCallRpc func " << func << " client " << std::hex << client_addr << std::dec << "\n";
    // We would look up the module and dispatch.
    return 0;
}

void sceSifSetDma(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t dma_desc_addr, uint32_t count) {
    std::cout << "[SIF] sceSifSetDma\n";
    // Direct memory copy operations
}

void InitIOP() {
    InitSif();
    InitCDVD();
    InitPAD();
    InitSPU2();
    InitMC();
}

} // namespace IOP
} // namespace OpenRatchet
