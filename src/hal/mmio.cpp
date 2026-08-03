#include "openratchet/mmio.h"
#include <vector>
#include <unordered_map>
#include <iostream>
#include <iomanip>

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

static RegisterHandler timer_handler("Timers");
static RegisterHandler ipu_handler("IPU");
static RegisterHandler gif_handler("GIF");
static RegisterHandler vif0_handler("VIF0");
static RegisterHandler vif1_handler("VIF1");
static RegisterHandler vu_handler("VU0/VU1 Mem");
static RegisterHandler dma_ch_handler("DMA Channels");
static RegisterHandler dma_ctrl_handler("DMA Control");
static RegisterHandler intc_handler("INTC/SBUS/Timer Ctrl");
static RegisterHandler default_handler("Unknown MMIO");
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
    InstallMMIOHandler(0x10001000, 0x10001FFF, &gif_handler);
    InstallMMIOHandler(0x10002000, 0x100023FF, &default_handler);
    InstallMMIOHandler(0x10003000, 0x100037FF, &vif0_handler);
    InstallMMIOHandler(0x10003800, 0x10003FFF, &vif1_handler);
    InstallMMIOHandler(0x10004000, 0x10007FFF, &vu_handler);
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
