#include "openratchet/syscalls.h"
#include "openratchet/kernel_state.h"

namespace OpenRatchet::Kernel {
static GSSystemState g_state;
const GSSystemState& GetGSSystemState() { return g_state; }
static void SysGsPutIMR(MIPS_EE_Context* c, EE_Memory*) { g_state.imr=c->r[4]; c->r[2]=0; }
static void SysSetGsCrt(MIPS_EE_Context* c, EE_Memory*) { g_state.interlace=c->r[4]; g_state.mode=c->r[5]; g_state.ffmd=c->r[6]; c->r[2]=0; }
static void SysGsSetDefDispEnv(MIPS_EE_Context* c, EE_Memory*) { c->r[2]=c->r[4]; }
static void SysGsSetDefDrawEnv(MIPS_EE_Context* c, EE_Memory*) { c->r[2]=c->r[4]; }
void InitGSSyscalls() { g_state={}; RegisterSyscall(0x71,SysGsPutIMR,"GsPutIMR"); RegisterSyscall(0x02,SysSetGsCrt,"SetGsCrt"); RegisterSyscall(0x75,SysGsSetDefDispEnv,"GsSetDefDispEnv"); RegisterSyscall(0x76,SysGsSetDefDrawEnv,"GsSetDefDrawEnv"); }
}
