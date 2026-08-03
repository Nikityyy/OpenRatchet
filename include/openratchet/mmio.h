#pragma once
#include <cstdint>
#include <memory>
#include <cstdint>

class MMIO_Handler {
public:
    virtual ~MMIO_Handler() = default;
    virtual uint32_t Read32(uint32_t addr) { return 0; }
    virtual void Write32(uint32_t addr, uint32_t val) {}
};

void RegisterMMIOHandlers();
void InstallMMIOHandler(uint32_t start, uint32_t end, MMIO_Handler* handler);
uint32_t ReadMMIOWord(uint32_t addr);
void WriteMMIOWord(uint32_t addr, uint32_t val);
