#include "openratchet/gs_vram.h"
#include <iostream>
#include <cstring>
#include <algorithm>

GS_VRAM::GS_VRAM() {}
GS_VRAM::~GS_VRAM() {}

void GS_VRAM::Init() {
    m_vram.resize(4 * 1024 * 1024, 0); // 4 MB PS2 VRAM
}

void GS_VRAM::WriteImage(uint32_t base_ptr, uint32_t /*width*/, uint32_t /*psm*/,
                         const uint8_t* data, size_t size) {
    // base_ptr is in 64-word (256-byte) blocks
    uint32_t byte_addr = base_ptr * 256;
    if (byte_addr >= m_vram.size()) return;
    size_t safe = std::min(size, m_vram.size() - byte_addr);
    if (safe < size) std::cerr << "[VRAM] WriteImage truncated — out of bounds\n";
    std::memcpy(&m_vram[byte_addr], data, safe);
}

// Helper: read a CLUT (color look-up table) entry at cbp
static uint32_t ReadCLUT32(const uint8_t* vram, uint32_t cbp, uint32_t index) {
    uint32_t addr = cbp * 256 + index * 4;
    uint32_t v;
    std::memcpy(&v, vram + addr, 4);
    return v;
}

void GS_VRAM::ReadTexture32(uint32_t base_ptr, uint32_t width, uint32_t height, uint32_t psm,
                            std::vector<uint32_t>& out_rgba) const {
    out_rgba.resize(width * height, 0xFF00FFFF); // magenta = missing
    uint32_t byte_addr = base_ptr * 256;
    if (byte_addr >= m_vram.size()) return;
    const uint8_t* src = m_vram.data() + byte_addr;
    size_t available = m_vram.size() - byte_addr;

    switch (psm) {
        case 0x00: // PSMCT32 — 32-bit RGBA
        case 0x01: { // PSMCT24 — 24-bit RGB packed in 32 bits
            size_t needed = (size_t)width * height * 4;
            if (needed > available) needed = available;
            std::memcpy(out_rgba.data(), src, needed);
            // For PSMCT24 the alpha byte is undefined on PS2; set it to 0xFF
            if (psm == 0x01) {
                for (uint32_t i = 0; i < width * height; ++i)
                    out_rgba[i] |= 0xFF000000u;
            }
            break;
        }
        case 0x02: // PSMCT16  — RGB5A1 (little-endian, B0 G1 R2 A0)
        case 0x0A: { // PSMCT16S
            size_t needed = (size_t)width * height * 2;
            if (needed > available) needed = available;
            for (uint32_t i = 0; i < width * height && (i * 2 + 1) < available; ++i) {
                uint16_t p;
                std::memcpy(&p, src + i * 2, 2);
                uint8_t r = (uint8_t)((p & 0x001F) << 3);
                uint8_t g = (uint8_t)(((p >> 5) & 0x001F) << 3);
                uint8_t b = (uint8_t)(((p >> 10) & 0x001F) << 3);
                uint8_t a = (p & 0x8000) ? 0xFF : 0x00;
                out_rgba[i] = r | (g << 8) | (b << 16) | (a << 24);
            }
            break;
        }
        case 0x13: { // PSMT8 — 8-bit indexed, CLUT at CLUT base pointer (we use 0 for M8)
            // Without knowing the CLUT base, output grayscale
            for (uint32_t i = 0; i < width * height && i < available; ++i) {
                uint8_t idx = src[i];
                out_rgba[i] = idx | (idx << 8) | (idx << 16) | 0xFF000000u;
            }
            break;
        }
        case 0x14: { // PSMT4 — 4-bit indexed, 2 pixels per byte
            for (uint32_t i = 0; i < width * height; ++i) {
                uint32_t byte_idx = i / 2;
                if (byte_idx >= available) break;
                uint8_t b2 = src[byte_idx];
                uint8_t idx = (i & 1) ? (b2 >> 4) : (b2 & 0xF);
                uint8_t v = idx * 17; // expand 0-15 to 0-255
                out_rgba[i] = v | (v << 8) | (v << 16) | 0xFF000000u;
            }
            break;
        }
        default:
            std::cerr << "[VRAM] Unknown PSM 0x" << std::hex << psm << std::dec << " — returning magenta\n";
            break;
    }
}
