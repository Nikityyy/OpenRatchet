#include "openratchet/ee_memory.h"
#include "openratchet/float_mode.h"
#include "openratchet/elf_loader.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <filesystem>
#include <limits>
#include <algorithm>
#include <cstring>

static std::filesystem::path make_elf(uint32_t vaddr, uint32_t filesz, uint32_t memsz,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> bytes(52 + 32 + payload.size(), 0);
    bytes[0] = 0x7f; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F';
    bytes[4] = 1; bytes[5] = 1;
    auto put16 = [&](size_t p, uint16_t v) { std::memcpy(bytes.data() + p, &v, 2); };
    auto put32 = [&](size_t p, uint32_t v) { std::memcpy(bytes.data() + p, &v, 4); };
    put16(18, 8); put32(24, 0x00100000); put32(28, 52); put16(40, 52);
    put16(42, 32); put16(44, 1);
    put32(52, 1); put32(56, 84); put32(60, vaddr); put32(68, filesz); put32(72, memsz);
    std::copy(payload.begin(), payload.end(), bytes.begin() + 84);
    const auto path = std::filesystem::temp_directory_path() / "openratchet_test.elf";
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return path;
}

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

    // Test all integer widths and KSEG1 aliasing.
    uint64_t wide = 0x1122334455667788ull;
    mem.Write<uint64_t>(0x00100008, wide);
    assert(mem.Read<uint64_t>(0xA0100008) == wide);
    uint128_t vector{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull};
    mem.Write<uint128_t>(0x00100010, vector);
    assert(mem.Read<uint128_t>(0x00100010).hi == vector.hi);

    // Test MMIO retained state and routing.
    mem.Write<uint32_t>(0x10000000, 0x11112222);
    assert(mem.Read<uint32_t>(0x10000000) == 0x11112222);

    const auto elf = make_elf(0x00102000, 4, 8, {1, 2, 3, 4});
    MIPS_EE_Context ctx{};
    assert(ELFLoader::LoadELF(elf.string(), mem, ctx));
    assert(ctx.pc == 0x00100000);
    assert(mem.Read<uint32_t>(0x00102000) == 0x04030201);
    assert(mem.Read<uint32_t>(0x00102004) == 0);
    std::filesystem::remove(elf);

    std::cout << "Memory tests pass!" << std::endl;
}

int main(int argc, char* argv[]) {
    // Basic test runner logic
    test_memory();

    InitPS2FloatMode();
    assert(ClampPS2Float(std::numeric_limits<float>::infinity()) == std::numeric_limits<float>::max());
    assert(ClampPS2Float(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
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
