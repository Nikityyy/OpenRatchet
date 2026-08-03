#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>
#include <functional>
#include "openratchet/gs_state.h"

struct GIF_Tag {
    uint16_t NLOOP;
    bool EOP;
    bool PRE;
    uint16_t PRIM;
    uint8_t FLG;
    uint8_t NREG;
    uint64_t REG;
};

// Vertex as seen by the GIF parser — accumulates from GS registers
struct GIF_Vertex {
    int32_t  x, y, z;   // XYZ fixed-point (XYZ2/XYZF2, 4-bit subpixel)
    uint8_t  r, g, b, a; // RGBAQ
    float    s, t;       // ST coords (or UV in 4-bit fixed)
    float    q;          // Q (for perspective correct texturing)
    uint8_t  fog;        // XYZF2 fog value
};

// Callback fired when a full primitive is ready to be drawn
using DrawCallback = std::function<void(uint8_t prim_type, const std::vector<GIF_Vertex>&)>;

class GIF_Parser {
public:
    // Parse a GIF packet. Calls on_draw each time a primitive is complete.
    size_t ParsePacket(GS_State& gs, const uint8_t* data, size_t size,
                       DrawCallback on_draw = nullptr);

    // Stats
    uint32_t GetPacketsProcessed() const { return m_packetsProcessed; }
    uint32_t GetDrawCalls()        const { return m_drawCalls; }

private:
    GIF_Tag DecodeTag(uint64_t tag0, uint64_t tag1);

    size_t ProcessPacked (GS_State& gs, const GIF_Tag& tag, const uint8_t* data, size_t size, DrawCallback& cb);
    size_t ProcessReglist(GS_State& gs, const GIF_Tag& tag, const uint8_t* data, size_t size, DrawCallback& cb);
    size_t ProcessImage  (GS_State& gs, const GIF_Tag& tag, const uint8_t* data, size_t size);

    // Vertex accumulation
    void PushVertex(GS_State& gs, bool kick, DrawCallback& cb);
    void FlushPrimitive(uint8_t prim_type, DrawCallback& cb);

    GIF_Vertex m_current{};
    std::vector<GIF_Vertex> m_verts;
    uint8_t m_primType = 0;

    uint32_t m_packetsProcessed = 0;
    uint32_t m_drawCalls = 0;
};
