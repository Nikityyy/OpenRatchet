#include "openratchet/syscalls.h"
#include "openratchet/kernel_state.h"

namespace OpenRatchet::Kernel {
static TimerState g_state;
const TimerState& GetTimerState() { return g_state; }
void TickTimers() { ++g_state.vsync_count; }
static void SysSetVSyncCallback(MIPS_EE_Context* c, EE_Memory*) { g_state.vsync_mode = static_cast<uint32_t>(c->r[4]); g_state.vsync_callback = static_cast<uint32_t>(c->r[5]); c->r[2] = 0; }
void InitTimerSyscalls() { g_state = {}; RegisterSyscall(0x73,SysSetVSyncCallback,"SetVSyncCallback"); }
}
