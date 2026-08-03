#include "openratchet/gs_vram.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// ─── PSMCT32 Swizzle Helper ──────────────────────────────────────────────────
// Returns the byte offset from the base of the VRAM buffer for a given (x, y)
// coordinate in a buffer of width `buffer_width_pages`.
// A page is 64x32 pixels (32 blocks). A block is 8x8 pixels.
static uint32_t GetPSMCT32Offset(uint32_t x, uint32_t y, uint32_t buffer_width_pages) {
    // 1. Page coordinates
    const uint32_t page_x = x / 64;
    const uint32_t page_y = y / 32;
    const uint32_t page_index = page_y * buffer_width_pages + page_x;
    
    // 2. Block coordinates within the page
    const uint32_t block_x = (x % 64) / 8;
    const uint32_t block_y = (y % 32) / 8;
    
    // PSMCT32 Block Swizzle Table
    static const uint32_t block_map[4][8] = {
        { 0,  1,  4,  5, 16, 17, 20, 21},
        { 2,  3,  6,  7, 18, 19, 22, 23},
        { 8,  9, 12, 13, 24, 25, 28, 29},
        {10, 11, 14, 15, 26, 27, 30, 31}
    };
    const uint32_t block_index = block_map[block_y][block_x];
    
    // 3. Pixel coordinates within the block
    const uint32_t px = x % 8;
    const uint32_t py = y % 8;
    
    // PSMCT32 Pixel Swizzle: pixels are actually interleaved by column,
    // but the column swizzle translates simply to standard linear layout 
    // within the 8x8 block for 32-bit mode!
    const uint32_t pixel_index = py * 8 + px;
    
    // Calculate total byte offset:
    // Page size = 8192 bytes. Block size = 256 bytes. Pixel = 4 bytes.
    return (page_index * 8192) + (block_index * 256) + (pixel_index * 4);
}

GS_VRAM::GS_VRAM() {}
GS_VRAM::~GS_VRAM() {}

void GS_VRAM::Init() {
    m_vram.resize(4 * 1024 * 1024, 0); // 4 MB PS2 VRAM
}

void GS_VRAM::WriteImage(uint32_t dbp, uint32_t rrw, uint32_t psm,
                         const uint8_t* data, size_t size) {
    // dbp is the base pointer in units of 256 bytes.
    const uint32_t base_addr = dbp * 256u;
    
    // In IMAGE mode, TRXREG contains RRW (width) and RRH (height).
    // The data comes in linearly as (RRW * RRH) pixels.
    // However, we only have 'size' (number of bytes transferred).
    // The DBW (Destination Buffer Width) is required to calculate the page stride.
    // For now, we assume RRW defines the width of the incoming image data.
    // NOTE: If DBW is needed, it must be extracted from BITBLTBUF by the caller.
    // We will assume DBW (in pages) is ceil(rrw / 64.0) for this transfer, which 
    // works for most texture uploads if they are uploaded tightly packed.
    // A more robust implementation would take DBW explicitly.
    const uint32_t dbw_pages = (rrw + 63) / 64;
    
    if (psm == 0x00 || psm == 0x01) { // PSMCT32 / PSMCT24
        const uint32_t num_pixels = size / 4;
        for (uint32_t i = 0; i < num_pixels; ++i) {
            uint32_t px = i % rrw;
            uint32_t py = i / rrw;
            uint32_t offset = GetPSMCT32Offset(px, py, dbw_pages);
            uint32_t byte_addr = base_addr + offset;
            if (byte_addr + 4 <= m_vram.size()) {
                std::memcpy(&m_vram[byte_addr], &data[i * 4], 4);
            }
        }
    } else {
        std::cerr << "[VRAM] WriteImage: Unsupported PSM 0x" << std::hex << psm << "\n";
    }
}

// Helper: read a CLUT (color look-up table) entry at cbp
static uint32_t ReadCLUT32(const uint8_t* vram, uint32_t cbp, uint32_t index) {
    uint32_t addr = cbp * 256 + index * 4;
    uint32_t v;
    std::memcpy(&v, vram + addr, 4);
    return v;
}

void GS_VRAM::ReadTexture32(uint32_t base_ptr, uint32_t tbw, uint32_t width, uint32_t height, uint32_t psm,
                            std::vector<uint32_t>& out_rgba) const {
    out_rgba.resize(width * height, 0xFF00FFFF); // magenta = missing
    const uint32_t base_addr = base_ptr * 256;
    if (base_addr >= m_vram.size()) return;

    const size_t available = m_vram.size() - base_addr;
    const uint8_t* src = m_vram.data() + base_addr;

    switch (psm) {
        case 0x00: // PSMCT32 — 32-bit RGBA
        case 0x01: { // PSMCT24 — 24-bit RGB packed in 32 bits
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    uint32_t offset = GetPSMCT32Offset(x, y, tbw);
                    uint32_t byte_addr = base_addr + offset;
                    if (byte_addr + 4 <= m_vram.size()) {
                        uint32_t pixel;
                        std::memcpy(&pixel, &m_vram[byte_addr], 4);
                        if (psm == 0x01) pixel |= 0xFF000000u; // Alpha = 255 for 24-bit
                        out_rgba[y * width + x] = pixel;
                    }
                }
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
