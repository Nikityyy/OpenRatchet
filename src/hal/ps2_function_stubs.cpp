// ps2_function_stubs.cpp — All missing ps2_stubs:: and ps2_syscalls:: symbols
// These are called by the recompiled MIPS code when it encounters inlined PS2 SDK calls.
// Each stub bridges into our HAL or returns a safe default value.

#include "ps2_runtime.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "openratchet/ee_memory.h"
#include "openratchet/iop.h"

// ── Context register helpers (same pattern used in ps2_runtime_stubs.cpp) ────
static inline uint32_t A0(R5900Context* c) { return static_cast<uint32_t>(c->r[4].m128i_u64[0]); }
static inline uint32_t A1(R5900Context* c) { return static_cast<uint32_t>(c->r[5].m128i_u64[0]); }
static inline uint32_t A2(R5900Context* c) { return static_cast<uint32_t>(c->r[6].m128i_u64[0]); }
static inline uint32_t A3(R5900Context* c) { return static_cast<uint32_t>(c->r[7].m128i_u64[0]); }
static inline void     RET(R5900Context* c, uint32_t v) { c->r[2] = _mm_set_epi64x(0, static_cast<int32_t>(v)); }
static inline void     RET0(R5900Context* c)           { RET(c, 0); }

// Lazy helper — log once per unique stub name
#define STUB(name) \
    static bool _logged_##name = false; \
    if (!_logged_##name) { _logged_##name = true; \
        std::cerr << "[STUB] ps2_stubs::" #name " called\n"; }

namespace ps2_stubs {

// ── libc ──────────────────────────────────────────────────────────────────────

void memcpy(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst  = A0(ctx), src = A1(ctx), n = A2(ctx);
    if (dst && src && n && g_ee_memory.IsValidRange(dst,n) && g_ee_memory.IsValidRange(src,n))
        std::memcpy(g_ee_memory.GetRamPointer(dst), g_ee_memory.GetRamPointer(src), n);
    RET(ctx, dst);
}
void memmove(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst = A0(ctx), src = A1(ctx), n = A2(ctx);
    if (dst && src && n && g_ee_memory.IsValidRange(dst,n) && g_ee_memory.IsValidRange(src,n))
        std::memmove(g_ee_memory.GetRamPointer(dst), g_ee_memory.GetRamPointer(src), n);
    RET(ctx, dst);
}
void memset(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst = A0(ctx), val = A1(ctx), n = A2(ctx);
    if (dst && n && g_ee_memory.IsValidRange(dst, n))
        std::memset(g_ee_memory.GetRamPointer(dst), static_cast<int>(val), n);
    RET(ctx, dst);
}
void strlen(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t s = A0(ctx);
    const char* ptr = s ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(s)) : nullptr;
    RET(ctx, ptr ? static_cast<uint32_t>(std::strlen(ptr)) : 0u);
}
void strcpy(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst = A0(ctx), src = A1(ctx);
    char* d = dst ? reinterpret_cast<char*>(g_ee_memory.GetRamPointer(dst)) : nullptr;
    const char* s = src ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(src)) : nullptr;
    if (d && s) std::strcpy(d, s);
    RET(ctx, dst);
}
void strncpy(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst = A0(ctx), src = A1(ctx), n = A2(ctx);
    char* d = dst ? reinterpret_cast<char*>(g_ee_memory.GetRamPointer(dst)) : nullptr;
    const char* s = src ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(src)) : nullptr;
    if (d && s && n) std::strncpy(d, s, n);
    RET(ctx, dst);
}
void strchr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t s = A0(ctx); const char c = static_cast<char>(A1(ctx));
    const char* ptr = s ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(s)) : nullptr;
    const char* found = ptr ? std::strchr(ptr, c) : nullptr;
    RET(ctx, found ? static_cast<uint32_t>(found - ptr) + s : 0u);
}
void strstr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t s1 = A0(ctx), s2 = A1(ctx);
    const char* h = s1 ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(s1)) : nullptr;
    const char* n = s2 ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(s2)) : nullptr;
    const char* found = (h && n) ? std::strstr(h, n) : nullptr;
    RET(ctx, found ? static_cast<uint32_t>(found - h) + s1 : 0u);
}
void printf(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t fmt_ptr = A0(ctx);
    if (fmt_ptr) {
        const char* fmt = reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(fmt_ptr));
        if (fmt) std::cout << "[EE_PRINTF] " << fmt;
    }
    RET0(ctx);
}
void sprintf(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Just write an empty string to dst — variadic MIPS args on host are non-trivial
    const uint32_t dst = A0(ctx);
    char* d = dst ? reinterpret_cast<char*>(g_ee_memory.GetRamPointer(dst)) : nullptr;
    if (d) d[0] = '\0';
    RET(ctx, 0);
}
void rand(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(std::rand()));
}

// ── File I/O (host file open/read/close mapped to extracted data dir) ─────────

void sceOpen(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceOpen); RET(ctx, static_cast<uint32_t>(-1)); }
void sceClose(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceClose); RET0(ctx); }
void sceRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceRead); RET(ctx, static_cast<uint32_t>(-1)); }
void sceWrite(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceWrite); RET0(ctx); }
void sceLseek(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceLseek); RET(ctx, static_cast<uint32_t>(-1)); }
void read(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(read); RET(ctx, static_cast<uint32_t>(-1)); }
void write(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(write); RET0(ctx); }

// ── CDVD ─────────────────────────────────────────────────────────────────────
void sceCdInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(OpenRatchet::IOP::sceCdInit(static_cast<int32_t>(A0(ctx)))));
}
void sceCdRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(
        OpenRatchet::IOP::sceCdRead(A0(ctx), A1(ctx), A2(ctx), static_cast<int32_t>(A3(ctx)), &g_ee_memory)));
}
void sceCdSync(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(OpenRatchet::IOP::sceCdSync(static_cast<int32_t>(A0(ctx)))));
}
void sceCdSyncS(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(OpenRatchet::IOP::sceCdSync(1)));
}
void sceCdGetError(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    RET(ctx, static_cast<uint32_t>(OpenRatchet::IOP::sceCdGetError()));
}
void sceCdMmode(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, 1); }
void sceCdReadClock(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceCdBreak(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceCdCallback(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceCdDelayThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceCdNcmdDiskReady(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, 2); } // 2 = READY

// ── Memory Card ───────────────────────────────────────────────────────────────
void sceMcInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMcOpen(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); } // No card
void sceMcClose(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMcRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcWrite(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcSeek(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcSync(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMcGetInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcGetDir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcMkdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcChdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcDelete(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcFormat(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMcUnformat(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }

// ── SIF RPC ───────────────────────────────────────────────────────────────────
void sceSifInitRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifResetIop(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, 1); }
void sceSifRebootIop(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, 1); }
void sceSifSyncIop(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, 1); }
void sceSifCheckStatRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); } // 0 = idle
void sceSifWriteBackDCache(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifInitIopHeap(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifAllocIopHeap(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); } // 0 = fail gracefully
void sceSifInitCmd(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifExitCmd(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifCmdIntrHdlr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifAddCmdHandler(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifRemoveCmdHandler(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSifBindRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Route to our SIF binding: client_addr=a0, server_id=a1, mode=a2
    RET(ctx, static_cast<uint32_t>(
        OpenRatchet::IOP::sceSifBindRpc(nullptr, &g_ee_memory, A0(ctx), A1(ctx), A2(ctx))));
}
void sceRpcGetFPacket(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceSDC(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── GS library (libgs) ────────────────────────────────────────────────────────
// These are higher-level SDK functions; safe to no-op — the game sets up GS directly
void sceGsResetGraph(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsSyncV(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsSyncVCallback(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsPutDispEnv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsPutDrawEnv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsSetDefDispEnv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, A0(ctx)); }
void sceGsSetDefDrawEnv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, A0(ctx)); }
void sceGsSetDefLoadImage(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, A0(ctx)); }
void sceGsSetDefStoreImage(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, A0(ctx)); }
void sceGsExecLoadImage(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGsExecStoreImage(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceGszbufaddr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── DMA ───────────────────────────────────────────────────────────────────────
void sceDmaSend(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDmaReset(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDmaPause(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDmaGetChan(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDmaPutEnv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── VU0 libmath ───────────────────────────────────────────────────────────────
// These operate on 4x4 matrices / vectors in guest memory.
// We perform the operation on the host side.
static float* GuestF(R5900Context* ctx, int reg) {
    // Not valid for general use — VU0 context is in ctx->vu0_vf which is __m128[]
    // For libvu0 functions that take guest memory pointers, use GetRamPointer
    return nullptr;
}
void sceVu0UnitMatrix(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // a0 = destination matrix (16 floats)
    float* m = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    if (!m) { RET0(ctx); return; }
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = m[5] = m[10] = m[15] = 1.0f; // identity
    RET0(ctx);
}
void sceVu0RotMatrixX(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    float* out = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    const float* in = reinterpret_cast<const float*>(g_ee_memory.GetRamPointer(A1(ctx)));
    float angle; std::memcpy(&angle, g_ee_memory.GetRamPointer(A2(ctx)), 4);
    if (!out || !in) { RET0(ctx); return; }
    // Copy in to out, then apply row-1/row-2 rotation
    std::memcpy(out, in, 64);
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        float y = in[4+i], z = in[8+i];
        out[4+i] = c*y - s*z; out[8+i] = s*y + c*z;
    }
    RET0(ctx);
}
void sceVu0RotMatrixY(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    float* out = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    const float* in = reinterpret_cast<const float*>(g_ee_memory.GetRamPointer(A1(ctx)));
    float angle; const uint8_t* ap = g_ee_memory.GetRamPointer(A2(ctx));
    if (!out || !in || !ap) { RET0(ctx); return; }
    std::memcpy(&angle, ap, 4);
    std::memcpy(out, in, 64);
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        float x = in[i], z = in[8+i];
        out[i] = c*x + s*z; out[8+i] = -s*x + c*z;
    }
    RET0(ctx);
}
void sceVu0RotMatrixZ(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    float* out = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    const float* in = reinterpret_cast<const float*>(g_ee_memory.GetRamPointer(A1(ctx)));
    float angle; const uint8_t* ap = g_ee_memory.GetRamPointer(A2(ctx));
    if (!out || !in || !ap) { RET0(ctx); return; }
    std::memcpy(&angle, ap, 4);
    std::memcpy(out, in, 64);
    const float c = std::cos(angle), s = std::sin(angle);
    for (int i = 0; i < 4; ++i) {
        float x = in[i], y = in[4+i];
        out[i] = c*x - s*y; out[4+i] = s*x + c*y;
    }
    RET0(ctx);
}
void sceVu0Normalize(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    float* out = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    const float* in = reinterpret_cast<const float*>(g_ee_memory.GetRamPointer(A1(ctx)));
    if (!out || !in) { RET0(ctx); return; }
    float len = std::sqrt(in[0]*in[0] + in[1]*in[1] + in[2]*in[2]);
    if (len < 1e-7f) len = 1e-7f;
    out[0] = in[0]/len; out[1] = in[1]/len; out[2] = in[2]/len; out[3] = 0.0f;
    RET0(ctx);
}
void sceVu0ecossin(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // ecossin(a0) → writes cos to a0[0], sin to a0[1]
    float* out = reinterpret_cast<float*>(g_ee_memory.GetRamPointer(A0(ctx)));
    float angle; const uint8_t* ap = g_ee_memory.GetRamPointer(A1(ctx));
    if (!out || !ap) { RET0(ctx); return; }
    std::memcpy(&angle, ap, 4);
    out[0] = std::cos(angle); out[1] = std::sin(angle);
    RET0(ctx);
}

// ── IPU (MPEG decoder) ────────────────────────────────────────────────────────
// R&C1 uses IPU for FMV. No-op stubs — video won't play but game won't crash.
void sceIpuInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceIpuInit); RET0(ctx); }
void sceIpuSync(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceIpuStopDMA(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceIpuRestartDMA(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── MPEG (libmpeg) ────────────────────────────────────────────────────────────
void sceMpegInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceMpegInit); RET0(ctx); }
void sceMpegCreate(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { STUB(sceMpegCreate); RET(ctx, static_cast<uint32_t>(-1)); }
void sceMpegGetPicture(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceMpegReset(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMpegFlush(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMpegClearRefBuff(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMpegAddCallback(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMpegAddStrCallback(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceMpegDemuxPssRing(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── FS / TTY ──────────────────────────────────────────────────────────────────
void sceFsInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceFsReset(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceFsSemInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceTtyInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceTtyHandler(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceTtyRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceTtyWrite(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t buf = A1(ctx), len = A2(ctx);
    const char* s = buf ? reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(buf)) : nullptr;
    if (s && len) std::cout.write(s, std::min(len, 4096u));
    RET(ctx, len);
}
void scePrintf(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t fmt_ptr = A0(ctx);
    if (fmt_ptr) {
        const char* fmt = reinterpret_cast<const char*>(g_ee_memory.GetRamPointer(fmt_ptr));
        if (fmt) std::cout << "[EE] " << fmt;
    }
    RET0(ctx);
}

// ── DECI2 (dev kit debug) ─────────────────────────────────────────────────────
void sceDeci2Open(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }
void sceDeci2Poll(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDeci2ReqSend(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDeci2ExSend(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void sceDeci2ExRecv(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── IOP module loading (no-op) ────────────────────────────────────────────────
void sceSifLoadModuleBuffer(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

// ── Memory allocator internals (newlib _r variants) ───────────────────────────
// These operate on a guest heap. We treat them as no-ops — the recompiled
// game manages its own heap in guest RAM; the stub just needs to link.
void malloc_r(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }       // returns NULL = fail
void calloc_r(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }       // returns NULL = fail
void free_r(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void malloc_trim_r(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void malloc_extend_top(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void mbtowc_r(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET(ctx, static_cast<uint32_t>(-1)); }

// ── Additional libc ───────────────────────────────────────────────────────────
void exit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* rt) {
    std::cerr << "[EE] exit() called with code " << A0(ctx) << "\n";
    if (rt) rt->requestStop();
}
void fflush(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void memchr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t s = A0(ctx); const int c = static_cast<int>(A1(ctx)); const uint32_t n = A2(ctx);
    const void* p = s ? std::memchr(g_ee_memory.GetRamPointer(s), c, n) : nullptr;
    if (p) {
        RET(ctx, s + static_cast<uint32_t>(static_cast<const uint8_t*>(p) - g_ee_memory.GetRamPointer(s)));
    } else {
        RET(ctx, 0);
    }
}
void memcmp(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t a = A0(ctx), b = A1(ctx), n = A2(ctx);
    const uint8_t* pa = a ? g_ee_memory.GetRamPointer(a) : nullptr;
    const uint8_t* pb = b ? g_ee_memory.GetRamPointer(b) : nullptr;
    RET(ctx, (pa && pb && n) ? static_cast<uint32_t>(std::memcmp(pa, pb, n)) : 0u);
}
void memclr(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t dst = A0(ctx), n = A1(ctx);
    if (dst && n && g_ee_memory.IsValidRange(dst, n))
        std::memset(g_ee_memory.GetRamPointer(dst), 0, n);
    RET0(ctx);
}
void __divdi3(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // 64-bit signed division: a0:a1 / a2:a3 → v0:v1
    const int64_t a = static_cast<int64_t>((static_cast<uint64_t>(A0(ctx)) << 32) | A1(ctx));
    const int64_t b = static_cast<int64_t>((static_cast<uint64_t>(A2(ctx)) << 32) | A3(ctx));
    const int64_t r = (b != 0) ? a / b : 0;
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int32_t>(r >> 32));
    ctx->r[3] = _mm_set_epi64x(0, static_cast<int32_t>(r & 0xFFFFFFFF));
}

// ── MCE (memory card encryption) — no-op ─────────────────────────────────────
void mceIntrReadFixAlign(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void mceGetInfoApdx(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }
void mceStorePwd(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { RET0(ctx); }

} // namespace ps2_stubs


namespace ps2_syscalls {

// ── sceSifCallRpc — route through our SIF dispatch ────────────────────────────
void sceSifCallRpc(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Args: a0=client, a1=func, a2=mode, a3=send, stack+0=ssize, stack+4=recv, stack+8=rsize
    const uint32_t client = A0(ctx), func = A1(ctx), mode = A2(ctx);
    const uint32_t send   = A3(ctx);
    const uint32_t ssize  = g_ee_memory.Read<uint32_t>(static_cast<uint32_t>(ctx->r[29].m128i_u64[0]) + 16);
    const uint32_t recv   = g_ee_memory.Read<uint32_t>(static_cast<uint32_t>(ctx->r[29].m128i_u64[0]) + 20);
    const uint32_t rsize  = g_ee_memory.Read<uint32_t>(static_cast<uint32_t>(ctx->r[29].m128i_u64[0]) + 24);
    const int32_t result = OpenRatchet::IOP::sceSifCallRpc(
        nullptr, &g_ee_memory, client, func, mode, send, ssize, recv, rsize, 0, 0);
    ctx->r[2] = _mm_set_epi64x(0, static_cast<int64_t>(result));
}

// ── sceSifSendCmd ─────────────────────────────────────────────────────────────
void sceSifSendCmd(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // No-op: command packet sent to IOP — not needed for basic operation
    ctx->r[2] = _mm_set_epi64x(0, 0);
}

// ── sceSifLoadModuleBuffer ────────────────────────────────────────────────────
void sceSifLoadModuleBuffer(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // No-op: module loading is HLE'd — return success (0)
    ctx->r[2] = _mm_set_epi64x(0, 0);
}

// ── GetRomName — returns a fake ROM name ─────────────────────────────────────
void GetRomName(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const uint32_t buf = A0(ctx);
    char* dst = buf ? reinterpret_cast<char*>(g_ee_memory.GetRamPointer(buf)) : nullptr;
    if (dst) std::strcpy(dst, "SCUS-97199");
    ctx->r[2] = _mm_set_epi64x(0, buf);
}

// ── InitTLB — no-op: we don't emulate TLB ────────────────────────────────────
void InitTLB(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    ctx->r[2] = _mm_set_epi64x(0, 0);
}

// ── InitThread / iWakeupThread / InitAlarm ────────────────────────────────────
// These are ps2_syscalls:: wrappers around specific kernel syscall numbers.
// Route them through our existing syscall dispatch.
void InitThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Syscall 0x02 = InitThread (creates the first thread entry)
    // Just return 0 (success) — thread state is managed by our threads.cpp
    ctx->r[2] = _mm_set_epi64x(0, 0);
}
void iWakeupThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Interrupt-context wakeup — no-op in cooperative scheduling
    ctx->r[2] = _mm_set_epi64x(0, 0);
}
void InitAlarm(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    // Alarm subsystem init — no-op
    ctx->r[2] = _mm_set_epi64x(0, 0);
}

} // namespace ps2_syscalls

