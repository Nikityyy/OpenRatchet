#include "openratchet/iop.h"
#include <iostream>

namespace OpenRatchet {
namespace IOP {

class PAD_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        return 0;
    }
};

static PAD_Module g_padModule;

void InitPAD() {
    RegisterModule(0x8000010F, &g_padModule);
}

int32_t scePadInit(int32_t mode) {
    std::cout << "[PAD] Initialized (stub).\n";
    return 1;
}

int32_t scePadRead(int32_t port, int32_t slot, uint32_t buffer_addr, EE_Memory* mem) {
    uint8_t pad_data[32] = {0};
    
    pad_data[0] = 0x00;
    pad_data[1] = 0x70; // 0x70 = DualShock 2 present

    uint16_t buttons = 0xFFFF; // Active low (no buttons pressed)
    uint8_t rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;

    pad_data[2] = buttons & 0xFF;
    pad_data[3] = (buttons >> 8) & 0xFF;
    pad_data[4] = rx;
    pad_data[5] = ry;
    pad_data[6] = lx;
    pad_data[7] = ly;
    
    // Bytes 8-19 are pressure sensitive values (0-255).
    // Let's just mock them as 0 (not pressed).
    for (int i = 8; i < 20; ++i) pad_data[i] = 0;

    for (int i = 0; i < 32; ++i) {
        mem->Write<uint8_t>(buffer_addr + i, pad_data[i]);
    }

    return 1;
}

int32_t scePadGetState(int32_t port, int32_t slot) {
    return 6; // 6 = Ready
}

} // namespace IOP
} // namespace OpenRatchet
