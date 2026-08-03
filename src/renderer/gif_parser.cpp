#include "openratchet/gif_parser.h"
#include <iostream>
#include <cstring>

// How many verts until a primitive is complete
static int vertsForPrim(uint8_t ptype) {
    switch (ptype & 7) {
        case 0: return 1;  // Point
        case 1: return 2;  // Line
        case 2: return 2;  // Line Strip (emit on every 2nd)
        case 3: return 3;  // Triangle
        case 4: return 3;  // Triangle Strip
        case 5: return 3;  // Triangle Fan
        case 6: return 2;  // Sprite (2 corners)
        default: return 3;
    }
}

GIF_Tag GIF_Parser::DecodeTag(uint64_t tag0, uint64_t tag1) {
    GIF_Tag tag;
    tag.NLOOP = tag0 & 0x7FFF;
    tag.EOP   = (tag0 >> 15) & 1;
    tag.PRE   = (tag0 >> 46) & 0x1;
    tag.PRIM  = (tag0 >> 47) & 0x7FF;
    tag.FLG   = (tag0 >> 58) & 0x3;
    tag.NREG  = (tag0 >> 60) & 0xF;
    if (tag.NREG == 0) tag.NREG = 16;
    tag.REG = tag1;
    return tag;
}

size_t GIF_Parser::ParsePacket(GS_State& gs, const uint8_t* data, size_t size,
                               DrawCallback on_draw) {
    if (size < 16) return 0;
    size_t offset = 0;
    bool eop = false;

    while (offset + 16 <= size && !eop) {
        uint64_t tag0, tag1;
        std::memcpy(&tag0, data + offset, 8);
        std::memcpy(&tag1, data + offset + 8, 8);
        offset += 16;

        GIF_Tag tag = DecodeTag(tag0, tag1);
        eop = tag.EOP;

        if (tag.PRE) {
            WriteGSReg(gs, 0x00, tag.PRIM);
            m_primType = gs.PRIM & 0x7;
        }

        size_t consumed = 0;
        switch (tag.FLG) {
            case 0: consumed = ProcessPacked (gs, tag, data + offset, size - offset, on_draw); break;
            case 1: consumed = ProcessReglist(gs, tag, data + offset, size - offset, on_draw); break;
            case 2: consumed = ProcessImage  (gs, tag, data + offset, size - offset); break;
            case 3: consumed = 0; break;
        }
        offset += consumed;
        m_packetsProcessed++;
    }
    return offset;
}

// ─── Vertex push / primitive flush ───────────────────────────────────────────

void GIF_Parser::PushVertex(GS_State& gs, bool kick, DrawCallback& cb) {
    // RGBAQ: R[7:0] G[15:8] B[23:16] A[31:24] Q[63:32]
    const uint64_t rgbaq = gs.RGBAQ;
    m_current.r = (rgbaq >>  0) & 0xFF;
    m_current.g = (rgbaq >>  8) & 0xFF;
    m_current.b = (rgbaq >> 16) & 0xFF;
    m_current.a = (rgbaq >> 24) & 0xFF;
    uint32_t q_bits = static_cast<uint32_t>(rgbaq >> 32);
    std::memcpy(&m_current.q, &q_bits, 4);

    // ST texture coordinates come from gs.ST_S / gs.ST_T
    // (set by WriteGSReg when reg 0x02/0x03 is written)
    m_current.s = gs.ST_S;
    m_current.t = gs.ST_T;

    m_verts.push_back(m_current);

    if (kick) {
        FlushPrimitive(m_primType, cb);
    }
}

void GIF_Parser::FlushPrimitive(uint8_t prim_type, DrawCallback& cb) {
    if (m_verts.empty()) return;
    if (cb) {
        cb(prim_type, m_verts);
        m_drawCalls++;
    }
    m_verts.clear();
}

// ─── PACKED format ───────────────────────────────────────────────────────────

size_t GIF_Parser::ProcessPacked(GS_State& gs, const GIF_Tag& tag,
                                  const uint8_t* data, size_t size, DrawCallback& cb) {
    size_t offset = 0;
    m_primType = gs.PRIM & 0x7;

    for (uint32_t loop = 0; loop < tag.NLOOP; ++loop) {
        for (uint32_t r = 0; r < tag.NREG; ++r) {
            if (offset + 16 > size) return offset;
            uint64_t val0, val1;
            std::memcpy(&val0, data + offset, 8);
            std::memcpy(&val1, data + offset + 8, 8);
            offset += 16;

            uint8_t reg = (tag.REG >> (r * 4)) & 0xF;

            if (reg == 0xE) { // A+D
                uint8_t addr = val1 & 0x7F;
                WriteGSReg(gs, addr, val0);
                if (addr == 0x00) m_primType = gs.PRIM & 0x7; // PRIM changed
            } else {
                // PACKED register descriptors (GS User Manual Table 7.1)
                // 0x00=PRIM 0x01=RGBAQ 0x02=ST 0x03=UV 0x04=XYZF2 0x05=XYZ2
                // 0x06=TEX0_1 0x07=TEX0_2 0x08=CLAMP_1 0x09=CLAMP_2
                // 0x0A=XYZF3 0x0B=XYZ3 (no kick) 0x0C=AD 0x0F=NOP
                switch (reg) {
                    case 0x01: // RGBAQ
                        gs.RGBAQ = val0;
                        break;
                    case 0x02: { // ST  (s,t as IEEE floats, q from RGBAQ)
                        float s, t;
                        std::memcpy(&s, &val0, 4);
                        std::memcpy(&t, ((uint8_t*)&val0) + 4, 4);
                        m_current.s = s;
                        m_current.t = t;
                        break;
                    }
                    case 0x03: { // UV (16.4 fixed point, s in [15:0], t in [31:16])
                        m_current.s = (val0 & 0xFFFF) / 16.0f;
                        m_current.t = ((val0 >> 16) & 0xFFFF) / 16.0f;
                        break;
                    }
                    case 0x04: { // XYZF2 — kick (finalise vertex)
                        m_current.x = (val0 >>  0) & 0xFFFF;
                        m_current.y = (val0 >> 16) & 0xFFFF;
                        m_current.z = (val0 >> 32) & 0xFFFFFF;
                        m_current.fog = (val0 >> 56) & 0xFF;
                        WriteGSReg(gs, 0x04, val0);
                        PushVertex(gs, true, cb);
                        break;
                    }
                    case 0x05: { // XYZ2 — kick
                        m_current.x = (val0 >>  0) & 0xFFFF;
                        m_current.y = (val0 >> 16) & 0xFFFF;
                        m_current.z = (val0 >> 32) & 0xFFFFFFFF;
                        WriteGSReg(gs, 0x05, val0);
                        PushVertex(gs, true, cb);
                        break;
                    }
                    case 0x0A: { // XYZF3 — no kick
                        m_current.x = (val0 >>  0) & 0xFFFF;
                        m_current.y = (val0 >> 16) & 0xFFFF;
                        m_current.z = (val0 >> 32) & 0xFFFFFF;
                        m_current.fog = (val0 >> 56) & 0xFF;
                        WriteGSReg(gs, 0x0A, val0);
                        m_verts.push_back(m_current);
                        break;
                    }
                    case 0x0B: { // XYZ3 — no kick
                        m_current.x = (val0 >>  0) & 0xFFFF;
                        m_current.y = (val0 >> 16) & 0xFFFF;
                        m_current.z = (val0 >> 32) & 0xFFFFFFFF;
                        WriteGSReg(gs, 0x0B, val0);
                        m_verts.push_back(m_current);
                        break;
                    }
                    default:
                        WriteGSReg(gs, reg, val0);
                        break;
                }
            }
        }
    }
    return offset;
}

// ─── REGLIST format ──────────────────────────────────────────────────────────

size_t GIF_Parser::ProcessReglist(GS_State& gs, const GIF_Tag& tag,
                                   const uint8_t* data, size_t size, DrawCallback& cb) {
    size_t offset = 0;
    m_primType = gs.PRIM & 0x7;

    for (uint32_t loop = 0; loop < tag.NLOOP; ++loop) {
        for (uint32_t r = 0; r < tag.NREG; ++r) {
            if (offset + 8 > size) return offset;
            uint64_t val;
            std::memcpy(&val, data + offset, 8);
            offset += 8;

            uint8_t reg = (tag.REG >> (r * 4)) & 0xF;
            if (reg == 0xE) {
                // A+D in reglist: skip (no address word available)
            } else {
                WriteGSReg(gs, reg, val);
                // Handle vertex kick registers the same as PACKED
                if (reg == 0x04 || reg == 0x05) {
                    m_current.x = (val >> 0) & 0xFFFF;
                    m_current.y = (val >> 16) & 0xFFFF;
                    m_current.z = (val >> 32) & 0xFFFFFF;
                    PushVertex(gs, true, cb);
                } else if (reg == 0x0A || reg == 0x0B) {
                    m_current.x = (val >> 0) & 0xFFFF;
                    m_current.y = (val >> 16) & 0xFFFF;
                    m_current.z = (val >> 32) & 0xFFFFFF;
                    m_verts.push_back(m_current);
                } else if (reg == 0x01) {
                    gs.RGBAQ = val;
                }
            }
        }
    }
    return offset;
}

// ─── IMAGE format ────────────────────────────────────────────────────────────

size_t GIF_Parser::ProcessImage(GS_State& gs, const GIF_Tag& tag,
                                 const uint8_t* data, size_t size) {
    // IMAGE (TRXDIR) mode: raw pixel data written to VRAM via HWREG.
    // NLOOP is the number of 128-bit (16-byte) data words.
    const size_t consumed = std::min(static_cast<size_t>(tag.NLOOP) * 16u, size);
    // Store BITBLTBUF/TRXPOS/TRXREG state is already in gs_state.
    // We store a pointer-sized tag in HWREG so the renderer can pick it up.
    // The actual VRAM write is done by the caller (VulkanRenderer) which has
    // a reference to GS_VRAM. Here we mark the transfer in the state.
    // (This field is safe to overwrite; it's per-transfer.)
    gs.HWREG = static_cast<uint64_t>(consumed); // bytes consumed this transfer
    (void)data; // data pointer available for the renderer to use via GS_VRAM::WriteImage
    return consumed;
}
