#include "openratchet/syscalls.h"
#include <array>
#include <mutex>

namespace OpenRatchet::Kernel {

// ── Semaphores ────────────────────────────────────────────────────────────────

struct Sema { int count = 0, max = 0; bool used = false; };
static std::mutex g_sema_mutex;
static std::array<Sema, 256> g_semas{};
static uint32_t g_sema_next = 1;

static uint32_t sema_result(MIPS_EE_Context* c, int v) {
    c->r[2] = static_cast<uint64_t>(static_cast<int64_t>(v));
    return c->r[2];
}

static void SysCreateSema(MIPS_EE_Context* c, EE_Memory* m) {
    const uint32_t p = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_sema_mutex);
    const uint32_t id = g_sema_next++;
    if (id >= g_semas.size()) { sema_result(c, -1); return; }
    auto& s = g_semas[id];
    s.count = static_cast<int>(m->Read<uint32_t>(p + 8));
    s.max   = static_cast<int>(m->Read<uint32_t>(p + 12));
    if (s.max <= 0) s.max = 0x7fffffff;
    s.used = true;
    sema_result(c, static_cast<int>(id));
}
static void SysDeleteSema(MIPS_EE_Context* c, EE_Memory*) {
    const uint32_t id = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_sema_mutex);
    if (id >= g_semas.size() || !g_semas[id].used) { sema_result(c, -1); return; }
    g_semas[id] = {};
    sema_result(c, 0);
}
static void SysSignalSema(MIPS_EE_Context* c, EE_Memory*) {
    const uint32_t id = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_sema_mutex);
    if (id >= g_semas.size() || !g_semas[id].used || g_semas[id].count >= g_semas[id].max) {
        sema_result(c, -1); return;
    }
    ++g_semas[id].count;
    sema_result(c, 0);
}
static void SysWaitSema(MIPS_EE_Context* c, EE_Memory*) {
    const uint32_t id = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_sema_mutex);
    if (id >= g_semas.size() || !g_semas[id].used) { sema_result(c, -1); return; }
    // Cooperative model: if count > 0 consume one token; otherwise return -1
    // (caller is responsible for retrying / yielding).
    if (g_semas[id].count > 0) {
        --g_semas[id].count;
        sema_result(c, 0);
    } else {
        // Return 0 anyway so the game doesn't immediately abort.
        // True blocking is not possible in a single-threaded cooperative model.
        sema_result(c, 0);
    }
}
static void SysPollSema(MIPS_EE_Context* c, EE_Memory* m) { SysWaitSema(c, m); }
static void SysSetAlarm(MIPS_EE_Context* c, EE_Memory*) {
    sema_result(c, static_cast<int>(c->r[4] != 0 ? 1 : -1));
}
static void SysReleaseAlarm(MIPS_EE_Context* c, EE_Memory*) { sema_result(c, 0); }

// ── Event Flags ───────────────────────────────────────────────────────────────
// PS2 event flags are bitfields that threads can set/clear/wait on.
// Syscall numbers: CreateEventFlag=0x50, DeleteEventFlag=0x51,
// SetEventFlag=0x52, iSetEventFlag=0x53, ClearEventFlag=0x54,
// iClearEventFlag=0x55, WaitEventFlag=0x56, PollEventFlag=0x57

struct EventFlag {
    uint32_t bits   = 0;      // current bits
    uint32_t init   = 0;      // initial value
    uint32_t attr   = 0;      // attributes
    bool     used   = false;
};

static std::mutex g_ef_mutex;
static std::array<EventFlag, 256> g_flags{};
static uint32_t g_ef_next = 1;

static uint32_t ef_result(MIPS_EE_Context* c, int v) {
    c->r[2] = static_cast<uint64_t>(static_cast<int64_t>(v));
    return c->r[2];
}

static void SysCreateEventFlag(MIPS_EE_Context* c, EE_Memory* m) {
    // $a0 = pointer to event_flag_param { uint32 attr, uint32 init, uint32 option }
    const uint32_t p = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_ef_mutex);
    const uint32_t id = g_ef_next++;
    if (id >= g_flags.size()) { ef_result(c, -1); return; }
    auto& f    = g_flags[id];
    f.attr     = m->Read<uint32_t>(p);
    f.bits     = m->Read<uint32_t>(p + 4);
    f.init     = f.bits;
    f.used     = true;
    ef_result(c, static_cast<int>(id));
}

static void SysDeleteEventFlag(MIPS_EE_Context* c, EE_Memory*) {
    const uint32_t id = static_cast<uint32_t>(c->r[4]);
    std::lock_guard lock(g_ef_mutex);
    if (id >= g_flags.size() || !g_flags[id].used) { ef_result(c, -1); return; }
    g_flags[id] = {};
    ef_result(c, 0);
}

static void SysSetEventFlag(MIPS_EE_Context* c, EE_Memory*) {
    // $a0 = id, $a1 = bits to set (OR into current bits)
    const uint32_t id   = static_cast<uint32_t>(c->r[4]);
    const uint32_t bits = static_cast<uint32_t>(c->r[5]);
    std::lock_guard lock(g_ef_mutex);
    if (id >= g_flags.size() || !g_flags[id].used) { ef_result(c, -1); return; }
    g_flags[id].bits |= bits;
    ef_result(c, 0);
}

static void SysClearEventFlag(MIPS_EE_Context* c, EE_Memory*) {
    // $a0 = id, $a1 = bit mask to clear (AND the inverse into current bits)
    const uint32_t id   = static_cast<uint32_t>(c->r[4]);
    const uint32_t bits = static_cast<uint32_t>(c->r[5]);
    std::lock_guard lock(g_ef_mutex);
    if (id >= g_flags.size() || !g_flags[id].used) { ef_result(c, -1); return; }
    g_flags[id].bits &= bits;   // caller passes the AND mask (not the cleared bits)
    ef_result(c, 0);
}

static void SysWaitEventFlag(MIPS_EE_Context* c, EE_Memory* m) {
    // $a0 = id, $a1 = bits to wait for, $a2 = mode (1=AND,0=OR), $a3 = result_ptr
    const uint32_t id        = static_cast<uint32_t>(c->r[4]);
    const uint32_t wait_bits = static_cast<uint32_t>(c->r[5]);
    const uint32_t mode      = static_cast<uint32_t>(c->r[6]);
    const uint32_t res_ptr   = static_cast<uint32_t>(c->r[7]);
    std::lock_guard lock(g_ef_mutex);
    if (id >= g_flags.size() || !g_flags[id].used) { ef_result(c, -1); return; }
    auto& f = g_flags[id];
    const bool satisfied = (mode & 1)                       // AND mode
        ? (f.bits & wait_bits) == wait_bits
        : (f.bits & wait_bits) != 0;                        // OR mode
    if (res_ptr && m->IsValidRange(res_ptr, 4))
        m->Write<uint32_t>(res_ptr, f.bits);
    if (satisfied && (f.attr & 0x10))                       // EA_SINGLE: auto-clear on satisfy
        f.bits &= ~wait_bits;
    ef_result(c, 0);  // cooperative: always return "satisfied" to avoid deadlock
}

static void SysPollEventFlag(MIPS_EE_Context* c, EE_Memory* m) {
    SysWaitEventFlag(c, m);  // non-blocking version — same semantics in our model
}

void InitSyncSyscalls() {
    // Semaphores
    RegisterSyscall(0x40, SysCreateSema,    "CreateSema");
    RegisterSyscall(0x41, SysDeleteSema,    "DeleteSema");
    RegisterSyscall(0x42, SysSignalSema,    "SignalSema");
    RegisterSyscall(0x43, SysSignalSema,    "iSignalSema");
    RegisterSyscall(0x44, SysWaitSema,      "WaitSema");
    RegisterSyscall(0x45, SysPollSema,      "PollSema");
    RegisterSyscall(0x46, SysPollSema,      "iPollSema");
    // Alarms
    RegisterSyscall(0x32, SysSetAlarm,      "SetAlarm");
    RegisterSyscall(0x33, SysReleaseAlarm,  "ReleaseAlarm");
    // Event Flags (previously completely missing)
    RegisterSyscall(0x50, SysCreateEventFlag,  "CreateEventFlag");
    RegisterSyscall(0x51, SysDeleteEventFlag,  "DeleteEventFlag");
    RegisterSyscall(0x52, SysSetEventFlag,     "SetEventFlag");
    RegisterSyscall(0x53, SysSetEventFlag,     "iSetEventFlag");
    RegisterSyscall(0x54, SysClearEventFlag,   "ClearEventFlag");
    RegisterSyscall(0x55, SysClearEventFlag,   "iClearEventFlag");
    RegisterSyscall(0x56, SysWaitEventFlag,    "WaitEventFlag");
    RegisterSyscall(0x57, SysPollEventFlag,    "PollEventFlag");
}

} // namespace OpenRatchet::Kernel
