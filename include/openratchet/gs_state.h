#pragma once
#include <cstdint>

// Graphics Synthesizer (GS) register state
struct GS_State {
    // Frame buffer
    uint64_t FRAME_1;   // base pointer, width, PSM, FBMSK
    uint64_t FRAME_2;
    uint64_t ZBUF_1;    // z-buffer base, PSM, ZMSK
    uint64_t ZBUF_2;
    uint64_t SCISSOR_1; // scissor rectangle
    uint64_t SCISSOR_2;
    uint64_t XYOFFSET_1;// drawing offset
    uint64_t XYOFFSET_2;
    
    // Texture
    uint64_t TEX0_1;    // texture base, width, PSM, TW, TH, TCC, TFX, etc.
    uint64_t TEX0_2;
    uint64_t TEX1_1;    // LOD params
    uint64_t TEX1_2;
    uint64_t TEX2_1;
    uint64_t TEX2_2;
    uint64_t TEXA;
    uint64_t TEXCLUT;
    uint64_t TEXFLUSH;
    uint64_t CLAMP_1;   // clamp/repeat/region modes
    uint64_t CLAMP_2;
    
    // Alpha and testing
    uint64_t ALPHA_1;   // alpha blending equation
    uint64_t ALPHA_2;
    uint64_t TEST_1;    // alpha/z/destination test
    uint64_t TEST_2;
    uint64_t PABE;
    uint64_t FBA_1;
    uint64_t FBA_2;
    
    // Primitive
    uint64_t PRIM;      // primitive type, shading, texture, fog, alpha, etc.
    uint64_t PRMODECONT;
    
    // Colors
    uint64_t RGBAQ;     // current vertex color
    uint64_t FOG;
    uint64_t FOGCOL;

    // Current texture coordinates (not a real GS register — accumulated per-vertex)
    float ST_S = 0.0f;  // from GS reg 0x02 (ST)
    float ST_T = 0.0f;
    
    // Others
    uint64_t DIMX;
    uint64_t DTHE;
    uint64_t COLCLAMP;
    uint64_t BITBLTBUF;
    uint64_t TRXPOS;
    uint64_t TRXREG;
    uint64_t TRXDIR;
    uint64_t HWREG;
    
    // Control
    uint64_t SIGNAL;
    uint64_t FINISH;
    uint64_t LABEL;
};

// Update the current state based on a GS register write
void WriteGSReg(GS_State& state, uint8_t reg, uint64_t data);

// Global GS state used by the renderer and the GS write callback
extern GS_State g_gs_state;
