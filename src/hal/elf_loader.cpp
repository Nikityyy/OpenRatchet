#include "openratchet/elf_loader.h"
#include "openratchet/ee_memory.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>
#include <limits>

struct ELFHeader {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct ProgramHeader {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

bool ELFLoader::LoadELF(const std::string& path, EE_Memory& mem, MIPS_EE_Context& ctx) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open ELF: " << path << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (file_size < static_cast<std::streamoff>(sizeof(ELFHeader))) {
        std::cerr << "ELF header is truncated.\n";
        return false;
    }
    ELFHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        std::cerr << "Failed to read ELF header.\n";
        return false;
    }

    // Check magic
    if (header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' || 
        header.e_ident[2] != 'L' || header.e_ident[3] != 'F') {
        std::cerr << "Invalid ELF magic." << std::endl;
        return false;
    }
    
    if (header.e_ident[4] != 1 || header.e_ident[5] != 1 || header.e_machine != 8) { // 32-bit LE MIPS
        std::cerr << "Not a MIPS ELF." << std::endl;
        return false;
    }

    if (header.e_phentsize != sizeof(ProgramHeader) || header.e_phnum == 0) {
        std::cerr << "Invalid ELF program header table.\n";
        return false;
    }
    const uint64_t ph_table_end = static_cast<uint64_t>(header.e_phoff) +
                                   static_cast<uint64_t>(header.e_phnum) * header.e_phentsize;
    if (ph_table_end > static_cast<uint64_t>(file_size)) {
        std::cerr << "ELF program header table is truncated.\n";
        return false;
    }

    bool loaded_segment = false;
    for (int i = 0; i < header.e_phnum; ++i) {
        ProgramHeader phdr{};
        file.seekg(static_cast<std::streamoff>(header.e_phoff) +
                   static_cast<std::streamoff>(i) * header.e_phentsize);
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (!file) {
            std::cerr << "Failed to read ELF program header " << i << ".\n";
            return false;
        }

        if (phdr.p_type == 1) { // PT_LOAD
            if (phdr.p_filesz > phdr.p_memsz) {
                std::cerr << "ELF PT_LOAD has filesz larger than memsz.\n";
                return false;
            }
            const uint64_t segment_end = static_cast<uint64_t>(phdr.p_offset) + phdr.p_filesz;
            if (segment_end > static_cast<uint64_t>(file_size) ||
                !mem.IsValidRange(phdr.p_vaddr, phdr.p_memsz)) {
                std::cerr << "ELF PT_LOAD is outside file or guest RAM at 0x"
                          << std::hex << phdr.p_vaddr << std::dec << "\n";
                return false;
            }
            std::vector<uint8_t> buffer(phdr.p_filesz);
            file.seekg(phdr.p_offset);
            file.read(reinterpret_cast<char*>(buffer.data()), phdr.p_filesz);
            if (!file) {
                std::cerr << "Failed to read ELF segment.\n";
                return false;
            }

            uint8_t* dest = mem.GetRamPointer(phdr.p_vaddr);
            if (!dest) return false;
            std::memcpy(dest, buffer.data(), phdr.p_filesz);
            std::memset(dest + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
            loaded_segment = true;
        }
    }

    // Set up context
    std::memset(&ctx, 0, sizeof(MIPS_EE_Context));
    ctx.pc = header.e_entry;
    if (!loaded_segment) {
        std::cerr << "ELF contains no loadable segments.\n";
        return false;
    }
    // The stripped image has no reliable GP symbol; use the start of EE RAM as a safe ABI default.
    ctx.r[28] = 0x00100000;
    ctx.r[29] = 0x01FFFFF0; // SP at top of 32MB RAM
    ctx.r[0] = 0;           // zero register

    return true;
}
