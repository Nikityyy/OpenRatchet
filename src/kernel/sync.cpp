#include "openratchet/syscalls.h"
#include <array>
#include <mutex>

namespace OpenRatchet::Kernel {
struct Sema { int count = 0, max = 0; bool used = false; };
static std::mutex g_mutex;
static std::array<Sema, 256> g_semas{};
static uint32_t g_next = 1;
static uint32_t result(MIPS_EE_Context* c, int v) { c->r[2] = static_cast<uint64_t>(static_cast<int64_t>(v)); return c->r[2]; }

static void SysCreateSema(MIPS_EE_Context* c, EE_Memory* m) {
    const uint32_t p = static_cast<uint32_t>(c->r[4]); std::lock_guard lock(g_mutex);
    const uint32_t id = g_next++;
    if (id >= g_semas.size()) { result(c, -1); return; }
    auto& s = g_semas[id]; s.count = static_cast<int>(m->Read<uint32_t>(p + 8)); s.max = static_cast<int>(m->Read<uint32_t>(p + 12));
    if (s.max <= 0) s.max = 0x7fffffff; s.used = true; result(c, static_cast<int>(id));
}
static void SysDeleteSema(MIPS_EE_Context* c, EE_Memory*) { const uint32_t id = static_cast<uint32_t>(c->r[4]); std::lock_guard lock(g_mutex); if (id >= g_semas.size() || !g_semas[id].used) { result(c,-1); return; } g_semas[id] = {}; result(c,0); }
static void SysSignalSema(MIPS_EE_Context* c, EE_Memory*) { const uint32_t id = static_cast<uint32_t>(c->r[4]); std::lock_guard lock(g_mutex); if (id >= g_semas.size() || !g_semas[id].used || g_semas[id].count >= g_semas[id].max) { result(c,-1); return; } ++g_semas[id].count; result(c,0); }
static void SysWaitSema(MIPS_EE_Context* c, EE_Memory*) { const uint32_t id = static_cast<uint32_t>(c->r[4]); std::lock_guard lock(g_mutex); if (id >= g_semas.size() || !g_semas[id].used || g_semas[id].count <= 0) { result(c,-1); return; } --g_semas[id].count; result(c,0); }
static void SysPollSema(MIPS_EE_Context* c, EE_Memory* m) { SysWaitSema(c,m); }
static void SysSetAlarm(MIPS_EE_Context* c, EE_Memory*) { result(c, static_cast<int>(c->r[4] != 0 ? 1 : -1)); }
static void SysReleaseAlarm(MIPS_EE_Context* c, EE_Memory*) { result(c,0); }
void InitSyncSyscalls() { RegisterSyscall(0x40,SysCreateSema,"CreateSema"); RegisterSyscall(0x41,SysDeleteSema,"DeleteSema"); RegisterSyscall(0x42,SysSignalSema,"SignalSema"); RegisterSyscall(0x43,SysSignalSema,"iSignalSema"); RegisterSyscall(0x44,SysWaitSema,"WaitSema"); RegisterSyscall(0x45,SysPollSema,"PollSema"); RegisterSyscall(0x46,SysPollSema,"iPollSema"); RegisterSyscall(0x32,SysSetAlarm,"SetAlarm"); RegisterSyscall(0x33,SysReleaseAlarm,"ReleaseAlarm"); }
}
