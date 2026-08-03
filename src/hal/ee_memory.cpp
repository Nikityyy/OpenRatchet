#include "openratchet/ee_memory.h"

EE_Memory g_ee_memory;

EE_Memory::EE_Memory() {
    Init();
}

void EE_Memory::Init() {
    main_ram.resize(EE_MAIN_RAM_SIZE, 0);
    scratchpad.resize(EE_SCRATCHPAD_SIZE, 0);
    RegisterMMIOHandlers();
}

template<>
uint32_t EE_Memory::ReadMMIO<uint32_t>(uint32_t addr) {
    return ReadMMIOWord(addr);
}

template<>
uint16_t EE_Memory::ReadMMIO<uint16_t>(uint32_t addr) {
    // Basic fallback/unaligned read is typically not how PS2 does it, 
    // but for completeness:
    return ReadMMIOWord(addr & ~3) >> ((addr & 2) * 8);
}

template<>
uint8_t EE_Memory::ReadMMIO<uint8_t>(uint32_t addr) {
    return ReadMMIOWord(addr & ~3) >> ((addr & 3) * 8);
}

template<>
uint64_t EE_Memory::ReadMMIO<uint64_t>(uint32_t addr) {
    uint32_t lo = ReadMMIOWord(addr);
    uint32_t hi = ReadMMIOWord(addr + 4);
    return ((uint64_t)hi << 32) | lo;
}

template<>
uint128_t EE_Memory::ReadMMIO<uint128_t>(uint32_t addr) {
    uint128_t res;
    res.lo = ReadMMIO<uint64_t>(addr);
    res.hi = ReadMMIO<uint64_t>(addr + 8);
    return res;
}


template<>
void EE_Memory::WriteMMIO<uint32_t>(uint32_t addr, uint32_t val) {
    WriteMMIOWord(addr, val);
}

template<>
void EE_Memory::WriteMMIO<uint16_t>(uint32_t addr, uint16_t val) {
    // In practice, PS2 hardware often ignores sub-word writes to MMIO or they work identically.
    // For now, redirect to Write32 with 0 padding for simplicity, though real hardware behavior varies.
    WriteMMIOWord(addr & ~3, val); 
}

template<>
void EE_Memory::WriteMMIO<uint8_t>(uint32_t addr, uint8_t val) {
    WriteMMIOWord(addr & ~3, val);
}

template<>
void EE_Memory::WriteMMIO<uint64_t>(uint32_t addr, uint64_t val) {
    WriteMMIOWord(addr, (uint32_t)val);
    WriteMMIOWord(addr + 4, (uint32_t)(val >> 32));
}

template<>
void EE_Memory::WriteMMIO<uint128_t>(uint32_t addr, uint128_t val) {
    WriteMMIO<uint64_t>(addr, val.lo);
    WriteMMIO<uint64_t>(addr + 8, val.hi);
}

// GS Read/Write
template<>
uint32_t EE_Memory::ReadGS<uint32_t>(uint32_t addr) {
    // TODO: implement GS read
    return 0;
}
template<>
uint64_t EE_Memory::ReadGS<uint64_t>(uint32_t addr) { return 0; }
template<>
uint128_t EE_Memory::ReadGS<uint128_t>(uint32_t addr) { return {0, 0}; }
template<>
uint16_t EE_Memory::ReadGS<uint16_t>(uint32_t addr) { return 0; }
template<>
uint8_t EE_Memory::ReadGS<uint8_t>(uint32_t addr) { return 0; }

template<>
void EE_Memory::WriteGS<uint32_t>(uint32_t addr, uint32_t val) {
    // TODO: implement GS write
}
template<>
void EE_Memory::WriteGS<uint64_t>(uint32_t addr, uint64_t val) { }
template<>
void EE_Memory::WriteGS<uint128_t>(uint32_t addr, uint128_t val) { }
template<>
void EE_Memory::WriteGS<uint16_t>(uint32_t addr, uint16_t val) { }
template<>
void EE_Memory::WriteGS<uint8_t>(uint32_t addr, uint8_t val) { }
