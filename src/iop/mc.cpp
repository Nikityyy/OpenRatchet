#include "openratchet/iop.h"
#include <iostream>

namespace OpenRatchet {
namespace IOP {

class MC_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        return 0;
    }
};

static MC_Module g_mcModule;

void InitMC() {
    RegisterModule(0x80000400, &g_mcModule);
}

int32_t sceMcInit() {
    std::cout << "[MC] sceMcInit()\n";
    return 0;
}

int32_t sceMcOpen() {
    std::cout << "[MC] sceMcOpen()\n";
    return -1; 
}

int32_t sceMcRead() {
    return -1;
}

int32_t sceMcWrite() {
    return -1;
}

int32_t sceMcClose() {
    return -1;
}

} // namespace IOP
} // namespace OpenRatchet
