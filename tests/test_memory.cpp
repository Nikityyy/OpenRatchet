#include "openratchet/ee_memory.h"
#include "openratchet/float_mode.h"
#include "openratchet/elf_loader.h"
#include <iostream>
#include <cassert>

void test_memory() {
    EE_Memory mem;
    mem.Init();

    // Test main RAM
    mem.Write<uint32_t>(0x00100000, 0xDEADBEEF);
    assert(mem.Read<uint32_t>(0x00100000) == 0xDEADBEEF);

    // Test scratchpad
    mem.Write<uint32_t>(0x70000000, 0xCAFEBABE);
    assert(mem.Read<uint32_t>(0x70000000) == 0xCAFEBABE);

    // Test KSEG0 translation
    mem.Write<uint32_t>(0x80100000, 0x12345678);
    assert(mem.Read<uint32_t>(0x00100000) == 0x12345678);

    // Test MMIO
    mem.Write<uint32_t>(0x10000000, 0x11112222);
    // Write routes to handler, read routes to handler (returns 0 from dummy)
    assert(mem.Read<uint32_t>(0x10000000) == 0);

    std::cout << "Memory tests pass!" << std::endl;
}

int main(int argc, char* argv[]) {
    // Basic test runner logic
    test_memory();

    InitPS2FloatMode();
    std::cout << "Float mode init pass!" << std::endl;

    if (argc > 1) {
        std::string mode = argv[1];
        if (mode == "--self-test") {
            // Memory tests already passed above
            return 0;
        }
    }

    return 0;
}
