// sif.cpp — SIF RPC dispatch and DMA
// Previously sceSifCallRpc was a no-op; now it actually dispatches.
#include "openratchet/iop.h"
#include <iostream>
#include <unordered_map>
#include <cstring>

namespace OpenRatchet {
namespace IOP {

// Server registry: server_id -> module
static std::unordered_map<uint32_t, IOP_Module*> g_modules;

// Binding registry: client_addr -> server_id (populated by sceSifBindRpc)
static std::unordered_map<uint32_t, uint32_t> g_bindings;

void InitSif() {
    g_modules.clear();
    g_bindings.clear();
}

void RegisterModule(uint32_t server_id, IOP_Module* module) {
    g_modules[server_id] = module;
}

void sceSifInitRpc(MIPS_EE_Context* /*ctx*/, EE_Memory* /*mem*/) {
    // No-op: RPC system is always ready
}

int32_t sceSifBindRpc(MIPS_EE_Context* /*ctx*/, EE_Memory* /*mem*/,
                      uint32_t client_addr, uint32_t server_id, uint32_t /*mode*/) {
    const auto it = g_modules.find(server_id);
    if (it == g_modules.end()) {
        std::cerr << "[SIF] sceSifBindRpc: unknown server 0x" << std::hex << server_id << std::dec << "\n";
        return -1;
    }
    // Record the binding so sceSifCallRpc can resolve client_addr -> server_id
    g_bindings[client_addr] = server_id;
    std::cout << "[SIF] Bound client 0x" << std::hex << client_addr
              << " -> server 0x" << server_id << std::dec << "\n";
    return 0;
}

int32_t sceSifCallRpc(MIPS_EE_Context* ctx, EE_Memory* mem,
                      uint32_t client_addr, uint32_t func, uint32_t /*mode*/,
                      uint32_t send_addr, uint32_t ssize,
                      uint32_t recv_addr, uint32_t rsize,
                      uint32_t /*end_func*/, uint32_t /*end_param*/) {
    // Look up which server this client is bound to
    const auto bind_it = g_bindings.find(client_addr);
    if (bind_it == g_bindings.end()) {
        // Some games also embed the server_id directly in the client struct at offset 0.
        // Try reading it from guest memory as a fallback.
        const uint32_t maybe_id = mem->Read<uint32_t>(client_addr);
        const auto mod_it = g_modules.find(maybe_id);
        if (mod_it != g_modules.end()) {
            return static_cast<int32_t>(
                mod_it->second->Dispatch(func, send_addr, ssize, recv_addr, rsize, mem));
        }
        std::cerr << "[SIF] sceSifCallRpc: client 0x" << std::hex << client_addr
                  << " not bound (func=" << func << ")\n" << std::dec;
        return -1;
    }

    const uint32_t server_id = bind_it->second;
    const auto mod_it = g_modules.find(server_id);
    if (mod_it == g_modules.end()) {
        std::cerr << "[SIF] sceSifCallRpc: server 0x" << std::hex << server_id << " not found\n";
        return -1;
    }

    return static_cast<int32_t>(
        mod_it->second->Dispatch(func, send_addr, ssize, recv_addr, rsize, mem));
}

// sceSifSetDma: each DMA descriptor is 16 bytes: {src_addr, dst_addr, size, attr}
// On PS2 this copies IOP memory to EE memory; we treat both as EE memory here.
void sceSifSetDma(MIPS_EE_Context* /*ctx*/, EE_Memory* mem,
                  uint32_t dma_desc_addr, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t base = dma_desc_addr + i * 16;
        const uint32_t src  = mem->Read<uint32_t>(base);
        const uint32_t dst  = mem->Read<uint32_t>(base + 4);
        const uint32_t size = mem->Read<uint32_t>(base + 8);
        if (size == 0 || !mem->IsValidRange(src, size) || !mem->IsValidRange(dst, size)) continue;
        const uint8_t* src_ptr = mem->GetRamPointer(src);
        uint8_t*       dst_ptr = mem->GetRamPointer(dst);
        if (src_ptr && dst_ptr) std::memcpy(dst_ptr, src_ptr, size);
    }
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
