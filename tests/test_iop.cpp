#include "openratchet/iop.h"
#include <cassert>
#include <filesystem>

int main() {
    EE_Memory mem; mem.Init();
    OpenRatchet::IOP::InitIOP();
    assert(OpenRatchet::IOP::sceCdInit(0) == 1);
    assert(OpenRatchet::IOP::sceCdRead(290, 1, 0x2000, 0, &mem) == 1);
    assert(OpenRatchet::IOP::sceCdSync(0) == 0);
    assert(OpenRatchet::IOP::sceCdGetError() == 0);
    assert(mem.Read<uint32_t>(0x2000) == 0x464c457f);
    assert(OpenRatchet::IOP::scePadInit(0) == 1);
    assert(OpenRatchet::IOP::scePadRead(0, 0, 0x3000, &mem) == 1);
    assert(mem.Read<uint8_t>(0x3001) == 0x70);
    assert(OpenRatchet::IOP::sceSdInit(0) == 0);
    assert(OpenRatchet::IOP::sceMcInit() == 0);
    return 0;
}
