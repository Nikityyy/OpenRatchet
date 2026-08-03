#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <iostream>
#include <functional>
#include "ee_context.h"
#include "mmio.h"

// Register a callback that is invoked whenever the game writes to the GS
// memory region (0x12000000–0x12001FFF). The renderer registers this at
// startup so the HAL doesn't need to include renderer headers.
// reg  = GS register index (0x00–0x7F), val = 64-bit register value.
void RegisterGSWriteCallback(std::function<void(uint8_t reg, uint64_t val)> cb);

// Register a callback that is invoked when the game sends a GIF packet
// to the GIF FIFO (MMIO 0x10006000) or via DMA channel 2 (PATH3).
// data = pointer into host EE RAM at the start of the packet, size = byte count.
void RegisterGIFPacketCallback(std::function<void(const uint8_t* data, size_t size)> cb);

// Called by MMIO or DMA to deliver a GIF packet to the registered callback.
void DeliverGIFPacket(const uint8_t* data, size_t size);

// 32 MB Main RAM
constexpr uint32_t EE_MAIN_RAM_SIZE = 32 * 1024 * 1024;
// 16 KB Scratchpad
constexpr uint32_t EE_SCRATCHPAD_SIZE = 16 * 1024;

class EE_Memory {
public:
    EE_Memory();
    ~EE_Memory() = default;

    void Init();

    template<typename T>
    T Read(uint32_t addr) {
        addr = TranslateAddress(addr);

        if (addr < EE_MAIN_RAM_SIZE) {
            if (addr + sizeof(T) > EE_MAIN_RAM_SIZE) return T(); // Out of bounds
            T val;
            std::memcpy(&val, &main_ram[addr], sizeof(T));
            return val;
        }
        else if (addr >= 0x70000000 && addr < 0x70000000 + EE_SCRATCHPAD_SIZE) {
            uint32_t offset = addr - 0x70000000;
            if (offset + sizeof(T) > EE_SCRATCHPAD_SIZE) return T();
            T val;
            std::memcpy(&val, &scratchpad[offset], sizeof(T));
            return val;
        }
        else if (addr >= 0x10000000 && addr <= 0x1000FFFF) {
            // MMIO Region
            return ReadMMIO<T>(addr);
        }
        else if (addr >= 0x12000000 && addr <= 0x12001FFF) {
            // GS Region
            return ReadGS<T>(addr);
        }
        else if (addr >= 0x1FC00000 && addr < 0x20000000) {
            // BIOS ROM region (just return 0 for now)
            return T();
        }

        // Unmapped memory
        return T();
    }

    template<typename T>
    void Write(uint32_t addr, T val) {
        addr = TranslateAddress(addr);

        if (addr < EE_MAIN_RAM_SIZE) {
            if (addr + sizeof(T) > EE_MAIN_RAM_SIZE) return; // Out of bounds
            std::memcpy(&main_ram[addr], &val, sizeof(T));
        }
        else if (addr >= 0x70000000 && addr < 0x70000000 + EE_SCRATCHPAD_SIZE) {
            uint32_t offset = addr - 0x70000000;
            if (offset + sizeof(T) > EE_SCRATCHPAD_SIZE) return;
            std::memcpy(&scratchpad[offset], &val, sizeof(T));
        }
        else if (addr >= 0x10000000 && addr <= 0x1000FFFF) {
            // MMIO Region
            WriteMMIO<T>(addr, val);
        }
        else if (addr >= 0x12000000 && addr <= 0x12001FFF) {
            // GS Region
            WriteGS<T>(addr, val);
        }
    }

    uint8_t* GetRamPointer(uint32_t addr) {
        addr = TranslateAddress(addr);
        if (addr < EE_MAIN_RAM_SIZE) return &main_ram[addr];
        if (addr >= 0x70000000 && addr < 0x70000000 + EE_SCRATCHPAD_SIZE) {
            return &scratchpad[addr - 0x70000000];
        }
        return nullptr;
    }

    bool IsValidRange(uint32_t addr, size_t size) {
        addr = TranslateAddress(addr);
        if (addr < EE_MAIN_RAM_SIZE) return size <= EE_MAIN_RAM_SIZE - addr;
        if (addr >= 0x70000000 && addr < 0x70000000 + EE_SCRATCHPAD_SIZE)
            return size <= EE_SCRATCHPAD_SIZE - (addr - 0x70000000);
        return false;
    }

private:
    std::vector<uint8_t> main_ram;
    std::vector<uint8_t> scratchpad;

    uint32_t TranslateAddress(uint32_t addr) {
        // Strip KSEG0/KSEG1 bits
        // KSEG0: 0x80000000 - 0x9FFFFFFF -> 0x00000000 - 0x1FFFFFFF
        // KSEG1: 0xA0000000 - 0xBFFFFFFF -> 0x00000000 - 0x1FFFFFFF
        // We can just mask with 0x1FFFFFFF, though scratchpad is at 0x70000000
        if (addr >= 0x80000000 && addr < 0xC0000000) {
            return addr & 0x1FFFFFFF;
        }
        return addr;
    }

    template<typename T> T ReadMMIO(uint32_t addr);
    template<typename T> void WriteMMIO(uint32_t addr, T val);

    template<typename T> T ReadGS(uint32_t addr);
    template<typename T> void WriteGS(uint32_t addr, T val);
};

extern EE_Memory g_ee_memory;

template<typename T>
inline T MEM_READ(MIPS_EE_Context* ctx, uint32_t addr) {
    (void)ctx;
    return g_ee_memory.Read<T>(addr);
}

template<typename T>
inline void MEM_WRITE(MIPS_EE_Context* ctx, uint32_t addr, T val) {
    (void)ctx;
    g_ee_memory.Write<T>(addr, val);
}
