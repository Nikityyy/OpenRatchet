#pragma once

#include <cstdint>

class EE_Memory;

namespace OpenRatchet::Kernel {

struct TimerState {
    uint32_t vsync_mode = 0;
    uint32_t vsync_callback = 0;
    uint32_t timer_id = 0;
    uint32_t timer_compare = 0;
    uint32_t timer_callback = 0;
    uint64_t vsync_count = 0;
};

struct GSSystemState {
    uint64_t imr = 0;
    uint32_t interlace = 0;
    uint32_t mode = 0;
    uint32_t ffmd = 0;
};

const TimerState& GetTimerState();
const GSSystemState& GetGSSystemState();
void TickTimers();
void ResetGuestSyscallTable(EE_Memory& memory);

}
