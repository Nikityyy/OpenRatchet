#include "openratchet/elf_loader.h"
#include "openratchet/ee_memory.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>

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

    ELFHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(ELFHeader));

    // Check magic
    if (header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' || 
        header.e_ident[2] != 'L' || header.e_ident[3] != 'F') {
        std::cerr << "Invalid ELF magic." << std::endl;
        return false;
    }
    
    if (header.e_machine != 8) { // MIPS
        std::cerr << "Not a MIPS ELF." << std::endl;
        return false;
    }

    for (int i = 0; i < header.e_phnum; ++i) {
        ProgramHeader phdr;
        file.seekg(header.e_phoff + i * header.e_phentsize);
        file.read(reinterpret_cast<char*>(&phdr), sizeof(ProgramHeader));

        if (phdr.p_type == 1) { // PT_LOAD
            std::vector<uint8_t> buffer(phdr.p_filesz);
            file.seekg(phdr.p_offset);
            file.read(reinterpret_cast<char*>(buffer.data()), phdr.p_filesz);

            uint8_t* dest = mem.GetRamPointer(phdr.p_vaddr);
            if (dest) {
                std::memcpy(dest, buffer.data(), phdr.p_filesz);
                // Zero out the rest of the memory segment
                if (phdr.p_memsz > phdr.p_filesz) {
                    std::memset(dest + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
                }
            } else {
                std::cerr << "ELF segment loads into invalid memory address: 0x" << std::hex << phdr.p_vaddr << std::dec << std::endl;
            }
        }
    }

    // Set up context
    std::memset(&ctx, 0, sizeof(MIPS_EE_Context));
    ctx.pc = header.e_entry;
    // GP can be set by the game later or extracted from symbol table, ignoring for now
    ctx.r[29] = 0x01FFFFF0; // SP at top of 32MB RAM
    ctx.r[0] = 0;           // zero register

    return true;
}
