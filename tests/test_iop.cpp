#include "openratchet/iop.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

int main() {
    EE_Memory mem; mem.Init();
    OpenRatchet::IOP::InitIOP();
    assert(OpenRatchet::IOP::sceCdInit(0) == 1);

    // Single sector read (boot ELF start at LSN 290)
    assert(OpenRatchet::IOP::sceCdRead(290, 1, 0x2000, 0, &mem) == 1);
    assert(OpenRatchet::IOP::sceCdSync(0) == 0);
    assert(OpenRatchet::IOP::sceCdGetError() == 0);
    assert(mem.Read<uint32_t>(0x2000) == 0x464c457f); // ELF magic '\x7fELF'

    // Multi-sector read inside the boot ELF.
    assert(OpenRatchet::IOP::sceCdRead(290, 4, 0x10000, 0, &mem) == 1);
    assert(OpenRatchet::IOP::sceCdSync(0) == 0);
    assert(OpenRatchet::IOP::sceCdGetError() == 0);
    assert(mem.Read<uint32_t>(0x10000) == 0x464c457f);

    // Read the final partial ELF sector and the first IOPRP sector. This is a
    // real extracted-file boundary: the ISO9660 tail is zero padded and the
    // following sector must come from the next extracted file, not the ISO.
    assert(OpenRatchet::IOP::sceCdRead(965, 2, 0x18000, 0, &mem) == 1);
    assert(OpenRatchet::IOP::sceCdSync(0) == 0);
    assert(OpenRatchet::IOP::sceCdGetError() == 0);
    std::ifstream elf("data/raw/SCUS_971.99", std::ios::binary);
    std::vector<uint8_t> elf_tail(628);
    elf.seekg(675 * 2048);
    elf.read(reinterpret_cast<char*>(elf_tail.data()), elf_tail.size());
    for (uint32_t i = 0; i < elf_tail.size(); ++i) assert(mem.Read<uint8_t>(0x18000 + i) == elf_tail[i]);
    for (uint32_t i = elf_tail.size(); i < 2048; ++i) assert(mem.Read<uint8_t>(0x18000 + i) == 0);
    std::ifstream ioprp("data/raw/IOPRP243.IMG", std::ios::binary);
    std::vector<uint8_t> ioprp_first(2048);
    ioprp.read(reinterpret_cast<char*>(ioprp_first.data()), ioprp_first.size());
    for (uint32_t i = 0; i < ioprp_first.size(); ++i) assert(mem.Read<uint8_t>(0x18800 + i) == ioprp_first[i]);

    // Unextracted sectors must fail; runtime CDVD cannot silently depend on
    // the user's ISO after extraction.
    assert(OpenRatchet::IOP::sceCdRead(1200, 1, 0x1A000, 0, &mem) == 0);

    // Controller input test
    assert(OpenRatchet::IOP::scePadInit(0) == 1);
    assert(OpenRatchet::IOP::scePadRead(0, 0, 0x3000, &mem) == 1);
    assert(mem.Read<uint8_t>(0x3001) == 0x70);

    // Audio & Memory Card stubs
    assert(OpenRatchet::IOP::sceSdInit(0) == 0);
    assert(OpenRatchet::IOP::sceMcInit() == 0);

    // A malformed manifest must fail initialization without throwing or
    // accepting paths outside data/raw.
    const auto original_path = std::filesystem::current_path();
    const auto malformed_root = std::filesystem::temp_directory_path() / "openratchet-cdvd-malformed";
    std::filesystem::remove_all(malformed_root);
    std::filesystem::create_directories(malformed_root / "data");
    {
        std::ofstream manifest(malformed_root / "data" / "manifest.txt");
        manifest << "---FILES---\n../outside.bin,1,2048,1,not-a-sha256\n";
    }
    std::filesystem::current_path(malformed_root);
    assert(OpenRatchet::IOP::sceCdInit(0) == 0);
    assert(OpenRatchet::IOP::sceCdGetError() != 0);
    std::filesystem::current_path(original_path);
    std::filesystem::remove_all(malformed_root);

    std::cout << "test_iop: ALL CDVD/IOP TESTS PASSED\n";
    return 0;
}
