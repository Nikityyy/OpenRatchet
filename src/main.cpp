#include <iostream>
#include <string>
#include <cassert>
#include "openratchet/ee_memory.h"
#include "openratchet/float_mode.h"
#include "openratchet/elf_loader.h"
#include "openratchet/syscalls.h"

void run_self_test() {
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

    // Test MMIO routing
    mem.Write<uint32_t>(0x10000000, 0x11112222);
    assert(mem.Read<uint32_t>(0x10000000) == 0); // dummy handler returns 0

    std::cout << "Memory tests pass!" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "OpenRatchet — Ratchet & Clank Native PC Port" << std::endl;
    InitPS2FloatMode();

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--self-test") {
            run_self_test();
            return 0;
        }

        // Otherwise assume it's an ELF path
        g_ee_memory.Init();
        OpenRatchet::Kernel::InitSyscalls();
        MIPS_EE_Context ctx;
        if (ELFLoader::LoadELF(arg, g_ee_memory, ctx)) {
            std::cout << "Successfully loaded ELF: " << arg << std::endl;
            std::cout << "Entry point (PC): 0x" << std::hex << ctx.pc << std::dec << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to load ELF." << std::endl;
            return 1;
        }
    }

    std::cout << "Status: Milestone 4 — EE Memory System" << std::endl;
    return 0;
}

