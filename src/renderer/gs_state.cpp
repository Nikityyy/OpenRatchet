#include "openratchet/gs_state.h"
#include <iostream>
#include <cstring>

// Global GS register state — written by the GS callback, read by the renderer
GS_State g_gs_state;

void WriteGSReg(GS_State& state, uint8_t reg, uint64_t data) {
    switch (reg) {
        case 0x00: state.PRIM = data; break;
        case 0x01: state.RGBAQ = data; break;
        case 0x02: { // ST — S and T as IEEE 754 floats packed in 64 bits
            std::memcpy(&state.ST_S, &data,     sizeof(float));
            std::memcpy(&state.ST_T, reinterpret_cast<const uint8_t*>(&data) + 4, sizeof(float));
            break;
        }
        case 0x03: { // UV — 16.4 fixed-point, S in [15:0], T in [31:16]
            state.ST_S = (data & 0xFFFF) / 16.0f;
            state.ST_T = ((data >> 16) & 0xFFFF) / 16.0f;
            break;
        }
        case 0x04: state.HWREG = data; break; // XYZF2 — kick, store for inspection
        case 0x05: state.HWREG = data; break; // XYZ2  — kick, store for inspection

        case 0x06: state.TEX0_1 = data; break;
        case 0x07: state.TEX0_2 = data; break;
        case 0x08: state.CLAMP_1 = data; break;
        case 0x09: state.CLAMP_2 = data; break;
        case 0x0A: /* XYZF3 */ break;
        case 0x0B: /* XYZ3 */ break;
        case 0x14: state.TEX1_1 = data; break;
        case 0x15: state.TEX1_2 = data; break;
        case 0x16: state.TEX2_1 = data; break;
        case 0x17: state.TEX2_2 = data; break;
        case 0x18: state.XYOFFSET_1 = data; break;
        case 0x19: state.XYOFFSET_2 = data; break;
        case 0x1A: state.PRMODECONT = data; break;
        case 0x22: state.TEXA = data; break;
        case 0x3D: state.FOGCOL = data; break;
        case 0x40: state.SCISSOR_1 = data; break;
        case 0x41: state.SCISSOR_2 = data; break;
        case 0x42: state.ALPHA_1 = data; break;
        case 0x43: state.ALPHA_2 = data; break;
        case 0x44: state.DIMX = data; break;
        case 0x45: state.DTHE = data; break;
        case 0x46: state.COLCLAMP = data; break;
        case 0x47: state.TEST_1 = data; break;
        case 0x48: state.TEST_2 = data; break;
        case 0x49: state.PABE = data; break;
        case 0x4A: state.FBA_1 = data; break;
        case 0x4B: state.FBA_2 = data; break;
        case 0x4C: state.FRAME_1 = data; break;
        case 0x4D: state.FRAME_2 = data; break;
        case 0x4E: state.ZBUF_1 = data; break;
        case 0x4F: state.ZBUF_2 = data; break;
        case 0x50: state.BITBLTBUF = data; break;
        case 0x51: state.TRXPOS = data; break;
        case 0x52: state.TRXREG = data; break;
        case 0x53: state.TRXDIR = data; break;
        case 0x54: state.HWREG = data; break;
        case 0x60: state.SIGNAL = data; break;
        case 0x61: state.FINISH = data; break;
        case 0x62: state.LABEL = data; break;
        case 0x3F: break; // NOP
        // Note: 0x50 - 0x54 are TRX/BITBLT, 0x60 - 0x62 are sync. We will implement these when we get to image transfers.
        default:
            // Unhandled register, just log quietly or ignore for now to prevent spam
            // std::cout << "WriteGSReg: Unhandled register 0x" << std::hex << (int)reg << std::endl;
            break;
    }
}
