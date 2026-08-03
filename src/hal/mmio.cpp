#include "openratchet/mmio.h"
#include "openratchet/ee_memory.h"
#include <vector>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <cstring>

struct MMIORange {
    uint32_t start;
    uint32_t end;
    MMIO_Handler* handler;
};

class RegisterHandler final : public MMIO_Handler {
    const char* name;
    std::unordered_map<uint32_t, uint32_t> values;
public:
    explicit RegisterHandler(const char* n) : name(n) {}
    
    uint32_t Read32(uint32_t addr) override {
        const auto it = values.find(addr & ~3u);
        const uint32_t value = it == values.end() ? 0u : it->second;
        std::clog << "[MMIO READ] " << name << " 0x" << std::hex << addr
                  << " -> 0x" << value << std::dec << '\n';
        return value;
    }
    void Write32(uint32_t addr, uint32_t val) override {
        values[addr & ~3u] = val;
        std::clog << "[MMIO WRITE] " << name << " 0x" << std::hex << addr
                  << " = 0x" << val << std::dec << '\n';
    }
};

class TimerHandler final : public MMIO_Handler {
    std::unordered_map<uint32_t, uint32_t> values;
public:
    uint32_t Read32(uint32_t addr) override {
        uint32_t reg = addr & ~3u;
        // Auto-increment COUNT registers (they end in 0x00)
        if ((reg & 0xFF) == 0x00) {
            values[reg] += 200; 
        }
        const auto it = values.find(reg);
        return it == values.end() ? 0u : it->second;
    }
    void Write32(uint32_t addr, uint32_t val) override {
        values[addr & ~3u] = val;
        // std::clog << "[MMIO WRITE] Timers 0x" << std::hex << addr
        //          << " = 0x" << val << std::dec << '\n';
    }
};

class DMAChannelHandler final : public MMIO_Handler {
    std::unordered_map<uint32_t, uint32_t> values;
public:
    uint32_t Read32(uint32_t addr) override {
        uint32_t reg = addr & ~3u;
        // If reading a CHCR register (ends in 0x00 within channel block)
        if ((reg & 0xFF) == 0x00 || (reg & 0xFF) == 0x10 || (reg & 0xFF) == 0x20 ||
            (reg & 0xFF) == 0x30 || (reg & 0xFF) == 0x40 || (reg & 0xFF) == 0x50 ||
            (reg & 0xFF) == 0x60 || (reg & 0xFF) == 0x70 || (reg & 0xFF) == 0x80 ||
            (reg & 0xFF) == 0x90 || (reg & 0xFF) == 0xA0 || (reg & 0xFF) == 0xB0 ||
            (reg & 0xFF) == 0xC0 || (reg & 0xFF) == 0xD0 || (reg & 0xFF) == 0xE0 ||
            (reg & 0xFF) == 0xF0) {
            // Auto-clear STR (start/busy bit 8) so DMA polling loop finishes
            values[reg] &= ~0x100u;
        }
        const auto it = values.find(reg);
        return it == values.end() ? 0u : it->second;
    }
    void Write32(uint32_t addr, uint32_t val) override {
        values[addr & ~3u] = val;
    }
};

class VIFHandler final : public MMIO_Handler {
    const char* name;
    std::unordered_map<uint32_t, uint32_t> values;
    uint32_t cycle = 0;
public:
    explicit VIFHandler(const char* n) : name(n) {}
    
    uint32_t Read32(uint32_t addr) override {
        uint32_t reg = addr & ~3u;
        uint32_t val = cycle++; // Return changing values to break polling loops
        std::clog << "[MMIO READ] " << name << " 0x" << std::hex << addr
                  << " -> 0x" << val << std::dec << '\n';
        return val;
    }
    void Write32(uint32_t addr, uint32_t val) override {
        values[addr & ~3u] = val;
        std::clog << "[MMIO WRITE] " << name << " 0x" << std::hex << addr
                  << " = 0x" << val << std::dec << '\n';
    }
};

static TimerHandler timer_handler;
static RegisterHandler ipu_handler("IPU");
static VIFHandler gif_handler("GIF"); // Using VIFHandler for GIF to get the cycle hack
static VIFHandler vif0_handler("VIF0");
static VIFHandler vif1_handler("VIF1");
static RegisterHandler vu_handler("VU0/VU1 Mem");
static DMAChannelHandler dma_ch_handler;
static RegisterHandler dma_ctrl_handler("DMA Control");
static RegisterHandler intc_handler("INTC/SBUS/Timer Ctrl");
static RegisterHandler default_handler("Unknown MMIO");

// GIF FIFO DIRECT handler
class GIFFifoHandler final : public MMIO_Handler {
    static constexpr size_t kMaxBuf = 64 * 1024;
    uint8_t  m_buf[kMaxBuf];
    size_t   m_len = 0;
public:
    uint32_t Read32(uint32_t) override { return 0; }
    void Write32(uint32_t addr, uint32_t val) override {
        if (m_len + 4 <= kMaxBuf) {
            std::memcpy(m_buf + m_len, &val, 4);
            m_len += 4;
            MaybeFlush();
        }
    }
private:
    void MaybeFlush() {
        if (m_len < 16) return;
        uint64_t tag0 = 0;
        std::memcpy(&tag0, m_buf, 8);
        const bool eop = (tag0 >> 15) & 1;
        const uint32_t nloop  = tag0 & 0x7FFF;
        const uint32_t flg    = (tag0 >> 58) & 0x3;
        const uint32_t nreg   = ((tag0 >> 60) & 0xF);
        const uint32_t nreg_r = nreg ? nreg : 16;
        size_t expected = 16;
        if (flg == 0 || flg == 1) expected += (size_t)nloop * nreg_r * (flg == 0 ? 16 : 8);
        else if (flg == 2)        expected += (size_t)nloop * 16;
        if (m_len >= expected) {
            DeliverGIFPacket(m_buf, expected);
            m_len -= expected;
            if (m_len > 0) std::memmove(m_buf, m_buf + expected, m_len);
        }
    }
};

static GIFFifoHandler gif_fifo_handler;
static std::vector<MMIORange> handlers;

static MMIO_Handler* GetHandler(uint32_t addr) {
    for (const auto& range : handlers) {
        if (addr >= range.start && addr <= range.end) return range.handler;
    }
    return &default_handler;
}

void RegisterMMIOHandlers() {
    if (!handlers.empty()) return;
    InstallMMIOHandler(0x10000000, 0x100003FF, &timer_handler);
    InstallMMIOHandler(0x10000800, 0x10000BFF, &ipu_handler);
    
    // Corrected PS2 MMIO mapping
    InstallMMIOHandler(0x10001000, 0x10001FFF, &default_handler); 
    InstallMMIOHandler(0x10002000, 0x100023FF, &default_handler);
    InstallMMIOHandler(0x10003000, 0x100037FF, &gif_handler); // GIF is 0x10003000
    InstallMMIOHandler(0x10003800, 0x10003BFF, &vif0_handler); // VIF0 is 0x10003800
    InstallMMIOHandler(0x10003C00, 0x10003FFF, &vif1_handler); // VIF1 is 0x10003C00
    
    InstallMMIOHandler(0x10004000, 0x10005FFF, &vu_handler);
    InstallMMIOHandler(0x10006000, 0x10006FFF, &gif_fifo_handler);
    InstallMMIOHandler(0x10007000, 0x10007FFF, &vu_handler);
    InstallMMIOHandler(0x10008000, 0x1000DFFF, &dma_ch_handler);
    InstallMMIOHandler(0x1000E000, 0x1000EFFF, &dma_ctrl_handler);
    InstallMMIOHandler(0x1000F000, 0x1000F5FF, &intc_handler);
}

void InstallMMIOHandler(uint32_t start, uint32_t end, MMIO_Handler* handler) {
    if (!handler || start > end) return;
    handlers.push_back({start, end, handler});
}

uint32_t ReadMMIOWord(uint32_t addr) {
    return GetHandler(addr)->Read32(addr);
}

void WriteMMIOWord(uint32_t addr, uint32_t val) {
    GetHandler(addr)->Write32(addr, val);
}
