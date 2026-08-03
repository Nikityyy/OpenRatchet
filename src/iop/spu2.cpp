#include "openratchet/iop.h"
#include <iostream>

namespace OpenRatchet {
namespace IOP {

class SPU2_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        if (func == 0) return sceSdInit(send_size >= 4 ? static_cast<int32_t>(mem->Read<uint32_t>(send_addr)) : 0);
        if (func == 1 && send_size >= 4) return sceSdSetParam(mem->Read<uint16_t>(send_addr), mem->Read<uint16_t>(send_addr + 2));
        return static_cast<uint32_t>(-1);
    }
};

static SPU2_Module g_spu2Module;

void InitSPU2() {
    RegisterModule(0x8000010E, &g_spu2Module);
}

int32_t sceSdInit(int32_t flag) {
    std::cout << "[SPU2] sceSdInit(" << flag << ")\n";
    return 0;
}

int32_t sceSdSetParam(uint16_t entry, uint16_t value) {
    return 0;
}

int32_t sceSdVoiceTrans(int16_t channel, int16_t mode, uint32_t m_addr, uint32_t size, uint32_t start) {
    return 0;
}

} // namespace IOP
} // namespace OpenRatchet
