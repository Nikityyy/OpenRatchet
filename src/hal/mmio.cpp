#include "openratchet/mmio.h"
#include <unordered_map>
#include <iostream>
#include <iomanip>

struct MMIORange {
    uint32_t start;
    uint32_t end;
    const char* name;
};

static const MMIORange MMIO_RANGES[] = {
    {0x10000000, 0x100003FF, "Timers"},
    {0x10000800, 0x10000BFF, "IPU"},
    {0x10001000, 0x10001FFF, "GIF"},
    {0x10002000, 0x100023FF, "Reserved"},
    {0x10003000, 0x100037FF, "VIF0"},
    {0x10003800, 0x10003FFF, "VIF1"},
    {0x10004000, 0x10007FFF, "VU0/VU1 Mem"},
    {0x10008000, 0x1000DFFF, "DMA Channels"},
    {0x1000E000, 0x1000EFFF, "DMA Control"},
    {0x1000F000, 0x1000F5FF, "INTC/SBUS/Timer Ctrl"}
};

class DummyHandler : public MMIO_Handler {
    const char* name;
public:
    DummyHandler(const char* n) : name(n) {}
    
    uint32_t Read32(uint32_t addr) override {
        // std::cout << "[MMIO READ] " << name << " at 0x" << std::hex << addr << std::dec << std::endl;
        return 0;
    }
    void Write32(uint32_t addr, uint32_t val) override {
        // std::cout << "[MMIO WRITE] " << name << " at 0x" << std::hex << addr << " = 0x" << val << std::dec << std::endl;
    }
};

static DummyHandler timer_handler("Timers");
static DummyHandler ipu_handler("IPU");
static DummyHandler gif_handler("GIF");
static DummyHandler vif0_handler("VIF0");
static DummyHandler vif1_handler("VIF1");
static DummyHandler vu_handler("VU0/VU1 Mem");
static DummyHandler dma_ch_handler("DMA Channels");
static DummyHandler dma_ctrl_handler("DMA Control");
static DummyHandler intc_handler("INTC/SBUS/Timer Ctrl");
static DummyHandler default_handler("Unknown MMIO");

static MMIO_Handler* GetHandler(uint32_t addr) {
    for (const auto& range : MMIO_RANGES) {
        if (addr >= range.start && addr <= range.end) {
            if (addr <= 0x100003FF) return &timer_handler;
            if (addr >= 0x10000800 && addr <= 0x10000BFF) return &ipu_handler;
            if (addr >= 0x10001000 && addr <= 0x10001FFF) return &gif_handler;
            if (addr >= 0x10003000 && addr <= 0x100037FF) return &vif0_handler;
            if (addr >= 0x10003800 && addr <= 0x10003FFF) return &vif1_handler;
            if (addr >= 0x10004000 && addr <= 0x10007FFF) return &vu_handler;
            if (addr >= 0x10008000 && addr <= 0x1000DFFF) return &dma_ch_handler;
            if (addr >= 0x1000E000 && addr <= 0x1000EFFF) return &dma_ctrl_handler;
            if (addr >= 0x1000F000 && addr <= 0x1000F5FF) return &intc_handler;
        }
    }
    return &default_handler;
}

void RegisterMMIOHandlers() {
    // Future: specific subclasses for each subsystem.
}

uint32_t ReadMMIOWord(uint32_t addr) {
    return GetHandler(addr)->Read32(addr);
}

void WriteMMIOWord(uint32_t addr, uint32_t val) {
    GetHandler(addr)->Write32(addr, val);
}
