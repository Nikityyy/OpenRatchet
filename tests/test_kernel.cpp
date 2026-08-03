#include "openratchet/syscalls.h"
#include "openratchet/kernel_state.h"
#include <cassert>

int main() {
    EE_Memory memory;
    memory.Init();
    MIPS_EE_Context ctx{};
    OpenRatchet::Kernel::InitSyscalls();

    memory.Write<uint32_t>(0x1000, 0x2000);
    memory.Write<uint32_t>(0x1004, 0x3000);
    memory.Write<uint32_t>(0x1010, 5);
    ctx.r[3] = 0x20; ctx.r[4] = 0x1000;
    OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory);
    assert(ctx.r[2] != static_cast<uint64_t>(-1));
    const uint64_t thread_id = ctx.r[2];

    ctx.r[3] = 0x22; ctx.r[4] = thread_id;
    OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory);
    assert(ctx.r[2] == 0);

    memory.Write<uint32_t>(0x1100, 0); memory.Write<uint32_t>(0x1104, 0);
    memory.Write<uint32_t>(0x1108, 1); memory.Write<uint32_t>(0x110C, 2);
    ctx.r[3] = 0x40; ctx.r[4] = 0x1100;
    OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory);
    const uint64_t sema_id = ctx.r[2]; assert(sema_id != static_cast<uint64_t>(-1));
    ctx.r[3] = 0x42; ctx.r[4] = sema_id; OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory); assert(ctx.r[2] == 0);
    ctx.r[3] = 0x44; ctx.r[4] = sema_id; OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory); assert(ctx.r[2] == 0);

    ctx.r[3] = 0x73; ctx.r[4] = 2; ctx.r[5] = 0x1234; OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory);
    assert(OpenRatchet::Kernel::GetTimerState().vsync_callback == 0x1234);
    ctx.r[3] = 0x02; ctx.r[4] = 1; ctx.r[5] = 2; ctx.r[6] = 3; OpenRatchet::Kernel::DispatchSyscall(&ctx, &memory);
    assert(OpenRatchet::Kernel::GetGSSystemState().mode == 2);
    return 0;
}
