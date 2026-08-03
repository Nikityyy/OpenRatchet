#pragma once

#include <chrono>
#include <cstdint>

#include "ps2_runtime.h"

namespace OpenRatchet::Runtime {

void RegisterGeneratedFunctions(PS2Runtime& runtime);
void SetGuestDeadline(std::chrono::steady_clock::time_point deadline);
void ClearGuestDeadline();
bool GuestDeadlineExpired();
uint64_t GetGuestDispatchCount();

}
