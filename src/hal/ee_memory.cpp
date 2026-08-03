#include "openratchet/ee_memory.h"
#include <functional>

EE_Memory g_ee_memory;

// ── GS write callback ─────────────────────────────────────────────────────────────────────────
// Registered by the renderer at startup so the HAL doesn't depend on renderer.
static std::function<void(uint8_t reg, uint64_t val)> g_gs_write_cb;

void RegisterGSWriteCallback(std::function<void(uint8_t reg, uint64_t val)> cb) {
    g_gs_write_cb = std::move(cb);
}

// ── GIF packet callback ─────────────────────────────────────────────────────────────────
// Registered by VulkanRenderer so GIF FIFO/DMA writes reach ProcessGIFPacket.
static std::function<void(const uint8_t*, size_t)> g_gif_packet_cb;

void RegisterGIFPacketCallback(std::function<void(const uint8_t* data, size_t size)> cb) {
    g_gif_packet_cb = std::move(cb);
}

void DeliverGIFPacket(const uint8_t* data, size_t size) {
    if (g_gif_packet_cb && data && size >= 16)
        g_gif_packet_cb(data, size);
}

EE_Memory::EE_Memory() = default;

void EE_Memory::Init() {
    main_ram.assign(EE_MAIN_RAM_SIZE, 0);
    scratchpad.assign(EE_SCRATCHPAD_SIZE, 0);
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
    // Read-modify-write: preserve the other 16-bit half of the 32-bit MMIO register.
    const uint32_t aligned = addr & ~3u;
    const uint32_t shift   = (addr & 2u) * 8u;
    const uint32_t old     = ReadMMIOWord(aligned);
    const uint32_t mask    = 0xFFFFu << shift;
    WriteMMIOWord(aligned, (old & ~mask) | (static_cast<uint32_t>(val) << shift));
}

template<>
void EE_Memory::WriteMMIO<uint8_t>(uint32_t addr, uint8_t val) {
    // Read-modify-write: preserve the other 3 bytes of the 32-bit MMIO register.
    const uint32_t aligned = addr & ~3u;
    const uint32_t shift   = (addr & 3u) * 8u;
    const uint32_t old     = ReadMMIOWord(aligned);
    const uint32_t mask    = 0xFFu << shift;
    WriteMMIOWord(aligned, (old & ~mask) | (static_cast<uint32_t>(val) << shift));
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

// ── GS Region Read/Write ────────────────────────────────────────────────────
// GS registers are at 0x12000000+. Each register is 64-bit, spaced 16 bytes
// apart (8-byte data + 8-byte pad). Register index = (addr & 0xFF) / 8.
// Writes of 64-bit values dispatch to the registered GS callback.

template<>
void EE_Memory::WriteGS<uint64_t>(uint32_t addr, uint64_t val) {
    if (g_gs_write_cb) {
        const uint8_t reg = static_cast<uint8_t>((addr & 0xFF) >> 3);
        g_gs_write_cb(reg, val);
    }
}
template<>
void EE_Memory::WriteGS<uint128_t>(uint32_t addr, uint128_t val) {
    // 128-bit GS write: lower 64 bits are the value, upper 64 bits are address select
    WriteGS<uint64_t>(addr, val.lo);
}
template<>
void EE_Memory::WriteGS<uint32_t>(uint32_t addr, uint32_t val) {
    // 32-bit writes to GS are unusual; treat as lower half of a 64-bit write
    WriteGS<uint64_t>(addr & ~7u, static_cast<uint64_t>(val));
}
template<>
void EE_Memory::WriteGS<uint16_t>(uint32_t addr, uint16_t val) {
    WriteGS<uint64_t>(addr & ~7u, static_cast<uint64_t>(val));
}
template<>
void EE_Memory::WriteGS<uint8_t>(uint32_t addr, uint8_t val) {
    WriteGS<uint64_t>(addr & ~7u, static_cast<uint64_t>(val));
}

template<>
uint64_t EE_Memory::ReadGS<uint64_t>(uint32_t addr) { return 0; }
template<>
uint128_t EE_Memory::ReadGS<uint128_t>(uint32_t addr) { return {0, 0}; }
template<>
uint32_t EE_Memory::ReadGS<uint32_t>(uint32_t addr) { return 0; }
template<>
uint16_t EE_Memory::ReadGS<uint16_t>(uint32_t addr) { return 0; }
template<>
uint8_t EE_Memory::ReadGS<uint8_t>(uint32_t addr) { return 0; }
